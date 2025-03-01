load("@rules_go//go:def.bzl", "go_library", "go_test")

go_library(
    name = "bundle",
    srcs = [
        "bundle.go",
        "bundle_all.go",
        "bundle_k8s_linux.go",
        "bundle_linux.go",
    ],
    importpath = "github.com/redpanda-data/redpanda/src/go/rpk/pkg/cli/debug/bundle",
    visibility = ["//visibility:public"],
    deps = [
        "//src/go/rpk/pkg/cli/debug/common",
        "//src/go/rpk/pkg/config",
        "//src/go/rpk/pkg/httpapi",
        "//src/go/rpk/pkg/kafka",
        "//src/go/rpk/pkg/out",
        "@com_github_docker_go_units//:go-units",
        "@com_github_spf13_afero//:afero",
        "@com_github_spf13_cobra//:cobra",
        "@com_github_twmb_franz_go//pkg/kgo",
        "@io_k8s_apimachinery//pkg/labels",
    ] + select({
        "@rules_go//go/platform:android": [
            "//src/go/rpk/pkg/adminapi",
            "//src/go/rpk/pkg/os",
            "//src/go/rpk/pkg/system",
            "//src/go/rpk/pkg/system/syslog",
            "@com_github_avast_retry_go//:retry-go",
            "@com_github_beevik_ntp//:ntp",
            "@com_github_hashicorp_go_multierror//:go-multierror",
            "@com_github_redpanda_data_common_go_rpadmin//:rpadmin",
            "@com_github_twmb_franz_go_pkg_kadm//:kadm",
            "@in_gopkg_yaml_v3//:yaml_v3",
            "@io_k8s_api//authorization/v1:authorization",
            "@io_k8s_api//core/v1:core",
            "@io_k8s_apimachinery//pkg/apis/meta/v1:meta",
            "@io_k8s_client_go//kubernetes",
            "@io_k8s_client_go//rest",
            "@org_golang_x_sys//unix",
            "@org_uber_go_zap//:zap",
        ],
        "@rules_go//go/platform:linux": [
            "//src/go/rpk/pkg/adminapi",
            "//src/go/rpk/pkg/os",
            "//src/go/rpk/pkg/system",
            "//src/go/rpk/pkg/system/syslog",
            "@com_github_avast_retry_go//:retry-go",
            "@com_github_beevik_ntp//:ntp",
            "@com_github_hashicorp_go_multierror//:go-multierror",
            "@com_github_redpanda_data_common_go_rpadmin//:rpadmin",
            "@com_github_twmb_franz_go_pkg_kadm//:kadm",
            "@in_gopkg_yaml_v3//:yaml_v3",
            "@io_k8s_api//authorization/v1:authorization",
            "@io_k8s_api//core/v1:core",
            "@io_k8s_apimachinery//pkg/apis/meta/v1:meta",
            "@io_k8s_client_go//kubernetes",
            "@io_k8s_client_go//rest",
            "@org_golang_x_sys//unix",
            "@org_uber_go_zap//:zap",
        ],
        "//conditions:default": [],
    }),
)

go_test(
    name = "bundle_test",
    size = "small",
    srcs = ["bundle_test.go"],
    embed = [":bundle"],
    deps = select({
        "@rules_go//go/platform:android": [
            "//src/go/rpk/pkg/config",
            "@com_github_spf13_afero//:afero",
            "@com_github_stretchr_testify//require",
        ],
        "@rules_go//go/platform:linux": [
            "//src/go/rpk/pkg/config",
            "@com_github_spf13_afero//:afero",
            "@com_github_stretchr_testify//require",
        ],
        "//conditions:default": [],
    }),
)
