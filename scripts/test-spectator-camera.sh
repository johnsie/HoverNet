#!/usr/bin/env bash
# A spectator can move the camera between the craft: Left and Right keys (real key events pushed by a test hook) in the game client
# watching a race on a server (headless window). The race must still be followed exactly (every tick's state hash).
# A replay is watched the same way with the [ and ] keys (Left and Right seek there).
#   scripts/test-spectator-camera.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player [build/HoverNetReplay]
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client>}"; bot="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --track ClassicH --players 3 --port 0 --once --limit-ticks 900 --start-delay-ms 300 --spectator-delay-ms 400 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
for i in 1 2 3; do "$bot" --port "$port" --name "bot$i" --track ClassicH > "$dir/bot$i.txt" 2>&1 & done
sleep 3
HOVERNET_TEST_WATCH=1 timeout 200 "$client" --auth-connect "127.0.0.1:$port" --spectate --auth-verify --auth-script --frames 100000 > "$dir/client.txt" 2>&1
status=$?
[ $status -eq 0 ] || { echo "FAIL: the client exited with status $status"; tail -5 "$dir/client.txt"; exit 1; }
grep -q "WATCHTEST watching player 2 of 3" "$dir/client.txt" || { echo "FAIL: Right did not move to player 2"; grep -E "WATCHTEST" "$dir/client.txt"; exit 1; }
grep -q "WATCHTEST watching player 1 of 3" "$dir/client.txt" || { echo "FAIL: Left did not move back to player 1"; grep -E "WATCHTEST" "$dir/client.txt"; exit 1; }
grep -q "WATCHTEST watching player 3 of 3" "$dir/client.txt" || { echo "FAIL: Left from player 1 did not wrap to player 3"; grep -E "WATCHTEST" "$dir/client.txt"; exit 1; }
grep -q "auth client: [1-9][0-9]* ticks verified, 0 tick failures" "$dir/client.txt" || { echo "FAIL: the race was not followed exactly while the camera moved"; tail -3 "$dir/client.txt"; exit 1; }
replay="${4:-}"
if [ -n "$replay" ]; then
  "$replay" record ClassicH 600 "$dir/camera.hnr" --players=3 > /dev/null 2>&1 || { echo "FAIL: could not record a replay"; exit 1; }
  HOVERNET_TEST_WATCH=1 timeout 200 "$client" --replay "$dir/camera.hnr" --frames 120 > "$dir/replay.txt" 2>&1
  grep -q "WATCHTEST watching player 2 of 3" "$dir/replay.txt" && grep -q "WATCHTEST watching player 3 of 3" "$dir/replay.txt" || { echo "FAIL: [ and ] did not switch craft in a replay"; grep WATCHTEST "$dir/replay.txt"; tail -3 "$dir/replay.txt"; exit 1; }
fi
echo "spectator camera: Right, Left and wrap-around moved between the craft, and the race was still followed exactly"
