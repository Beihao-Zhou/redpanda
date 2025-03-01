load("//bazel:test.bzl", "redpanda_cc_bench", "redpanda_cc_btest", "redpanda_cc_gtest", "redpanda_test_cc_library")

redpanda_test_cc_library(
    name = "storage_e2e_fixture",
    hdrs = [
        "storage_e2e_fixture.h",
    ],
    include_prefix = "storage/tests",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/kafka/server/tests:kafka_test_utils",
        "//src/v/redpanda/tests:fixture",
        "//src/v/storage",
        "//src/v/test_utils:scoped_config",
        "@seastar",
    ],
)

redpanda_test_cc_library(
    name = "manual_mixin",
    hdrs = [
        "manual_mixin.h",
    ],
    include_prefix = "storage/tests",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/test_utils:scoped_config",
    ],
)

redpanda_test_cc_library(
    name = "kvstore_fixture",
    hdrs = [
        "kvstore_fixture.h",
    ],
    include_prefix = "storage/tests",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/config",
        "//src/v/features",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
    ],
)

redpanda_test_cc_library(
    name = "random",
    hdrs = [
        "randoms.h",
    ],
    include_prefix = "storage/tests",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/storage",
    ],
)

redpanda_test_cc_library(
    name = "disk_log_builder_fixture",
    hdrs = [
        "disk_log_builder_fixture.h",
    ],
    include_prefix = "storage/tests",
    deps = [
        ":disk_log_builder",
    ],
)

redpanda_test_cc_library(
    name = "disk_log_builder",
    srcs = [
        "utils/disk_log_builder.cc",
    ],
    hdrs = [
        "utils/disk_log_builder.h",
    ],
    include_prefix = "storage/tests",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/base",
        "//src/v/features",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/ssx:sformat",
        "//src/v/storage",
        "@seastar",
    ],
)

redpanda_test_cc_library(
    name = "common",
    hdrs = [
        "common.h",
    ],
    include_prefix = "storage/tests",
    deps = [
        "//src/v/storage",
    ],
)

redpanda_test_cc_library(
    name = "storage_test_fixture",
    hdrs = [
        "storage_test_fixture.h",
    ],
    include_prefix = "storage/tests",
    deps = [
        "//src/v/compression",
        "//src/v/config",
        "//src/v/features",
        "//src/v/model/tests:random",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
    ],
)

redpanda_test_cc_library(
    name = "log_gap_analysis",
    srcs = [
        "utils/log_gap_analysis.cc",
    ],
    hdrs = [
        "utils/log_gap_analysis.h",
    ],
    include_prefix = "storage/tests",
    deps = [
        "//src/v/model",
        "//src/v/storage:parser_utils",
    ],
)

redpanda_cc_gtest(
    name = "segment_offset_tracker_test",
    timeout = "short",
    srcs = [
        "segment_offset_tracker_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/features",
        "//src/v/model",
        "//src/v/storage",
        "//src/v/storage:resources",
        "//src/v/test_utils:gtest",
        "@googletest//:gtest",
    ],
)

