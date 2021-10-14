#!/bin/bash

set -e

bazel build \
  --config=clang-opt \
  --incompatible_require_linker_input_cc_api=false \
  //heyp/alg:export_bundle
rm -f go/calg/libexport_bundle.a
cp bazel-bin/heyp/alg/export_bundle.a go/calg/libexport_bundle.a
