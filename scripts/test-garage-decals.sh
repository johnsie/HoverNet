#!/usr/bin/env bash
# The Garage's decal buttons, with a real pointer click (SDL mouse events through ImGui, the button found where it is drawn; test hook
# HOVERNET_TEST_GARAGE_CLICK_DECAL=n): each decal open from the start (Star 0, Flag 3, Arrow 8) puts paint on the craft; a locked decal (Trophy 1) does
# nothing at level 1 but works once unlocked (HOVERNET_GARAGE_ALL=1). Clicking one decal and then another replaces the first instead of piling
# them up (the paint equals what the second decal alone makes), and a decal clicked twice stays one decal.
#   scripts/test-garage-decals.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOVERNET_OPEN_GARAGE=1 HOVERNET_NO_FADE=1
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
cells() {   # $1 = decal, rest = extra environment; prints the number of paint cells after the click
  env "${@:2}" HOVERNET_TEST_GARAGE_CLICK_DECAL="$1" timeout -s KILL 20 "$client" --play --frames 40 2>&1 | grep -m1 "^GARAGETEST" | sed -n 's/.*after the clicks: \([0-9]*\);.*/\1/p'
}
for d in 0 3 8; do
  n=$(cells "$d" A=1); if [ "${n:-0}" -gt 0 ]; then echo "ok   decal $d (open from the start): a click paints $n cells"; else echo "FAIL: decal $d painted nothing (got '${n:-none}')"; fail=1; fi
done
n=$(cells 1 A=1); if [ "${n:-x}" = "0" ]; then echo "ok   a locked decal does nothing"; else echo "FAIL: the locked decal gave '${n:-none}'"; fail=1; fi
n=$(cells 1 HOVERNET_GARAGE_ALL=1); if [ "${n:-0}" -gt 0 ]; then echo "ok   the same decal paints $n cells once unlocked"; else echo "FAIL: the unlocked decal painted nothing"; fail=1; fi
one=$(cells 3 A=1); two=$(cells 0,3 A=1); twice=$(cells 3,3 A=1)
if [ -n "$one" ] && [ "$one" = "$two" ]; then echo "ok   Star then Flag: only the Flag is left ($two cells, the same as the Flag alone)"; else echo "FAIL: Star then Flag gave '${two:-none}' cells, the Flag alone '${one:-none}'"; fail=1; fi
if [ -n "$one" ] && [ "$one" = "$twice" ]; then echo "ok   the same decal twice stays one decal"; else echo "FAIL: Flag twice gave '${twice:-none}', once '${one:-none}'"; fail=1; fi
[ $fail -eq 0 ] && echo "garage decals: real clicks stamp the open decals"
exit $fail
