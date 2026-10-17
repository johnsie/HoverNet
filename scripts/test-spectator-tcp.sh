#!/usr/bin/env bash
# Spectating over real TCP: three scripted drivers race; a spectator joins a few seconds into the race, is given a keyframe
# snapshot and then the delayed Tick stream, and verifies every state hash it is sent. Done once on a single-race server and
# once through the lobby. Run from the repository root.
#   scripts/test-spectator-tcp.sh build/HoverNetAuthServer build/HoverNetAuthBot
set -uo pipefail
server="${1:?usage: $0 <server> <bot>}"; bot="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
fail=0
start_server() {  # args...
  "$server" "$@" > "$dir/server.txt" 2>&1 &
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
  port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
  [ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
}
# 1. single-race server
start_server --track ClassicH --players 3 --port 0 --once --limit-ticks 700 --start-delay-ms 300 --spectator-delay-ms 1000
pids=()
for i in 1 2 3; do "$bot" --port "$port" --name "driver$i" > "$dir/d$i.txt" 2>&1 & pids+=($!); done
sleep 4
"$bot" --port "$port" --name watcher --spectate > "$dir/w.txt" 2>&1 || { echo "FAIL: spectator (single race)"; cat "$dir/w.txt"; fail=1; }
for p in "${pids[@]}"; do wait "$p" || fail=1; done
tail -1 "$dir/w.txt"
kill %1 2>/dev/null; wait 2>/dev/null
# 2. through the lobby
start_server --lobby --port 0 --limit-ticks 700 --start-delay-ms 300 --spectator-delay-ms 1000
pids=()
"$bot" --port "$port" --name l1 --host-race ClassicH:3 > "$dir/l1.txt" 2>&1 & pids+=($!)
sleep 0.5
"$bot" --port "$port" --name l2 --join-race ClassicH --track ClassicH > "$dir/l2.txt" 2>&1 & pids+=($!)
"$bot" --port "$port" --name l3 --join-race ClassicH --track ClassicH > "$dir/l3.txt" 2>&1 & pids+=($!)
sleep 4
"$bot" --port "$port" --name lwatcher --spectate-race ClassicH --track ClassicH > "$dir/lw.txt" 2>&1 || { echo "FAIL: spectator (lobby)"; cat "$dir/lw.txt"; fail=1; }
for p in "${pids[@]}"; do wait "$p" || fail=1; done
tail -1 "$dir/lw.txt"
grep -q "no divergence" "$dir/w.txt" && grep -q "no divergence" "$dir/lw.txt" || fail=1
[ $fail -eq 0 ] && echo "spectator-tcp: spectators joined running races mid-way and followed them exactly, directly and through the lobby"
exit $fail
