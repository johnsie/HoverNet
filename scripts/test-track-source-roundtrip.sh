#!/usr/bin/env bash
# The track editor core must not change a track (docs/roadmap-track-editor.md, parity rule 2). For every track source in NetTarget/TrackSources:
#   1. Parse and Write give the file back byte for byte, and the model reads it with no problems;
#   2. the model's canonical text compiles to exactly the same .trk as the original (compiled by the real compiler, never by the editor code).
#   scripts/test-track-source-roundtrip.sh build/HoverNetTrackSource build/HoverNetMazeCompiler
set -uo pipefail
tool="${1:?usage: $0 <HoverNetTrackSource> <HoverNetMazeCompiler>}"; compiler="${2:?}"
cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fail=0; count=0
for src in NetTarget/TrackSources/*.track.txt; do
  name="$(basename "$src" .track.txt)"; count=$((count + 1))
  if "$tool" check "$src" --strict > "$out/check.txt" 2>&1; then echo "ok   $name: $(head -1 "$out/check.txt" | sed 's/^[^:]*: //')"; else echo "FAIL $name: check"; cat "$out/check.txt"; fail=1; continue; fi
  "$tool" canonical "$src" "$out/canonical.txt" || { echo "FAIL $name: canonical text not written"; fail=1; continue; }
  "$compiler" "$out/a.trk" "$src" > /dev/null 2>&1 || { echo "FAIL $name: the original did not compile"; fail=1; continue; }
  "$compiler" "$out/b.trk" "$out/canonical.txt" > /dev/null 2>&1 || { echo "FAIL $name: the canonical text did not compile"; fail=1; continue; }
  if cmp -s "$out/a.trk" "$out/b.trk"; then echo "ok   $name: canonical text compiles to the same bytes"; else echo "FAIL $name: canonical text compiles to different bytes"; fail=1; fi
  if cmp -s "$src" "$out/canonical.txt"; then echo "ok   $name: canonical text equals the source file"; else echo "note $name: canonical text differs from the source file (layout only; the compiled bytes are what count)"; fi
done
[ "$count" -gt 0 ] || { echo "FAIL: no track sources found"; exit 1; }
exit $fail
