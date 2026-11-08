#!/usr/bin/env bash
# The modern look (docs/render-golden.md, "The modern look") on real race frames. For three bundled tracks, in a chase view and a cockpit view,
# `HoverNetRenderGolden <track> <scene> modern` draws the scene with the look and checks, by property rather than by committed hash (its
# floating-point effects are only promised to repeat on the same machine):
#   - the rasteriser-only line it prints first is exactly the committed golden (the look is applied after it, and must not change it);
#   - the look changes the picture, and the same frame processed twice is identical;
#   - the observer reported a race frame, with the craft on screen and a trail in the chase view, and no craft effects in the cockpit view;
#   - the lighting gets the scene's depth and at least an engine light and a headlight;
#   - the rasteriser's surface tags are there: floor and wall pixels are tagged (and floor and craft in the chase view);
#   - shadows are drawn (the 8-bit scene changes) in the chase views;
#   - the camera is never changed by the look: with the shadows off too, the 8-bit scene is exactly the one drawn without the look.
# Run from the repository root:   scripts/test-modern-graphics.sh build/HoverNetRenderGolden
set -uo pipefail
cmd="${1:?usage: $0 <HoverNetRenderGolden command>}"
golden="docs/render-golden.txt"
fail=0
for track in "ClassicH" "Steeplechase" "Tidal Causeway"; do
  for scene in drive640 wide1280 cockpit640; do
    out="$($cmd "$track" "$scene" modern 2>/dev/null | tr -d '\r')"
    classic="$(echo "$out" | grep -E "^$track $scene " | tail -1)"
    modern="$(echo "$out" | grep -E "^MODERN $track $scene " | tail -1)"
    want="$(grep -E "^$track $scene " "$golden" | head -1)"
    if [ -z "$modern" ]; then echo "FAIL $track $scene: no MODERN line"; fail=1; continue; fi
    if [ "$classic" != "$want" ]; then echo "FAIL $track $scene: the rasteriser's own frame differs from the committed golden"; fail=1; continue; fi
    changed="$(echo "$modern" | sed -E 's/.* changed=([0-9]+).*/\1/')"
    shadowed="$(echo "$modern" | sed -E 's/.* shadowed=([0-9]+).*/\1/')"
    camerad="$(echo "$modern" | sed -E 's/.* cameradiff=([0-9]+).*/\1/')"
    speed="$(echo "$modern" | sed -E 's/.* speed=([0-9.]+).*/\1/')"
    lights="$(echo "$modern" | sed -E 's/.* lights=([0-9]+).*/\1/')"
    depth="$(echo "$modern" | sed -E 's/.* depth=([0-9]+).*/\1/')"
    tags="$(echo "$modern" | sed -E 's/.* tags=([0-9/]+).*/\1/')"
    repeat="$(echo "$modern" | sed -E 's/.* repeat=([a-zA-Z]+).*/\1/')"
    hints="$(echo "$modern" | sed -E 's/.* hints=([0-9]+).*/\1/')"
    craft="$(echo "$modern" | sed -E 's/.* craft=([0-9]+).*/\1/')"
    trail="$(echo "$modern" | sed -E 's/.* trail=([0-9]+).*/\1/')"
    ok=1
    [ "$repeat" = "yes" ] || { echo "FAIL $track $scene: the look is not repeatable"; ok=0; }
    [ "$changed" -gt 1000 ] || { echo "FAIL $track $scene: the look hardly changed the picture ($changed)"; ok=0; }
    [ "$hints" = "1" ] || { echo "FAIL $track $scene: the observer gave no frame hints"; ok=0; }
    [ "$depth" = "1" ] || { echo "FAIL $track $scene: no depth buffer was given to the lighting"; ok=0; }
    [ "$lights" -ge 2 ] || { echo "FAIL $track $scene: expected at least an engine light and a headlight, got $lights"; ok=0; }
    IFS=/ read -r tFloor tCeil tWall tObj tCraft <<< "$tags"
    [ $(( ${tFloor:-0} + ${tWall:-0} )) -gt 1000 ] || { echo "FAIL $track $scene: the rasteriser tagged almost no floor or wall (tags=$tags)"; ok=0; }
    if [ "$scene" != "cockpit640" ]; then [ "${tFloor:-0}" -gt 100 ] && [ "${tCraft:-0}" -gt 100 ] || { echo "FAIL $track $scene: no floor or craft tagged in the chase view (tags=$tags)"; ok=0; }; fi
    if [ "$scene" = "cockpit640" ]; then
      [ "$craft" = "0" ] && [ "$trail" = "0" ] || { echo "FAIL $track $scene: craft effects in the cockpit view (craft=$craft trail=$trail)"; ok=0; }
    else
      [ "$craft" = "1" ] || { echo "FAIL $track $scene: the craft is not reported on screen"; ok=0; }
      [ "$trail" -ge 2 ] || { echo "FAIL $track $scene: no trail ($trail points)"; ok=0; }
    fi
    if [ "$scene" != "cockpit640" ] && [ "$shadowed" -le 0 ]; then echo "FAIL $track $scene: no craft shadow was drawn"; ok=0; fi
    [ "$camerad" -eq 0 ] || { echo "FAIL $track $scene: the look changed the camera ($camerad pixels of the 8-bit scene differ)"; ok=0; }
    if [ "$ok" = 1 ]; then echo "ok   $track $scene (shadow pixels $shadowed, camera pixels changed $camerad, changed values $changed, trail $trail, lights $lights)"; else fail=1; fi
  done
done
if [ "$fail" = 0 ]; then echo "modern graphics scenes: all checks passed"; exit 0; fi
echo "modern graphics scenes: FAILED"; exit 1
