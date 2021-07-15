#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

outdir=${1%/}

first=1

mkdir -p "$outdir/proc-indiv"
for d in "$outdir"/data/*; do
  (
    echo proc $d =====
    o="$outdir/proc-indiv/${d##*/}"
    c="$outdir/configs/${d##*/}.textproto"
    ./proc.bash "$d" "$o" "$c"
  ) &
done

ret=0
for p in `jobs -p`; do
  wait $p || ret=1
done

exit $ret
