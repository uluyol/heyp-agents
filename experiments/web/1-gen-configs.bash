#!/bin/bash

set -e

strjoin() {
  local IFS="$1"
  shift
  echo "$*"
}

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

outdir=${1%/}

rm -rf "$outdir"

exec bin/deploy-heyp gen-configs \
    -i config.star \
    -o "$outdir/configs" \
    -oshard "$outdir/shards" \
    -rspec "$(strjoin ';' rspec*.xml)" \
    -ssh-user uluyol
