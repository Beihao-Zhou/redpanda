// Copyright 2020 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0

#include "compression/stream_zstd.h"
#include "model/fundamental.h"
#include "model/metadata.h"
#include "model/record.h"
#include "model/record_batch_reader.h"
#include "model/tests/random_batch.h"
#include "model/tests/randoms.h"
#include "model/timeout_clock.h"
#include "raft/consensus_utils.h"
#include "raft/group_configuration.h"
#include "raft/heartbeats.h"
#include "raft/types.h"
#include "random/generators.h"
#include "reflection/adl.h"
#include "storage/record_batch_builder.h"
#include "test_utils/randoms.h"
#include "test_utils/rpc.h"

#include <seastar/core/future.hh>
#include <seastar/testing/thread_test_case.hh>

#include <absl/container/flat_hash_map.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test_log.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

void verify_batches(
  const chunked_vector<model::record_batch>& expected,
  const chunked_vector<model::record_batch>& current) {
    BOOST_REQUIRE_EQUAL(expected.size(), current.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        auto& current_batch = current[i];
        auto& expected_batch = current[i];
        BOOST_REQUIRE_EQUAL(
          current_batch.base_offset(), expected_batch.base_offset());
        BOOST_REQUIRE_EQUAL(
          current_batch.last_offset(), expected_batch.last_offset());
        BOOST_REQUIRE_EQUAL(
          current_batch.header().crc, expected_batch.header().crc);
        BOOST_REQUIRE_EQUAL(
          current_batch.compressed(), expected_batch.compressed());
        BOOST_REQUIRE_EQUAL(
          current_batch.header().type, expected_batch.header().type);
        BOOST_REQUIRE_EQUAL(
          current_batch.size_bytes(), expected_batch.size_bytes());
        BOOST_REQUIRE_EQUAL(
          current_batch.record_count(), expected_batch.record_count());
        BOOST_REQUIRE_EQUAL(current_batch.term(), expected_batch.term());
    }
}

SEASTAR_THREAD_TEST_CASE(append_entries_requests) {
    chunked_vector<model::record_batch> batches{
      model::test::make_random_batches(model::offset(1), 3, false).get()};

    chunked_vector<model::record_batch> reference_batches;

    for (auto& b : batches) {
        b.set_term(model::term_id(123));
        reference_batches.push_back(b.share());
    }

    auto meta = raft::protocol_metadata{
      .group = raft::group_id(1),
      .commit_index = model::offset(100),
      .term = model::term_id(10),
      .prev_log_index = model::offset(99),
      .prev_log_term = model::term_id(-1),
      .last_visible_index = model::offset(200),
      .dirty_offset = model::offset(99),
    };
    raft::append_entries_request req(
      raft::vnode(model::node_id(1), model::revision_id(10)),
      raft::vnode(model::node_id(10), model::revision_id(101)),
      meta,
      std::move(batches),
      0);

    const auto target_node_id = req.target_node();

    iobuf buf;
    serde::write_async(buf, std::move(req)).get();
    iobuf_parser p(std::move(buf));
    auto d = serde::read_async<raft::append_entries_request>(p).get();

    BOOST_REQUIRE_EQUAL(
      d.source_node(), raft::vnode(model::node_id(1), model::revision_id(10)));
    BOOST_REQUIRE_EQUAL(d.target_node(), target_node_id);
    BOOST_REQUIRE_EQUAL(d.metadata().group, meta.group);
    BOOST_REQUIRE_EQUAL(d.metadata().commit_index, meta.commit_index);
    BOOST_REQUIRE_EQUAL(d.metadata().term, meta.term);
    BOOST_REQUIRE_EQUAL(d.metadata().prev_log_index, meta.prev_log_index);
    BOOST_REQUIRE_EQUAL(d.metadata().prev_log_term, meta.prev_log_term);
    BOOST_REQUIRE_EQUAL(
      d.metadata().last_visible_index, meta.last_visible_index);
    BOOST_REQUIRE_EQUAL(d.metadata().dirty_offset, meta.dirty_offset);

    verify_batches(reference_batches, d.batches());
}

