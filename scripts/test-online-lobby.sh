#!/usr/bin/env bash
# The game client's lobby logic against a real server. Run from the repository root.
#   scripts/test-online-lobby.sh build/HoverNetAuthServer build/HoverNetOnlineLobbyTest
set -uo pipefail
server="${1:?usage: $0 <server> <test>}"; test="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
"$server" --lobby --port 0 --limit-ticks 3000 --start-delay-ms 300 --spectator-delay-ms 200 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
"$test" "$port"
