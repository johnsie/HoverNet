#!/usr/bin/env bash
# The "Race view" setting picks the shape of the picture the race is drawn in, when the game starts (Settings > Race view).
# Classic stays 1024x768; Widescreen is 1280x720; Match my window follows the saved window (or screen) shape, between 4:3 and 2.4:1.
#   scripts/test-race-view.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; unset XDG_CONFIG_HOME
cfg="$HOME/.config/hovernet"; mkdir -p "$cfg"
size() { HOVERNET_TEST_PRINT_RES=1 timeout 60 "$client" --play --track ClassicH --frames 2 2>&1 | sed -n 's/^RACEVIEW //p' | head -1; }
check() { [ "$(size)" = "$1" ] || { echo "FAIL: $2: expected $1, got '$(size)'"; exit 1; }; }
check 1024x768 "no setting"
echo classic > "$cfg/race_view";  check 1024x768 "classic"
echo wide > "$cfg/race_view";     check 1280x720 "widescreen"
echo window > "$cfg/race_view"
echo "1024 768" > "$cfg/window_size";  check 1024x768 "match window, a 4:3 window"
echo "1600 900" > "$cfg/window_size";  check 1280x720 "match window, a 16:9 window"
echo "2560 1080" > "$cfg/window_size"; check 1280x540 "match window, a 21:9 window"
echo "3440 1000" > "$cfg/window_size"; check 1280x532 "match window, an extremely wide window is limited to 2.4:1"
echo nonsense > "$cfg/race_view";  check 1024x768 "an unreadable setting falls back to classic"
echo "race view: classic, widescreen and match-my-window pick 1024x768, 1280x720 and the window's shape (limited to 4:3 .. 2.4:1)"
