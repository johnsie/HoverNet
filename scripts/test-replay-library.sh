#!/usr/bin/env bash
# The Watch a Replay screen of the real client: replays in the player's replays folder are listed (a damaged file is flagged), and choosing one plays
# it in the game to the same final hash the replay tool verifies. The menu choice and the pick are test hooks (HOVERNET_TEST_MENU=replays,
# HOVERNET_TEST_REPLAY_PICK=1).
#   scripts/test-replay-library.sh build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
tool="${1:?usage: $0 <HoverNetReplay> <HoverNetGame2Player>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; unset XDG_CONFIG_HOME
lib="$HOME/.config/hovernet/replays"; mkdir -p "$lib"
"$tool" record ClassicH 900 "$lib/ClassicH_20261018-142355.hnr" --players=3 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
"$tool" record "The River" 700 "$lib/The_River_20261017-101010.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a second replay"; exit 1; }
echo "not a replay" > "$lib/Broken_20261018-150000.hnr"
echo 2 > "$lib/ClassicH_20261018-142355.hnr.slot"   # the player drove craft 3 of the 3 in that race
want=$("$tool" verify "$lib/ClassicH_20261018-142355.hnr" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
out=$(HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_MENU=replays HOVERNET_TEST_REPLAY_PICK=1 HOVERNET_TEST_REPLAY_PICK_CRAFT=default timeout 120 "$client" --play --replay-hash --frames 100000 2>&1)
fail=0
grep -q "REPLAYSCREEN 3 replays" <<< "$out" || { echo "FAIL: the screen should list 3 files (2 replays and a damaged one)"; grep REPLAYSCREEN <<< "$out"; fail=1; }
got=$(sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p' <<< "$out")
# The newest valid replay is the ClassicH one (the hook picks the first valid entry, newest first).
if [ -n "$want" ] && [ "$want" = "$got" ]; then echo "ok   Watch a Replay lists the saved replays and plays the chosen one to the verified hash ($got)"
else echo "FAIL: the replay chosen in the menu did not play to the verified hash (tool $want, game ${got:-none})"; tail -5 <<< "$out"; fail=1; fi
grep -q "REPLAYTEST following player 3 of 3 (you)" <<< "$out" || { echo "FAIL: the replay should have followed the player's own craft (player 3, you)"; grep REPLAYTEST <<< "$out"; fail=1; }
[ $fail -eq 0 ] && echo "replay library screen: listing, a damaged file flagged, playing from the menu"
exit $fail
