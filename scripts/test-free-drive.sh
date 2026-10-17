#!/usr/bin/env bash
# After the race the craft is not left stuck where the server's last state put it: the client keeps driving it from the player's own input.
# A one-player race ends at its tick limit; the client (scripted input, headless window) must then move the craft on its own.
#   scripts/test-free-drive.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --track ClassicH --players 1 --port 0 --once --limit-ticks 400 --start-delay-ms 500 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_FREEDRIVE=1 timeout 120 "$client" --auth-connect "127.0.0.1:$port" --auth-script --frames 100000 > "$dir/client.txt" 2>&1
at=$(sed -n 's/.*position at the result (\(-\?[0-9]*\),\(-\?[0-9]*\)).*/\1 \2/p' "$dir/client.txt" | head -1)
after=$(sed -n 's/.*position after the result (\(-\?[0-9]*\),\(-\?[0-9]*\)); free-driven ticks \([0-9]*\).*/\1 \2 \3/p' "$dir/client.txt" | head -1)
[ -n "$at" ] && [ -n "$after" ] || { echo "FAIL: the race did not end into free driving"; tail -4 "$dir/client.txt"; exit 1; }
read -r x0 y0 <<< "$at"; read -r x1 y1 ticks <<< "$after"
moved=$(python3 -c "import math; print(int(math.hypot($x1-$x0, $y1-$y0)))")
[ "$moved" -gt 2000 ] || { echo "FAIL: the craft did not move after the result (moved ${moved} mm in ${ticks} ticks)"; exit 1; }
echo "free drive: after the result the craft was driven on by the client (${moved} mm in ${ticks} ticks)"
