/*
 * Copyright 2022 Redpanda Data, Inc.
 *
 * Use of this software is governed by the Business Source License
 * included in the file licenses/BSL.md
 *
 * As of the Change Date specified in that file, in accordance with
 * the Business Source License, use of this software will be governed
 * by the Apache License, Version 2.0
 */
#pragma once

#include "cluster/tx_protocol_types.h"
#include "compat/generator.h"
#include "model/tests/randoms.h"
#include "model/timeout_clock.h"
#include "test_utils/randoms.h"

namespace compat {

template<>
struct instance_generator<cluster::init_tm_tx_request> {
    static cluster::init_tm_tx_request random() {
        return cluster::init_tm_tx_request(
          tests::random_named_string<kafka::transactional_id>(),
          tests::random_duration<std::chrono::milliseconds>(),
          tests::random_duration<model::timeout_clock::duration>());
    }
    static std::vector<cluster::init_tm_tx_request> limits() { return {}; }
};

template<>
struct instance_generator<cluster::init_tm_tx_reply> {
    static cluster::init_tm_tx_reply random() {
        return cluster::init_tm_tx_reply(
          model::random_producer_identity(),
          cluster::tx::errc(random_generators::get_int<int>(
            static_cast<int>(cluster::tx::errc::none),
            static_cast<int>(cluster::tx::errc::invalid_txn_state))));
    }
    static std::vector<cluster::init_tm_tx_reply> limits() { return {}; }
};

} // namespace compat
