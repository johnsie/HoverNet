#!/usr/bin/env bash
# Moderation over real TCP: players chat and report each other during a race; a moderator logs in to the loopback console with a
# challenge-response proof, reads the report with its chat context, mutes the player, and bans an address; a wrong secret is
# refused; the audit log on disk verifies, and a tampered copy does not.
#   scripts/test-moderation-tcp.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetModTool
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <modtool>}"; bot="${2:?}"; mod="${3:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
mkdir -p "$dir/mod"
echo "ada correct-horse-battery-staple" > "$dir/mod/moderators.txt"
echo "correct-horse-battery-staple" > "$dir/ada.secret"
echo "wrong-secret" > "$dir/bad.secret"
"$server" --track ClassicH --players 3 --port 0 --once --limit-ticks 900 --start-delay-ms 300 --moderation-dir "$dir/mod" --admin-port 0 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "moderation console" "$dir/server.txt" 2>/dev/null && grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
admin=$(sed -n 's/.*moderation console on 127.0.0.1:\([0-9]*\) .*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ -n "$admin" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
fail=0
pids=()
"$bot" --port "$port" --name b1 > "$dir/b1.txt" 2>&1 & pids+=($!)
"$bot" --port "$port" --name b2 --say "you are all terrible drivers" > "$dir/b2.txt" 2>&1 & pids+=($!)
"$bot" --port "$port" --name b3 --report b2 --report-delay-ms 800 > "$dir/b3.txt" 2>&1 & pids+=($!)
# Wait until the bots have connected and the report exists (a fixed sleep failed when the machine was busy with other tests).
for _ in $(seq 1 150); do "$mod" --port "$admin" --name ada --secret-file "$dir/ada.secret" "REPORTS" 2>&1 | grep -q "b3 reports b2" && break; sleep 0.2; done
out=$("$mod" --port "$admin" --name ada --secret-file "$dir/ada.secret" "REPORTS" "REPORT 1" "MUTE b2 5" "MUTES" "BAN 203.0.113.9" "AUDIT-CHECK" 2>&1)
echo "$out" | grep -q "b3 reports b2" || { echo "FAIL: the report is not listed"; echo "$out"; fail=1; }
echo "$out" | grep -q "chat: b2: you are all terrible drivers" || { echo "FAIL: the report lacks its chat context"; echo "$out"; fail=1; }
echo "$out" | grep -q "b2 (" || { echo "FAIL: the mute is not listed"; fail=1; }
echo "$out" | grep -q "the audit log is intact" || { echo "FAIL: the audit check"; fail=1; }
"$mod" --port "$admin" --name ada --secret-file "$dir/bad.secret" "REPORTS" > "$dir/bad.txt" 2>&1 && { echo "FAIL: a wrong secret was accepted"; fail=1; }
grep -q "refused" "$dir/bad.txt" || { echo "FAIL: the wrong secret was not refused"; cat "$dir/bad.txt"; fail=1; }
for p in "${pids[@]}"; do wait "$p" || fail=1; done
wait %1 2>/dev/null
grep -q "(server)\|\[chat\]" "$dir/b1.txt" || { echo "FAIL: b1 did not hear the chat"; cat "$dir/b1.txt"; fail=1; }
"$mod" audit-verify "$dir/mod/audit.log" | grep -q intact || { echo "FAIL: the audit log does not verify"; fail=1; }
grep -q "login" "$dir/mod/audit.log" && grep -q "mute" "$dir/mod/audit.log" && grep -q "login-failed" "$dir/mod/audit.log" && grep -q "ban" "$dir/mod/audit.log" || { echo "FAIL: the audit log lacks entries"; cat "$dir/mod/audit.log"; fail=1; }
sed 's/203.0.113.9/203.0.113.8/' "$dir/mod/audit.log" > "$dir/tampered.log"
res=$("$mod" audit-verify "$dir/tampered.log" || true); echo "$res" | grep -q BROKEN || { echo "FAIL: a tampered audit log verified"; fail=1; }
[ $fail -eq 0 ] && echo "moderation-tcp: chat, report with context, mute and ban through the authenticated console; audit log verifies and detects tampering"
exit $fail
