#!/usr/bin/env bash
# The craft is picked with the steering keys during the countdown of an online race (rules 7: the countdown is simulated, so the pick is part of the race):
# the real client sends N presses of the right key in the countdown, the server simulates the race, and the results table beside its replay says which
# craft the player drove. Two presses give the third craft (Bi-Turbo, number 2); none keep the Basic craft (number 0).
# The test hook HOVERNET_TEST_PICK_CRAFT=N makes the client press right N times (its own input path: prediction, sending, the server's simulation).
#   scripts/test-craft-pick.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
fail=0
race() {   # $1 = presses, $2 = replay folder; prints the craft number the server recorded
  mkdir -p "$2"
  "$server" --track ClassicH --players 1 --port 0 --once --limit-ticks 900 --start-delay-ms 1500 --replay-dir "$2" > "$dir/server$1.txt" 2>&1 &
  local pid=$!
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/server$1.txt" 2>/dev/null && break; sleep 0.1; done
  local port; port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server$1.txt" | head -1)
  [ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server$1.txt" >&2; return 1; }
  HOVERNET_TEST_FREEDRIVE=1 HOVERNET_TEST_PICK_CRAFT="$1" timeout 60 "$client" --auth-connect "127.0.0.1:$port" --auth-script --frames 100000 > "$dir/client$1.txt" 2>&1
  wait "$pid" 2>/dev/null
  local table; table=$(ls "$2"/*.results.tsv 2>/dev/null | head -1)
  [ -n "$table" ] || { echo "FAIL: the server wrote no results table" >&2; tail -3 "$dir/server$1.txt" >&2; return 1; }
  awk -F'\t' 'NR==2 {print $8}' "$table"
}
craft=$(race 2 "$dir/r2") || exit 1
if [ "$craft" = "2" ]; then echo "ok   two presses of right in the countdown: the server recorded the Bi-Turbo (craft 2)"; else echo "FAIL: two presses gave craft '$craft', wanted 2"; fail=1; fi
craft=$(race 0 "$dir/r0") || exit 1
if [ "$craft" = "0" ]; then echo "ok   no press: the Basic craft (craft 0)"; else echo "FAIL: no press gave craft '$craft', wanted 0"; fail=1; fi
[ $fail -eq 0 ] && echo "craft pick: the countdown of an online race picks the craft"
exit $fail
