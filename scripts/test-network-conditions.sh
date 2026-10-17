#!/usr/bin/env bash
# Authoritative races over bad networks (Stage 2's network-condition harness): a proxy between the players and the server adds delay,
# jitter, bandwidth limits, freezes (loss bursts) and disconnects. Every player must still follow the race exactly, and the game
# client must ride out disconnects it did not plan.
#   scripts/test-network-conditions.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetNetProxy build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <proxy> <client>}"; bot="${2:?}"; proxy="${3:?}"; client="${4:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
fail=0
run_case() {  # name players ticks "proxy args"
  local name="$1" players="$2" ticks="$3" pargs="$4"
  "$server" --track ClassicH --players "$players" --port 0 --once --limit-ticks "$ticks" --start-delay-ms 1000 > "$dir/$name.server" 2>&1 &
  local spid=$!
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/$name.server" 2>/dev/null && break; sleep 0.1; done
  local port; port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/$name.server" | head -1)
  "$proxy" --listen-port 0 --target-port "$port" $pargs > "$dir/$name.proxy" 2>&1 &
  local ppid=$!
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/$name.proxy" 2>/dev/null && break; sleep 0.1; done
  local pport; pport=$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\),.*/\1/p' "$dir/$name.proxy" | head -1)
  local pids=()
  for i in $(seq 1 "$players"); do "$bot" --port "$pport" --name "$name$i" > "$dir/$name.$i" 2>&1 & pids+=($!); done
  local ok=1
  for p in "${pids[@]}"; do wait "$p" || ok=0; done
  wait "$spid" 2>/dev/null; kill "$ppid" 2>/dev/null; wait "$ppid" 2>/dev/null
  for i in $(seq 1 "$players"); do grep -q "no divergence" "$dir/$name.$i" || ok=0; done
  if [ $ok -eq 1 ]; then echo "ok   $name: $players players, $(tail -1 "$dir/$name.1" | sed 's/^[^:]*: //')   [proxy $pargs]"; else echo "FAIL $name"; tail -2 "$dir"/$name.* ; fail=1; fi
}
run_case delay   8 500 "--delay-ms 60 --jitter-ms 10 --seed 1"
run_case slow    4 500 "--delay-ms 150 --jitter-ms 40 --seed 2"
run_case freeze  4 600 "--delay-ms 40 --jitter-ms 10 --stall-ms 500 --stall-every-ms 3000 --seed 3"
run_case thin    4 500 "--delay-ms 30 --kbps 48 --seed 4"
# The game client rides out disconnects nobody planned: the proxy hangs up on it every 14,000 bytes (about every six seconds).
AUTH_PROXY="$proxy" AUTH_PROXY_ARGS="--delay-ms 40 --jitter-ms 10 --drop-after-bytes 14000 --seed 5" \
  "$(dirname "$0")/test-authclient.sh" "$server" "$bot" "$client" 3 1100 > "$dir/client.txt" 2>&1
tail -2 "$dir/client.txt"
grep -q "followed an authoritative" "$dir/client.txt" || { echo "FAIL: the game client over a dropping network"; fail=1; }
grep -q "reconnected" "$dir/client.txt" || { echo "FAIL: the proxy never dropped the game client (the test proved nothing)"; fail=1; }
[ $fail -eq 0 ] && echo "network conditions: delay, jitter, slow paths, freezes, a thin pipe and disconnects all handled"
exit $fail
