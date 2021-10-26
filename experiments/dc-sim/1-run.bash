#!/bin/bash

set -e


if [[ $# -lt 1 ]]; then
  echo usage: $0 outdir >&2
  exit 1
fi

outdir=${1%/}

mkdir -p "$outdir"
cp config.yaml "$outdir/config.yaml"
bin/dc-control-sim \
    -c "$outdir/config.yaml" \
    -o "$outdir/sim-data.json"