model::broker create_test_broker() {
    return model::broker(
      model::node_id(random_generators::get_int(1000)), // id
      net::unresolved_address(
        "127.0.0.1",
        random_generators::get_int(10000, 20000)), // kafka api address
      net::unresolved_address(
        "127.0.0.1", random_generators::get_int(10000, 20000)), // rpc address
      model::rack_id("some_rack"),
      model::broker_properties{
        .cores = 8 // cores
      });
}

SEASTAR_THREAD_TEST_CASE(heartbeat_request_roundtrip) {
    static constexpr int64_t one_k = 1'000;
    raft::heartbeat_request req;
    req.heartbeats = std::vector<raft::heartbeat_metadata>(one_k);
    for (int64_t i = 0; i < one_k; ++i) {
        req.heartbeats[i].node_id = raft::vnode(
          model::node_id(one_k), model::revision_id(i));
        req.heartbeats[i].meta.group = raft::group_id(i);
        req.heartbeats[i].meta.commit_index = model::offset(i);
        req.heartbeats[i].meta.term = model::term_id(i);
        req.heartbeats[i].meta.prev_log_index = model::offset(i);
        req.heartbeats[i].meta.prev_log_term = model::term_id(i);
        req.heartbeats[i].meta.last_visible_index = model::offset(i);
        req.heartbeats[i].meta.dirty_offset = model::offset(i);
        req.heartbeats[i].target_node_id = raft::vnode(
          model::node_id(0), model::revision_id(i));
    }

    iobuf buf;
    serde::write_async(buf, std::move(req)).get();
    auto res = serde::from_iobuf<raft::heartbeat_request>(std::move(buf));

    for (int64_t i = 0; i < one_k; ++i) {
        BOOST_REQUIRE_EQUAL(res.heartbeats[i].meta.group, raft::group_id(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].meta.commit_index, model::offset(i));
        BOOST_REQUIRE_EQUAL(res.heartbeats[i].meta.term, model::term_id(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].meta.prev_log_index, model::offset(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].meta.prev_log_term, model::term_id(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].meta.last_visible_index, model::offset(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].meta.dirty_offset, model::offset(i));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].target_node_id,
          raft::vnode(model::node_id(0), model::revision_id(i)));
        BOOST_REQUIRE_EQUAL(
          res.heartbeats[i].node_id,
          raft::vnode(model::node_id(one_k), model::revision_id(i)));
    }
}
SEASTAR_THREAD_TEST_CASE(heartbeat_request_roundtrip_with_negative) {
    static constexpr int64_t one_k = 10;
    raft::heartbeat_request req;

    req.heartbeats = std::vector<raft::heartbeat_metadata>(one_k);
    for (int64_t i = 0; i < one_k; ++i) {
        req.heartbeats[i].target_node_id = raft::vnode(
          model::node_id(0), model::revision_id(-i - 100));
        req.heartbeats[i].node_id = raft::vnode(
          model::node_id(one_k), model::revision_id(-i - 100));
        req.heartbeats[i].meta.group = raft::group_id(i);
        req.heartbeats[i].meta.commit_index = model::offset(-i - 100);
        req.heartbeats[i].meta.term = model::term_id(-i - 100);
        req.heartbeats[i].meta.prev_log_index = model::offset(-i - 100);
        req.heartbeats[i].meta.prev_log_term = model::term_id(-i - 100);
        req.heartbeats[i].meta.last_visible_index = model::offset(-i - 100);
    }

    iobuf buf;
    serde::write_async(buf, std::move(req)).get();
    auto res = serde::from_iobuf<raft::heartbeat_request>(std::move(buf));
    for (auto& hb : res.heartbeats) {
        BOOST_REQUIRE_EQUAL(hb.meta.commit_index, model::offset{});
        BOOST_REQUIRE_EQUAL(hb.meta.term, model::term_id{-1});
        BOOST_REQUIRE_EQUAL(hb.meta.prev_log_index, model::offset{});
        BOOST_REQUIRE_EQUAL(hb.meta.prev_log_term, model::term_id{});
        BOOST_REQUIRE_EQUAL(hb.meta.last_visible_index, model::offset{});
        BOOST_REQUIRE_EQUAL(
          hb.node_id, raft::vnode(model::node_id(one_k), model::revision_id{}));
        BOOST_REQUIRE_EQUAL(
          hb.target_node_id,
          raft::vnode(model::node_id(0), model::revision_id{}));
    }
}
SEASTAR_THREAD_TEST_CASE(heartbeat_response_roundtrip) {
    static constexpr int64_t group_count = 10000;
    raft::heartbeat_reply reply;
    reply.meta.reserve(group_count);

    for (size_t i = 0; i < group_count; ++i) {
        // Negative offsets are special, so pick range starting at 0 here
        // (heartbeat_response_negative below covers the negative case)
        auto commited_idx = model::offset(
          random_generators::get_int(0, 1000000000));
        auto dirty_idx = commited_idx
                         + model::offset(random_generators::get_int(10000));

        reply.meta.push_back(raft::append_entries_reply{
          .target_node_id = raft::vnode(
            model::node_id(2), model::revision_id(i)),
          .node_id = raft::vnode(model::node_id(1), model::revision_id(i)),
          .group = raft::group_id(i),
          .term = model::term_id(random_generators::get_int(0, 1000)),
          .last_flushed_log_index = commited_idx,
          .last_dirty_log_index = dirty_idx,
          .result = raft::reply_result::success});
    }
    absl::flat_hash_map<raft::group_id, raft::append_entries_reply> expected;
    expected.reserve(reply.meta.size());
    for (const auto& m : reply.meta) {
        expected.emplace(m.group, m);
    }
    auto buf = serde::to_iobuf(reply);
    auto result = serde::from_iobuf<raft::heartbeat_reply>(std::move(buf));

    for (size_t i = 0; i < result.meta.size(); ++i) {
        auto gr = result.meta[i].group;
        BOOST_REQUIRE_EQUAL(expected[gr].node_id, result.meta[i].node_id);
        BOOST_REQUIRE_EQUAL(
          expected[gr].target_node_id, result.meta[i].target_node_id);
        BOOST_REQUIRE_EQUAL(expected[gr].group, result.meta[i].group);
        BOOST_REQUIRE_EQUAL(expected[gr].term, result.meta[i].term);
        BOOST_REQUIRE_EQUAL(
          expected[gr].last_flushed_log_index,
          result.meta[i].last_flushed_log_index);
        BOOST_REQUIRE_EQUAL(
          expected[gr].last_dirty_log_index,
          result.meta[i].last_dirty_log_index);
        BOOST_REQUIRE_EQUAL(expected[gr].result, result.meta[i].result);
        // v1 heartbeats always have default may_recover
        BOOST_REQUIRE_EQUAL(result.meta[i].may_recover, true);
    }
}

