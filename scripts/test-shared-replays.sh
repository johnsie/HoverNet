#!/usr/bin/env bash
# The real client plays a replay of a race it was not in: a lobby server with a replay folder shares its replays; the game's Watch a Replay screen
# (Server replays tab) lists them, downloads the newest, checks it, and plays it to the same final hash the replay tool verifies. Test hooks open the
# screen on that tab (HOVERNET_TEST_MENU=replays, HOVERNET_TEST_REPLAY_TAB=1) and pick the newest (HOVERNET_TEST_REPLAY_SERVER_PICK=1).
#   scripts/test-shared-replays.sh build/HoverNetAuthServer build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <replay tool> <client>}"; tool="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$tool" record ClassicH 600 "$dir/replays/ClassicH-1-100.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
"$tool" record "The River" 900 "$dir/replays/The_River-2-200.hnr" --players=3 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
printf 'rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\n1\t1\t5\t1\t154000\t29000\tAnna\n2\t0\t5\t1\t160000\t30500\tBen\n' > "$dir/replays/The_River-2-200.results.tsv"   # player 2 (slot 1) won
touch -d '2 minutes ago' "$dir/replays/ClassicH-1-100.hnr"
want=$("$tool" verify "$dir/replays/The_River-2-200.hnr" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
"$server" --lobby --port 0 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
out=$(HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_MENU=replays HOVERNET_TEST_REPLAY_TAB=1 HOVERNET_TEST_REPLAY_SERVER_PICK=1 HOVERNET_TEST_REPLAY_PICK_CRAFT=default HOVERNET_TEST_REPLAY_LIBRARY=1 timeout 120 "$client" --play --replay-hash --frames 100000 2>&1)
got=$(sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p' <<< "$out")
if [ -n "$want" ] && [ "$want" = "$got" ] && grep -q "REPLAYTEST following player 2 of 3 (the winner, Anna)" <<< "$out"; then echo "shared replays: the game downloaded a race it was not in from the server and played it to the verified hash ($got), following the winner (named in the list) by default"; exit 0; fi
echo "FAIL: the downloaded replay did not play to the verified hash (tool $want, game ${got:-none})"; tail -12 <<< "$out"; exit 1
