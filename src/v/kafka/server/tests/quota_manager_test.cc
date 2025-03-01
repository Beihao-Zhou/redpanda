// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "cluster/client_quota_serde.h"
#include "cluster/client_quota_store.h"
#include "config/configuration.h"
#include "kafka/server/client_quota_translator.h"
#include "kafka/server/quota_manager.h"

#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/sstring.hh>
#include <seastar/testing/thread_test_case.hh>
#include <seastar/util/defer.hh>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace std::chrono_literals;
using cluster::client_quota::entity_key;
using cluster::client_quota::entity_value;

static const auto default_key = entity_key(
  entity_key::client_id_default_match{});
static const auto client_id = "franz-go";
static const auto franz_go_key = entity_key{
  entity_key::client_id_prefix_match{"franz-go"}};
static const auto not_franz_go_key = entity_key{
  entity_key::client_id_prefix_match{"not-franz-go"}};

struct fixture {
    ss::sharded<cluster::client_quota::store> quota_store;
    ss::sharded<kafka::quota_manager> sqm;

    fixture() {
        quota_store.start().get();
        sqm.start(std::ref(quota_store)).get();
        sqm.invoke_on_all(&kafka::quota_manager::start).get();
    }

    ~fixture() {
        sqm.stop().get();
        quota_store.stop().get();
    }

    void set_basic_quotas() {
        auto default_values = entity_value{
          .producer_byte_rate = 1024,
          .consumer_byte_rate = 1025,
          .controller_mutation_rate = 1026,
        };

        auto franz_go_values = entity_value{
          .producer_byte_rate = 4096,
          .consumer_byte_rate = 4097,
        };

        auto not_franz_go_values = entity_value{
          .producer_byte_rate = 2048,
          .consumer_byte_rate = 2049,
        };

        quota_store.local().set_quota(default_key, default_values);
        quota_store.local().set_quota(franz_go_key, franz_go_values);
        quota_store.local().set_quota(not_franz_go_key, not_franz_go_values);
    }
};

template<typename F>
ss::future<> set_config(F update) {
    co_await ss::smp::invoke_on_all(
      [update{std::move(update)}]() { update(config::shard_local_cfg()); });
    co_await ss::sleep(std::chrono::milliseconds(1));
}

SEASTAR_THREAD_TEST_CASE(quota_manager_fetch_no_throttling) {
    fixture f;

    auto& qm = f.sqm.local();

    // Test that if fetch throttling is disabled, we don't throttle
    qm.record_fetch_tp(client_id, 10000000000000).get();
    auto delay = qm.throttle_fetch_tp(client_id).get();

    BOOST_CHECK_EQUAL(0ms, delay);
}

SEASTAR_THREAD_TEST_CASE(quota_manager_fetch_throttling) {
    fixture f;

    auto default_values = entity_value{
      .consumer_byte_rate = 100,
    };
    f.quota_store.local().set_quota(default_key, default_values);

    auto& qm = f.sqm.local();

    auto now = kafka::quota_manager::clock::now();

    // Test that below the fetch quota we don't throttle
    qm.record_fetch_tp(client_id, 99, now).get();
    auto delay = qm.throttle_fetch_tp(client_id, now).get();

    BOOST_CHECK_EQUAL(delay, 0ms);

    // Test that above the fetch quota we throttle
    qm.record_fetch_tp(client_id, 10, now).get();
    delay = qm.throttle_fetch_tp(client_id, now).get();

    BOOST_CHECK_GT(delay, 0ms);

    // Test that once we wait out the throttling delay, we don't
    // throttle again (as long as we stay under the limit)
    now += 1s;
    qm.record_fetch_tp(client_id, 10, now).get();
    delay = qm.throttle_fetch_tp(client_id, now).get();

    BOOST_CHECK_EQUAL(delay, 0ms);
}

SEASTAR_THREAD_TEST_CASE(quota_manager_fetch_stress_test) {
    fixture f;

    set_config([](config::configuration& conf) {
        conf.max_kafka_throttle_delay_ms.set_value(
          std::chrono::milliseconds::max());
    }).get();

    auto default_values = entity_value{
      .consumer_byte_rate = 100,
    };
    f.quota_store
      .invoke_on_all([&default_values](cluster::client_quota::store& store) {
          store.set_quota(default_key, default_values);
      })
      .get();

    // Exercise the quota manager from multiple cores to attempt to
    // discover segfaults caused by data races/use-after-free
    f.sqm
      .invoke_on_all(
        ss::coroutine::lambda([](kafka::quota_manager& qm) -> ss::future<> {
            for (size_t i = 0; i < 1000; ++i) {
                co_await qm.record_fetch_tp(client_id, 1);
                auto delay [[maybe_unused]] = co_await qm.throttle_fetch_tp(
                  client_id);
                co_await ss::maybe_yield();
            }
        }))
      .get();
}

