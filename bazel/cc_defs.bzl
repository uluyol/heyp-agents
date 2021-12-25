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
        fully_static_link = False,
        linkopts = []):
    features = []
    if fully_static_link:
        features = ["fully_static_link"]
    linkopts = linkopts + [
        "-fno-builtin-malloc",
        "-fno-builtin-calloc",
        "-fno-builtin-realloc",
        "-fno-builtin-free",
    ]
    cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        copts = copts,
        linkopts = linkopts,
        testonly = testonly,
        features = features,
        linkstatic = 1,
        visibility = visibility,
        malloc = select({
            "//heyp:sanitizer": "@bazel_tools//tools/cpp:malloc",
            "@bazel_tools//platforms:linux": "@com_google_tcmalloc//tcmalloc",
            "//conditions:default": "@bazel_tools//tools/cpp:malloc",
        }),
        deps = deps,
    )
