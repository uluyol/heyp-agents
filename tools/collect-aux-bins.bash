#!/bin/bash

set -e

TOINSTALL=${@:-envoy fortio collect-host-stats firecracker graceful-stop host-agent-sim prebuilt}

ENVOY_URL="https://archive.tetratelabs.io/envoy/download/v1.20.1/envoy-v1.20.1-linux-amd64.tar.xz"
FIRECRACKER_URL="https://github.com/firecracker-microvm/firecracker/releases/download/v0.25.2/firecracker-v0.25.2-x86_64.tgz"

mkdir -p aux-bin

for cmd in $TOINSTALL; do
  echo fetch $cmd
  case $cmd in
  envoy)
    wget -qO- "$ENVOY_URL" | tar xJ -C aux-bin
    mv aux-bin/envoy*linux*/bin/envoy aux-bin/envoy
    rm -r aux-bin/envoy*linux*
    ;;
  firecracker)
    wget -qO- "$FIRECRACKER_URL" | tar xz -C aux-bin
    mv aux-bin/release-*-x86_64/firecracker-v*-x86_64 aux-bin/firecracker
    rm -r aux-bin/release-*-x86_64
    ;;
  fortio)
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 GOBIN=$PWD/aux-bin go install fortio.org/fortio@v1.16.0
    GOOS=linux GOARCH=amd64 ./wrapgo.bash build -o $PWD/aux-bin ./go/cmd/fortio-client ./go/cmd/vfortio
    ;;
  collect-host-stats)
    GOOS=linux GOARCH=amd64 ./wrapgo.bash build -o $PWD/aux-bin ./go/cmd/collect-host-stats
    ;;
  graceful-stop)
    GOOS=linux GOARCH=amd64 ./wrapgo.bash build -o $PWD/aux-bin ./go/cmd/graceful-stop
    ;;
  host-agent-sim)
    GOOS=linux GOARCH=amd64 ./wrapgo.bash build -o $PWD/aux-bin ./go/cmd/host-agent-sim
    ;;
  prebuilt)
    cp prebuilt/* aux-bin/
    ;;
  *)
    echo "unknown cmd $cmd"
    exit 2
    ;;
  esac
done

chmod +x aux-bin/*
