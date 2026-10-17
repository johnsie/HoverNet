#!/usr/bin/env bash
# Chat on a HoverNet 3.0 server, after the player has finished: the real client (headless window) joins a time trial through the
# servers screen, "finishes" (test hook), and then types a message and presses Enter; the server relays it and the client must show it
# when it comes back.
#   scripts/test-chat-after-finish.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --lobby --port 0 --limit-ticks 6000 --start-delay-ms 300 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_FINISH=1 HOVERNET_TEST_CHAT=1 \
  timeout 200 "$client" --auth-script --frames 260 > "$dir/client.txt" 2>&1
grep -q "FINISHTEST finished" "$dir/client.txt" || { echo "FAIL: the finish was not simulated"; tail -5 "$dir/client.txt"; exit 1; }
grep -q "CHATTEST received: Player: hello from the finish line" "$dir/client.txt" || { echo "FAIL: the message typed after the finish did not come back from the server"; grep -E "CHATTEST|FINISHTEST" "$dir/client.txt"; exit 1; }
echo "chat after finishing: the message typed after the finish was relayed by the server and shown"
