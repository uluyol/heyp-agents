#!/usr/bin/env bash
#
# Builds newer, statically linked versions of select iproute2 tools.

set -e

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat >"$TMP/build.bash" <<EOF
set -e
apt-get update && \
  env DEBIAN_FRONTEND=noninteractive apt-get install -y --force-yes \
  bison flex libxtables-dev libatm1-dev libbsd-dev libcap-dev libdb-dev libelf-dev libmnl-dev libselinux1-dev linux-libc-dev make pkg-config wget zlib1g-dev
mkdir /build && cd /build
wget -qO- https://mirrors.edge.kernel.org/pub/linux/utils/net/iproute2/iproute2-5.15.0.tar.xz | tar xJ

# Initial build against shared libraries.
# Not all binaries can successfully link statically.
cd iproute2-5.15.0 && ./configure && make -j4

# Delete and rebuild ss with static linkage.
rm misc/ss && make CFLAGS=-O2 LDFLAGS=-static
cp misc/ss /v/ss
EOF

docker run --rm -v "$TMP:/v" ubuntu:20.04 bash /v/build.bash
mkdir -p prebuilt
cp "$TMP/ss" prebuilt
