#!/usr/bin/env bash
# The classic renderer's output for fixed scenes, compared with the committed hashes (docs/render-golden.txt, docs/render-golden.md).
# Each of the seven bundled tracks is raced by a scripted two-craft race to a fixed tick and drawn four ways (grid, mid-race, widescreen
# 1280x720, cockpit view); the 8-bit framebuffer is hashed. Run from the repository root.
#
#   scripts/test-render-golden.sh build/HoverNetRenderGolden                      # native
#   scripts/test-render-golden.sh "node build-web/HoverNetRenderGolden.js"        # WebAssembly under Node
#   scripts/test-render-golden.sh <command> --update                              # rewrite the golden file
set -uo pipefail
cmd="${1:?usage: $0 <runner command> [--update]}"
golden="docs/render-golden.txt"
tracks=("ClassicH" "Metro Spiral" "Steeplechase" "Switchback" "The Alley2" "The River" "Tidal Causeway")
scenes=(grid640 drive640 wide1280 cockpit640)
out="$(mktemp)"
for track in "${tracks[@]}"; do
  for scene in "${scenes[@]}"; do
    line="$($cmd "$track" "$scene" 2>/dev/null | tr -d '\r' | grep -E "^$track $scene " | tail -1)"
    [ -n "$line" ] || line="$track $scene FAILED"
    echo "$line" >> "$out"
  done
done
if [ "${2:-}" = "--update" ]; then cp "$out" "$golden"; echo "updated $golden"; rm -f "$out"; exit 0; fi
if diff -u "$golden" "$out"; then echo "render golden: all $(( ${#tracks[@]} * ${#scenes[@]} )) frames match the committed hashes"; rm -f "$out"; exit 0; fi
echo "render golden: MISMATCH (a frame differs above; HOVERNET_RENDER_DUMP=<dir> HoverNetRenderGolden <track> <scene> writes it as a PGM to look at)"; rm -f "$out"; exit 1
