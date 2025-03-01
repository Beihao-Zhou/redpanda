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

#include "base/seastarx.h"

#include <seastar/core/sstring.hh>

#include <optional>
#include <vector>

namespace config {

std::optional<std::pair<ss::sstring, int64_t>>
parse_connection_rate_override(const ss::sstring& raw_option);

std::optional<ss::sstring>
validate_connection_rate(const std::vector<ss::sstring>& ips_with_limit);

std::optional<ss::sstring>
validate_sasl_mechanisms(const std::vector<ss::sstring>& mechanisms);

std::optional<ss::sstring>
validate_http_authn_mechanisms(const std::vector<ss::sstring>& mechanisms);

bool oidc_is_enabled_http();
bool oidc_is_enabled_kafka();

std::optional<ss::sstring> validate_0_to_1_ratio(const double d);

std::optional<ss::sstring>
validate_non_empty_string_vec(const std::vector<ss::sstring>&);

std::optional<ss::sstring>
validate_non_empty_string_opt(const std::optional<ss::sstring>&);

std::optional<ss::sstring>
validate_audit_event_types(const std::vector<ss::sstring>& vs);

std::optional<ss::sstring>
validate_audit_excluded_topics(const std::vector<ss::sstring>&);

std::optional<ss::sstring>
validate_api_endpoint(const std::optional<ss::sstring>& os);

std::optional<ss::sstring> validate_tombstone_retention_ms(
  const std::optional<std::chrono::milliseconds>& ms);

std::optional<ss::sstring>
validate_iceberg_partition_spec(const ss::sstring& spec);

}; // namespace config
