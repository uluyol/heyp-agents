#!/bin/bash

set -e

if [[ $# -lt 1 ]]; then
  echo bad
  exit 1
fi

outdir=${1%/}

wait_all() {
  local p
  local ret=0
  for p in $@; do
    wait $p || ret=1
  done
  return $ret
}

run_one() {
  local c=$1
  local o=$2

  if [[ -f "$o/done" ]]; then
    echo "already have $o/done; skipping..."
    return 0
  fi

  echo kill existing daemons
  local dpids=""
  bin/deploy-heyp kill-fortio -c $c &
  dpids="$dpids $!"
  bin/deploy-heyp kill-heyp-agents -c $c &
  dpids="$dpids $!"
  bin/deploy-heyp kill-iperf -c $c &
  dpids="$dpids $!"
  wait_all $dpids || return 1

  echo delete remote logs
  bin/deploy-heyp delete-logs -c $c || return 1

  echo start HEYP agents
  bin/deploy-heyp start-heyp-agents -c $c || return 1
  echo start fortio servers
  bin/deploy-heyp fortio-start-servers -c $c || return 1
  sleep 10
  echo start collecting host stats
  bin/deploy-heyp collect-host-stats -c $c || return 1
  echo run fortio clients
  bin/deploy-heyp fortio-run-clients -c $c || return 1
  echo stop heyp agents and host stat collection
  dpids=""
  bin/deploy-heyp collect-host-stats -c $c -stop &
  dpids="$dpids $!"
  bin/deploy-heyp stop-heyp-agents -c $c & # graceful shutdown
  dpids="$dpids $!"

  wait_all $dpids || true # ignore shutdown errors

  sleep 3

  local did_fetch=0
  local try=1
  while (( did_fetch == 0 )); do
    echo "fetch data (try = $try)"
    if bin/deploy-heyp fetch-data -c $c -o $o; then
      did_fetch=1
    fi
    try=$((try + 1))
  done

  if ((did_fetch == 0)); then
    return 1
  fi

  touch "$o/done"
  return 0
}


pids=""
mkdir -p "$outdir/data"
for shard in "$outdir"/shards/*; do
  (
    all_good=1
    first=1
    for name in $(sort -r $shard); do
      c="$outdir/configs/$name.textproto"
      echo starting $name =====
      o="$outdir/data/$name"
      if (( first == 1 )); then
        echo check nodes
        bin/deploy-heyp check-nodes -c $c
        echo config sys
        bin/deploy-heyp config-sys -c $c -cc bbr -debugmon
        echo install bundle
        bin/deploy-heyp install-bundle -c $c
        first=0
      fi

      if ! run_one "$c" "$o"; then
        echo "error when running $c"
        all_good=0
      fi
    done
    if (( all_good != 1 )); then
      exit 1
    fi
  ) &
  pids="$pids $!"
done

ret=0
for p in $pids; do
  wait $p || ret=1
done

exit $ret