/**
 * Verify that negative values get transformed to ::min values
 * during an encode/decode cycle.  These are encoded to -1 to
 * save space in varint encoding, then all negative values decode
 * to T{}
 */
SEASTAR_THREAD_TEST_CASE(heartbeat_response_negatives) {
    raft::heartbeat_reply reply;

    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id(2), model::revision_id(-1)),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(-1)),
      .group = raft::group_id(1),
      .term = model::term_id(random_generators::get_int(0, 1000)),
      .last_flushed_log_index = model::offset(-1),
      .last_dirty_log_index = model::offset(-1),
      .last_term_base_offset = model::offset(-1),
      .result = raft::reply_result::success});

    auto buf = serde::to_iobuf(reply);
    auto result = serde::from_iobuf<raft::heartbeat_reply>(std::move(buf));
    BOOST_REQUIRE_EQUAL(result.meta[0].last_flushed_log_index, model::offset{});
    BOOST_REQUIRE_EQUAL(result.meta[0].last_dirty_log_index, model::offset{});
    BOOST_REQUIRE_EQUAL(result.meta[0].last_term_base_offset, model::offset{});
    BOOST_REQUIRE_EQUAL(
      result.meta[0].node_id.revision(), model::revision_id{});
    BOOST_REQUIRE_EQUAL(
      result.meta[0].target_node_id.revision(), model::revision_id{});
}

