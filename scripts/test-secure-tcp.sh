#!/usr/bin/env bash
# The secure session over real TCP: a server that requires encryption serves players who pin its public key; a player with no key,
# and one with the wrong key, are refused; the game client plays a whole race over the secure session. Run from the repository root.
#   scripts/test-secure-tcp.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client>}"; bot="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
fail=0
"$server" --track ClassicH --players 3 --port 0 --once --limit-ticks 500 --start-delay-ms 1500 --identity-file "$dir/identity.key" --require-encryption > "$dir/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
key=$(sed -n 's/^server key \([0-9a-f]*\)$/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ ${#key} -eq 64 ] || { echo "FAIL: the server did not start with a key"; cat "$dir/server.txt"; exit 1; }
stat -c %a "$dir/identity.key" 2>/dev/null | grep -q '^600$' || echo "(note: the identity file is not owner-only on this platform)"
# Refused: no key (plain connection to a server that requires encryption), and the wrong key.
"$bot" --port "$port" --name plain > "$dir/plain.txt" 2>&1 && { echo "FAIL: a plain connection was served by a server that requires encryption"; fail=1; }
wrong=$(printf '%064d' 7)
"$bot" --port "$port" --name wrong --server-key "$wrong" > "$dir/wrong.txt" 2>&1 && { echo "FAIL: a client with the wrong server key was served"; fail=1; }
# Served: two bots with the right key and the game client (which also checks every tick).
pids=()
for i in 1 2; do "$bot" --port "$port" --name "s$i" --server-key "$key" > "$dir/s$i.txt" 2>&1 & pids+=($!); done
"$client" --auth-connect "127.0.0.1:$port" --server-key "$key" --auth-verify --auth-script --frames 100000 > "$dir/client.txt" 2>&1 || { echo "FAIL: the game client over the secure session"; tail -3 "$dir/client.txt"; fail=1; }
for p in "${pids[@]}"; do wait "$p" || fail=1; done
wait "$spid" 2>/dev/null
for i in 1 2; do grep -q "no divergence" "$dir/s$i.txt" || { echo "FAIL: secure bot $i"; cat "$dir/s$i.txt"; fail=1; }; done
grep "auth client" "$dir/client.txt"
[ $fail -eq 0 ] && echo "secure-tcp: players who pin the server key race over an encrypted, authenticated session; plain and wrong-key clients are refused"
exit $fail
