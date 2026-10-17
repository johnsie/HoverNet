#!/usr/bin/env bash
# Leaderboards and events from races that really finish. HoverNetDriver searches the real simulation for controls that drive a craft
# around ClassicH, and writes the server's replay; two different drives give two different finish times. Run from the repository root.
#   scripts/test-leaderboard.sh build/HoverNetDriver build/HoverNetReplay
set -uo pipefail
driver="${1:?usage: $0 <HoverNetDriver> <HoverNetReplay>}"
replay="${2:?usage: $0 <HoverNetDriver> <HoverNetReplay>}"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
check() { if ! eval "$2"; then echo "FAIL: $1"; fail=1; fi; }

"$driver" ClassicH "$dir/fast.hnr" --seed=1 --hold=12 >/dev/null 2>&1 || { echo "FAIL: the driver found no finish (seed 1)"; exit 1; }
"$driver" ClassicH "$dir/slow.hnr" --seed=3 --hold=10 >/dev/null 2>&1 || { echo "FAIL: the driver found no finish (seed 3)"; exit 1; }
check "both replays verify" '"$replay" verify "$dir/fast.hnr" >/dev/null && "$replay" verify "$dir/slow.hnr" >/dev/null'
fast_ms=$("$replay" results "$dir/fast.hnr" | sed -n 's/.*finish_ms \([0-9]*\).*/\1/p')
slow_ms=$("$replay" results "$dir/slow.hnr" | sed -n 's/.*finish_ms \([0-9]*\).*/\1/p')
check "both races finished with a time" '[ -n "$fast_ms" ] && [ -n "$slow_ms" ] && [ "$fast_ms" -gt 0 ] && [ "$slow_ms" -gt "$fast_ms" ]'
check "events lists the finish" '"$replay" events "$dir/fast.hnr" | grep -q "finish 1 lap_ms $fast_ms race_ms $fast_ms"'
board=$("$replay" leaderboard "$dir/slow.hnr" "$dir/fast.hnr")
check "the leaderboard ranks two finishers" 'grep -q "2 replays ranked, 0 skipped" <<< "$board"'
first=$(sed -n '/fastest finish/{n;p}' <<< "$board")
check "the faster race ranks first" 'grep -q "1\. $((fast_ms / 1000))\.$(printf %03d $((fast_ms % 1000))) s" <<< "$first"'
check "the slower race ranks second" 'grep -q "2\. $((slow_ms / 1000))\.$(printf %03d $((slow_ms % 1000))) s" <<< "$board"'
check "--top=1 keeps only the winner" '! "$replay" leaderboard "$dir/slow.hnr" "$dir/fast.hnr" --top=1 | grep -q "2\. "'
[ "$fail" -eq 0 ] && echo "leaderboard checks passed (fast ${fast_ms} ms, slow ${slow_ms} ms)"
exit "$fail"
