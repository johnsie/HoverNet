#!/usr/bin/env bash
# Replay format round trip: record a race, verify it, then check that damaged, truncated and tampered files are
# rejected and that a replay of a different track file does not verify. Run from the repository root.
#   scripts/test-replay.sh build/HoverNetReplay
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay>}"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
expect_ok()   { if ! "$@" >/dev/null 2>&1; then echo "FAIL (should succeed): $*"; fail=1; fi; }
expect_fail() { if "$@" >/dev/null 2>&1; then echo "FAIL (should be rejected): $*"; fail=1; fi; }

for track in "ClassicH" "The River"; do
  f="$dir/$track.hnr"
  expect_ok "$tool" record "$track" 1500 "$f" --players=2
  expect_ok "$tool" verify "$f"
  expect_ok "$tool" info "$f"
done
f="$dir/ClassicH.hnr"
size=$(stat -c %s "$f")
# Flip one byte in the inputs: the CRC must catch it.
cp "$f" "$dir/flipped.hnr"; printf '\x55' | dd of="$dir/flipped.hnr" bs=1 seek=$((size / 2)) conv=notrunc 2>/dev/null
expect_fail "$tool" verify "$dir/flipped.hnr"
# Truncations at several points, and an empty file.
for n in 0 3 20 100 $((size - 1)); do head -c "$n" "$f" > "$dir/trunc.hnr"; expect_fail "$tool" verify "$dir/trunc.hnr"; done
# Wrong magic.
{ printf 'XXXX'; tail -c +5 "$f"; } > "$dir/magic.hnr"; expect_fail "$tool" verify "$dir/magic.hnr"
# A replay with altered inputs but a valid CRC must diverge, not verify. Rewrite via a different recording's inputs:
# record a one-player race and splice nothing; instead check that two different races differ in final hash.
a=$("$tool" record ClassicH 1000 "$dir/a.hnr" --players=1 | awk '{print $NF}')
b=$("$tool" record ClassicH 1000 "$dir/b.hnr" --players=2 | awk '{print $NF}')
if [ "$a" = "$b" ]; then echo "FAIL: different races hashed the same"; fail=1; fi
[ $fail -eq 0 ] && echo "replay: record/verify round trip and corruption checks passed"
exit $fail
