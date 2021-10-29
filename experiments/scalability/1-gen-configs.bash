#!/bin/bash

set -e

strjoin() {
  local IFS="$1"
  shift
  echo "$*"
}

if [[ $# -lt 1 ]]; then
  echo usage: ./1-gen-configs.bash outdir config config...
  exit 1
fi

outdir=${1%/}
shift
configs=("$@")
if [[ ${#configs[@]} -lt 1 ]]; then
  configs=(rlsweep)
fi

rm -rf "$outdir"

exec bin/deploy-heyp gen-configs \
    -i config.star \
    -o "$outdir/configs" \
    -oshard "$outdir/shards" \
    -rspec "$(strjoin ';' rspec*.xml)" \
    -ssh-user uluyol \
    "${configs[@]}"
