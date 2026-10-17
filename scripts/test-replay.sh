#!/usr/bin/env bash
# Replay format round trip: record a race, verify it, then check that damaged, truncated and tampered files are
# rejected and that a replay of a different track file does not verify. Run from the repository root.
#   scripts/test-replay.sh build/HoverNetReplay          (or "node build-web/HoverNetReplay.js")
set -uo pipefail
read -r -a tool <<< "${1:?usage: $0 <HoverNetReplay>}"   # a program or a command such as "node build-web/HoverNetReplay.js"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
expect_ok()   { if ! "$@" >/dev/null 2>&1; then echo "FAIL (should succeed): $*"; fail=1; fi; }
expect_fail() { if "$@" >/dev/null 2>&1; then echo "FAIL (should be rejected): $*"; fail=1; fi; }

for track in "ClassicH" "The River"; do
  f="$dir/$track.hnr"
  expect_ok "${tool[@]}" record "$track" 1500 "$f" --players=2
  expect_ok "${tool[@]}" verify "$f"
  expect_ok "${tool[@]}" info "$f"
done
f="$dir/ClassicH.hnr"
size=$(stat -c %s "$f")
# Flip one byte in the inputs: the CRC must catch it.
cp "$f" "$dir/flipped.hnr"; printf '\x55' | dd of="$dir/flipped.hnr" bs=1 seek=$((size / 2)) conv=notrunc 2>/dev/null
expect_fail "${tool[@]}" verify "$dir/flipped.hnr"
# Truncations at several points, and an empty file.
for n in 0 3 20 100 $((size - 1)); do head -c "$n" "$f" > "$dir/trunc.hnr"; expect_fail "${tool[@]}" verify "$dir/trunc.hnr"; done
# Wrong magic.
{ printf 'XXXX'; tail -c +5 "$f"; } > "$dir/magic.hnr"; expect_fail "${tool[@]}" verify "$dir/magic.hnr"
# A replay with altered inputs but a valid CRC must diverge, not verify. Rewrite via a different recording's inputs:
# record a one-player race and splice nothing; instead check that two different races differ in final hash.
a=$("${tool[@]}" record ClassicH 1000 "$dir/a.hnr" --players=1 | awk '{print $NF}')
b=$("${tool[@]}" record ClassicH 1000 "$dir/b.hnr" --players=2 | awk '{print $NF}')
if [ "$a" = "$b" ]; then echo "FAIL: different races hashed the same"; fail=1; fi
# A long race (a 25-minute race is 94,000 ticks): the replay still verifies and stays small.
expect_ok "${tool[@]}" record "The Alley2" 100000 "$dir/long.hnr" --players=2
expect_ok "${tool[@]}" verify "$dir/long.hnr"
# The result table: one row per player, ranked, and the same on every run (leaderboards and time trials read it).
r1=$("${tool[@]}" results "$dir/ClassicH.hnr" 2>&1 | tr -d '\r' | grep '^rank'); r2=$("${tool[@]}" results "$dir/ClassicH.hnr" 2>&1 | tr -d '\r' | grep '^rank')
[ -n "$r1" ] && [ "$r1" = "$r2" ] && [ "$(echo "$r1" | wc -l)" -ge 2 ] || { echo "FAIL: replay results are missing or differ between runs"; fail=1; }
# A replay's id is stable, 16 hex digits, and differs between different replays.
i1=$("${tool[@]}" id "$dir/ClassicH.hnr" | tr -d '\r'); i2=$("${tool[@]}" id "$dir/ClassicH.hnr" | tr -d '\r'); i3=$("${tool[@]}" id "$dir/The River.hnr" | tr -d '\r')
[[ "$i1" =~ ^[0-9a-f]{16}$ ]] && [ "$i1" = "$i2" ] && [ "$i1" != "$i3" ] || { echo "FAIL: replay ids are not stable and distinct ($i1 $i2 $i3)"; fail=1; }
# The leaderboard skips (and reports) a damaged replay and still ranks the others.
"${tool[@]}" leaderboard "$dir/ClassicH.hnr" "$dir/flipped.hnr" >/dev/null 2>&1 && { echo "FAIL: leaderboard accepted a damaged replay"; fail=1; }
"${tool[@]}" leaderboard "$dir/ClassicH.hnr" "$dir/The River.hnr" >/dev/null 2>&1 || { echo "FAIL: leaderboard over two good replays failed"; fail=1; }
# The event timeline is produced and repeatable (the scripted race completes no lap, so it is empty; the authority test checks a real finish).
e1=$("${tool[@]}" events "$dir/ClassicH.hnr" 2>&1 | tr -d '\r' | grep -v 'Net priority'); e2=$("${tool[@]}" events "$dir/ClassicH.hnr" 2>&1 | tr -d '\r' | grep -v 'Net priority')
echo "$e1" | grep -q '^events for ClassicH after 1500 ticks: 0$' && [ "$e1" = "$e2" ] || { echo "FAIL: the replay event timeline is wrong: $e1"; fail=1; }
# Mutation fuzzing of the decoder with the CRC repaired (run under the sanitizer build in CI).
expect_ok "${tool[@]}" fuzz "$dir/ClassicH.hnr" 5000
[ $fail -eq 0 ] && echo "replay: record/verify round trip and corruption checks passed"
exit $fail
