#!/usr/bin/env bash
# The real client records a finish in the player's personal bests: the first finish is stored, a slower one leaves it alone, a faster one replaces it,
# and the results screen opens each time. A test hook makes the client finish with a chosen time (HOVERNET_TEST_FINISH_TIME, milliseconds).
#   scripts/test-personal-bests.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
finish() { HOVERNET_TEST_FINISH=1 HOVERNET_TEST_FINISH_TIME="$1" timeout 120 "$client" --frames 300 > "$dir/out.txt" 2>&1; grep -q "FINISHTEST results screen opened" "$dir/out.txt" || { echo "FAIL: the results screen did not open"; tail -5 "$dir/out.txt"; exit 1; }; }
file="$HOME/.config/hovernet/personal_bests"
expect() { # expected race time, expected lap time
  [ -f "$file" ] || { echo "FAIL: no personal bests file after $3"; exit 1; }
  grep -qP "^R\t[^\t]+\t[0-9]+\t$1\$" "$file" || { echo "FAIL: after $3 the best finish should be $1:"; cat "$file"; exit 1; }
  grep -qP "^L\t[^\t]+\t$2\$" "$file" || { echo "FAIL: after $3 the best lap should be $2:"; cat "$file"; exit 1; }
}
finish 90000; expect 90000 30000 "the first finish"
finish 95000; expect 90000 30000 "a slower finish"
finish 80001; expect 80001 26667 "a faster finish"
echo "personal bests: a first finish is recorded, a slower one is not, a faster one replaces it"
