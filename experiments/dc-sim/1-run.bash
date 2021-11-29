#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo usage: $0 outdir >&2
  exit 1
fi

outdir=${1%/}
config=${CONFIG:-config.yaml}
runs=${RUNS:-100}

mkdir -p "$outdir"
cp "$config" "$outdir/config.yaml"
bin/dc-control-sim \
  -c "$outdir/config.yaml" \
  -o "$outdir/sim-data.json" \
  -runs "$runs" \
  2>&1 |
  tee "$outdir/sim.log"
