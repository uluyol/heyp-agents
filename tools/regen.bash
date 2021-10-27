#!/bin/bash

set -e

(
  for x in $PWD/heyp/proto/*.proto; do
    vtproto_flags=()
    if [[ $x =~ /heyp.proto$ ]]; then
      vtproto_flags=(
        --go-vtproto_out=$PWD --plugin protoc-gen-go-vtproto="${GOBIN}/protoc-gen-go-vtproto"
        --go-vtproto_opt=features=marshal+unmarshal+size
        --go-vtproto_opt=module=github.com/uluyol/heyp-agents
      )
    fi
    protoc \
      -I=$PWD \
      --go_out=$PWD \
      --go_opt=module=github.com/uluyol/heyp-agents \
      --go-grpc_out=$PWD --plugin protoc-gen-go-grpc="${GOBIN}/protoc-gen-go-grpc" \
      --go-grpc_opt=module=github.com/uluyol/heyp-agents \
      "${vtproto_flags[@]}" \
      "$x"
  done
)
