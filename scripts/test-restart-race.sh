#!/usr/bin/env bash
# "Restart race" in a local race: the pause menu and the results screen start the same race again (the same track, laps and weapons). The real client with
# test hooks (HOVERNET_TEST_PAUSE_AT_FRAME and _PAUSE_CHOICE=restart; HOVERNET_TEST_FINISH and _RESULTS_CHOICE=restart; HOVERNET_TEST_RESTART prints).
#   scripts/test-restart-race.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
out=$(HOVERNET_TEST_RESTART=1 HOVERNET_TEST_PAUSE_AT_FRAME=60 HOVERNET_TEST_PAUSE_CHOICE=restart timeout -s KILL 30 "$client" --play --track "The River" --autoplay --frames 100000 2>&1)
if grep -q "RACETEST restarted The River laps 1 weapons 1" <<< "$out"; then echo "ok   RESTART RACE in the pause menu starts the same local race again"; else echo "FAIL: pause menu restart"; tail -4 <<< "$out"; fail=1; fi
out=$(HOVERNET_TEST_RESTART=1 HOVERNET_TEST_FINISH=1 HOVERNET_TEST_RESULTS_CHOICE=restart timeout -s KILL 40 "$client" --play --track "The River" --autoplay --frames 100000 2>&1)
if grep -q "FINISHTEST results screen opened" <<< "$out" && grep -q "RACETEST restarted The River laps 1 weapons 1" <<< "$out"; then echo "ok   RESTART RACE on the results screen starts the same local race again"; else echo "FAIL: results screen restart"; tail -4 <<< "$out"; fail=1; fi
[ $fail -eq 0 ] && echo "restart race: from the pause menu and the results screen"
exit $fail
