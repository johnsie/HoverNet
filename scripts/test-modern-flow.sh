#!/usr/bin/env bash
# The modern menu flow (the default; the classic one is checked by scripts/test-rml-menu.sh): the Continue card on the main menu, the single race screen, Race again on the results,
# and the switch back to the classic menus. Scripted input through the real event path (test hook HOVERNET_TEST_RML); headless windows are drawn in software, so each run takes a while.
#   scripts/test-modern-flow.sh build/HoverNetGame2Player
set -uo pipefail
client="${1:?usage: $0 <client>}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOVERNET_RML_MENU=1 HOVERNET_NO_FADE=1
unset HOVERNET_MENU_STYLE
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
# 1. Enter on the main menu takes the Continue card (RACE) and the last local race (ClassicH, the default) loads.
out=$(HOVERNET_TEST_RML="enter" timeout -s KILL 60 "$client" --play --frames 100000 2>&1)
if grep -aq "^RMLTEST chose quick" <<< "$out" && grep -aq "ClassicH 3D view" <<< "$out"; then ok "the Continue card starts the last race with one key press"; else bad "Continue card: $(grep -a 'RMLTEST chose\|3D view' <<< "$out" | head -2 | tr '\n' '|')"; fi
# 2. Choose a track opens the one race screen; its RACE button starts the race.
out=$(HOVERNET_TEST_RML="down,down,enter;click:start" timeout -s KILL 90 "$client" --play --frames 100000 2>&1)
if grep -aq "^RMLTEST chose offline" <<< "$out" && grep -aq "3D view" <<< "$out"; then ok "CHOOSE A TRACK opens the race screen and its RACE button starts the race"; else bad "race screen: $(grep -a 'RMLTEST chose\|3D view' <<< "$out" | head -2 | tr '\n' '|')"; fi
# 3. The classic menus come back with HOVERNET_MENU_STYLE=classic (the setting does the same): no Continue card, so the first door is RACE ONLINE.
out=$(HOVERNET_MENU_STYLE=classic HOVERNET_TEST_RML="enter" timeout -s KILL 40 "$client" --play --frames 150 2>&1)
if grep -aq "^RMLTEST chose online" <<< "$out"; then ok "the classic style has no Continue card: Enter is RACE ONLINE again"; else bad "classic style: $(grep -a 'RMLTEST chose' <<< "$out" | head -1)"; fi
# 4. After an offline race the results screen offers RACE AGAIN first, and F2 (scripted as "enter") chooses it.
out=$(HOVERNET_TEST_FINISH=1 HOVERNET_TEST_RML="enter" timeout -s KILL 120 "$client" --play --track "The River" --autoplay --frames 100000 2>&1)
if grep -aq "^RMLTEST results chose again" <<< "$out"; then ok "the results screen offers RACE AGAIN and F2 chooses it"; else bad "results: $(grep -a 'RMLTEST results' <<< "$out" | head -2 | tr '\n' '|')"; fi
# 5. CHANGE TRACK opens the track browser; picking a card comes back to the race screen with that track, and RACE starts it.
out=$(HOVERNET_TEST_RML="down,down,enter;click:browse;click:card1;click:start" timeout -s KILL 170 "$client" --play --frames 100000 2>&1)
if grep -aq "^Steeplechase 3D view" <<< "$out"; then ok "CHANGE TRACK opens the browser, a card comes back to the race screen, and RACE starts that track"; else bad "track browser round trip: $(grep -a '3D view' <<< "$out" | head -1)"; fi
# 6. EDIT IN GARAGE opens the Garage; leaving it comes back to the race screen, and RACE starts.
out=$(HOVERNET_TEST_RML="down,down,enter;click:garage;esc;click:start" timeout -s KILL 190 "$client" --play --frames 100000 2>&1)
if grep -aq "^ClassicH 3D view" <<< "$out"; then ok "EDIT IN GARAGE goes to the Garage and comes back to the race screen"; else bad "Garage round trip: $(grep -a '3D view' <<< "$out" | head -1)"; fi
[ $fail -eq 0 ] && echo "modern flow: the Continue card, the race screen, Race again and the switch back to classic work"
exit $fail
