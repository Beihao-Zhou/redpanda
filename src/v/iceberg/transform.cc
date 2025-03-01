// Copyright 2024 Redpanda Data, Inc.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.md
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0
#include "iceberg/transform.h"

#include <fmt/ostream.h>

namespace iceberg {

namespace {

struct transform_comparison_visitor {
    template<typename T, typename U>
    bool operator()(const T&, const U&) const {
        static_assert(!std::is_same<T, U>::value);
        return false;
    }
    bool
    operator()(const bucket_transform& lhs, const bucket_transform& rhs) const {
        return lhs.n == rhs.n;
    }
    bool operator()(
      const truncate_transform& lhs, const truncate_transform& rhs) const {
        return lhs.length == rhs.length;
    }
    template<typename T>
    bool operator()(const T&, const T&) const {
        return true;
    }
};

struct print_visitor {
    std::ostream& os;

    void operator()(const identity_transform&) { os << "identity"; }
    void operator()(const bucket_transform& bt) {
        fmt::print(os, "bucket({})", bt.n);
    }
    void operator()(const truncate_transform& tt) {
        fmt::print(os, "truncate({})", tt.length);
    }
    void operator()(const year_transform&) { os << "year"; }
    void operator()(const month_transform&) { os << "month"; }
    void operator()(const day_transform&) { os << "day"; }
    void operator()(const hour_transform&) { os << "hour"; }
    void operator()(const void_transform&) { os << "void"; }
};

} // namespace

bool operator==(const transform& lhs, const transform& rhs) {
    return std::visit(transform_comparison_visitor{}, lhs, rhs);
}

std::ostream& operator<<(std::ostream& os, const transform& t) {
    std::visit(print_visitor{os}, t);
    return os;
}

} // namespace iceberg