redpanda_cc_btest(
    name = "log_segment_appender_test",
    timeout = "short",
    srcs = [
        "log_segment_appender_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/bytes:iobuf",
        "//src/v/bytes:iostream",
        "//src/v/bytes:random",
        "//src/v/config",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:chunk_cache",
        "//src/v/storage:resources",
        "//src/v/storage:segment_appender",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@fmt",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "log_manager_test",
    timeout = "short",
    srcs = [
        "log_manager_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/bytes:random",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:logger",
        "//src/v/storage:record_batch_utils",
        "//src/v/storage:segment_appender",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "index_state_test",
    timeout = "short",
    srcs = [
        "index_state_test.cc",
    ],
    deps = [
        "//src/v/bytes",
        "//src/v/random:generators",
        "//src/v/serde",
        "//src/v/storage:index_state",
        "@boost//:test",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "batch_cache_test",
    timeout = "short",
    srcs = [
        "batch_cache_test.cc",
    ],
    deps = [
        "//src/v/bytes:random",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/storage:batch_cache",
        "//src/v/storage:record_batch_builder",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "batch_cache_reclaim_test",
    timeout = "short",
    srcs = [
        "batch_cache_reclaim_test.cc",
    ],
    target_compatible_with = select({
        "@seastar//:use_system_allocator": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
    deps = [
        "//src/v/model",
        "//src/v/storage:batch_cache",
        "//src/v/storage:record_batch_builder",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_gtest(
    name = "key_offset_map_test",
    timeout = "short",
    srcs = [
        "key_offset_map_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/storage:key_offset_map",
        "//src/v/test_utils:gtest",
        "@googletest//:gtest",
    ],
)

redpanda_cc_btest(
    name = "kvstore_test",
    timeout = "short",
    srcs = [
        "kvstore_test.cc",
    ],
    deps = [
        ":kvstore_fixture",
        "//src/v/bytes:random",
        "//src/v/config",
        "//src/v/random:generators",
        "//src/v/reflection:adl",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "appender_chunk_manipulations_test",
    timeout = "short",
    srcs = [
        "appender_chunk_manipulations.cc",
    ],
    deps = [
        "//src/v/config",
        "//src/v/random:generators",
        "//src/v/storage:segment_appender",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "backlog_controller_test",
    timeout = "short",
    srcs = [
        "backlog_controller_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@seastar",
    ],
)

redpanda_cc_btest(
    name = "concat_segment_reader_test",
    timeout = "short",
    srcs = [
        "concat_segment_reader_test.cc",
    ],
    deps = [
        ":disk_log_builder",
        "//src/v/bytes:iostream",
        "//src/v/model/tests:random",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "timequery_test",
    timeout = "short",
    srcs = [
        "timequery_test.cc",
    ],
    deps = [
        ":disk_log_builder_fixture",
        "//src/v/config",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
    ],
)

redpanda_cc_btest(
    name = "snapshot_test",
    timeout = "short",
    srcs = [
        "snapshot_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/bytes:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "record_batch_builder_test",
    timeout = "short",
    srcs = [
        "record_batch_builder_test.cc",
    ],
    deps = [
        "//src/v/bytes:iobuf",
        "//src/v/bytes:iobuf_parser",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/reflection:adl",
        "//src/v/storage:record_batch_builder",
        "//src/v/test_utils:seastar_boost",
        "@abseil-cpp//absl/container:btree",
        "@boost//:test",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "produce_consume_test",
    timeout = "short",
    srcs = [
        "produce_consume_test.cc",
    ],
    cpu = 1,
    deps = [
        ":disk_log_builder",
        "//src/v/model",
        "//src/v/test_utils:seastar_boost",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "offset_translator_state_test",
    timeout = "short",
    srcs = [
        "offset_translator_state_test.cc",
    ],
    cpu = 1,
    deps = [
        "//src/v/model",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "offset_to_filepos_test",
    timeout = "short",
    srcs = [
        "offset_to_filepos_test.cc",
    ],
    cpu = 1,
    deps = [
        "//src/v/storage",
        "//src/v/test_utils:archival",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "offset_index_utils_tests",
    timeout = "short",
    srcs = [
        "offset_index_utils_tests.cc",
    ],
    cpu = 1,
    deps = [
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "//src/v/test_utils:tmpbuf_file",
        "//src/v/utils:file_io",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "offset_translator_tests",
    timeout = "moderate",
    srcs = [
        "offset_translator_tests.cc",
    ],
    cpu = 8,
    deps = [
        "//src/v/bytes:random",
        "//src/v/model",
        "//src/v/raft",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:record_batch_builder",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_gtest(
    name = "scoped_file_tracker_test",
    timeout = "short",
    srcs = [
        "scoped_file_tracker_test.cc",
    ],
    deps = [
        "//src/v/storage",
        "//src/v/test_utils:gtest",
        "@abseil-cpp//absl/container:btree",
        "@googletest//:gtest",
    ],
)

redpanda_cc_gtest(
    name = "segment_deduplication_test",
    timeout = "short",
    srcs = [
        "segment_deduplication_test.cc",
    ],
    deps = [
        ":disk_log_builder",
        ":disk_log_builder_fixture",
        "//src/v/config",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:chunk_cache",
        "//src/v/storage:key_offset_map",
        "//src/v/test_utils:gtest",
        "@googletest//:gtest",
        "@seastar",
    ],
)

redpanda_cc_gtest(
    name = "readers_cache_test",
    timeout = "short",
    srcs = [
        "readers_cache_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/config",
        "//src/v/features",
        "//src/v/model",
        "//src/v/storage",
        "//src/v/storage:resources",
        "//src/v/test_utils:gtest",
        "@fmt",
        "@googletest//:gtest",
        "@seastar",
    ],
)

redpanda_cc_btest(
    name = "storage_e2e_single_thread",
    timeout = "moderate",
    srcs = [
        "storage_e2e_test.cc",
    ],
    cpu = 1,
    deps = [
        ":common",
        ":disk_log_builder",
        ":disk_log_builder_fixture",
        ":log_gap_analysis",
        ":storage_test_fixture",
        "//src/v/base",
        "//src/v/bytes",
        "//src/v/bytes:random",
        "//src/v/compression",
        "//src/v/config",
        "//src/v/features",
        "//src/v/finjector",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/reflection:adl",
        "//src/v/storage",
        "//src/v/storage:batch_cache",
        "//src/v/storage:record_batch_builder",
        "//src/v/storage:resources",
        "//src/v/test_utils:random",
        "//src/v/test_utils:seastar_boost",
        "//src/v/utils:directory_walker",
        "//src/v/utils:to_string",
        "@boost//:test",
        "@fmt",
        "@seastar",
    ],
)

redpanda_cc_btest(
    name = "segment_size_jitter_test",
    timeout = "short",
    srcs = [
        "segment_size_jitter_test.cc",
    ],
    deps = [
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "log_segment_reader_test",
    timeout = "short",
    srcs = [
        "log_segment_reader_test.cc",
    ],
    deps = [
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:logger",
        "//src/v/storage:parser_utils",
        "//src/v/storage:record_batch_utils",
        "//src/v/storage:segment_appender",
        "//src/v/storage/tests:disk_log_builder",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "offset_assignment_test",
    timeout = "short",
    srcs = [
        "offset_assignment_test.cc",
    ],
    deps = [
        "//src/v/model/tests:random",
        "//src/v/storage",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "log_truncate_test",
    timeout = "short",
    srcs = [
        "log_truncate_test.cc",
    ],
    deps = [
        "//src/v/model",
        "//src/v/storage",
        "//src/v/storage/tests:disk_log_builder",
        "//src/v/storage/tests:storage_test_fixture",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:range",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "compaction_index_format_tests",
    timeout = "short",
    srcs = [
        "compaction_index_format_tests.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/bytes",
        "//src/v/bytes:iobuf_parser",
        "//src/v/bytes:random",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/reflection:adl",
        "//src/v/storage",
        "//src/v/storage:logger",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:random",
        "//src/v/test_utils:seastar_boost",
        "//src/v/test_utils:tmpbuf_file",
        "//src/v/utils:vint",
        "@boost//:test",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "disk_log_builder_test",
    timeout = "short",
    srcs = [
        "disk_log_builder_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/storage",
        "//src/v/storage/tests:disk_log_builder_fixture",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "log_retention_tests",
    timeout = "short",
    srcs = [
        "log_retention_tests.cc",
    ],
    deps = [
        "//src/v/model",
        "//src/v/storage/tests:disk_log_builder",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "half_page_concurrent_dispatch",
    timeout = "short",
    srcs = [
        "half_page_concurrent_dispatch.cc",
    ],
    deps = [
        "//src/v/storage",
        "//src/v/storage:record_batch_builder",
        "//src/v/storage/tests:disk_log_builder",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "file_sanitizer_test",
    timeout = "short",
    srcs = [
        "file_sanitizer_test.cc",
    ],
    deps = [
        "//src/v/bytes:iobuf",
        "//src/v/bytes:iostream",
        "//src/v/storage",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "compaction_reducer_test",
    timeout = "short",
    srcs = [
        "compaction_reducer_test.cc",
    ],
    deps = [
        "//src/v/bytes:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:logger",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "batch_consumer_utils_test",
    timeout = "short",
    srcs = [
        "batch_consumer_utils_test.cc",
    ],
    deps = [
        "//src/v/bytes:iostream",
        "//src/v/model/tests:random",
        "//src/v/storage",
        "//src/v/storage/tests:disk_log_builder",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "log_replayer_test",
    timeout = "short",
    srcs = [
        "log_replayer_test.cc",
    ],
    deps = [
        "//src/v/base",
        "//src/v/features",
        "//src/v/model",
        "//src/v/model/tests:random",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:logger",
        "//src/v/storage:record_batch_utils",
        "//src/v/storage:resources",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "storage_e2e_fixture_test",
    timeout = "moderate",
    srcs = [
        "storage_e2e_fixture_test.cc",
    ],
    cpu = 1,
    deps = [
        "//src/v/kafka/server/tests:kafka_test_utils",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/redpanda/tests:fixture",
        "//src/v/storage",
        "//src/v/storage/tests:storage_e2e_fixture",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:scoped_config",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_gtest(
    name = "compaction_e2e_test",
    timeout = "moderate",
    srcs = [
        "compaction_e2e_test.cc",
    ],
    cpu = 1,
    deps = [
        "//src/v/base",
        "//src/v/kafka/server/tests:kafka_test_utils",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/redpanda/tests:fixture",
        "//src/v/storage",
        "//src/v/storage/tests:manual_mixin",
        "//src/v/test_utils:fixture",
        "//src/v/test_utils:gtest",
        "//src/v/test_utils:scoped_config",
        "//src/v/test_utils:seastar_boost",
        "//src/v/utils:directory_walker",
        "@abseil-cpp//absl/container:btree",
        "@googletest//:gtest",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "compaction_fuzz_test",
    timeout = "short",
    srcs = [
        "compaction_fuzz_test.cc",
    ],
    deps = [
        ":disk_log_builder",
        "//src/v/base",
        "//src/v/container:fragmented_vector",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:record_batch_builder",
        "//src/v/test_utils:seastar_boost",
        "@abseil-cpp//absl/container:btree",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_bench(
    name = "storage_rpbench",
    srcs = ["compaction_idx_bench.cc"],
    deps = [
        "//src/v/bytes:random",
        "//src/v/model",
        "//src/v/random:generators",
        "//src/v/storage",
        "//src/v/storage:logger",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_map",
        "@seastar",
        "@seastar//:benchmark",
    ],
)
