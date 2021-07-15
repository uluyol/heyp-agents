#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

declare -A SYS_LONG=(
  [hsc]=HSC20
  [nl]=NoLimit
  [qd]=QD
  [qdlrl]="QD+LimitLO"
  [rl]=RateLimit
)

outdir=${1%/}

first=1

mkdir -p "$outdir/proc-summary"

first=1
echo "X,C,Y,Sys,BE,Perc,LatencyNanos" \
  > "$outdir/proc-summary/latency-per-instance.csv"
echo "X,C,Y,Sys,BE,ActualGbps" \
  > "$outdir/proc-summary/tput-per-instance.csv"
echo "X,C,Y,Sys,FG,Approval,MeanUsage" \
  > "$outdir/proc-summary/fg-usage-per-instance.csv"
for d in "$outdir"/proc-indiv/*; do
  fields=($(sed -r 's:.*X-([0-9.]+)-C-([0-9.]+)-Y-([0-9.]+)-([a-zA-Z0-9]+):\1 \2 \3 \4:' <<<"$d"))
  x=${fields[0]}
  c=${fields[1]}
  y=${fields[2]}
  sys=${fields[3]}

  wlconfig="X-$x-C-$x-Y-$y"

  if (( first == 1 )); then
    first=0

    awk -F, -v WLConfig=$wlconfig -v SYS="${SYS_LONG[$sys]}" '
      {
        if (NR == 1) {
          printf "WLConfig,Sys,%s\n", $0;
        } else {
          printf "%s,%s,%s\n", WLConfig, SYS, $0;
        }
      }
      ' "$d/cdf-per-instance.csv" \
      > "$outdir/proc-summary/cdf-per-instance.csv"
  else
    awk -F, -v WLConfig=$wlconfig -v SYS="${SYS_LONG[$sys]}" '
      {
        if (NR > 1) {
          printf "%s,%s,%s\n", WLConfig, SYS, $0;
        }
      }
      ' "$d/cdf-per-instance.csv" \
      >> "$outdir/proc-summary/cdf-per-instance.csv"
  fi

  awk -F, -v X="$x" -v C="$c" -v Y="$y" -v SYS="${SYS_LONG[$sys]}" '
  {
    if (NR > 1 && $5 == "net") {
      ginst = $1 "/" $2;
      if (have_p50[ginst] != 1 && $6 >= 50) {
        printf "%s,%s,%s,%s,%s,50,%d\n", X, C, Y, SYS, ginst, $7;
        have_p50[ginst] = 1;
      }
      if (have_p90[ginst] != 1 && $6 >= 90) {
        printf "%s,%s,%s,%s,%s,90,%d\n", X, C, Y, SYS, ginst, $7;
        have_p90[ginst] = 1;
      }
      if (have_p95[ginst] != 1 && $6 >= 95) {
        printf "%s,%s,%s,%s,%s,95,%d\n", X, C, Y, SYS, ginst, $7;
        have_p95[ginst] = 1;
      }
      if (have_p99[ginst] != 1 && $6 >= 99) {
        printf "%s,%s,%s,%s,%s,99,%d\n", X, C, Y, SYS, ginst, $7;
        have_p99[ginst] = 1;
      }
    }
  }' "$d/cdf-per-instance.csv" \
    >> "$outdir/proc-summary/latency-per-instance.csv"

  awk -F, -v X="$x" -v C="$c" -v Y="$y" -v SYS="${SYS_LONG[$sys]}" '
  {
    if (NR > 1 && $3 != "Merged") {
      full_inst = $1 "/" $2 "/" $3 "/" $4;
      sum_bps[full_inst] = sum_bps[full_inst] + $6;
      count_bps[full_inst] = count_bps[full_inst] + 1;
      ginst[full_inst] = $1 "/" $2;
    }
  }
  END {
    for (full_inst in sum_bps) {
      gi = ginst[full_inst];
      g_sum_bps[gi] = g_sum_bps[gi] + (sum_bps[full_inst] / count_bps[full_inst]);
    }

    for (gi in g_sum_bps) {
      printf "%s,%s,%s,%s,%s,%f\n", X, C, Y, SYS, gi, g_sum_bps[gi] / (1024 * 1024 * 1024);
    }
  }' "$d/ts.csv" \
    >> "$outdir/proc-summary/tput-per-instance.csv"

  awk -F, -v X="$x" -v C="$c" -v Y="$y" -v SYS="${SYS_LONG[$sys]}" '
  BEGIN {
    approvals["A_TO_EDGE"] = X * 1024 * 1024 * 1024
    approvals["B_TO_EDGE"] = Y * 1024 * 1024 * 1024
    last_time = -1.0
  }
  {
    if (NR > 1) {
      if ($1 != last_time) {
        for (fg in sum_usages) {
          printf "%s,%s,%s,%s,%s,%s,%s\n", X, C, Y, SYS, fg, approvals[fg], sum_usages[fg] / weights[fg];
        }
        delete sum_usages;
        delete weights;
      }
      last_time = $1;
      sum_usages[$4] += $5 * $2
      weights[$4] += $2
    }
  }
  END {
    for (fg in sum_usages) {
      printf "%s,%s,%s,%s,%s,%s,%s\n", X, C, Y, SYS, fg, approvals[fg], sum_usages[fg] / weights[fg];
    }
  }' "$d/host-fg-usage-ts.csv" \
    >> "$outdir/proc-summary/fg-usage-per-instance.csv"
done

code/plot-sweep-multisys-latency-cdfs.R "$outdir/proc-summary/cdf-per-instance.csv" "$outdir/proc-summary/cdf-per-instance-"
code/plot-sweep-multisys-latency.R "$outdir/proc-summary/latency-per-instance.csv" "$outdir/proc-summary/latency-per-instance-"
code/plot-sweep-multisys-tput.R "$outdir/proc-summary/tput-per-instance.csv" "$outdir/proc-summary/tput-per-instance-"
code/plot-sweep-multisys-overage.R "$outdir/proc-summary/fg-usage-per-instance.csv" "$outdir/proc-summary/overage-per-instance-"
