workspace(name = "heyp-agents")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_absl",
    strip_prefix = "abseil-cpp-184d2f8364bcb05e413ec4c72cad0cf86e712d1c",
    urls = ["https://github.com/abseil/abseil-cpp/archive/184d2f8364bcb05e413ec4c72cad0cf86e712d1c.zip"],
)

http_archive(
    name = "gtest",
    strip_prefix = "googletest-18f8200e3079b0e54fa00cb7ac55d4c39dcf6da6",
    urls = ["https://github.com/google/googletest/archive/18f8200e3079b0e54fa00cb7ac55d4c39dcf6da6.zip"],
)

http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
    strip_prefix = "gflags-2.2.2",
    urls = ["https://github.com/gflags/gflags/archive/v2.2.2.tar.gz"],
)

http_archive(
    name = "glog",
    strip_prefix = "glog-c8f8135a5720aee7de8328b42e4c43f8aa2e60aa",
    urls = ["https://github.com/google/glog/archive/c8f8135a5720aee7de8328b42e4c43f8aa2e60aa.zip"],
)

http_archive(
    name = "com_google_benchmark",
    strip_prefix = "benchmark-bf585a2789e30585b4e3ce6baf11ef2750b54677",
    urls = ["https://github.com/google/benchmark/archive/bf585a2789e30585b4e3ce6baf11ef2750b54677.zip"],
)

http_archive(
    name = "com_github_nelhage_rules_boost",
    strip_prefix = "rules_boost-fbac9be7640ecc0fab075233d394f08f1a37e449",
    urls = ["https://github.com/nelhage/rules_boost/archive/fbac9be7640ecc0fab075233d394f08f1a37e449.zip"],
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

http_archive(
    name = "com_github_grpc_grpc",
    strip_prefix = "grpc-893466127810d97d8f9ce8f2b312423a11912b48",
    urls = [
        "https://github.com/grpc/grpc/archive/893466127810d97d8f9ce8f2b312423a11912b48.tar.gz",
    ],
)

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

http_archive(
    name = "com_github_uluyol_routing_algos",
    strip_prefix = "routing-algos-a43ab38b70d0301e38f0114df651edbffa730d4c",
    urls = [
        "https://github.com/uluyol/routing-algos/archive/a43ab38b70d0301e38f0114df651edbffa730d4c.tar.gz",
    ],
)
