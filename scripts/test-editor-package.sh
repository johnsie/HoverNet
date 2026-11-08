#!/usr/bin/env bash
# The hovernet-editor package works when installed: it is built from a build tree, unpacked into a folder (dpkg-deb -x, no root needed), and run from there with the
# same HOVERNET_DATA_DIR the launcher sets, from a different folder than the data: it must find its fonts, compile and check a bundled example (with its background
# picture), and playtest it with the game from the game package.
#   scripts/test-editor-package.sh BUILD_DIR
set -uo pipefail
command -v dpkg-deb >/dev/null || { echo "SKIP: dpkg-deb is not installed"; exit 77; }
b="${1:?usage: $0 BUILD_DIR}"
cd "$(dirname "$0")/.."
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOVERNET_NO_MARKET=1

# the packages: the editor's, and the game's (built the way CI builds it) so the editor's dependency is really there
rm -rf dist/debian/hovernet-editor_* dist/debian/hovernet-game_*
bash packaging/debian/build-editor-deb.sh 9.9.9 "$b" > "$w/build.txt" 2>&1 && ok "the editor package builds" || { bad "the editor package did not build"; cat "$w/build.txt"; exit 1; }
bash packaging/debian/build-game-deb.sh 9.9.9 "$b/HoverNetGame2Player" "$b/ObjFac1.so" > "$w/gbuild.txt" 2>&1 && ok "the game package builds (the editor's dependency)" || { bad "the game package did not build"; cat "$w/gbuild.txt"; exit 1; }
editor="dist/hovernet-editor_9.9.9_$(dpkg --print-architecture).deb"; game="dist/hovernet-game_9.9.9_$(dpkg --print-architecture).deb"
editor_info="$(dpkg-deb -I "$editor")"   # read it all first: grep -q exits early and, under pipefail, breaks the pipe
grep -q "^ Depends: hovernet-game" <<< "$editor_info" && ok "the editor package depends on the game package" || bad "the editor package does not depend on the game"
root="$w/root"; mkdir -p "$root"; dpkg-deb -x "$game" "$root"; dpkg-deb -x "$editor" "$root"
for f in usr/lib/hovernet/HoverNetTrackEditor usr/lib/hovernet/HoverNetMazeCompiler usr/lib/hovernet/HoverNetTrackValidator usr/lib/hovernet/HoverNetGame2Player usr/lib/hovernet/ObjFac1.so \
         usr/games/hovernet-editor usr/share/games/hovernet/NetTarget/mazes/BG-CITY.pcx "usr/share/games/hovernet/NetTarget/TrackSources/Tidal Causeway.track.txt" usr/share/games/hovernet/NetTarget/LinuxClient/assets/Inter-Variable.ttf; do
  [ -e "$root/$f" ] || bad "the installed tree has no $f"
done
ok "the installed tree has the programs, the plug-in, the fonts, the backgrounds and the examples"

# run it the way the launcher does, but from the unpacked tree and from an unrelated folder
export HOVERNET_DATA_DIR="$root/usr/share/games/hovernet"
cd "$w"
lib="$root/usr/lib/hovernet"
sample="$HOVERNET_DATA_DIR/NetTarget/TrackSources/Tidal Causeway.track.txt"
out="$("$lib/HoverNetTrackEditor" "$sample" --check 2>&1)"; code=$?
[ $code -eq 0 ] && grep -q "^  VALID " <<< "$out" && ok "the installed editor compiles and strictly checks an example, finding its background picture" || { bad "compile and check failed from the installed tree (exit $code)"; echo "$out" | tail -5; }
out="$("$lib/HoverNetTrackEditor" --new-loop --frames 3 --capture "$w/shot.bmp" 2>&1)"; code=$?
[ $code -eq 0 ] && ! grep -q "was not found" <<< "$out" && [ -s "$w/shot.bmp" ] && ok "the installed editor opens its window and finds its fonts" || { bad "the installed editor could not draw (exit $code)"; echo "$out" | tail -3; }
out="$(HOME="$w/home" HOVERNET_PLAYTEST_EXTRA="--frames 40" timeout 120 "$lib/HoverNetTrackEditor" "$sample" --playtest 2>&1)"; code=$?
[ $code -eq 0 ] && grep -q "^Playtest 3D view: room=" <<< "$out" && ok "the installed editor playtests the example in the installed game" || { bad "playtest failed from the installed tree (exit $code)"; echo "$out" | tail -4; }
# the launcher resolves a relative file name before it changes anything
mkdir -p "$w/work"; cp "$sample" "$w/work/mine.track.txt"; cd "$w/work"
sed "s#/usr/share/games/hovernet#$HOVERNET_DATA_DIR#; s#/usr/lib/hovernet#$lib#" "$root/usr/games/hovernet-editor" > "$w/launch.sh"
out="$(bash "$w/launch.sh" mine.track.txt --check 2>&1)"; code=$?
[ $code -eq 0 ] && grep -q "^  VALID " <<< "$out" && ok "the launcher takes a file name relative to where you are" || { bad "the launcher did not handle a relative file name (exit $code)"; echo "$out" | tail -3; }
exit $fail
