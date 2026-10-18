#!/usr/bin/env bash
# The game client's lobby logic against a real server. Run from the repository root.
#   scripts/test-online-lobby.sh build/HoverNetAuthServer build/HoverNetOnlineLobbyTest
set -uo pipefail
#                    (with a third argument, build/HoverNetReplay, it also tests the replays and records the server shares)
server="${1:?usage: $0 <server> <test> [<replay tool>]}"; test="${2:?}"; tool="${3:-}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
replayargs=()
if [ -n "$tool" ]; then
  mkdir -p "$dir/replays"
  "$tool" record ClassicH 600 "$dir/replays/ClassicH-1-100.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
  "$tool" record ClassicH 700 "$dir/replays/ClassicH-2-200.hnr" --players=3 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
  printf 'rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n1\t0\t1\t1\t61000\t61000\n2\t1\t1\t1\t70000\t70000\n' > "$dir/replays/ClassicH-1-100.results.tsv"
  printf 'rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n1\t2\t1\t1\t65000\t65000\n' > "$dir/replays/ClassicH-2-200.results.tsv"
  touch -d '2 minutes ago' "$dir/replays/ClassicH-1-100.hnr"
  replayargs=(--replay-dir "$dir/replays"); export HOVERNET_TEST_SHARED_REPLAYS=1
fi
"$server" --lobby "${replayargs[@]}" --port 0 --limit-ticks 3000 --start-delay-ms 300 --spectator-delay-ms 200 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
"$test" "$port"
