#!/usr/bin/env bash
# The Track Market's submission check (scripts/check-track-submission.sh) must accept a good submission and refuse each kind of bad one
# (docs/specs/track-market.md section 2; docs/adr/0004-track-marketplace.md decision 1).
#   scripts/test-track-submission-gate.sh BUILD_DIR
set -uo pipefail
b="${1:?usage: $0 BUILD_DIR}"
cd "$(dirname "$0")/.."
out="$(mktemp -d)"; trap 'rm -rf "$out"' EXIT
fail=0
expect() { # description, expected exit (0 or 1), command...
  local what="$1" want="$2"; shift 2
  "$@" > "$out/o.txt" 2>&1; local got=$?; [ $got -ne 0 ] && got=1
  if [ $got -eq "$want" ]; then echo "ok   $what"; else echo "FAIL $what (wanted exit $want, got $got)"; cat "$out/o.txt"; fail=1; fi
}
make_track() { # name, extra options -> $out/NAME.trk
  local n="$1"; shift
  "$b/HoverNetTrackSource" new ring "$out/$n.track.txt" --name="$n" "$@" && "$b/HoverNetMazeCompiler" "$out/$n.trk" "$out/$n.track.txt" > /dev/null 2>&1
}
finish() { # NAME -> $out/NAME.replay
  for seed in 7 1 2 3 4 5; do
    "$b/HoverNetDriver" "$out/$1.trk" "$out/$1.replay" --laps=1 --seed=$seed --width=300 2>&1 | grep -q "finished 1 lap" && return 0
  done; return 1
}
make_track good && finish good || { echo "FAIL could not make a finishable good track"; exit 1; }
make_track other --rooms=32 --radius=200 --width=40 && finish other || { echo "FAIL could not make the second track"; exit 1; }
"$b/HoverNetTrackPack" pack "$out/good.trk" "$out/good.hntp" --author "Test Author" --licence CC-BY-4.0 > /dev/null
check() { bash scripts/check-track-submission.sh --tools "$b" "$@"; }

expect "a good submission is accepted" 0 check "$out/good.hntp" "$out/good.replay"

# no author: not admissible to the catalogue
"$b/HoverNetTrackPack" pack "$out/good.trk" "$out/noauthor.hntp" --licence CC-BY-4.0 > /dev/null 2>&1
expect "a package with no named author is refused" 1 check "$out/noauthor.hntp" "$out/good.replay"

# a licence the market does not accept
"$b/HoverNetTrackPack" pack "$out/good.trk" "$out/badlicence.hntp" --author "Test Author" --licence "CC-BY-NC-4.0" > /dev/null 2>&1
expect "a package with a non-approved licence is refused" 1 check "$out/badlicence.hntp" "$out/good.replay"

# the finishing replay belongs to a different track
expect "a replay of some other track is refused" 1 check "$out/good.hntp" "$out/other.replay"

# a replay that does not finish a lap (a scripted race that only drives for a few seconds)
"$b/HoverNetReplay" record "$out/good.trk" 120 "$out/short.replay" > /dev/null 2>&1
expect "a replay that never finishes a lap is refused" 1 check "$out/good.hntp" "$out/short.replay"

# a track that is only free play / lints badly (no pickups or fuel)
make_track plain --plain && "$b/HoverNetTrackPack" pack "$out/plain.trk" "$out/plain.hntp" --author "Test Author" --licence CC0-1.0 > /dev/null
finish plain
expect "a track with no power-ups or fuel is refused by the strict lint" 1 check "$out/plain.hntp" "$out/plain.replay"

# not a package at all
printf 'not a package' > "$out/junk.hntp"
expect "a file that is not a package is refused" 1 check "$out/junk.hntp" "$out/good.replay"
exit $fail
