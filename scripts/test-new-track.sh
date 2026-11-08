#!/usr/bin/env bash
# A track made from nothing by the editor core must be a good track (docs/roadmap-track-editor.md, Phase 2 "New track", and the marketplace's submission
# checks, docs/adr/0004-track-marketplace.md). For each template variant this runs exactly what a publish step runs, with the real tools:
#   compile (HoverNetMazeCompiler) -> strict validator -> strict lint -> a finishing replay found by the game's own driver, which must verify
#   -> package (HoverNetTrackPack pack, author and licence) -> admissible to the catalogue.
# The driver is a search over the real simulation, so it is tried with a few seeds; one finishing run is the proof the track can be driven round.
#   scripts/test-new-track.sh build/HoverNetTrackSource build/HoverNetMazeCompiler build/HoverNetTrackValidator build/HoverNetTrackPack build/HoverNetDriver build/HoverNetReplay
set -uo pipefail
source_tool="${1:?usage: $0 <TrackSource> <MazeCompiler> <TrackValidator> <TrackPack> <Driver> <Replay>}"; compiler="${2:?}"; validator="${3:?}"; pack="${4:?}"; driver="${5:?}"; replay="${6:?}"
cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fail=0
variant() {   # name, then options for "new ring"
  local name="$1"; shift
  local base="$out/$(echo "$name" | tr ' ' '_')"
  "$source_tool" new ring "$base.track.txt" --name="$name" "$@" || { echo "FAIL $name: the template could not make the track"; fail=1; return; }
  "$source_tool" check "$base.track.txt" --strict > /dev/null || { echo "FAIL $name: the editor model reports problems"; fail=1; return; }
  "$compiler" "$base.trk" "$base.track.txt" > /dev/null 2>&1 || { echo "FAIL $name: the compiler refused it"; fail=1; return; }
  "$validator" "$base.trk" "$name" > "$base.validate.txt" 2>&1 && grep -q "^VALID .* mode=race .*warnings=0" "$base.validate.txt" || { echo "FAIL $name: strict validator"; cat "$base.validate.txt"; fail=1; return; }
  "$pack" lint "$base.trk" --strict > /dev/null 2>&1 || { echo "FAIL $name: strict lint"; fail=1; return; }
  local finished=no
  for seed in 7 1 2 3 4 5; do
    if timeout 300 "$driver" "$base.trk" "$base.replay" --laps=1 --seed=$seed --width=300 > "$base.driver.txt" 2>&1 && grep -q "finished 1 lap" "$base.driver.txt"; then finished=yes; break; fi
  done
  [ "$finished" = yes ] || { echo "FAIL $name: no finishing run was found (tried seeds 7 1 2 3 4 5): the track may not be drivable"; tail -3 "$base.driver.txt"; fail=1; return; }
  "$replay" verify "$base.replay" > /dev/null 2>&1 || { echo "FAIL $name: the finishing replay does not verify"; fail=1; return; }
  "$pack" pack "$base.trk" "$base.hntp" --author "Test Author" --licence CC-BY-4.0 > /dev/null 2>&1 || { echo "FAIL $name: packaging"; fail=1; return; }
  "$pack" verify "$base.hntp" --catalogue 2>&1 | grep -q "admissible to the catalogue" || { echo "FAIL $name: not admissible to the catalogue"; fail=1; return; }
  echo "ok   $name: compiles, strict validator and lint clean, finished (seed $seed), replay verifies, packaged and admissible"
}
variant "Test Loop"
variant "Wide Sweep" --rooms=32 --radius=200 --width=40
# A track with no pickups or fuel is valid but has no weapons or refuelling: the strict lint must say so (it is the editor's warning to the author).
"$source_tool" new ring "$out/plain.track.txt" --name="Plain Ring" --plain && "$compiler" "$out/plain.trk" "$out/plain.track.txt" > /dev/null 2>&1 || { echo "FAIL Plain Ring: could not be made"; fail=1; }
if "$pack" lint "$out/plain.trk" > "$out/plain.lint.txt" 2>&1 && grep -q "no power-ups or fuel zones" "$out/plain.lint.txt" && ! "$pack" lint "$out/plain.trk" --strict > /dev/null 2>&1; then
  echo "ok   Plain Ring: lint warns that there are no power-ups or fuel zones, and --strict refuses it"
else echo "FAIL Plain Ring: expected the no-pickups lint warning"; fail=1; fi
exit $fail
