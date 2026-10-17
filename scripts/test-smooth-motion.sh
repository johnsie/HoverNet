#!/usr/bin/env bash
# On a network with jitter the craft must move smoothly on screen: the picture's pace (ticks shown against ticks of real time elapsed, per frame)
# must stay close to 1.0. Before the fix, 60 +- 40 ms of jitter made it swing between 0.44 and 1.88 (up to 3.4): the picture jumped.
#   scripts/test-smooth-motion.sh build/HoverNetAuthServer build/HoverNetNetProxy build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <proxy> <client>}"; proxy="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"
"$server" --track ClassicH --players 1 --port 0 --once --limit-ticks 1500 --start-delay-ms 800 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
"$proxy" --listen-port 0 --target-port "$port" --delay-ms 60 --jitter-ms 40 --seed 5 > "$dir/proxy.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/proxy.txt" 2>/dev/null && break; sleep 0.1; done
cport=$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\),.*/\1/p' "$dir/proxy.txt" | head -1)
[ -n "$port" ] && [ -n "$cport" ] || { echo "FAIL: the server or the proxy did not start"; exit 1; }
HOVERNET_TEST_JITTER=1 timeout 120 "$client" --auth-connect "127.0.0.1:$cport" --auth-script --frames 100000 > "$dir/client.txt" 2>&1
line=$(grep JITTERTEST "$dir/client.txt" | head -1)
[ -n "$line" ] || { echo "FAIL: no measurement"; tail -3 "$dir/client.txt"; exit 1; }
pace=$(grep -o 'pace.*craft' <<< "$line")
p5=$(sed -n 's/.*p5=\([0-9.]*\).*/\1/p' <<< "$pace"); p95=$(sed -n 's/.*p95=\([0-9.]*\) max.*/\1/p' <<< "$pace")
python3 -c "import sys; p5=float('$p5'); p95=float('$p95'); sys.exit(0 if (p5 >= 0.65 and p95 <= 1.65) else 1)" || { echo "FAIL: the picture is not smooth (p5=$p5 p95=$p95; want >= 0.65 and <= 1.65)"; echo "$line"; exit 1; }
echo "smooth motion: with 60 +- 40 ms of jitter the picture's pace stayed between ${p5} and ${p95} of real time"
