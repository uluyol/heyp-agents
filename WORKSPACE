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
    sha256 = "f8db92e5d3b9a880797b5f04839b36e1880253a97fd590db534f2efa5560fe43",
    strip_prefix = "abseil-cpp-732c6540c19610d2653ce73c09eb6cb66da15f42",
    urls = ["https://github.com/abseil/abseil-cpp/archive/732c6540c19610d2653ce73c09eb6cb66da15f42.zip"],
)

http_archive(
    name = "gtest",
    sha256 = "03930e55830249eb0bf58c52bcacc7fbe309d9208f1048bb3e93466582cd7b0f",
    strip_prefix = "googletest-eaf9a3fd77869cf95befb87455a2e2a2e85044ff",
    urls = ["https://github.com/google/googletest/archive/eaf9a3fd77869cf95befb87455a2e2a2e85044ff.zip"],
)

http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
    strip_prefix = "gflags-2.2.2",
    urls = ["https://github.com/gflags/gflags/archive/v2.2.2.tar.gz"],
)

http_archive(
    name = "glog",
    sha256 = "83f3099c9c5fd81f0a4ee76a24b08134d83e8da4101771334e1fa52d41d27385",
    strip_prefix = "glog-8d40d7564d4a06d77d707d7c4a50c8b5dc45dd80",
    urls = ["https://github.com/google/glog/archive/8d40d7564d4a06d77d707d7c4a50c8b5dc45dd80.zip"],
)

http_archive(
    name = "com_google_benchmark",
    strip_prefix = "benchmark-bf585a2789e30585b4e3ce6baf11ef2750b54677",
    urls = ["https://github.com/google/benchmark/archive/bf585a2789e30585b4e3ce6baf11ef2750b54677.zip"],
)

http_archive(
    name = "com_github_nelhage_rules_boost",
    sha256 = "f37ef75e6dff554cd97cac3e4640a9933df3730ccb2a061f525528a69a622f42",
    strip_prefix = "rules_boost-4ed7be18eef9798fb7f8c52c1235818be6b8d0fe",
    urls = ["https://github.com/nelhage/rules_boost/archive/4ed7be18eef9798fb7f8c52c1235818be6b8d0fe.zip"],
)

load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")

boost_deps()

http_archive(
    name = "com_github_grpc_grpc",
    sha256 = "509a2bc258e20a9573555733e6a126048f1beeb321c18c969758ccce1171ddf7",
    strip_prefix = "grpc-ab3096f12c92044b9d0a38c1c2dfcbea58488da6",
    urls = [
        "https://github.com/grpc/grpc/archive/ab3096f12c92044b9d0a38c1c2dfcbea58488da6.tar.gz",
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
    strip_prefix = "rules_foreign_cc-b66074cdd25589cded2bc8cbf624854ad72134c1",
    url = "https://github.com/bazelbuild/rules_foreign_cc/archive/b66074cdd25589cded2bc8cbf624854ad72134c1.zip",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

# This sets up some common toolchains for building targets. For more details, please see
# https://github.com/bazelbuild/rules_foreign_cc/tree/main/docs#rules_foreign_cc_dependencies
rules_foreign_cc_dependencies()

http_archive(
    name = "com_google_tcmalloc",
    sha256 = "1b806acb7abef750d1cbf50aa2cbd7600b51565fbffd5431d7bd4b0ff2285b83",
    strip_prefix = "tcmalloc-c1938e80cb0d3610d6b4469c6a032076ceeafb7a",
    url = "https://github.com/google/tcmalloc/archive/c1938e80cb0d3610d6b4469c6a032076ceeafb7a.zip",
)