SEASTAR_THREAD_TEST_CASE(heartbeat_response_with_failures) {
    raft::heartbeat_reply reply;
    // first reply is a failure
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(1),
      .term = model::term_id(random_generators::get_int(0, 1000)),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::follower_busy});

    /**
     * Two other replies are successful
     */
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id(0), model::revision_id{1}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(2),
      .term = model::term_id(1),
      .last_flushed_log_index = model::offset(100),
      .last_dirty_log_index = model::offset(101),
      .last_term_base_offset = model::offset(102),
      .result = raft::reply_result::success});

    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id(0), model::revision_id{1}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(3),
      .term = model::term_id(5),
      .last_flushed_log_index = model::offset(200),
      .last_dirty_log_index = model::offset(201),
      .last_term_base_offset = model::offset(202),
      .result = raft::reply_result::success});

    auto buf = serde::to_iobuf(reply);

    auto result = serde::from_iobuf<raft::heartbeat_reply>(std::move(buf));
    for (size_t i = 0; i < reply.meta.size(); ++i) {
        BOOST_REQUIRE_EQUAL(reply.meta[i].group, result.meta[i].group);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_flushed_log_index,
          result.meta[i].last_flushed_log_index);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_dirty_log_index,
          result.meta[i].last_dirty_log_index);
        BOOST_REQUIRE_EQUAL(reply.meta[i].term, result.meta[i].term);
        BOOST_REQUIRE_EQUAL(reply.meta[i].result, result.meta[i].result);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_term_base_offset,
          result.meta[i].last_term_base_offset);
        BOOST_REQUIRE_EQUAL(reply.meta[i].node_id, result.meta[i].node_id);
        if (i != 0) {
            // we do not require failure node_id to match as it will be replaced
            // with node id coming from successful response
            BOOST_REQUIRE_EQUAL(
              reply.meta[i].target_node_id, result.meta[i].target_node_id);
        }
    }
}

SEASTAR_THREAD_TEST_CASE(
  heartbeat_response_with_failures_and_not_initialized_groups) {
    raft::heartbeat_reply reply;
    // first reply is a success but group is empty
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{0}, model::revision_id{0}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(1),
      .term = model::term_id(1),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::success});

    /**
     * Two other replies are failures
     */
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(2),
      .term = model::term_id(1),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::group_unavailable});

    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(3),
      .term = model::term_id(5),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::group_unavailable});

    auto buf = serde::to_iobuf(reply);

    auto result = serde::from_iobuf<raft::heartbeat_reply>(std::move(buf));
    for (size_t i = 0; i < reply.meta.size(); ++i) {
        BOOST_REQUIRE_EQUAL(reply.meta[i].group, result.meta[i].group);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_flushed_log_index,
          result.meta[i].last_flushed_log_index);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_dirty_log_index,
          result.meta[i].last_dirty_log_index);
        BOOST_REQUIRE_EQUAL(reply.meta[i].term, result.meta[i].term);
        BOOST_REQUIRE_EQUAL(reply.meta[i].result, result.meta[i].result);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_term_base_offset,
          result.meta[i].last_term_base_offset);
        BOOST_REQUIRE_EQUAL(reply.meta[i].node_id, result.meta[i].node_id);
        // all replies should have node id set
        BOOST_REQUIRE_EQUAL(
          reply.meta[0].target_node_id.id(),
          result.meta[i].target_node_id.id());
    }
}

