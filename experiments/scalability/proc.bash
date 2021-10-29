#!/bin/bash

set -e

if [[ $# -lt 3 ]]; then
  echo usage: $0 outdir procdir configpath >&2
  exit 2
fi

outdir=$1
procdir=$2
config=$3

rm -rf "$procdir"
mkdir -p "$procdir/cluster-alloc"

./bin/proc-heyp cluster-alloc-changes -out "$procdir/cluster-alloc-changes.csv" "$outdir" &

(
  ./bin/proc-heyp align-host-stats -deploy-config "$config" -summary "$procdir/host-stats-summary.csv" -diff -workload host-agent-sim -prec 1s -out "$procdir/global-host-stats.json" "$outdir" && \
  echo UnixTime,Node,RetransSegs,IngressBps,EgressBps,CPUUsage > "$procdir/global-host-ts.csv" && \
  jq -r '
      .unixSec as $time |
      .data |
      to_entries[] |
      .key as $key |
      "\($time),\($key),\(.value.Global.TCP.RetransSegs // 0),\(.value.MainDev.RX.Bytes * 8),\(.value.MainDev.TX.Bytes * 8),\( (.value.CPUCounters.Total - .value.CPUCounters.Idle) / .value.CPUCounters.Total )"' \
    "$procdir/global-host-stats.json" \
    >> "$procdir/global-host-ts.csv"
) &

ret=0
for p in `jobs -p`; do
  wait $p || ret=1
done

# if [[ $ret -ne 0 ]]; then
#   exit $ret
# fi

./code/plot-global-host-usage-ts.R "$procdir/global-host-ts.csv" \
  "$procdir/global-host-ts-" &
./code/plot-global-host-loss-ts.R "$procdir/global-host-ts.csv" \
  "$procdir/global-host-loss-ts-" &
./code/plot-host-role-summary.R "$procdir/host-stats-summary.csv" \
  "$procdir/host-role-summary-" &

# ret=0
for p in `jobs -p`; do
  wait $p || ret=1
done

if [[ $ret -ne 0 ]]; then
  exit $ret
fi
