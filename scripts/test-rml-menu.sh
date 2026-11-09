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
# The interface sounds: moving with the keys ticks, choosing clicks (the sounds are the game's own; the hook logs each one).
snd=$(HOVERNET_TEST_RML="down,down,enter" timeout -s KILL 25 "$client" --play --frames 150 2>&1 | grep -a "^RMLTEST sound" | sed 's/^RMLTEST sound //' | head -3 | tr '\n' ' ')
if [ "$snd" = "hover hover click " ]; then echo "ok   two keystrokes tick twice and Enter clicks: $snd"; else echo "FAIL: sounds were '$snd', wanted 'hover hover click'"; fail=1; fi
# The results screen after a race (the real client, an autoplay race that finishes): Down then Enter chooses the one button an offline race offers, MAIN MENU.
got=$(HOVERNET_TEST_FINISH=1 HOVERNET_TEST_RML="down,enter" timeout -s KILL 60 "$client" --play --track "The River" --autoplay --frames 100000 2>&1 | grep -a -m1 "^RMLTEST results chose" | sed 's/^RMLTEST results chose //')
if [ "$got" = "menu" ]; then echo "ok   the results screen opens after a race and its button works: menu"; else echo "FAIL: results screen: wanted 'menu', got '${got:-nothing}'"; fail=1; fi
[ $fail -eq 0 ] && echo "RmlUi main menu and results screen: keyboard and mouse reach every door"
exit $fail
