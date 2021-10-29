#!/bin/bash -x

set -e

TOOLCHAIN=${TOOLCHAIN:-/w/uluyol/fp-toolchain}

mkdir -p bin
wd=$PWD
cd ../..
env CGO_LDFLAGS=-L$TOOLCHAIN/clang+llvm/lib go build -o "$wd/bin" ./go/cmd/...
if [[ $1 == cmdsonly ]]; then
    exit
fi
cd "$wd"
bin/deploy-heyp mk-bundle -bin ../../bazel-bin -auxbin ../../aux-bin
