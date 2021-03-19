#!/bin/bash

set -e

(
  for x in $PWD/heyp/proto/*.proto; do
    protoc \
      -I=$PWD \
      --go_out=$PWD \
      --go_opt=module=github.com/uluyol/heyp-agents \
      "$x"
  done
)
