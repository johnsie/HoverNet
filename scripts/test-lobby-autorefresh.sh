#!/usr/bin/env bash
# The race list refreshes by itself: the real client sits in the lobby (nothing is pressed) and a race that a bot hosts afterwards must show up.
#   scripts/test-lobby-autorefresh.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client>}"; bot="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --lobby --port 0 --limit-ticks 100000 --start-delay-ms 100000 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_AUTH_BROWSE=1 HOVERNET_TEST_AUTH_AUTOREFRESH=1 \
  timeout 100 "$client" --frames 100000 > "$dir/client.txt" 2>&1 &
cpid=$!
for _ in $(seq 1 200); do grep -q "AUTOREFRESHTEST waiting" "$dir/client.txt" 2>/dev/null && break; sleep 0.1; done
grep -q "AUTOREFRESHTEST waiting" "$dir/client.txt" || { echo "FAIL: the client never reached the lobby"; tail -3 "$dir/client.txt"; exit 1; }
"$bot" --port "$port" --name Ada --host-race ClassicH:4:3 > /dev/null 2>&1 &     # hosts after the client is already looking
wait "$cpid"
grep -q "AUTOREFRESHTEST ok" "$dir/client.txt" || { echo "FAIL: the new race did not appear by itself"; tail -3 "$dir/client.txt"; exit 1; }
echo "lobby auto refresh: a race hosted after the client arrived appeared in its list with nothing pressed"
