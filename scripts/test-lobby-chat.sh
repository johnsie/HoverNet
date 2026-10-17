#!/usr/bin/env bash
# Chat in the online lobby, before any race is joined: the real client (headless window) connects to a lobby server by itself, a message is typed
# with real key events into the chat box and sent with Enter, and it must come back from the server into the chat panel.
#   scripts/test-lobby-chat.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --lobby --port 0 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_AUTH_LOBBYCHAT=1 HOVERNET_TEST_AUTH_BROWSE=1 \
  timeout 100 "$client" --frames 100000 > "$dir/client.txt" 2>&1
grep -q "LOBBYCHATTEST ok" "$dir/client.txt" || { echo "FAIL: lobby chat"; grep LOBBYCHATTEST "$dir/client.txt"; tail -3 "$dir/client.txt"; exit 1; }
echo "lobby chat: a message typed in the lobby was sent with Enter and came back from the server"
