#!/bin/bash

set -e

TOINSTALL=${@:-envoy fortio}

ENVOY_URL="https://dl.getenvoy.io/public/raw/files/getenvoy-envoy-1.18.3.p0.g98c1c9e-1p77.gb76c773-linux-glibc-release-x86_64.tar.xz"

mkdir -p aux-bin

for cmd in $TOINSTALL; do
  echo fetch $cmd
  case $cmd in
  envoy)
    wget -qO- "$ENVOY_URL" | tar xJ -C aux-bin
    mv aux-bin/getenvoy*glibc*/bin/envoy aux-bin/envoy
    rm -r aux-bin/getenvoy*glibc*
    ;;
  fortio)
    GOOS=linux GOARCH=amd64 GOBIN=$PWD/aux-bin go install fortio.org/fortio@v1.16.0
    ;;
  *)
    echo "unknown cmd $cmd"
    exit 2
    ;;
  esac
done
