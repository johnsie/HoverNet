#!/usr/bin/env bash
# A lobby server with several races at once over real TCP: pairs of scripted players host and join races on different
# tracks; each player rebuilds its own race from the Tick stream and checks every hash. All races must complete.
#   scripts/test-lobby-tcp.sh build/HoverNetAuthServer build/HoverNetAuthBot [ticks]
set -uo pipefail
server="${1:?usage: $0 <server> <bot> [ticks]}"; bot="${2:?}"; ticks="${3:-400}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/replays"
"$server" --lobby --port 0 --limit-ticks "$ticks" --start-delay-ms 500 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
pids=()
run() { local name="$1"; shift; "$bot" --port "$port" --name "$name" "$@" > "$dir/$name.txt" 2>&1 & pids+=($!); }
# Race 1: ClassicH, 3 players. Race 2: The River, 2 players. Race 3: Switchback, 4 players.
run a1 --host-race ClassicH:3; sleep 0.5
run a2 --join-race ClassicH --track ClassicH; run a3 --join-race ClassicH --track ClassicH
run b1 --host-race "The River:2" --track "The River"; sleep 0.5; run b2 --join-race "The River" --track "The River"
run c1 --host-race Switchback:4 --track Switchback; sleep 0.5
run c2 --join-race Switchback --track Switchback; run c3 --join-race Switchback --track Switchback; run c4 --join-race Switchback --track Switchback
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
for n in a1 a2 a3 b1 b2 c1 c2 c3 c4; do tail -1 "$dir/$n.txt" | sed "s/^/  /"; grep -q "no divergence" "$dir/$n.txt" || { echo "FAIL: $n"; cat "$dir/$n.txt"; fail=1; }; done
saved=$(ls "$dir"/replays/*.hnr 2>/dev/null | wc -l)
[ "$saved" -eq 3 ] || { echo "FAIL: expected 3 replays, saw $saved"; fail=1; }
[ $fail -eq 0 ] && echo "lobby-tcp: three races (9 players) ran side by side over TCP, every tick verified, three replays saved"
exit $fail
