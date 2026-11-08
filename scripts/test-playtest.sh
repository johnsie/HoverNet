#!/usr/bin/env bash
# Playtest (docs/roadmap-track-editor.md, Phase 3): the editor compiles the track being edited with the real compiler and the real game races on it.
#   - the game: --playtest FILE.trk lists the track as "Playtest", skips the menus and loads it (a missing file is a clear error);
#   - the editor: --playtest SOURCE compiles the source and runs the game on it, for every bundled source and for tracks made in the editor; a source the compiler
#     refuses, or a missing game program, is reported in plain words and the exit status says so.
#   scripts/test-playtest.sh BUILD_DIR
set -uo pipefail
b="${1:?usage: $0 BUILD_DIR}"
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOVERNET_NO_MARKET=1
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
export TMPDIR="$w"
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

# the game on a compiled file
"$b/HoverNetTrackSource" new ring "$w/ring.track.txt" --name=Ring > /dev/null && "$b/HoverNetMazeCompiler" "$w/ring.trk" "$w/ring.track.txt" > /dev/null 2>&1
out="$(HOME="$w/home" timeout 90 "$b/HoverNetGame2Player" --play --playtest "$w/ring.trk" --laps 2 --frames 40 2>&1)"; code=$?
[ $code -eq 0 ] && grep -q "^Playtest 3D view: room=0" <<< "$out" && ok "the game goes straight to a race on the compiled track, listed as Playtest" || { bad "the game did not start on the playtest track (exit $code)"; echo "$out" | tail -3; }
out="$(HOME="$w/home2" timeout 90 "$b/HoverNetGame2Player" --play --playtest "$w/nothing.trk" --frames 40 2>&1)"; code=$?
[ $code -ne 0 ] && grep -q "Could not load the track to playtest" <<< "$out" && ok "a playtest file that is not there is refused with a message" || bad "a missing playtest file was not refused (exit $code)"

# the editor on every bundled source and on tracks it made
"$b/HoverNetTrackSource" new rebuilt "$w/built.track.txt" --name=Built --rooms=16 --radius=120 > /dev/null
for src in NetTarget/TrackSources/*.track.txt "$w/built.track.txt"; do
  name="$(basename "$src" .track.txt)"
  out="$(HOME="$w/home3" HOVERNET_PLAYTEST_EXTRA="--frames 40" timeout 120 "$b/HoverNetTrackEditor" "$src" --playtest 2>&1)"; code=$?
  if [ $code -eq 0 ] && grep -q "^Playtest 3D view: room=" <<< "$out"; then ok "$name: the editor compiles it and the game races on it"; else bad "$name: playtest failed (exit $code)"; echo "$out" | tail -3; fi
done
# a track with a fault the editor can see (a surface the game does not have): refused before anything is launched, with the reason
sed 's/wall=\(.*\), 1, 52/wall=\1, 1, 999/' NetTarget/TrackSources/Switchback.track.txt > "$w/broken.track.txt"
out="$("$b/HoverNetTrackEditor" "$w/broken.track.txt" --playtest 2>&1)"; code=$?
[ $code -ne 0 ] && grep -q "The game cannot play this track yet: .*surface 999 is not an element the game knows" <<< "$out" && ! grep -q "Playtest 3D view" <<< "$out" && ok "a track with a fault is refused before the game starts, and the reason is plain" || { bad "a broken track was not refused with the reason (exit $code)"; echo "$out" | tail -3; }
# the guidance is not a fault: a start room with no checkpoints is free play, and can still be driven
"$b/HoverNetTrackSource" new start "$w/start.track.txt" --name=Start > /dev/null
out="$(HOME="$w/home4" HOVERNET_PLAYTEST_EXTRA="--frames 40" timeout 120 "$b/HoverNetTrackEditor" "$w/start.track.txt" --playtest 2>&1)"; code=$?
[ $code -eq 0 ] && grep -q "^Playtest 3D view: room=" <<< "$out" && ok "a start room on its own (free play, no checkpoints yet) can be playtested" || { bad "a start room could not be playtested (exit $code)"; echo "$out" | tail -3; }
# no game program next to the editor
out="$("$b/HoverNetTrackEditor" "$w/built.track.txt" --playtest --tools /nonexistent/ 2>&1)"; code=$?
[ $code -ne 0 ] && grep -q "was not found next to the editor" <<< "$out" && ok "a missing game program is reported in plain words" || { bad "a missing game was not reported (exit $code)"; echo "$out" | tail -2; }
exit $fail
