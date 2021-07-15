#!/bin/bash -x

set -e

mkdir -p bin
wd=$PWD
cd ../..
go build -o "$wd/bin" ./go/cmd/...
cd "$wd"
bin/deploy-heyp mk-bundle -bin ../../bazel-bin -auxbin ../../aux-bin
