#!/bin/bash

set -e

if [[ $# -lt 2 ]]; then
  echo usage: $0 outdir procdir [configpath] >&2
  exit 2
fi

outdir=$1
procdir=$2
config=$3

rm -rf "$procdir"
mkdir -p "$procdir/cluster-alloc"

./bin/proc-heyp approvals -deploy-config "$config" -out "$procdir/approvals.csv" &
./bin/proc-heyp fortio-mk-timeseries -trimdur 0s "$outdir" > "$procdir/ts.csv" &
# ./bin/proc-heyp fortio-mk-latency-cdfs -trimdur 15s -level per-client "$outdir" > "$procdir/cdf-per-client.csv" &
./bin/proc-heyp fortio-mk-latency-cdfs -trimdur 15s -level per-instance "$outdir" > "$procdir/cdf-per-instance.csv" &
./bin/proc-heyp cluster-alloc-bw-stats -workload fortio -out "$procdir/cluster-alloc-bw-stats.csv" "$outdir" &
(
  ./bin/proc-heyp align-infos -workload fortio -prec 500ms -out "$procdir/host-stats.json" "$outdir" && \
  echo UnixTime,Duration,Node,FG,QoS,Usage > "$procdir/host-fg-usage-ts.csv" && \
   jq -r '
    .unixSec as $time |
      .data |
      to_entries[] |
      .key as $key |
      .value.flowInfos[] |
      "\($time),\($key),\(.flow.srcDc)_TO_\(.flow.dstDc),\(.cumHipriUsageBytes),\(.cumLopriUsageBytes)"
    ' \
    "$procdir/host-stats.json" \
    | sort -t, -k1,1 -k3,3 \
    | awk -F, '
    {
      node_fg = $2 "," $3;
      if (node_fg in last_time && $1 != last_time[node_fg]) {
        diff = $1 - last_time[node_fg];
        printf "%s,%s,%s,HIPRI,%s\n", last_time[node_fg], diff, node_fg,
          8 * ($4 - cum_hipri_usage[node_fg]) / diff;
        printf "%s,%s,%s,LOPRI,%s\n", last_time[node_fg], diff, node_fg,
          8 * ($5 - cum_lopri_usage[node_fg]) / diff;
      }
      cum_hipri_usage[node_fg] = $4;
      cum_lopri_usage[node_fg] = $5;
      last_time[node_fg] = $1;
    }
  ' >> "$procdir/host-fg-usage-ts.csv"
  # jq -r '
  #   .unixSec as $time |
  #     .data |
  #     to_entries[] |
  #     .key as $key |
  #     .value.flowInfos[] |
  #     if .currentlyLopri == true then
  #       "\($time),\($key),\(.flow.srcDc)_TO_\(.flow.dstDc),HIPRI,0\n\($time),\($key),\(.flow.srcDc)_TO_\(.flow.dstDc),LOPRI,\(.ewmaUsageBps)"
  #     else
  #       "\($time),\($key),\(.flow.srcDc)_TO_\(.flow.dstDc),HIPRI,\(.ewmaUsageBps)\n\($time),\($key),\(.flow.srcDc)_TO_\(.flow.dstDc),LOPRI,0"
  #     end
  #   ' \
  #   "$procdir/host-stats.json" \
  #   | sort -t, -k1,1 -k3,3 \
  #   | awk -F, '
  #   BEGIN {
  #     last_time = -1.0;
  #   }
  #   {
  #     if ($1 != last_time && last_time >= 0) {
  #       for (node_fg in usage) {
  #         printf "%s,%s,%s,%s\n", last_time, $1-last_time, node_fg, usage[node_fg];
  #       }
  #       delete usage;
  #     }
  #     usage[$2 "," $3 "," $4] = $5;
  #     last_time = $1;
  #   }
  # ' >> "$procdir/host-fg-usage-ts.csv"
) &
(
  ./bin/proc-heyp align-host-stats -deploy-config "$config" -summary "$procdir/host-stats-summary.csv" -diff -workload fortio -prec 1s -out "$procdir/global-host-stats.json" "$outdir" && \
  echo UnixTime,Node,RetransSegs,IngressBps,EgressBps > "$procdir/global-host-ts.csv" && \
  jq -r '
      .unixSec as $time |
      .data |
      to_entries[] |
      .key as $key |
      "\($time),\($key),\(.value.Global.RetransSegs // 0),\(.value.MainDev.RX.Bytes * 8),\(.value.MainDev.TX.Bytes * 8)"' \
    "$procdir/global-host-stats.json" \
    >> "$procdir/global-host-ts.csv"
) &
(
  ./bin/proc-heyp host-enforcer-logs -deploy-config "$config" -workload fortio -prec 1s -out "$procdir/host-enforcer-logs.json" "$outdir" && \
  echo UnixTime,Node,QoS,SrcDC,SrcIP,DstDC,DstIP,LimitBps > "$procdir/host-enforcer-limits.csv" && \
  jq -r '
      .unixSec as $time |
      .data |
      to_entries[] |
      .key as $key |
      .value.HIPRI?[] |
      "\($time),\($key),HIPRI,\(.SrcDC),\"\(.SrcIP)\",\(.DstDC),\"\(.DstIP)\",\(.Limiter.RateBps)"' \
    "$procdir/host-enforcer-logs.json" \
    >> "$procdir/host-enforcer-limits.csv" && \
  jq -r '
      .unixSec as $time |
      .data |
      to_entries[] |
      .key as $key |
      .value.LOPRI?[] |
      "\($time),\($key),LOPRI,\(.SrcDC),\"\(.SrcIP)\",\(.DstDC),\"\(.DstIP)\",\(.Limiter.RateBps)"' \
    "$procdir/host-enforcer-logs.json" \
    >> "$procdir/host-enforcer-limits.csv"
)&
ret=0
for p in `jobs -p`; do
  wait $p || ret=1
done

# if [[ $ret -ne 0 ]]; then
#   exit $ret
# fi

./code/plot-timeseries.R "$procdir/ts.csv" "$procdir/ts-" &
# ./code/plot-cdfs-per-client.R "$procdir/cdf-per-client.csv" \
#   "$procdir/cdf-per-client.pdf" &
./code/plot-cdfs-per-instance.R "$procdir/cdf-per-instance.csv" \
  "$procdir/cdf-per-instance.pdf" &
./code/plot-fg-usage-ts.R \
  "$procdir/approvals.csv" \
  "$procdir/host-fg-usage-ts.csv" \
  "$procdir/fg-usage-ts-" &
./code/plot-global-host-usage-ts.R "$procdir/global-host-ts.csv" \
  "$procdir/global-host-ts-" &
./code/plot-cluster-alloc-bw.R "$procdir/cluster-alloc-bw-stats.csv" \
  "$procdir/cluster-alloc/" &
./code/plot-installed-limits-ts.R "$procdir/host-enforcer-limits.csv" \
  "$procdir/host-limits-ts-" &
./code/plot-host-role-summary.R "$procdir/host-stats-summary.csv" \
  "$procdir/host-role-summary-" &

# ret=0
for p in `jobs -p`; do
  wait $p || ret=1
done

if [[ $ret -ne 0 ]]; then
  exit $ret
fi