#!/bin/bash

set -e

TOOLCHAIN=${TOOLCHAIN:-/w/uluyol/fp-toolchain}

mkdir -p bin

env CGO_LDFLAGS=-L$TOOLCHAIN/clang+llvm/lib go build \
    -o bin/dc-control-sim \
    github.com/uluyol/heyp-agents/go/cmd/dc-control-sim
