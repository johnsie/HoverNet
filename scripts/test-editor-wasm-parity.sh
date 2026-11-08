#!/usr/bin/env bash
# The browser editor must make and compile exactly what the native editors do (docs/roadmap-track-editor.md, parity rules 2 and 5). With the editor core and
# the compiler both built for WebAssembly (build/web) and run under Node, this checks, against the native programs:
#   - the editor core's self-test passes;
#   - the canonical text of every bundled track source is identical;
#   - each new-track template, with several sets of options, writes an identical file;
#   - those new tracks compile to identical bytes.
#   scripts/test-editor-wasm-parity.sh build/HoverNetTrackSource build/HoverNetMazeCompiler build/web/HoverNetTrackSource.js build/web/HoverNetMazeCompiler.js [node]
set -uo pipefail
nsrc="${1:?usage: $0 <native TrackSource> <native MazeCompiler> <wasm TrackSource.js> <wasm MazeCompiler.js> [node]}"; ncomp="${2:?}"; wsrc="${3:?}"; wcomp="${4:?}"; node="${5:-node}"
cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fail=0
"$node" "$wsrc" > "$out/self.txt" 2>&1 && grep -q "self-test passed" "$out/self.txt" && echo "ok   WebAssembly self-test passes" || { echo "FAIL WebAssembly self-test"; tail -3 "$out/self.txt"; fail=1; }
for src in NetTarget/TrackSources/*.track.txt; do
  name="$(basename "$src" .track.txt)"
  "$nsrc" canonical "$src" "$out/n.txt" && "$node" "$wsrc" canonical "$src" "$out/w.txt" && cmp -s "$out/n.txt" "$out/w.txt" && echo "ok   $name: canonical text identical" || { echo "FAIL $name: canonical text differs"; fail=1; }
done
i=0
for opts in "--name=Test Loop" "--rooms=32 --radius=200 --width=40" "--rooms=17 --radius=333.3 --width=23.7" "--rooms=96 --radius=900 --width=60 --plain"; do
  i=$((i + 1))
  IFS=' ' read -r -a a <<< "$opts"
  # the name may contain a space: keep it one argument
  if [[ "$opts" == --name=* ]]; then a=("--name=Test Loop"); fi
  "$nsrc" new ring "$out/n$i.txt" "${a[@]}" && "$node" "$wsrc" new ring "$out/w$i.txt" "${a[@]}" || { echo "FAIL template $i: could not be made"; fail=1; continue; }
  if cmp -s "$out/n$i.txt" "$out/w$i.txt"; then echo "ok   template $i ($opts): identical file"; else echo "FAIL template $i ($opts): files differ"; fail=1; continue; fi
  "$ncomp" "$out/n$i.trk" "$out/n$i.txt" > /dev/null 2>&1; rm -f "$out/w$i.trk"; timeout 600 "$node" "$wcomp" "$out/w$i.trk" "$out/n$i.txt" > /dev/null 2>&1
  if cmp -s "$out/n$i.trk" "$out/w$i.trk"; then echo "ok   template $i: compiles to identical bytes"; else echo "FAIL template $i: compiled bytes differ"; fail=1; fi
done
exit $fail
