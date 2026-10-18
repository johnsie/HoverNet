#!/usr/bin/env bash
# The Leaderboards screen of the real client against a real server: the community's times per track and per craft, with the players' names (a time in one
# craft never shares a board with another craft's), and WATCH plays the record's run to the replay's verified hash, following the craft that set it.
# Test hooks: HOVERNET_TEST_MENU=leaderboards (with _MENU_FORCE) opens the screen, HOVERNET_TEST_LEADERBOARD_PICK watches the first record,
# HOVERNET_TEST_RECORDS_TRACK / _LAPS choose what is shown, HOVERNET_TEST_REPLAY_LIBRARY prints what the screen received.
#   scripts/test-leaderboards.sh build/HoverNetAuthServer build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <replay tool> <client>}"; tool="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$tool" record "The River" 900 "$dir/replays/The_River-1-100.hnr" --players=3 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
"$tool" record "The River" 700 "$dir/replays/The_River-2-200.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
printf 'rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n1\t1\t5\t1\t154000\t29000\tAnna\t2\n2\t0\t5\t1\t160000\t30500\tBen\t0\n3\t2\t5\t0\t0\t31000\tCara\t2\n' > "$dir/replays/The_River-1-100.results.tsv"
printf 'rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n1\t0\t5\t1\t150000\t28000\tDee\t0\n2\t1\t5\t1\t158000\t29500\tEli\t0\n' > "$dir/replays/The_River-2-200.results.tsv"
want=$("$tool" verify "$dir/replays/The_River-2-200.hnr" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
"$server" --lobby --port 0 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
run() { HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_MENU=leaderboards HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_RECORDS_TRACK="The River" "$@" timeout -s KILL 60 "$client" --play --replay-hash --frames 100000 2>&1; }
out=$(HOVERNET_TEST_LEADERBOARD_PICK=1 run env)
# Fastest laps (any race): Basic has Dee 28000 first (then Eli, Ben); Bi-Turbo has Anna 29000 first (then Cara).
if grep -q "LEADERBOARDS The River laps 0 boards 2 | Basic: Dee 28000 | Bi-Turbo: Anna 29000" <<< "$out"; then echo "ok   the leaderboard has a board per craft with the names and best laps (a Bi-Turbo time does not touch the Basic board)"; else echo "FAIL: the fastest laps per craft"; grep LEADERBOARDS <<< "$out"; fail=1; fi
got=$(sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p' <<< "$out")
if [ -n "$want" ] && [ "$want" = "$got" ] && grep -q "REPLAYTEST following player 1 of 2 (the fastest lap)" <<< "$out"; then echo "ok   WATCH plays the record holder's race to the verified hash, following the craft that set it ($got)"; else echo "FAIL: watching the first record (tool $want, game ${got:-none})"; grep REPLAYTEST <<< "$out"; fail=1; fi
# Fastest races of 5 laps: Basic Dee 150000 first; Bi-Turbo Anna 154000.
out=$(HOVERNET_TEST_RECORDS_LAPS=5 timeout -s KILL 30 env HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_MENU=leaderboards HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_RECORDS_TRACK="The River" "$client" --play --leaderboards-screen --frames 200 2>&1)
if grep -q "LEADERBOARDS The River laps 5 boards 2 | Basic: Dee 150000 | Bi-Turbo: Anna 154000" <<< "$out"; then echo "ok   the fastest races of a lap count have their own per-craft boards"; else echo "FAIL: the fastest races per craft"; grep LEADERBOARDS <<< "$out"; fail=1; fi
# From the end of a replay: its end screen offers LEADERBOARDS; choosing it opens the screen, and watching a record plays that run to its end.
lib="$HOME/.config/hovernet/replays"; mkdir -p "$lib"
"$tool" record ClassicH 300 "$lib/ClassicH_20261018-142355.hnr" --players=2 > /dev/null 2>&1
out=$(HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_RECORDS_TRACK="The River" HOVERNET_TEST_LEADERBOARD_PICK=1 HOVERNET_TEST_REPLAY_SPEED=4 HOVERNET_TEST_REPLAY_END=leaderboards,quit timeout -s KILL 90 "$client" --play --replay "$lib/ClassicH_20261018-142355.hnr" --frames 100000 2>&1)
if grep -q "REPLAYEND shown 1: leaderboards" <<< "$out" && grep -q "LEADERBOARDS The River" <<< "$out" && grep -q "REPLAYEND shown 2: quit" <<< "$out"; then echo "ok   the end of a replay offers the Leaderboards, and watching a record from there plays it to its end"; else echo "FAIL: leaderboards from the end of a replay"; grep -E "REPLAYEND|LEADERBOARDS" <<< "$out"; fail=1; fi
[ $fail -eq 0 ] && echo "leaderboards: per craft, with names, and watchable"
exit $fail
