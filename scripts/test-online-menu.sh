#!/usr/bin/env bash
# The "HoverNet Servers" screen of the real game client (headless window): it opens from the main menu, connects to a lobby server,
# hosts a one-player time trial, and the client then follows that race exactly (every tick's state hash, every snapshot restore).
# The screen's own buttons are drawn by ImGui and cannot be clicked here; the test hook connects and hosts on the screen's first
# frames (HOVERNET_TEST_AUTH_SERVER), and the lobby logic itself is tested against a real server by HoverNetOnlineLobby.
#   scripts/test-online-menu.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"     # the screen saves the server address in the player's config: keep it out of the real one
"$server" --lobby --port 0 --limit-ticks 500 --start-delay-ms 500 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" \
  "$client" --auth-verify --auth-script --frames 100000 > "$dir/client.txt" 2>&1
status=$?
[ $status -eq 0 ] || { echo "FAIL: the client exited with status $status"; tail -5 "$dir/client.txt"; exit 1; }
grep -q "auth client: [1-9][0-9]* ticks verified, 0 tick failures, [0-9]* restore checks, 0 restore failures" "$dir/client.txt" || { echo "FAIL: the race was not followed exactly"; tail -5 "$dir/client.txt"; exit 1; }
grep "auth client" "$dir/client.txt"
echo "online menu: the servers screen connected, hosted a time trial and the client followed it exactly"
