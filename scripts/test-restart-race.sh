#!/usr/bin/env bash
# "Restart race" in a local race: the pause menu starts the same race again (the same track, laps and weapons). Results deliberately offer only Main Menu.
# The real client uses HOVERNET_TEST_PAUSE_AT_FRAME, _PAUSE_CHOICE=restart and HOVERNET_TEST_RESTART for this check.
#   scripts/test-restart-race.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
out=$(HOVERNET_TEST_RESTART=1 HOVERNET_TEST_PAUSE_AT_FRAME=60 HOVERNET_TEST_PAUSE_CHOICE=restart timeout -s KILL 30 "$client" --play --track "The River" --autoplay --frames 100000 2>&1)
if grep -q "RACETEST restarted The River laps 1 weapons 1" <<< "$out"; then echo "ok   RESTART RACE in the pause menu starts the same local race again"; else echo "FAIL: pause menu restart"; tail -4 <<< "$out"; fail=1; fi
# The results screen of an offline race offers only MAIN MENU (no Restart, Keep Driving, New Local Race, Online Multiplayer or Quit): that was asked for, so it is checked here.
out=$(HOVERNET_TEST_FINISH=1 timeout -s KILL 40 "$client" --play --track "The River" --autoplay --frames 100000 2>&1)
if grep -q "^FINISHTEST results buttons: MAIN MENU$" <<< "$out"; then echo "ok   the results screen of an offline race offers only MAIN MENU"; else echo "FAIL: results screen buttons"; grep "FINISHTEST" <<< "$out" | tail -3; fail=1; fi
[ $fail -eq 0 ] && echo "restart race: from the pause menu; results screen has only the main menu"
exit $fail
