load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

_HDRHISTOGRAM_C_HEADERS = [
    "hdr_histogram.h",
    "hdr_histogram_log.h",
    "hdr_interval_recorder.h",
    "hdr_thread.h",
    "hdr_time.h",
    "hdr_writer_reader_phaser.h",
]

# Used as src in build
_HDRHISTOGRAM_C_LOCAL_HEADERS = ["src/" + hdr for hdr in _HDRHISTOGRAM_C_HEADERS]

# Exported
_HDRHISTOGRAM_C_PREFIXED_HEADERS = ["hdrhistogram_c/include/" + hdr for hdr in _HDRHISTOGRAM_C_HEADERS]

# In order to limit the damage from the `includes` propagation
# via `:hdrhistogram_c`, copy the public headers to a subdirectory and
# expose those.
genrule(
    name = "copy_public_headers",
    srcs = _HDRHISTOGRAM_C_LOCAL_HEADERS,
    outs = _HDRHISTOGRAM_C_PREFIXED_HEADERS,
    cmd = "cp $(SRCS) $(@D)/hdrhistogram_c/include/",
    visibility = ["//visibility:private"],
)

cc_library(
    name = "hdrhistogram_c",
    srcs = [
        "src/hdr_atomic.h",
        "src/hdr_encoding.c",
        "src/hdr_encoding.h",
        "src/hdr_endian.h",
        "src/hdr_histogram.c",
        "src/hdr_histogram_log.c",
        "src/hdr_interval_recorder.c",
        "src/hdr_malloc.h",
        "src/hdr_tests.h",
        "src/hdr_thread.c",
        "src/hdr_time.c",
        "src/hdr_writer_reader_phaser.c",
    ] + _HDRHISTOGRAM_C_LOCAL_HEADERS,
    hdrs = _HDRHISTOGRAM_C_PREFIXED_HEADERS,
    includes = ["hdrhistogram_c/include"],
    linkopts = [
        "-pthread",
        "-lm",
    ] + select({
        "@platforms//os:linux": ["-lrt"],
        "//conditions:default": [],
    }),
    deps = ["@net_zlib//:zlib"],
)
