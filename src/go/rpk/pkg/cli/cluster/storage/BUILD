load("@rules_go//go:def.bzl", "go_library")

go_library(
    name = "storage",
    srcs = [
        "cancel-mount.go",
        "list-mount.go",
        "list-mountable.go",
        "mount.go",
        "status-mount.go",
        "storage.go",
        "unmount.go",
    ],
    importpath = "github.com/redpanda-data/redpanda/src/go/rpk/pkg/cli/cluster/storage",
    visibility = ["//visibility:public"],
    deps = [
        "//src/go/rpk/pkg/adminapi",
        "//src/go/rpk/pkg/cli/cluster/storage/recovery",
        "//src/go/rpk/pkg/config",
        "//src/go/rpk/pkg/out",
        "@build_buf_gen_go_redpandadata_dataplane_protocolbuffers_go//redpanda/api/dataplane/v1alpha2",
        "@com_connectrpc_connect//:connect",
        "@com_github_redpanda_data_common_go_rpadmin//:rpadmin",
        "@com_github_spf13_afero//:afero",
        "@com_github_spf13_cobra//:cobra",
    ],
)
