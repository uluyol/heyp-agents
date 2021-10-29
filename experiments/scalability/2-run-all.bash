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
    echo "[$HEYP_RUN_NAME] already have $o/done; skipping..."
    return 0
  fi

  echo "[$HEYP_RUN_NAME] kill existing daemons"
  local dpids=""
  bin/deploy-heyp kill-heyp-agents -c $c &
  dpids="$dpids $!"
  bin/deploy-heyp kill-iperf -c $c &
  dpids="$dpids $!"
  wait_all $dpids || return 1

  echo "[$HEYP_RUN_NAME] delete remote logs"
  bin/deploy-heyp delete-logs -c $c || return 1

  echo "[$HEYP_RUN_NAME] start HEYP agents"
  bin/deploy-heyp start-heyp-agents -c $c || return 1
  sleep 10
  echo "[$HEYP_RUN_NAME] start collecting host stats"
  bin/deploy-heyp collect-host-stats -c $c || return 1
  echo "[$HEYP_RUN_NAME] run host agent simulators"
  bin/deploy-heyp host-agent-sim-run -c $c || return 1
  echo "[$HEYP_RUN_NAME] stop heyp agents and host stat collection"
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
    echo "[$HEYP_RUN_NAME] fetch data (try = $try)"
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
    shardi=$(basename "$shard")
    if [[ $shardi -ne 0 ]]; then
      sleep 5
    fi
    all_good=1
    first=1
    for name in $(sort $shard); do
      c="$outdir/configs/$name.textproto"
      o="$outdir/data/$name"
      if (( first == 1 )); then
        echo "[shard $shardi] check nodes"
        bin/deploy-heyp check-nodes -c $c
        echo "[shard $shardi] config sys"
        bin/deploy-heyp config-sys -c $c -cc bbr -debugmon
        echo "[shard $shardi] roughly measure per-QoS BW w/ congestion (info only)"
        bin/deploy-heyp report-pri-bw -c $c || true
        echo "[shard $shardi] install bundle"
        bin/deploy-heyp install-bundle -c $c
        first=0
      fi

      echo "[$name] start run ====="
      export HEYP_RUN_NAME=$name
      if ! run_one "$c" "$o"; then
        echo "[$name] run failed !!!!!"
        all_good=0
      else
        echo "[$name] finished run ====="
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
