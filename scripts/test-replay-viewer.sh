#!/usr/bin/env bash
# The game client plays a replay back with its real renderer: the state hash it reaches after the last tick must equal the
# hash the replay tool verifies, for different tracks, player counts and watched players. Needs a headless SDL
# (SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy is set here). Run from the repository root.
#   scripts/test-replay-viewer.sh build/HoverNetReplay build/HoverNetGame2Player [build/HoverNetDriver]
# With the driver, a two-lap race that really finishes also checks the timeline markers: the lap crossings the viewer notes while the
# replay plays must be the ones "HoverNetReplay events" lists, and jumping back to the previous lap marker must not change the result.
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay> <HoverNetGame2Player> [<HoverNetDriver>]}"; player="${2:?}"; driver="${3:-}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
check() {  # track players ticks watch [extra client option]
  local track="$1" players="$2" ticks="$3" watch="$4" f="$dir/r.hnr"
  "$tool" record "$track" "$ticks" "$f" --players="$players" >/dev/null 2>&1 || { echo "FAIL: could not record $track"; fail=1; return; }
  local want got
  want=$("$tool" verify "$f" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
  got=$("$player" --replay "$f" ${5:-"--replay-hash"} --replay-player "$watch" --frames 100000 2>/dev/null | sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p')
  if [ -n "$want" ] && [ "$want" = "$got" ]; then echo "ok   $track, $players players, watching $watch${5:+ ($5)}: $got"; else echo "FAIL $track, $players players, watching $watch: replay tool $want, client ${got:-none}"; fail=1; fi
}
check "ClassicH" 2 1500 0
check "ClassicH" 2 1500 1
check "The River" 4 1200 2
check "Steeplechase" 8 1000 5
check "Tidal Causeway" 8 1000 0
check "ClassicH" 2 1500 0 --replay-seek-test      # plays to tick 1000, jumps back to 400 from a snapshot, plays on
check "Steeplechase" 8 1400 3 --replay-seek-test
if [ -n "$driver" ]; then
  f="$dir/laps.hnr"
  if "$driver" ClassicH "$f" --laps=2 --seed=2 --hold=14 --max-ticks=40000 >/dev/null 2>&1; then
    want=$("$tool" verify "$f" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
    out=$("$player" --replay "$f" --replay-marker-test --frames 100000 2>/dev/null)
    got=$(sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p' <<< "$out")
    marks=$(sed -n 's/^marker tick \([0-9]*\) player \([0-9]*\) lap \([0-9]*\).*/\1 \2 \3/p' <<< "$out")
    events=$("$tool" events "$f" 2>/dev/null | sed -n 's/^tick \([0-9]*\) player \([0-9]*\) \(lap\|finish\) \([0-9]*\) .*/\1 \2 \4/p')
    if [ -n "$want" ] && [ "$want" = "$got" ] && [ -n "$marks" ] && [ "$marks" = "$events" ] && grep -q "^marker jump from [0-9]* to [0-9]* with 1 markers" <<< "$out"; then
      echo "ok   lap markers: $(wc -l <<< "$marks") found, equal to the events list, jump back and replay hash $got"
    else echo "FAIL lap markers: replay tool $want, client ${got:-none}; markers [$marks] events [$events]"; fail=1; fi
  else echo "FAIL: the driver found no two-lap finish"; fail=1; fi
fi
[ $fail -eq 0 ] && echo "replay viewer: the client reproduces every replay exactly"
exit $fail