SEASTAR_THREAD_TEST_CASE(static_config_test) {
    using k_client_id = kafka::k_client_id;
    using k_group_name = kafka::k_group_name;
    fixture f;

    f.set_basic_quotas();

    auto& buckets_map = f.sqm.local().get_global_map_for_testing();

    BOOST_REQUIRE_EQUAL(buckets_map->size(), 0);

    {
        ss::sstring client_id = "franz-go";
        f.sqm.local().record_fetch_tp(client_id, 1).get();
        f.sqm.local().record_produce_tp_and_throttle(client_id, 1).get();
        f.sqm.local().record_partition_mutations(client_id, 1).get();
        auto it = buckets_map->find(k_group_name{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_REQUIRE(it->second->tp_produce_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_produce_rate->rate(), 4096);
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_fetch_rate->rate(), 4097);
        BOOST_REQUIRE(!it->second->pm_rate.has_value());
    }
    {
        ss::sstring client_id = "not-franz-go";
        f.sqm.local().record_fetch_tp(client_id, 1).get();
        f.sqm.local().record_produce_tp_and_throttle(client_id, 1).get();
        f.sqm.local().record_partition_mutations(client_id, 1).get();
        auto it = buckets_map->find(k_group_name{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_REQUIRE(it->second->tp_produce_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_produce_rate->rate(), 2048);
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_fetch_rate->rate(), 2049);
        BOOST_REQUIRE(!it->second->pm_rate.has_value());
    }
    {
        ss::sstring client_id = "unconfigured";
        f.sqm.local().record_fetch_tp(client_id, 1).get();
        f.sqm.local().record_produce_tp_and_throttle(client_id, 1).get();
        f.sqm.local().record_partition_mutations(client_id, 1).get();
        auto it = buckets_map->find(k_client_id{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_REQUIRE(it->second->tp_produce_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_produce_rate->rate(), 1024);
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_fetch_rate->rate(), 1025);
        BOOST_REQUIRE(it->second->pm_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->pm_rate->rate(), 1026);
    }
}

SEASTAR_THREAD_TEST_CASE(update_test) {
    using clock = kafka::quota_manager::clock;
    using k_group_name = kafka::k_group_name;
    using k_client_id = kafka::k_client_id;
    fixture f;

    f.set_basic_quotas();

    auto& buckets_map = f.sqm.local().get_global_map_for_testing();

    auto now = clock::now();
    {
        // Update fetch config
        ss::sstring client_id = "franz-go";
        f.sqm.local().record_fetch_tp(client_id, 8194, now).get();
        f.sqm.local()
          .record_produce_tp_and_throttle(client_id, 8192, now)
          .get();

        // Increment the franz-go and not-franz-go group fetch quotas
        auto franz_go_values = f.quota_store.local().get_quota(franz_go_key);
        auto not_franz_go_values = f.quota_store.local().get_quota(
          not_franz_go_key);

        // Sanity check that the fetch quotas are present
        auto has_fetch_quota = [](const auto& qv) {
            return qv.has_value() && qv->consumer_byte_rate.has_value();
        };
        BOOST_REQUIRE(has_fetch_quota(franz_go_values));
        BOOST_REQUIRE(has_fetch_quota(not_franz_go_values));

        *franz_go_values->consumer_byte_rate += 1;
        *not_franz_go_values->consumer_byte_rate += 1;

        f.quota_store.local().set_quota(franz_go_key, *franz_go_values);
        f.quota_store.local().set_quota(not_franz_go_key, *not_franz_go_values);

        // Wait for the quota update to propagate
        ss::sleep(std::chrono::milliseconds(1)).get();

        // Check the rate has been updated
        auto it = buckets_map->find(k_group_name{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_fetch_rate->rate(), 4098);

        // Check produce is the same bucket
        BOOST_REQUIRE(it->second->tp_produce_rate.has_value());
        auto delay = f.sqm.local()
                       .record_produce_tp_and_throttle(client_id, 1, now)
                       .get();
        BOOST_CHECK_EQUAL(delay / 1ms, 1000);
    }

    {
        // Remove produce config
        ss::sstring client_id = "franz-go";
        f.sqm.local().record_fetch_tp(client_id, 8196, now).get();
        f.sqm.local()
          .record_produce_tp_and_throttle(client_id, 8192, now)
          .get();

        auto franz_go_values = f.quota_store.local().get_quota(franz_go_key);
        BOOST_REQUIRE(
          franz_go_values.has_value()
          && franz_go_values->producer_byte_rate.has_value());
        franz_go_values->producer_byte_rate = std::nullopt;
        f.quota_store.local().set_quota(franz_go_key, *franz_go_values);

        // Wait for the quota update to propagate
        ss::sleep(std::chrono::milliseconds(1)).get();

        // Check the produce rate has been updated on the group
        auto it = buckets_map->find(k_group_name{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_CHECK(!it->second->tp_produce_rate.has_value());

        // Check fetch is the same bucket
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        auto delay = f.sqm.local().throttle_fetch_tp(client_id, now).get();
        BOOST_CHECK_EQUAL(delay / 1ms, 1000);

        // Check the new produce rate now applies
        f.sqm.local()
          .record_produce_tp_and_throttle(client_id, 8192, now)
          .get();
        auto client_it = buckets_map->find(k_client_id{client_id});
        BOOST_REQUIRE(client_it != buckets_map->end());
        BOOST_REQUIRE(client_it->second->tp_produce_rate.has_value());
        BOOST_CHECK_EQUAL(client_it->second->tp_produce_rate->rate(), 1024);
    }

    {
        // Update fetch config again using the quota store
        ss::sstring client_id = "franz-go";
        auto key = entity_key{entity_key::client_id_match{client_id}};
        auto value = entity_value{.consumer_byte_rate = 16384};
        f.quota_store.local().set_quota(key, value);

        // Wait for the quota update to propagate
        ss::sleep(std::chrono::milliseconds(1)).get();

        // Check the rate has been updated
        auto it = buckets_map->find(k_client_id{client_id});
        BOOST_REQUIRE(it != buckets_map->end());
        BOOST_REQUIRE(it->second->tp_fetch_rate.has_value());
        BOOST_CHECK_EQUAL(it->second->tp_fetch_rate->rate(), 16384);
    }
}
