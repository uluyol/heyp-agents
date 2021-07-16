#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

outdir=${1%/}

rm -rf "$outdir"

exec bin/deploy-heyp gen-configs \
    -i config.star \
    -o "$outdir/configs" \
    -rspec rspec.xml \
    -ssh-user uluyol
