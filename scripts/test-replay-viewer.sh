#!/usr/bin/env bash
# The game client plays a replay back with its real renderer: the state hash it reaches after the last tick must equal the
# hash the replay tool verifies, for different tracks, player counts and watched players. Needs a headless SDL
# (SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy is set here). Run from the repository root.
#   scripts/test-replay-viewer.sh build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay> <HoverNetGame2Player>}"; player="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
check() {  # track players ticks watch
  local track="$1" players="$2" ticks="$3" watch="$4" f="$dir/r.hnr"
  "$tool" record "$track" "$ticks" "$f" --players="$players" >/dev/null 2>&1 || { echo "FAIL: could not record $track"; fail=1; return; }
  local want got
  want=$("$tool" verify "$f" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
  got=$("$player" --replay "$f" --replay-hash --replay-player "$watch" --frames 100000 2>/dev/null | sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p')
  if [ -n "$want" ] && [ "$want" = "$got" ]; then echo "ok   $track, $players players, watching $watch: $got"; else echo "FAIL $track, $players players, watching $watch: replay tool $want, client ${got:-none}"; fail=1; fi
}
check "ClassicH" 2 1500 0
check "ClassicH" 2 1500 1
check "The River" 4 1200 2
check "Steeplechase" 8 1000 5
check "Tidal Causeway" 8 1000 0
[ $fail -eq 0 ] && echo "replay viewer: the client reproduces every replay exactly"
exit $fail
