#!/usr/bin/env bash
# The ghost of the player's best run: the real client records a driven run, saves it when the finish is a new personal best, and shows it in the
# next race on the same track and lap count. Test hooks finish the race at a chosen frame (HOVERNET_TEST_FINISH, _FRAME, _TIME) and report the
# ghost (HOVERNET_TEST_GHOST). Takes about half a minute: the countdown runs in real time.
#   scripts/test-ghost.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
cfg="$HOME/.config/hovernet"
drive() { # finish time
  env HOVERNET_TEST_GHOST=1 HOVERNET_TEST_FINISH=1 HOVERNET_TEST_FINISH_FRAME=560 HOVERNET_TEST_FINISH_TIME="$1" timeout 120 "$client" --play --track ClassicH --autoplay --frames 640 > "$dir/out.txt" 2>&1
}
drive 90000
grep -q "GHOSTTEST saved [1-9][0-9]* samples" "$dir/out.txt" || { echo "FAIL: the first finish did not save a ghost"; grep -E "GHOST|FINISH" "$dir/out.txt"; tail -3 "$dir/out.txt"; exit 1; }
ls "$cfg"/ghost_ClassicH_*.bin > /dev/null 2>&1 || { echo "FAIL: no ghost file was written"; ls "$cfg"; exit 1; }
grep -q "GHOSTTEST ghost shown" "$dir/out.txt" && { echo "FAIL: a ghost was shown in the very first race"; exit 1; }
first=$(stat -c %s "$cfg"/ghost_ClassicH_*.bin)

drive 95000     # a slower finish: the ghost stays, and is shown during the race
grep -q "GHOSTTEST ghost shown" "$dir/out.txt" || { echo "FAIL: the saved ghost was not shown in the next race"; grep -E "GHOST" "$dir/out.txt"; exit 1; }
grep -q "GHOSTTEST saved" "$dir/out.txt" && { echo "FAIL: a slower finish replaced the ghost"; exit 1; }

echo 0 > "$cfg/ghost_run"      # the setting is off: no ghost is shown
drive 85000
grep -q "GHOSTTEST ghost shown" "$dir/out.txt" && { echo "FAIL: the ghost was shown with the setting off"; exit 1; }
grep -q "GHOSTTEST saved" "$dir/out.txt" || { echo "FAIL: a faster finish did not save a new ghost (even with the ghost hidden)"; grep GHOST "$dir/out.txt"; exit 1; }
rm -f "$cfg/ghost_run"

drive 80000     # a faster finish replaces it; the ghost of the previous run was shown while it was driven
grep -q "GHOSTTEST ghost shown" "$dir/out.txt" && grep -q "GHOSTTEST saved" "$dir/out.txt" || { echo "FAIL: a faster finish should show the old ghost and save a new one"; grep GHOST "$dir/out.txt"; exit 1; }
echo "ghost: a new best is saved and shown in the next race, a slower one is not saved, the setting hides it"
