#!/bin/bash
#
# Run from root of repo

set -e

for dist in uni exp const; do
  bazel-bin/heyp/app/testlopri/mk-expected-interarrival-dist \
    -c doc/testlopri/validation/config-$dist.textproto \
    >doc/testlopri/validation/expected-interarrival-$dist.csv

  bazel-bin/heyp/app/testlopri/mk-expected-tput \
    -c doc/testlopri/validation/config-$dist.textproto \
    >doc/testlopri/validation/expected-goodput-$dist.csv

  bazel-bin/heyp/app/testlopri/client \
    -c doc/testlopri/validation/config-$dist.textproto \
    -shards 1 \
    -server 127.0.0.1:19999 \
    -logtostderr \
    -interarrival doc/testlopri/validation/interarrival-$dist.textproto \
    -msgput doc/testlopri/validation/goodput-ts-$dist.csv \
    -out doc/testlopri/validation/$dist.log
    
    bazel-bin/heyp/stats/hdrhist2csv \
      doc/testlopri/validation/interarrival-$dist.textproto.shard.0 \
      > doc/testlopri/validation/got-interarrival-$dist.csv
done
