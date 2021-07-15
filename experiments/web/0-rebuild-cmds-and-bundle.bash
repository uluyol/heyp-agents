#!/bin/bash

set -e

wd=$PWD
cd ../..
go build -o "$wd/bin" ./go/cmd/...
cd "$wd"
bin/deploy-heyp mk-bundle -bin /w/uluyol/heyp-agents/bazel-bin -auxbin /w/uluyol/heyp-agents/aux-bin
