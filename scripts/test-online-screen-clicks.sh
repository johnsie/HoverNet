#!/usr/bin/env bash
# Real mouse events on the online lobby screen (headless window): it connects to the server by itself (no address is typed), then the test
# pushes SDL mouse motion, press and release at the centre of HOST A RACE and then at the dialog's create button, and a race must start
# hosting. This is the check that the buttons answer a click at the place they are drawn, not only that the logic behind them works.
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
grep -q "CLICKTEST connected by itself" "$dir/client.txt" || { echo "FAIL: the screen did not connect by itself"; grep CLICKTEST "$dir/client.txt"; exit 1; }
grep -q "CLICKTEST host dialog opened by a click" "$dir/client.txt" || { echo "FAIL: clicking HOST A RACE did not open the dialog"; grep CLICKTEST "$dir/client.txt"; exit 1; }
grep -q "CLICKTEST ok" "$dir/client.txt" || { echo "FAIL: clicking Host did not start hosting"; grep CLICKTEST "$dir/client.txt"; exit 1; }
# The laps chosen for a hosted race are remembered: a second session starts the dialog with the same number (it used to go back to 1).
grep -q "CLICKTEST host dialog laps 1$" "$dir/client.txt" || { echo "FAIL: a new player's dialog did not start at 1 lap"; grep CLICKTEST "$dir/client.txt"; exit 1; }
HOVERNET_TEST_HOST_LAPS=4 HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_CLICKS="127.0.0.1:$port" timeout 120 "$client" --frames 100000 > "$dir/client2.txt" 2>&1
grep -q "CLICKTEST ok" "$dir/client2.txt" || { echo "FAIL: the second run did not host"; grep CLICKTEST "$dir/client2.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_CLICKS="127.0.0.1:$port" timeout 120 "$client" --frames 100000 > "$dir/client3.txt" 2>&1
grep -q "CLICKTEST host dialog laps 4$" "$dir/client3.txt" || { echo "FAIL: after hosting with 4 laps the next dialog did not start at 4"; grep CLICKTEST "$dir/client3.txt"; exit 1; }
echo "ok   hosting with 4 laps is remembered: the next session starts at 4"
echo "online lobby: it connected by itself, a real click on HOST A RACE opened the dialog and a real click on create started a race"