SEASTAR_THREAD_TEST_CASE(heartbeat_response_with_only_failures) {
    raft::heartbeat_reply reply;
    // first reply is a success but group is empty
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(1),
      .term = model::term_id(1),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::group_unavailable});

    /**
     * Two other replies are failures
     */
    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(2),
      .term = model::term_id(1),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::group_unavailable});

    reply.meta.push_back(raft::append_entries_reply{
      .target_node_id = raft::vnode(model::node_id{}, model::revision_id{}),
      .node_id = raft::vnode(model::node_id(1), model::revision_id(1)),
      .group = raft::group_id(3),
      .term = model::term_id(5),
      .last_flushed_log_index = model::offset{},
      .last_dirty_log_index = model::offset{},
      .last_term_base_offset = model::offset{},
      .result = raft::reply_result::group_unavailable});

    auto buf = serde::to_iobuf(reply);

    auto result = serde::from_iobuf<raft::heartbeat_reply>(std::move(buf));
    for (size_t i = 0; i < reply.meta.size(); ++i) {
        BOOST_REQUIRE_EQUAL(reply.meta[i].group, result.meta[i].group);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_flushed_log_index,
          result.meta[i].last_flushed_log_index);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_dirty_log_index,
          result.meta[i].last_dirty_log_index);
        BOOST_REQUIRE_EQUAL(reply.meta[i].term, result.meta[i].term);
        BOOST_REQUIRE_EQUAL(reply.meta[i].result, result.meta[i].result);
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].last_term_base_offset,
          result.meta[i].last_term_base_offset);
        BOOST_REQUIRE_EQUAL(reply.meta[i].node_id, result.meta[i].node_id);
        // all replies should have node id set
        BOOST_REQUIRE_EQUAL(
          reply.meta[i].target_node_id, result.meta[i].target_node_id);
    }
}

SEASTAR_THREAD_TEST_CASE(snapshot_metadata_roundtrip) {
    auto n1 = model::random_broker(0, 100);
    auto n2 = model::random_broker(0, 100);
    auto n3 = model::random_broker(0, 100);
    std::vector<model::broker> nodes{n1, n2, n3};
    raft::group_nodes current{
      .voters
      = {raft::vnode(n1.id(), model::revision_id(1)), raft::vnode(n3.id(), model::revision_id(3))},
      .learners = {raft::vnode(n2.id(), model::revision_id(1))}};

    raft::group_configuration cfg(
      nodes, current, model::revision_id(0), std::nullopt);

    auto ct = ss::lowres_clock::now();
    raft::offset_translator_delta delta{
      random_generators::get_int<int64_t>(5000)};
    raft::snapshot_metadata metadata{
      .last_included_index = model::offset(123),
      .last_included_term = model::term_id(32),
      .latest_configuration = cfg,
      .cluster_time = ct,
      .log_start_delta = delta};

    auto d = serialize_roundtrip_adl(std::move(metadata));

    BOOST_REQUIRE_EQUAL(d.last_included_index, model::offset(123));
    BOOST_REQUIRE_EQUAL(d.last_included_term, model::term_id(32));
    BOOST_REQUIRE(
      std::chrono::time_point_cast<std::chrono::milliseconds>(d.cluster_time)
      == std::chrono::time_point_cast<std::chrono::milliseconds>(ct));
    BOOST_REQUIRE_EQUAL(d.latest_configuration.all_nodes(), cfg.all_nodes());
    BOOST_REQUIRE_EQUAL(d.log_start_delta, delta);
}

