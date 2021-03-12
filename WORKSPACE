workspace(name = "heyp-agents")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_ALL_CONTENT = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

http_archive(
    name = "libuv",
    build_file_content = _ALL_CONTENT,
    strip_prefix = "libuv-1.41.0",
    urls = [
        "https://github.com/libuv/libuv/archive/v1.41.0.zip",
    ],
)

http_archive(
    name = "com_google_absl",
    sha256 = "46e8249a18be8968777890803b57f9d7a5e5f3b6e432951f121114161cb51427",
    strip_prefix = "abseil-cpp-184d2f8364bcb05e413ec4c72cad0cf86e712d1c",
    urls = ["https://github.com/abseil/abseil-cpp/archive/184d2f8364bcb05e413ec4c72cad0cf86e712d1c.zip"],
)

http_archive(
    name = "gtest",
    sha256 = "355097d91fc3b412f122958f57cdc6ec42eb60d445b6c48be580ee639aed5b26",
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
    sha256 = "cbba86b5a63063999e0fc86de620a3ad22d6fd2aa5948bff4995dcd851074a0b",
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
    sha256 = "23365db335d049a97620ab6aa639eeee7bf0d0d262c778e8dbe70164b249c918",
    strip_prefix = "rules_boost-fbac9be7640ecc0fab075233d394f08f1a37e449",
    urls = ["https://github.com/nelhage/rules_boost/archive/fbac9be7640ecc0fab075233d394f08f1a37e449.zip"],
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

http_archive(
    name = "com_github_grpc_grpc",
    sha256 = "0af0b937d5a766006f3dba6507ebbe698128a868bc0c7e5dd63f6cbb764a4e9c",
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
    sha256 = "873dd78369f5a13f56df603bc35b2f7fdf2057ce54b8114c88af60282c16a3f3",
    strip_prefix = "routing-algos-a43ab38b70d0301e38f0114df651edbffa730d4c",
    urls = [
        "https://github.com/uluyol/routing-algos/archive/a43ab38b70d0301e38f0114df651edbffa730d4c.tar.gz",
    ],
)

http_archive(
    name = "net_zlib",
    build_file = "zlib.BUILD",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    strip_prefix = "zlib-1.2.11",
    # Use the same URL twice to trick bazel into re-trying if connection fails
    urls = [
        "https://zlib.net/zlib-1.2.11.tar.gz",
        "https://zlib.net/zlib-1.2.11.tar.gz",
    ],
)

http_archive(
    name = "hdrhistogram_c",
    build_file = "hdrhistogram_c.BUILD",
    sha256 = "be2e7163ee6247b370ccc52ac6276d0542f0cf1368646ba32c1290ab957503f4",
    strip_prefix = "HdrHistogram_c-706a9e06477472e4a34531640c15a641ba2c83cb",
    urls = [
        "https://github.com/HdrHistogram/HdrHistogram_c/archive/706a9e06477472e4a34531640c15a641ba2c83cb.zip",
    ],
)

http_archive(
    name = "rules_foreign_cc",
    sha256 = "c2cdcf55ffaf49366725639e45dedd449b8c3fe22b54e31625eb80ce3a240f1e",
    strip_prefix = "rules_foreign_cc-0.1.0",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/0.1.0.zip",
)

load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")

# This sets up some common toolchains for building targets. For more details, please see
# https://github.com/bazelbuild/rules_foreign_cc/tree/main/docs#rules_foreign_cc_dependencies
rules_foreign_cc_dependencies()

http_archive(
    name = "com_google_tcmalloc",
    strip_prefix = "tcmalloc-2258a12ee4516d27c675c2e968210f1836cf49db",
    url = "https://github.com/google/tcmalloc/archive/2258a12ee4516d27c675c2e968210f1836cf49db.zip",
)
