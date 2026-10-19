#!/usr/bin/env bash
# On a network with jitter the craft must move smoothly on screen: the picture's pace (ticks shown against ticks of real time elapsed, per frame)
# must stay close to 1.0, and the craft's on-screen speed must not change by more than 14% from one frame to the next (drawn between two ticks: 4-8%; whole ticks: 15-19%). Before the fix, 60 +- 40 ms of jitter made it swing between 0.44 and 1.88 (up to 3.4): the picture jumped.
# The pace is measured against the wall clock, so a loaded shared CI runner can stall a few frames and miss the bound by a hair (p5=0.62 against 0.65 on GitHub Actions while the median was normal). The check therefore runs up to 5 times and fails only if every run (up to five) misses: the bug it guards against (pace 0.44 to 1.88) fails every run.
#   scripts/test-smooth-motion.sh build/HoverNetAuthServer build/HoverNetNetProxy build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <proxy> <client>}"; proxy="${2:?}"; client="${3:?}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
attempt() {
  dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
  export HOME="$dir/home"; mkdir -p "$HOME"
  "$server" --track ClassicH --players 1 --port 0 --once --limit-ticks 2000 --start-delay-ms 800 > "$dir/server.txt" 2>&1 &
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
  port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
  "$proxy" --listen-port 0 --target-port "$port" --delay-ms 60 --jitter-ms 40 --seed 5 > "$dir/proxy.txt" 2>&1 &
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/proxy.txt" 2>/dev/null && break; sleep 0.1; done
  cport=$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\),.*/\1/p' "$dir/proxy.txt" | head -1)
  [ -n "$port" ] && [ -n "$cport" ] || { echo "FAIL: the server or the proxy did not start"; return 1; }
  HOVERNET_TEST_JITTER=1 timeout 120 "$client" --auth-connect "127.0.0.1:$cport" --auth-script --frames 100000 > "$dir/client.txt" 2>&1
  line=$(grep JITTERTEST "$dir/client.txt" | head -1)
  [ -n "$line" ] || { echo "FAIL: no measurement"; tail -3 "$dir/client.txt"; return 1; }
  pace=$(grep -o 'pace.*craft' <<< "$line")
  p5=$(sed -n 's/.*p5=\([0-9.]*\).*/\1/p' <<< "$pace"); p95=$(sed -n 's/.*p95=\([0-9.]*\) max.*/\1/p' <<< "$pace")
  python3 -c "import sys; p5=float('$p5'); p95=float('$p95'); sys.exit(0 if (p5 >= 0.65 and p95 <= 1.65) else 1)" || { echo "FAIL: the picture is not smooth (p5=$p5 p95=$p95; want >= 0.65 and <= 1.65)"; echo "$line"; return 1; }
  chg=$(grep -o 'speed changes by [0-9.]*' "$dir/client.txt" | head -1 | grep -o '[0-9.]*$')
  [ -n "$chg" ] || { echo "FAIL: no on-screen speed measurement"; return 1; }
  python3 -c "import sys; sys.exit(0 if float('$chg') <= 14.0 else 1)" || { echo "FAIL: the craft's on-screen speed changes by ${chg}% from frame to frame (want <= 14%; whole-tick drawing gives 15-19%)"; return 1; }
  echo "smooth motion: the craft's on-screen speed changed by ${chg}% between frames"
  echo "smooth motion: with 60 +- 40 ms of jitter the picture's pace stayed between ${p5} and ${p95} of real time"
}
for n in 1 2 3 4 5; do
  if (attempt); then exit 0; fi
  [ "$n" -lt 5 ] && echo "attempt $n missed the bound; trying again (a busy machine can cause this)"
done
exit 1
