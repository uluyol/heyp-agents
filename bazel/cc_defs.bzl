load("@rules_cc//cc:defs.bzl", "cc_binary")

# HEYP C++ binary targets should be specified with this function.
def heyp_cc_binary(
        name,
        srcs = [],
        data = [],
        testonly = 0,
        copts = [],
        visibility = None,
        deps = [],
        linkopts = []):
    cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        copts = copts,
        linkopts = linkopts,
        testonly = testonly,
        linkstatic = 1,
        visibility = visibility,
        malloc = select({
            "@bazel_tools//platforms:linux": "@com_google_tcmalloc//tcmalloc",
            "//conditions:default": "@bazel_tools//tools/cpp:malloc",
        }),
        deps = deps,
    )
