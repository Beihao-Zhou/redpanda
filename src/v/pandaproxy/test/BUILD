load("//bazel:test.bzl", "redpanda_cc_btest", "redpanda_test_cc_library")

redpanda_test_cc_library(
    name = "utils",
    hdrs = [
        "utils.h",
    ],
    implementation_deps = [
    ],
    include_prefix = "pandaproxy/test",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/pandaproxy",
    ],
)

redpanda_test_cc_library(
    name = "fixture",
    hdrs = [
        "pandaproxy_fixture.h",
    ],
    implementation_deps = [
    ],
    include_prefix = "pandaproxy/test",
    visibility = ["//visibility:public"],
    deps = [
        "//src/v/http",
        "//src/v/pandaproxy",
        "//src/v/redpanda/tests:fixture",
    ],
)

redpanda_cc_btest(
    name = "errors_test",
    timeout = "short",
    srcs = [
        "errors.cc",
    ],
    deps = [
        "//src/v/kafka/protocol",
        "//src/v/pandaproxy",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
    ],
)

redpanda_cc_btest(
    name = "one_shot_test",
    timeout = "short",
    srcs = [
        "one_shot.cc",
    ],
    deps = [
        "//src/v/pandaproxy",
        "//src/v/test_utils:seastar_boost",
        "@boost//:range",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)

redpanda_cc_btest(
    name = "kafka_client_cache_test",
    timeout = "short",
    srcs = [
        "kafka_client_cache.cc",
    ],
    deps = [
        "//src/v/config",
        "//src/v/kafka/client",
        "//src/v/pandaproxy",
        "//src/v/security",
        "//src/v/ssx:future_util",
        "//src/v/test_utils:seastar_boost",
        "@boost//:test",
        "@seastar",
        "@seastar//:testing",
    ],
)
