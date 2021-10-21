#!/bin/bash

TOOLCHAIN=${TOOLCHAIN:-/w/uluyol/fp-toolchain}

exec env CGO_LDFLAGS=-L$TOOLCHAIN/clang+llvm/lib go "$@"
