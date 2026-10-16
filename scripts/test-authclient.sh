#!/usr/bin/env bash
# The game client itself on an authoritative server over TCP: a server, N-1 scripted bots and one real game client (headless
# window, scripted input). The client checks, every frame, that restoring the server's confirmed snapshot into its rendered
# session reproduces the confirmed state exactly, and that every Tick's state hash matches; it must also receive the Result.
#   scripts/test-authclient.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player [players] [ticks] [track]
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client> [players] [ticks] [track]}"; bot="${2:?}"; client="${3:?}"
players="${4:-4}"; ticks="${5:-500}"; track="${6:-ClassicH}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
"$server" --track "$track" --players "$players" --port 0 --once --limit-ticks "$ticks" --start-delay-ms 1500 > "$dir/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
pids=()
for i in $(seq 2 "$players"); do
  "$bot" --port "$port" --name "bot$i" --track "$track" > "$dir/bot$i.txt" 2>&1 &
  pids+=($!)
done
"$client" --auth-connect "127.0.0.1:$port" --auth-verify --auth-script ${AUTH_DROP:+--auth-drop "$AUTH_DROP"} --frames 100000 > "$dir/client.txt" 2>&1
cstatus=$?
fail=0
[ $cstatus -eq 0 ] || { echo "FAIL: the game client exited with status $cstatus"; tail -5 "$dir/client.txt"; fail=1; }
for p in "${pids[@]}"; do wait "$p" || { echo "FAIL: a bot"; fail=1; }; done
wait "$spid" || { echo "FAIL: the server"; fail=1; }
grep "auth client" "$dir/client.txt"
[ $fail -eq 0 ] && echo "authclient: the game client followed an authoritative $players-player race exactly"
exit $fail
