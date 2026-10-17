#!/usr/bin/env bash
# Private races over real TCP: a race hosted with a code is not listed, cannot be joined without the code or with a wrong one, and
# runs to the end for the friend who has it. Run from the repository root.
#   scripts/test-private-tcp.sh build/HoverNetAuthServer build/HoverNetAuthBot
set -uo pipefail
server="${1:?usage: $0 <server> <bot>}"; bot="${2:?}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
"$server" --lobby --port 0 --limit-ticks 400 --start-delay-ms 500 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
fail=0
"$bot" --port "$port" --name host --host-race ClassicH:2 --code kestrel7 > "$dir/host.txt" 2>&1 & p1=$!
sleep 0.7
"$bot" --port "$port" --name lister --join-race ClassicH --track ClassicH > "$dir/lister.txt" 2>&1 && { echo "FAIL: a private race was listed and joined"; fail=1; }
grep -q "no open race to join" "$dir/lister.txt" || { echo "FAIL: the public listing was not empty"; cat "$dir/lister.txt"; fail=1; }
"$bot" --port "$port" --name nocode --race-id 1 --track ClassicH > "$dir/nocode.txt" 2>&1 && { echo "FAIL: joined a private race without its code"; fail=1; }
"$bot" --port "$port" --name wrong --race-id 1 --code kestrel8 --track ClassicH > "$dir/wrong.txt" 2>&1 && { echo "FAIL: joined with a wrong code"; fail=1; }
grep -q "rejected" "$dir/wrong.txt" || { echo "FAIL: the wrong code was not rejected"; fail=1; }
"$bot" --port "$port" --name friend --race-id 1 --code kestrel7 --track ClassicH > "$dir/friend.txt" 2>&1 & p2=$!
wait "$p1" || fail=1; wait "$p2" || fail=1
for n in host friend; do grep -q "no divergence" "$dir/$n.txt" || { echo "FAIL: $n did not see the race through"; cat "$dir/$n.txt"; fail=1; }; done
[ $fail -eq 0 ] && echo "private-tcp: unlisted, refused without the code, run to the end for the friend with it"
exit $fail
