#!/bin/bash

set -e

if [[ $# -ne 2 ]]; then
  echo bad
  exit 1
fi

shardfile=${1%/}
rspecfile=$2

sed -E "s|(.*)|${shardfile%shards/*}configs/\\1.textproto|" \
    "$shardfile" \
  | xargs bin/deploy-heyp update-config \
    -rspec "$rspecfile" \
    -ssh-user uluyol \
