#!/usr/bin/env bash
# Rolling restart: SIGTERM puts a lobby server into a drain. A race under way finishes and its replay is saved, new races cannot be
# hosted (the player is told the server is restarting), /health turns to 503 so a load balancer stops sending players, and the process
# exits 0 once the race is over. Run from the repository root.
#   scripts/test-drain.sh build/HoverNetAuthServer build/HoverNetAuthBot
set -uo pipefail
server="${1:?usage: $0 <server> <bot>}"; bot="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$server" --lobby --port 0 --status-port 0 --limit-ticks 400 --start-delay-ms 500 --replay-dir "$dir/replays" --event-log "$dir/events.jsonl" > "$dir/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
sport=$(sed -n 's/.*status on 127.0.0.1:\([0-9]*\) .*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ -n "$sport" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
fail=0
[ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$sport/health")" = 200 ] || { echo "FAIL: /health is not 200 before the drain"; fail=1; }
"$bot" --port "$port" --name a1 --host-race ClassicH:2 > "$dir/a1.txt" 2>&1 & p1=$!
sleep 0.5
"$bot" --port "$port" --name a2 --join-race ClassicH --track ClassicH > "$dir/a2.txt" 2>&1 & p2=$!
sleep 2.5                                  # the race has started
kill -TERM "$spid"
sleep 0.5
[ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$sport/health")" = 503 ] || { echo "FAIL: /health is not 503 while draining"; fail=1; }
"$bot" --port "$port" --name late --host-race "The River:2" > "$dir/late.txt" 2>&1 && { echo "FAIL: a race was hosted during the drain"; fail=1; }
grep -qi "restarting" "$dir/late.txt" || { echo "FAIL: the late player was not told the server is restarting"; cat "$dir/late.txt"; fail=1; }
wait "$p1" || fail=1; wait "$p2" || fail=1
for n in a1 a2; do grep -q "no divergence" "$dir/$n.txt" || { echo "FAIL: $n did not see the race through"; cat "$dir/$n.txt"; fail=1; }; done
wait "$spid"; rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the server exited with $rc"; fail=1; }
grep -q "^drained after" "$dir/server.txt" || { echo "FAIL: no drain message"; cat "$dir/server.txt"; fail=1; }
[ "$(ls "$dir"/replays/*.hnr 2>/dev/null | wc -l)" -eq 1 ] || { echo "FAIL: the race under way was not saved"; fail=1; }
# The event log is one JSON object per line and tells the whole story, with no player names in it.
python3 -I - "$dir/events.jsonl" <<'PY' || { echo "FAIL: the event log is wrong"; fail=1; }
import json, sys
ev = [json.loads(l) for l in open(sys.argv[1])]
names = [e["event"] for e in ev]
assert names[0] == "server_start", names
assert names.index("drain_start") < names.index("drained"), names
done = [e for e in ev if e["event"] == "race_finished"]
assert len(done) == 1 and len(done[0]["results"]) == 2 and done[0]["track"] == "ClassicH", done
assert "a1" not in open(sys.argv[1]).read()
PY
[ $fail -eq 0 ] && echo "drain: the race under way finished and was saved, new races were refused, /health went 503, the server exited 0"
exit $fail
