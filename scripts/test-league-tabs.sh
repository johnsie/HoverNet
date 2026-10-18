#!/usr/bin/env bash
# The RECENT WINNERS, MONTHLY LEAGUE and RECORDS BROKEN tabs of the Leaderboards screen of the real client against a real server with a seeded league log and results tables:
# only races of two or more players that passed the league's checks are winners, the league shows this month's standings, the winner of each earlier month and
# the winner of each earlier year. Test hooks: HOVERNET_TEST_LEADERBOARD_TAB (1 = recent winners, 2 = league, 3 = records broken), HOVERNET_TEST_REPLAY_LIBRARY prints what the tabs received.
#   scripts/test-league-tabs.sh build/HoverNetAuthServer build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <replay tool> <client>}"; tool="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
H='rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\tvalid\n'
"$tool" record ClassicH 600 "$dir/replays/ClassicH-1-100.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
"$tool" record ClassicH 700 "$dir/replays/ClassicH-2-200.hnr" --players=2 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
printf "$H"'1\t1\t1\t1\t61000\t61000\tAnna\t0\t1\n2\t0\t1\t1\t70000\t70000\tBen\t2\t1\n' > "$dir/replays/ClassicH-1-100.results.tsv"
printf "$H"'1\t0\t1\t1\t50000\t50000\tSelf\t0\t0\n2\t1\t1\t1\t51000\t51000\tSelf2\t0\t0\n' > "$dir/replays/ClassicH-2-200.results.tsv"
touch -d '2 hours ago' "$dir/replays/ClassicH-1-100.hnr"
now=$(date +%s); lastmonth=$(date -d "$(date +%Y-%m-15) -1 month" +%s); lastyear=$(date -d "$(( $(date +%Y) - 1 ))-06-15" +%s)
thismonth=$(date -u +%Y-%m); prevmonth=$(date -u -d "$(date -u +%Y-%m-15) -1 month" +%Y-%m); prevyear=$(( $(date -u +%Y) - 1 ))
A=$(printf 'a%.0s' $(seq 32)); B=$(printf 'b%.0s' $(seq 32)); C=$(printf 'c%.0s' $(seq 32))   # a device is 32 hex digits
row() { printf 'S\t%s\t%s\t%s\t%s\t%s\n' "$@"; }
{
  row "$now" "$A" Anna 3 1; row "$now" "$B" Ben 0 2; row "$now" "$A" Anna 3 1; row "$now" "$C" Cara 0 2
  row "$lastmonth" "$C" Cara 3 1; row "$lastmonth" "$B" Ben 0 2
  row "$lastyear" "$B" Ben 3 1; row "$lastyear" "$A" Anna 0 2
} > "$dir/replays/league.tsv"
"$server" --lobby --port 0 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
shot() { HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_MENU=leaderboards HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_LEADERBOARD_TAB="$1" ${2:+HOVERNET_CAPTURE_FRAME_FILE="$2"} timeout -s KILL 30 "$client" --play --leaderboards-screen --frames 200 2>&1; }
out=$(shot 1)
if grep -q "^WINNERS 1 | ClassicH: Anna 61000 of 2$" <<< "$out"; then echo "ok   recent winners lists the race of two players and not the race one person made against themselves"; else echo "FAIL: recent winners"; grep WINNERS <<< "$out"; fail=1; fi
out=$(shot 2)
if grep -q "^LEAGUE $thismonth of $thismonth | Anna 6 | Cara 0 | Ben 0 || months | $prevmonth Cara 3 | $prevyear-06 Ben 3 || years | $prevyear Ben 3$" <<< "$out" || grep -q "^LEAGUE $thismonth of $thismonth | Anna 6 | Ben 0 | Cara 0 || months | $prevmonth Cara 3 | $prevyear-06 Ben 3 || years | $prevyear Ben 3$" <<< "$out"; then echo "ok   the league shows this month's standings, the winner of the month before and of last year"; else echo "FAIL: the league"; grep LEAGUE <<< "$out"; fail=1; fi
out=$(shot 3)
if grep -q "^BREAKS 2 | ClassicH Basic lap: Self 50000 beat Anna 61000 | ClassicH Basic race: Self 50000 beat Anna 61000$" <<< "$out"; then echo "ok   records broken lists the newest results that beat a record, with the time and the holder they beat (the first result on a board breaks nothing)"; else echo "FAIL: records broken"; grep BREAKS <<< "$out"; fail=1; fi
[ $fail -eq 0 ] && echo "league tabs: recent winners, monthly league and records broken"
exit $fail
