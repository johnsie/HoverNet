#!/usr/bin/env bash
# The host starts the race: the real client hosts a two-seat race, a bot joins, the race must NOT start by itself although every seat is taken,
# and must start when a real mouse click lands on the Start Race button. The client then follows the race exactly.
#   scripts/test-start-race.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client>}"; bot="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --lobby --port 0 --limit-ticks 500 --start-delay-ms 500 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_AUTH_PLAYERS=2 HOVERNET_TEST_AUTH_STARTCLICK=1 \
  timeout 150 "$client" --auth-verify --auth-script --frames 100000 > "$dir/client.txt" 2>&1 &
cpid=$!
for _ in $(seq 1 200); do grep -q "STARTTEST waiting for a second player" "$dir/client.txt" 2>/dev/null && break; sleep 0.1; done
grep -q "STARTTEST waiting for a second player" "$dir/client.txt" || { echo "FAIL: the client never hosted"; tail -3 "$dir/client.txt"; exit 1; }
"$bot" --port "$port" --name Joiner --join-race ClassicH --track ClassicH > "$dir/bot.txt" 2>&1 &
wait "$cpid"; status=$?
[ $status -eq 0 ] || { echo "FAIL: the client exited with status $status"; tail -4 "$dir/client.txt"; exit 1; }
grep -q "STARTTEST two players are in and the race has not started by itself" "$dir/client.txt" || { echo "FAIL: the race started by itself or the joiner never arrived"; tail -4 "$dir/client.txt"; exit 1; }
grep -q "STARTTEST Start Race clicked" "$dir/client.txt" || { echo "FAIL: the Start Race button was never found"; exit 1; }
grep -q "auth client: [1-9][0-9]* ticks verified, 0 tick failures, [0-9]* restore checks, 0 restore failures" "$dir/client.txt" || { echo "FAIL: the race did not run and verify after Start Race"; tail -4 "$dir/client.txt"; exit 1; }
grep -q "no divergence" "$dir/bot.txt" || { echo "FAIL: the joiner did not see the race through"; tail -3 "$dir/bot.txt"; exit 1; }
echo "start race: with every seat taken the race waited; a real click on Start Race started it for both players"
