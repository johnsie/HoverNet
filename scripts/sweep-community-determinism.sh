#!/usr/bin/env bash
# Runs the fixed-step determinism script on every .trk in a directory with two builds of HoverNetDeterminismRun
# (normally a plain build and an ASan+UBSan build) and reports sanitizer errors and any track whose hash chain
# differs between them. Run from the repository root.
#
#   scripts/sweep-community-determinism.sh TRACK_DIR build/HoverNetDeterminismRun build/san/HoverNetDeterminismRun [frames]
set -uo pipefail
dir="${1:?usage: $0 TRACK_DIR PLAIN_RUNNER SANITIZER_RUNNER [frames]}"; plain="${2:?}"; san="${3:?}"; frames="${4:-1500}"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
export plain san frames
one() {
  f="$1"; e="$(mktemp)"
  p="$("$plain" "$f" "$frames" 2>/dev/null | tail -1)"
  s="$(ASAN_OPTIONS=detect_leaks=0 "$san" "$f" "$frames" 2>"$e" | tail -1)"
  n="$(grep -c 'runtime error\|AddressSanitizer' "$e")"; rm -f "$e"
  if [ -z "$p" ] || [ -z "$s" ]; then echo "FAIL (no result) $f"
  elif [ "$p" != "$s" ]; then echo "DIFF $f"
  elif [ "$n" -gt 0 ]; then echo "SANITIZER $f"
  else echo "ok $f"; fi
}
export -f one
find "$dir" -name '*.trk' -print0 | xargs -0 -P"$(nproc)" -I{} bash -c 'one "$1"' _ {} > "$work/results"
total=$(wc -l < "$work/results"); bad=$(grep -vc '^ok ' "$work/results")
grep -v '^ok ' "$work/results"
echo "community determinism sweep: $total tracks, $bad problem(s)"
[ "$bad" -eq 0 ]
