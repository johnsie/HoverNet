#!/usr/bin/env bash
# Observability of the dedicated server: the loopback status port answers /health and /metrics (Prometheus text) with counters that
# match what happened, and a crash leaves a report in the crash directory. Run from the repository root.
#   scripts/test-observability.sh build/HoverNetAuthServer build/HoverNetAuthBot
set -uo pipefail
server="${1:?usage: $0 <server> <bot>}"; bot="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
fail=0
PY=python3; command -v python3 >/dev/null 2>&1 || PY=python
if ! command -v "$PY" >/dev/null 2>&1 || ! "$PY" -c "import socket" >/dev/null 2>&1; then echo "observability: skipped the HTTP checks (no Python on this machine)"; PY=""; fi
get() { "$PY" - "$1" "$2" <<'P'
import socket, sys
data = b""
try:
    s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=5)
    s.sendall(("GET /%s HTTP/1.1\r\nHost: x\r\n\r\n" % sys.argv[2]).encode())
    while True:
        chunk = s.recv(4096)
        if not chunk: break
        data += chunk
except Exception as e:
    sys.stderr.write("get %s failed: %r after %d bytes\n" % (sys.argv[2], e, len(data)))
sys.stdout.write(data.decode(errors="replace"))
P
}
"$server" --lobby --port 0 --status-port 0 --limit-ticks 300 --start-delay-ms 300 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "status on" "$dir/server.txt" 2>/dev/null && grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
status=$(sed -n 's/.*status on 127.0.0.1:\([0-9]*\) .*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ -n "$status" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
[ -z "$PY" ] || get "$status" health | grep -q "200 OK" || { echo "FAIL: /health ($PY: $(get "$status" health 2>&1 | head -c 300))"; fail=1; }
[ -z "$PY" ] || get "$status" nothing | grep -q "404" || { echo "FAIL: unknown path should be 404"; fail=1; }
"$bot" --port "$port" --name o1 --host-race ClassicH:2 > "$dir/o1.txt" 2>&1 &
sleep 0.4
"$bot" --port "$port" --name o2 --join-race ClassicH --track ClassicH > "$dir/o2.txt" 2>&1 &
sleep 0.5
active=""; [ -z "$PY" ] || active=$(get "$status" metrics | tr -d "\r")
[ -z "$PY" ] || echo "$active" | grep -q "^hovernet_players_active 2$" || { echo "FAIL: the players_active gauge should count the two drivers during the race"; fail=1; }
wait %2 %3 2>/dev/null
sleep 1
m=""; [ -z "$PY" ] || m=$(get "$status" metrics | tr -d "\r")
[ -z "$PY" ] || echo "$m" | grep -q "^hovernet_races_hosted_total 1$" || { echo "FAIL: races hosted"; echo "$m" | grep hosted; fail=1; }
ticks=$(echo "$m" | sed -n 's/^hovernet_ticks_simulated_total \([0-9]*\)$/\1/p')
[ -z "$PY" ] || [ "${ticks:-0}" -ge 290 ] || { echo "FAIL: ticks simulated is ${ticks:-none}"; fail=1; }
[ -z "$PY" ] || echo "$m" | grep -q "^hovernet_invalid_messages_total 0$" || { echo "FAIL: invalid messages"; fail=1; }
[ -z "$PY" ] || { echo "$m" | grep -q "^# TYPE hovernet_players_active gauge$" && echo "$m" | grep -q "^hovernet_players_active 0$"; } || { echo "FAIL: the players_active gauge should be 0 after the race finishes (the Outiva status adapter reads it)"; fail=1; }
[ -z "$PY" ] || echo "$m" | grep -q "^# TYPE hovernet_bytes_sent_total counter$" || { echo "FAIL: metrics format"; fail=1; }
kill %1 2>/dev/null; wait %1 2>/dev/null
# A crash leaves a report.
mkdir -p "$dir/crash"
( "$server" --track ClassicH --players 2 --port 0 --crash-dir "$dir/crash" --crash-test > "$dir/crash.txt" 2>&1 ) 2>/dev/null || true
ls "$dir"/crash/crash-*.txt > /dev/null 2>&1 || { echo "FAIL: no crash report"; fail=1; }
grep -qE "signal 11|exception code" "$dir"/crash/crash-*.txt 2>/dev/null || { echo "FAIL: the crash report lacks the signal"; fail=1; }
[ $fail -eq 0 ] && echo "observability: /health and /metrics report what happened; a crash leaves a report"
exit $fail
