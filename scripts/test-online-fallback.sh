#!/usr/bin/env bash
# ONLINE MULTIPLAYER with no HoverNet server reachable (a closed port stands in for it): the client must not hang or ask for an address; it says
# so and opens the classic lobby. A bounded run ends by itself.
#   scripts/test-online-fallback.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
start=$(date +%s)
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:1" timeout 60 "$client" --frames 30 > "$dir/out.txt" 2>&1
elapsed=$(( $(date +%s) - start ))
grep -q "The HoverNet server is not available: using the classic lobby" "$dir/out.txt" || { echo "FAIL: no fallback to the classic lobby"; tail -5 "$dir/out.txt"; exit 1; }
[ "$elapsed" -lt 40 ] || { echo "FAIL: it took ${elapsed}s (a missing server must not hang the menu)"; exit 1; }
echo "online fallback: with no HoverNet server the client moved on to the classic lobby in ${elapsed}s, asking for nothing"
