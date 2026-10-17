#!/usr/bin/env bash
# Real-time soak of the authoritative server: one lobby server, and round after round of eight-player races played by scripted bots
# over real TCP, for a given number of minutes. Every round every bot must see its race through with no divergence; the server's memory
# (resident set), open connections, races running and replay files are sampled after each round. The summary says whether memory grew.
# This is the unattended run that backs the Stage 2 exit gate's soak; its length is whatever you ask for (the gate says days).
#   scripts/soak-authserver.sh build 60 soak.log          (60 minutes)
set -uo pipefail
build="${1:?usage: $0 <build-dir> <minutes> [log]}"; minutes="${2:?}"; log="${3:-soak-authserver.log}"
[[ "$minutes" =~ ^[1-9][0-9]*$ ]] || { echo "minutes must be a positive integer" >&2; exit 64; }
server="$build/HoverNetAuthServer"; bot="$build/HoverNetAuthBot"
[ -x "$server" ] && [ -x "$bot" ] || { echo "HoverNetAuthServer and HoverNetAuthBot are needed in $build" >&2; exit 66; }
work="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/replays"
ticks="${SOAK_TICKS:-3000}"           # per race: 50 seconds of racing
"$server" --lobby --port 0 --status-port 0 --max-connections 256 --limit-ticks "$ticks" --start-delay-ms 1000 --replay-dir "$work/replays" > "$work/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$work/server.txt" 2>/dev/null && grep -q "status" "$work/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$work/server.txt" | head -1)
status=$(grep -o "127.0.0.1:[0-9]*" "$work/server.txt" | grep -v ":$port\$" | head -1 | cut -d: -f2)
[ -n "$port" ] || { echo "the server did not start"; cat "$work/server.txt"; exit 1; }
rss() { awk '/VmRSS/ {print $2}' "/proc/$spid/status" 2>/dev/null; }
{
  echo "soak start $(date -u +%FT%TZ): $minutes minutes, 8-player ClassicH races of $ticks ticks, server pid $spid, port $port"
  uname -srm; nproc | sed 's/^/cpus: /'
} | tee "$log"
end=$(( $(date +%s) + minutes * 60 )); round=0; failures=0; first_rss=""
while [ "$(date +%s)" -lt "$end" ]; do
  round=$((round + 1))
  pids=()
  "$bot" --port "$port" --name "host$round" --host-race ClassicH:8:2 > "$work/b0.txt" 2>&1 & pids+=($!)
  sleep 1.5
  for i in $(seq 1 7); do "$bot" --port "$port" --name "p$round-$i" --join-race ClassicH --track ClassicH > "$work/b$i.txt" 2>&1 & pids+=($!); done
  bad=0
  for p in "${pids[@]}"; do wait "$p" || bad=$((bad + 1)); done
  for i in 0 1 2 3 4 5 6 7; do grep -q "no divergence" "$work/b$i.txt" || bad=$((bad + 1)); done
  [ "$bad" -eq 0 ] || { failures=$((failures + 1)); echo "round $round: $bad bot problem(s)"; tail -2 "$work/b0.txt"; } | tee -a "$log"
  kill -0 "$spid" 2>/dev/null || { echo "round $round: THE SERVER DIED" | tee -a "$log"; failures=$((failures + 1)); break; }
  r=$(rss); [ -n "$first_rss" ] || first_rss=$r
  conns=$(curl -s -m 3 "http://127.0.0.1:$status/metrics" | awk '/^hovernet_connections_open / {print $2}')
  races=$(curl -s -m 3 "http://127.0.0.1:$status/metrics" | awk '/^hovernet_races_running / {print $2}')
  echo "round $round $(date -u +%T): rss ${r} kB, connections ${conns:-?}, races running ${races:-?}, replays $(ls "$work/replays" 2>/dev/null | grep -c '\.hnr$'), bad bots $bad" | tee -a "$log"
  sleep 2
done
last_rss=$(rss)
health=$(curl -s -m 3 "http://127.0.0.1:$status/health")
{
  echo "soak end $(date -u +%FT%TZ): $round rounds, $failures failed round(s), server rss first round ${first_rss:-?} kB, last ${last_rss:-?} kB, health: ${health:-no answer}"
} | tee -a "$log"
[ "$failures" -eq 0 ] && [ "$health" = "ok" ]
