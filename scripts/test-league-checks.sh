#!/usr/bin/env bash
# The server's league checks over real TCP: scripted players host and join races on a lobby server, each saying which device it is on (kCapDeviceId).
# A race whose players are on different devices is marked valid in its results table (it may be listed as a recent winner, and it can score); a race whose
# players share a device (the same player in two tabs or windows), or a player whose game sent no device id, is not. Nobody finishes these races (the
# scripted players do not drive to the line), so no points are given here: the scoring is tested on its own (LeagueTest) and the recent-winners list on the
# store (ReplayShareTest).
#   scripts/test-league-checks.sh build/HoverNetAuthServer build/HoverNetAuthBot
set -uo pipefail
server="${1:?usage: $0 <server> <bot>}"; bot="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$server" --lobby --port 0 --limit-ticks 300 --start-delay-ms 400 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
pids=()
run() { local name="$1"; shift; "$bot" --port "$port" --name "$name" "$@" > "$dir/$name.txt" 2>&1 & pids+=($!); }
# Race 1 (ClassicH): two different devices. Race 2 (The River): the same device twice. Race 3 (Switchback): one bot with no device id.
run a1 --host-race ClassicH:2 --device 11; sleep 0.5; run a2 --join-race ClassicH --track ClassicH --device 12
run b1 --host-race "The River:2" --track "The River" --device 21; sleep 0.5; run b2 --join-race "The River" --track "The River" --device 21
run c1 --host-race Switchback:2 --track Switchback --device 31; sleep 0.5; run c2 --join-race Switchback --track Switchback
for p in "${pids[@]}"; do wait "$p" || true; done
sleep 1
fail=0
valid() { f=$(ls "$dir"/replays/"$1"-*.results.tsv 2>/dev/null | head -1); [ -n "$f" ] && awk -F'\t' 'NR==2 {print $9}' "$f"; }
[ "$(valid ClassicH)" = "1" ] && echo "ok   two players on different devices: the race is valid" || { echo "FAIL: ClassicH should be valid"; fail=1; }
[ "$(valid "The River")" = "0" ] && echo "ok   the same device twice: the race is not valid (it scores for nobody)" || { echo "FAIL: The River should not be valid ($(valid "The River"))"; fail=1; }
[ "$(valid Switchback)" = "0" ] && echo "ok   a player with no device id (an older game): the race is not valid" || { echo "FAIL: Switchback should not be valid ($(valid Switchback))"; fail=1; }
grep -c "^league: not scored" "$dir/server.txt" | grep -q 3 && echo "ok   the server says why in its log" || { echo "FAIL: the server log"; grep league "$dir/server.txt"; fail=1; }
[ $fail -eq 0 ] && echo "league checks: different devices, the same device twice, no device"
exit $fail
