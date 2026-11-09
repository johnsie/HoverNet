#!/usr/bin/env bash
# The RmlUi main menu prototype (built with -DHOVERNET_RMLUI=ON, shown with HOVERNET_RML_MENU=1): it opens, takes the keyboard and a real mouse click, and
# each door it offers is the one it reports. Scripted input through the real event path (test hook HOVERNET_TEST_RML).
#   scripts/test-rml-menu.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOVERNET_RML_MENU=1 HOVERNET_NO_FADE=1
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
chose() {   # $1 = the script; prints the id the menu reported
  HOVERNET_TEST_RML="$1" timeout -s KILL 25 "$client" --play --frames 150 2>&1 | grep -a -m1 "^RMLTEST chose" | sed 's/^RMLTEST chose //'
}
check() { local got; got=$(chose "$1"); if [ "$got" = "$2" ]; then echo "ok   $3: $2"; else echo "FAIL: $3: wanted '$2', got '${got:-nothing}'"; fail=1; fi; }
check "enter" online "Enter on the first button (RACE ONLINE has the focus when it opens)"
check "down,enter" offline "Down, Enter"
check "down,down,enter" leaderboards "Down, Down, Enter"
check "up,enter" settings "Up from the first button wraps to the last"
for id in online offline leaderboards garage tutorial settings; do check "click:$id" "$id" "a real mouse click on $id"; done
[ $fail -eq 0 ] && echo "RmlUi main menu: keyboard and mouse reach every door"
exit $fail
