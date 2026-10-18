#!/usr/bin/env bash
# What happens when a replay reaches its end, and how the menus lead back to the main menu: the real client shows the end screen, "watch again" plays the
# replay again, "main menu" goes to the main menu (a test hook then opens Watch a Replay from it) and from there another replay plays, and "quit" leaves.
# Test hooks: HOVERNET_TEST_REPLAY_END (the choices), HOVERNET_TEST_MENU=replays with HOVERNET_TEST_MENU_FORCE, HOVERNET_TEST_REPLAY_PICK.
#   scripts/test-replay-end.sh build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay> <HoverNetGame2Player>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; unset XDG_CONFIG_HOME
lib="$HOME/.config/hovernet/replays"; mkdir -p "$lib"
"$tool" record ClassicH 300 "$lib/ClassicH_20261018-142355.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
fail=0
run() { local limit="${2:-120}"; HOVERNET_TEST_REPLAY_SPEED=4 HOVERNET_TEST_REPLAY_END="$1" HOVERNET_TEST_MENU=replays HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_REPLAY_PICK=1 timeout -s KILL "$limit" "$client" --play --replay "$lib/ClassicH_20261018-142355.hnr" --frames 100000 > "$dir/out.txt" 2>&1; echo $?; }
rc=$(run "quit")
[ "$rc" = 0 ] && [ "$(grep -c "REPLAYEND shown" "$dir/out.txt")" = 1 ] && echo "ok   a replay that reaches its end shows the end screen, and Quit leaves" || { echo "FAIL: end screen then quit (exit $rc)"; cat "$dir/out.txt" | tail -4; fail=1; }
rc=$(run "again,quit")
[ "$rc" = 0 ] && [ "$(grep -c "REPLAYEND shown" "$dir/out.txt")" = 2 ] && echo "ok   Watch Again plays the replay again, to a second end screen" || { echo "FAIL: watch again (exit $rc)"; tail -4 "$dir/out.txt"; fail=1; }
rc=$(run "main,quit")
[ "$rc" = 0 ] && [ "$(grep -c "REPLAYEND shown" "$dir/out.txt")" = 2 ] && grep -q "REPLAYSCREEN\|REPLAYEND shown 2" "$dir/out.txt" && echo "ok   Main Menu leaves the replay, the main menu opens Watch a Replay, and the chosen replay plays to its end" || { echo "FAIL: main menu (exit $rc)"; tail -6 "$dir/out.txt"; fail=1; }
rc=$(run "stay" 20)     # staying keeps the game running on the finished replay: it is still there when the time limit ends it (killed: exit 137)
{ [ "$rc" = 124 ] || [ "$rc" = 137 ]; } && [ "$(grep -c "REPLAYEND shown" "$dir/out.txt")" = 1 ] && echo "ok   Stay keeps looking at the finished replay (the game is still running)" || { echo "FAIL: stay (exit $rc)"; tail -3 "$dir/out.txt"; fail=1; }
[ $fail -eq 0 ] && echo "replay end: the end screen, watch again, main menu, another replay"
exit $fail
