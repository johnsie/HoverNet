#!/usr/bin/env bash
# End to end over real TCP on the loopback address: a dedicated authoritative server and N scripted players. Every player
# rebuilds the race from the server's Tick messages and checks the state hash of every tick; all must finish with the Result.
#   scripts/test-authserver.sh build/HoverNetAuthServer build/HoverNetAuthBot [players] [ticks] [track]
set -uo pipefail
server="${1:?usage: $0 <server> <bot> [players] [ticks] [track]}"; bot="${2:?}"
players="${3:-8}"; ticks="${4:-500}"; track="${5:-ClassicH}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$server" --track "$track" --players "$players" --port 0 --once --limit-ticks "$ticks" --start-delay-ms 500 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
pids=()
for i in $(seq 1 "$players"); do
  "$bot" --port "$port" --name "bot$i" --track "$track" > "$dir/bot$i.txt" 2>&1 &
  pids+=($!)
done
fail=0
for i in $(seq 1 "$players"); do
  wait "${pids[$((i-1))]}" || { echo "FAIL: bot$i"; cat "$dir/bot$i.txt"; fail=1; }
done
wait "$spid" || { echo "FAIL: the server exited with an error"; fail=1; }
replay=$(ls "$dir"/replays/*.hnr 2>/dev/null | head -1)
if [ -z "$replay" ]; then echo "FAIL: the server saved no replay"; fail=1
elif [ -n "${HOVERNET_REPLAY_TOOL:-}" ]; then
  "$HOVERNET_REPLAY_TOOL" verify "$replay" > "$dir/verify.txt" 2>&1 || { echo "FAIL: the server's replay does not verify"; cat "$dir/verify.txt"; fail=1; }
fi
cat "$dir/bot1.txt" | tail -1
grep -E "race over|player" "$dir/server.txt" | head -$((players + 1))
[ $fail -eq 0 ] && echo "authserver: $players players over TCP, every tick verified"
exit $fail
