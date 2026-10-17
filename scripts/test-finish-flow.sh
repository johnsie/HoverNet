#!/usr/bin/env bash
# After a finish the race goes on and the results screen opens only when the player asks for it (Enter). A test hook makes the real
# client act as if the craft crossed the line on frame 30 and pushes a real Enter key press on frame 80 (headless window).
#   scripts/test-finish-flow.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
HOVERNET_TEST_FINISH=1 timeout 120 "$client" --frames 300 > "$dir/out.txt" 2>&1
fin=$(grep -n "FINISHTEST finished" "$dir/out.txt" | head -1 | cut -d: -f1)
still=$(grep -n "FINISHTEST still driving" "$dir/out.txt" | head -1 | cut -d: -f1)
opened=$(grep -n "FINISHTEST results screen opened" "$dir/out.txt" | head -1 | cut -d: -f1)
[ -n "$fin" ]    || { echo "FAIL: the finish was not seen"; tail -5 "$dir/out.txt"; exit 1; }
[ -n "$still" ]  || { echo "FAIL: the race did not go on after the finish (or the screen opened by itself)"; tail -5 "$dir/out.txt"; exit 1; }
[ -n "$opened" ] || { echo "FAIL: Enter did not open the results screen"; tail -5 "$dir/out.txt"; exit 1; }
[ "$fin" -lt "$still" ] && [ "$still" -lt "$opened" ] || { echo "FAIL: wrong order (finish $fin, driving $still, screen $opened)"; exit 1; }
echo "finish flow: the race went on after the finish, and Enter opened the results screen"
