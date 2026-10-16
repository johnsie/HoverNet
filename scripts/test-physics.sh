#!/usr/bin/env bash
# Golden physics suite: one craft alone, one behaviour per scenario (acceleration, steering, slalom, jumps, braking,
# missiles, weapon cycling, idle), on two bundled tracks, as fixed inputs plus expected hash chains
# (docs/physics-golden.txt). A handling change shows up in the scenario that owns it. Run from the repository root.
#
#   scripts/test-physics.sh build/HoverNetDeterminismRun
#   scripts/test-physics.sh "node build-web/HoverNetDeterminismRun.js"
#   scripts/test-physics.sh <command> --update        # rewrite the golden file (only for an intended change)
set -uo pipefail
cmd="${1:?usage: $0 <runner command> [--update]}"
golden="docs/physics-golden.txt"
frames=1500
tracks=("ClassicH" "The River")
scenarios=(idle accelerate steer-left steer-right slalom jump brake missile weapons)
out="$(mktemp)"
for track in "${tracks[@]}"; do
  for scenario in "${scenarios[@]}"; do
    line="$($cmd "$track" "$frames" "--scenario=$scenario" 2>/dev/null | tail -1)"
    echo "$scenario $line" >> "$out"
  done
done
if [ "${2:-}" = "--update" ]; then cp "$out" "$golden"; echo "updated $golden"; rm -f "$out"; exit 0; fi
if diff -u "$golden" "$out"; then echo "physics: all $(wc -l < "$out") scenarios match the golden hashes"; rm -f "$out"; exit 0; fi
echo "physics: MISMATCH (see the scenario above; run it with --scenario=<name> --state to find the frame)"; rm -f "$out"; exit 1
