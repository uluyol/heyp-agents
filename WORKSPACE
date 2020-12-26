workspace(name = "heyp-agents")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_absl",
    strip_prefix = "abseil-cpp-1918ad2ae38aa32c74b558b322479a8efdd76363",
    urls = ["https://github.com/abseil/abseil-cpp/archive/1918ad2ae38aa32c74b558b322479a8efdd76363.zip"],
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
