#!/usr/bin/env bash
# Output parity of the track compiler (docs/roadmap-track-editor.md, parity rule 2): every track source in NetTarget/TrackSources must compile to
# exactly the committed .trk in NetTarget/Tracks, with the native compiler and (when given) the WebAssembly compiler run under Node. The editor's
# browser build compiles with the WebAssembly one, so a difference here means the same track would come out different in the browser.
#   scripts/test-compiler-parity.sh build/HoverNetMazeCompiler [build/web/HoverNetMazeCompiler.js [path/to/node]]
# Without the WebAssembly arguments only the native half runs (and says so).
set -uo pipefail
native="${1:?usage: $0 <HoverNetMazeCompiler> [HoverNetMazeCompiler.js [node]]}"; wasm="${2:-}"; node="${3:-node}"
cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fail=0; count=0
for src in NetTarget/TrackSources/*.track.txt; do
  name="$(basename "$src" .track.txt)"; committed="NetTarget/Tracks/$name.trk"
  [ -f "$committed" ] || { echo "skip $name: no committed .trk"; continue; }
  count=$((count + 1))
  "$native" "$out/native.trk" "$src" > /dev/null 2>&1 || { echo "FAIL $name: the native compiler failed"; fail=1; continue; }
  if cmp -s "$out/native.trk" "$committed"; then echo "ok   $name: native output equals the committed .trk"; else echo "FAIL $name: native output differs from $committed"; fail=1; fi
  if [ -n "$wasm" ]; then
    rm -f "$out/wasm.trk"
    timeout 600 "$node" "$wasm" "$out/wasm.trk" "$src" > /dev/null 2>&1
    if cmp -s "$out/wasm.trk" "$out/native.trk"; then echo "ok   $name: WebAssembly output equals the native output"; else echo "FAIL $name: WebAssembly output differs from the native output"; fail=1; fi
  fi
done
[ "$count" -gt 0 ] || { echo "FAIL: no track sources found"; exit 1; }
[ -n "$wasm" ] || echo "note: the WebAssembly half was not run (no HoverNetMazeCompiler.js given)"
exit $fail
