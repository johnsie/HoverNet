#!/usr/bin/env bash
# The way back to the main menu: from the pause menu in a race, and from the results screen after a finish, the real client goes to the main menu (a test
# hook then opens Watch a Replay from it, and the replay it plays ends the run). Test hooks: HOVERNET_TEST_PAUSE_AT_FRAME, HOVERNET_TEST_PAUSE_CHOICE=main,
# HOVERNET_TEST_FINISH, HOVERNET_TEST_RESULTS_CHOICE=main, HOVERNET_TEST_MENU=replays + HOVERNET_TEST_MENU_FORCE, HOVERNET_TEST_REPLAY_PICK, HOVERNET_TEST_REPLAY_END.
#   scripts/test-main-menu.sh build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay> <HoverNetGame2Player>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; unset XDG_CONFIG_HOME
lib="$HOME/.config/hovernet/replays"; mkdir -p "$lib"
"$tool" record ClassicH 300 "$lib/ClassicH_20261018-142355.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
common=(HOVERNET_TEST_MENU=replays HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_MENU_AFTER=1 HOVERNET_TEST_REPLAY_PICK=1 HOVERNET_TEST_REPLAY_SPEED=4 HOVERNET_TEST_REPLAY_END=quit HOVERNET_TEST_REPLAY_LIBRARY=1)
fail=0
# 1. from the pause menu of a local race (the first time the main menu is shown it is left alone: --autoplay starts the race; the second time it is forced)
out=$(env "${common[@]}" HOVERNET_TEST_PAUSE_AT_FRAME=60 HOVERNET_TEST_PAUSE_CHOICE=main timeout -s KILL 120 "$client" --play --track ClassicH --autoplay --frames 100000 2>&1)
if grep -q "REPLAYSCREEN 1 replays" <<< "$out" && grep -q "REPLAYEND shown 1" <<< "$out"; then echo "ok   MAIN MENU in the pause menu leaves the race for the main menu"; else echo "FAIL: pause menu to main menu"; tail -5 <<< "$out"; fail=1; fi
# 2. from the results screen after a finish
out=$(env "${common[@]}" HOVERNET_TEST_FINISH=1 HOVERNET_TEST_RESULTS_CHOICE=main timeout -s KILL 120 "$client" --play --track ClassicH --autoplay --frames 100000 2>&1)
if grep -q "FINISHTEST results screen opened" <<< "$out" && grep -q "REPLAYSCREEN 1 replays" <<< "$out" && grep -q "REPLAYEND shown 1" <<< "$out"; then echo "ok   MAIN MENU on the results screen leaves the race for the main menu"; else echo "FAIL: results screen to main menu"; tail -6 <<< "$out"; fail=1; fi
[ $fail -eq 0 ] && echo "main menu: reachable from the pause menu and the results screen"
exit $fail
