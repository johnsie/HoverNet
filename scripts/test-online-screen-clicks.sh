#!/usr/bin/env bash
# Real mouse events on the HoverNet Servers screen (headless window): the test pushes SDL mouse motion, press and release at the
# on-screen centre of the Connect button and then the Host button, and the screen must connect to a lobby server and start hosting.
# This is the check that the buttons answer a click at the place they are drawn, not only that the logic behind them works.
#   scripts/test-online-screen-clicks.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --lobby --port 0 --limit-ticks 500 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_CLICKS="127.0.0.1:$port" timeout 120 "$client" --frames 100000 > "$dir/client.txt" 2>&1
grep -q "CLICKTEST connect clicked" "$dir/client.txt" || { echo "FAIL: clicking Connect did not connect"; grep CLICKTEST "$dir/client.txt"; exit 1; }
grep -q "CLICKTEST ok" "$dir/client.txt" || { echo "FAIL: clicking Host did not start hosting"; grep CLICKTEST "$dir/client.txt"; exit 1; }
echo "online screen: a real click on Connect connected and a real click on Host started a race"
