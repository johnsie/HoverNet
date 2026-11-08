#!/usr/bin/env bash
# HoverCad importer (docs/roadmap-track-editor.md): the real HoverCad projects shipped in NetTarget/mazes must import, compile, and (ClassicH) drive exactly like the
# official track: the same stage ticks and lap time with HoverNetDriver.
#   scripts/test-hovercad-import.sh build
set -uo pipefail
b="${1:?usage: $0 <build dir>}"; cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT; fail=0
chk() { if [ "$1" = ok ]; then echo "PASS: $2"; else echo "FAIL: $2"; fail=1; fi; }
for n in ClassicH Steeplechase "The Alley2"; do
  f="${n// /_}"
  "$b/HoverNetTrackSource" import "NetTarget/mazes/$n.TR" "$out/$f.track.txt" > "$out/$f.log" 2>&1 && chk ok "$n imports" || chk bad "$n imports"
  "$b/HoverNetTrackSource" check "$out/$f.track.txt" > /dev/null 2>&1 && chk ok "$n passes the source checks" || chk bad "$n passes the source checks"
  timeout 900 "$b/HoverNetMazeCompiler" "$out/$f.trk" "$out/$f.track.txt" > /dev/null 2>&1 && chk ok "$n compiles" || chk bad "$n compiles"
done
"$b/HoverNetTrackSource" import HoverCad/HCad1.TR "$out/x.txt" > /dev/null 2>&1 && chk bad "a file that is not a HoverCad project is refused" || chk ok "a file that is not a HoverCad project is refused"
head -c 200 NetTarget/mazes/ClassicH.TR > "$out/cut.TR"
"$b/HoverNetTrackSource" import "$out/cut.TR" "$out/x.txt" > /dev/null 2>&1 && chk bad "a cut-off project is refused" || chk ok "a cut-off project is refused"
a="$(timeout 300 "$b/HoverNetDriver" NetTarget/Tracks/ClassicH.trk "$out/a.replay" --laps=1 2>&1 | tail -1 | sed 's/^.*finished/finished/; s/, replay.*//')"
c="$(timeout 300 "$b/HoverNetDriver" "$out/ClassicH.trk" "$out/c.replay" --laps=1 2>&1 | tail -1 | sed 's/^.*finished/finished/; s/, replay.*//')"
[ -n "$a" ] && [ "$a" = "$c" ] && chk ok "ClassicH drives like the official track ($a)" || chk bad "ClassicH drives like the official track (official: $a; imported: $c)"
exit $fail