SEASTAR_THREAD_TEST_CASE(snapshot_metadata_backward_compatibility) {
    auto bp1 = model::random_broker_properties();
    // Zero out the available_memory_bytes field which isn't supported
    // by adl since the field was added after serde serialization became
    // the default.
    bp1.available_memory_bytes = 0;
    auto n1 = model::random_broker(0, 100, bp1);

    auto bp2 = model::random_broker_properties();
    bp2.available_memory_bytes = 0;
    auto n2 = model::random_broker(0, 100, bp2);

    auto bp3 = model::random_broker_properties();
    bp3.available_memory_bytes = 0;
    auto n3 = model::random_broker(0, 100, bp3);

    std::vector<model::broker> nodes{n1, n2, n3};
    raft::group_nodes current{
      .voters
      = {raft::vnode(n1.id(), model::revision_id(1)), raft::vnode(n3.id(), model::revision_id(3))},
      .learners = {raft::vnode(n2.id(), model::revision_id(1))}};

    raft::group_configuration cfg(
      nodes, current, model::revision_id(0), std::nullopt);
    auto c = cfg;
    auto ct = ss::lowres_clock::now();
    // serialize using old format (no version included)
    iobuf serialize_buf;
    reflection::serialize(
      serialize_buf,
      model::offset(123),
      model::term_id(32),
      std::move(cfg),
      std::chrono::duration_cast<std::chrono::milliseconds>(
        ct.time_since_epoch()));

    iobuf_parser parser(std::move(serialize_buf));

    // deserialize with current format
    raft::snapshot_metadata metadata
      = reflection::adl<raft::snapshot_metadata>{}.from(parser);

    BOOST_REQUIRE_EQUAL(metadata.last_included_index, model::offset(123));
    BOOST_REQUIRE_EQUAL(metadata.last_included_term, model::term_id(32));
    BOOST_REQUIRE(
      std::chrono::time_point_cast<std::chrono::milliseconds>(
        metadata.cluster_time)
      == std::chrono::time_point_cast<std::chrono::milliseconds>(ct));
    BOOST_REQUIRE_EQUAL(metadata.latest_configuration, c);
    BOOST_REQUIRE_EQUAL(
      metadata.log_start_delta, raft::offset_translator_delta{});
}

SEASTAR_THREAD_TEST_CASE(append_entries_request_serde_wrapper_serde) {
    chunked_vector<model::record_batch> batches{
      model::test::make_random_batches(model::offset(1), 3, false).get()};
    chunked_vector<model::record_batch> reference_batches;
    for (auto& b : batches) {
        b.set_term(model::term_id(123));
        reference_batches.push_back(b.share());
    }

    auto meta = raft::protocol_metadata{
      .group = raft::group_id(1),
      .commit_index = model::offset(100),
      .term = model::term_id(10),
      .prev_log_index = model::offset(99),
      .prev_log_term = model::term_id(-1),
      .last_visible_index = model::offset(200),
      .dirty_offset = model::offset(99),
    };
    raft::append_entries_request req(
      raft::vnode(model::node_id(1), model::revision_id(10)),
      raft::vnode(model::node_id(10), model::revision_id(101)),
      meta,
      std::move(batches),
      0);

    const auto src_node = req.source_node();
    const auto target_node = req.target_node();

    raft::append_entries_request_serde_wrapper wrapper(std::move(req));

    iobuf buf;
    serde::write_async(buf, std::move(wrapper)).get();
    iobuf_parser parser(std::move(buf));
    auto decoded_wrapper
      = serde::read_async<raft::append_entries_request_serde_wrapper>(parser)
          .get();
    auto decoded_req = std::move(decoded_wrapper).release();

    BOOST_REQUIRE_EQUAL(decoded_req.source_node(), src_node);
    BOOST_REQUIRE_EQUAL(decoded_req.target_node(), target_node);
    BOOST_REQUIRE_EQUAL(decoded_req.metadata().group, meta.group);
    BOOST_REQUIRE_EQUAL(decoded_req.metadata().commit_index, meta.commit_index);
    BOOST_REQUIRE_EQUAL(decoded_req.metadata().term, meta.term);
    BOOST_REQUIRE_EQUAL(
      decoded_req.metadata().prev_log_index, meta.prev_log_index);
    BOOST_REQUIRE_EQUAL(
      decoded_req.metadata().prev_log_term, meta.prev_log_term);
    BOOST_REQUIRE_EQUAL(
      decoded_req.metadata().last_visible_index, meta.last_visible_index);
    BOOST_REQUIRE_EQUAL(decoded_req.metadata().dirty_offset, meta.dirty_offset);

    verify_batches(reference_batches, decoded_req.batches());
}
