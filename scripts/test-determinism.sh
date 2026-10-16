#!/usr/bin/env bash
# Replays the scripted, fixed-step races of HoverNetDeterminismRun and compares each track's hash chain with the
# committed golden values (docs/determinism-golden.txt). Run from the repository root.
#
#   scripts/test-determinism.sh build/HoverNetDeterminismRun                       # native
#   scripts/test-determinism.sh "node build-web/HoverNetDeterminismRun.js"         # WebAssembly under Node
#   scripts/test-determinism.sh <command> --update                                 # rewrite the golden file
set -uo pipefail
cmd="${1:?usage: $0 <runner command> [--update]}"
golden="docs/determinism-golden.txt"
frames=3000
tracks=("ClassicH" "Metro Spiral" "Steeplechase" "Switchback" "The Alley2" "The River" "Tidal Causeway")
out="$(mktemp)"
for track in "${tracks[@]}"; do
  line="$($cmd "$track" "$frames" 2>/dev/null | tail -1)"
  echo "$line" >> "$out"
done
if [ "${2:-}" = "--update" ]; then cp "$out" "$golden"; echo "updated $golden"; rm -f "$out"; exit 0; fi
if diff -u "$golden" "$out"; then echo "determinism: all ${#tracks[@]} tracks match the golden hashes"; rm -f "$out"; exit 0; fi
echo "determinism: MISMATCH (first line differing above; run with --state to find the frame)"; rm -f "$out"; exit 1
