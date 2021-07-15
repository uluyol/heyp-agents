#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

outdir=${1%/}

first=1

mkdir -p "$outdir/proc-summary"

echo "WantGbps,BE,Perc,LatencyNanos" \
  > "$outdir/proc-summary/latency-per-instance.csv"
echo "WantGbps,BE,ActualGbps" \
  > "$outdir/proc-summary/tput-per-instance.csv"
for d in "$outdir"/proc-indiv/*; do
  awk -F, -v run_name=${d##*/gbps-} '
  {
    if (NR > 1 && $5 == "net") {
      ginst = $1 "/" $2;
      if (have_p50[ginst] != 1 && $6 >= 50) {
        printf "%s,%s,50,%d\n", run_name, ginst, $7;
        have_p50[ginst] = 1;
      }
      if (have_p90[ginst] != 1 && $6 >= 90) {
        printf "%s,%s,90,%d\n", run_name, ginst, $7;
        have_p90[ginst] = 1;
      }
      if (have_p95[ginst] != 1 && $6 >= 95) {
        printf "%s,%s,95,%d\n", run_name, ginst, $7;
        have_p95[ginst] = 1;
      }
      if (have_p99[ginst] != 1 && $6 >= 99) {
        printf "%s,%s,99,%d\n", run_name, ginst, $7;
        have_p99[ginst] = 1;
      }
    }
  }' "$d/cdf-per-instance.csv" \
    >> "$outdir/proc-summary/latency-per-instance.csv"

  awk -F, -v run_name=${d##*/gbps-} '
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
      printf "%s,%s,%f\n", run_name, gi, g_sum_bps[gi] / (1024 * 1024 * 1024);
    }
  }' "$d/ts.csv" \
    >> "$outdir/proc-summary/tput-per-instance.csv"
done

code/plot-sweep-latency.R "$outdir/proc-summary/latency-per-instance.csv" "$outdir/proc-summary/latency-per-instance-"
code/plot-sweep-tput.R "$outdir/proc-summary/tput-per-instance.csv" "$outdir/proc-summary/tput-per-instance.pdf"