#!/bin/bash

set -e

if [[ $# -ne 2 ]]; then
    echo usage: ./4-oneoffs-qflip.bash dataprefix procdir >&2
    exit 2
fi

prefix=$1
procdir=$2

rm -rf "$procdir"
mkdir -p "$procdir"

echo "$prefix" >"$procdir/prefix"

datasets=()
pids=""

for data in "$prefix"*; do
    dataset=$(sed "s|^$prefix||" <<<"$data")
    datasets+=("$dataset")
    (
        echo proc $dataset
        bin/proc-heyp align-infos \
            -fine -prec 1s \
            -out "$procdir/aligned-$dataset.log" \
            "$data"
        code/dump-fine-grained-stat-dists.py \
            "$procdir/aligned-$dataset.log" \
            "$procdir/stats-$dataset"
    ) &
    pids="$pids $!"
done

ret=0
for pid in $pids; do
    wait $pid || ret=1
done

if [[ $ret -ne 0 ]]; then
    exit 1
fi

rm -f "$procdir/stat-summary.log" || true

for dataset in "${datasets[@]}"; do
    echo summarize $dataset
    code/summarize-fine-grained-stats.py \
        "$procdir/stats-$dataset" "$dataset" \
        >>"$procdir/stat-summary.csv"
done

mkdir -p "$procdir/plot-summaries/"
code/plot-oneoffs-qflip.R "$procdir/stat-summary.csv" \
    "$procdir/plot-summaries/"

dist_metrics=(
    aux_cwnd
    aux_bbrBw
    aux_bbrMinRttMs
    got_bbr_mode
    got_probeonly_aux_cwnd
    got_probeonly_aux_bbrBw
    got_probeonly_aux_bbrMinRttMs
)

echo "Metric,Dataset,FG,UnixSec,IsLOPRI,Value" >"$procdir/stat-dists.csv"
for metric in "${dist_metrics[@]}"; do
    for dataset in "${datasets[@]}"; do
        for fg in AA_TO_EDGE WA_TO_EDGE; do
            if [[ ! -f "$procdir/stats-$dataset/$fg/$metric" ]]; then
                continue
            fi
            awk "{ printf \"$metric,$dataset,$fg,%s\\n\", \$0; }" \
                "$procdir/stats-$dataset/$fg/$metric" \
                >>"$procdir/stat-dists.csv"
        done
    done
done

mkdir -p "$procdir/plot-dists"
code/plot-oneoffs-qflip-dists.R "$procdir/stat-dists.csv" \
    "$procdir/plot-dists/"
