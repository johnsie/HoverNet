#!/usr/bin/env bash
# The game client saves the race it followed as a replay: a server (with --replay-dir), scripted bots and one real game client (headless window,
# scripted input). The replay in the client's own replays folder must be byte for byte the official one the server wrote, and the game must play it
# back to the same final hash. A client that lost its connection mid-race (and so missed ticks) must not save one.
#   scripts/test-client-replay.sh build/HoverNetAuthServer build/HoverNetAuthBot build/HoverNetGame2Player build/HoverNetReplay [players] [ticks]
set -uo pipefail
server="${1:?usage: $0 <server> <bot> <client> <replay tool> [players] [ticks]}"; bot="${2:?}"; client="${3:?}"; tool="${4:?}"
players="${5:-3}"; ticks="${6:-500}"; track=ClassicH
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
race() { # label, extra client options
  rm -rf "$dir/official" "$HOME/.config/hovernet/replays"; mkdir -p "$dir/official"
  "$server" --track "$track" --players "$players" --port 0 --once --limit-ticks "$ticks" --start-delay-ms 1500 --replay-dir "$dir/official" > "$dir/server.txt" 2>&1 &
  local spid=$!
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
  local port; port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
  [ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
  local pids=()
  for i in $(seq 2 "$players"); do "$bot" --port "$port" --name "bot$i" --track "$track" > "$dir/bot$i.txt" 2>&1 & pids+=($!); done
  HOVERNET_TEST_REPLAY_LIBRARY=1 "$client" --auth-connect "127.0.0.1:$port" --auth-verify --auth-script $2 --frames 100000 > "$dir/client.txt" 2>&1 || { echo "FAIL ($1): the game client failed"; tail -5 "$dir/client.txt"; fail=1; }
  for p in "${pids[@]}"; do wait "$p" || true; done
  wait "$spid" || true
}
race "full race" ""
mine=$(ls "$HOME/.config/hovernet/replays/"*.hnr 2>/dev/null | head -1); theirs=$(ls "$dir/official/"*.hnr 2>/dev/null | head -1)
if [ -z "$mine" ]; then echo "FAIL: the client saved no replay"; grep REPLAYTEST "$dir/client.txt"; fail=1
elif [ -z "$theirs" ]; then echo "FAIL: the server wrote no replay"; fail=1
elif ! cmp -s "$mine" "$theirs"; then echo "FAIL: the client's replay differs from the server's"; ls -l "$mine" "$theirs"; fail=1
else
  want=$("$tool" verify "$mine" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
  got=$("$client" --replay "$mine" --replay-hash --frames 100000 2>/dev/null | sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p')
  [ -n "$want" ] && [ "$want" = "$got" ] && echo "ok   the client's replay is byte for byte the server's, and the game plays it back to $got" || { echo "FAIL: the saved replay does not play back (tool $want, game ${got:-none})"; fail=1; }
fi
race "dropped connection" "--auth-drop 150"
# After a reconnect the server may or may not resend what was missed. Either the client saved nothing (a gap spoils the recording), or what it
# saved is exactly the server's replay: it must never save a replay that is not the real race.
mine=$(ls "$HOME/.config/hovernet/replays/"*.hnr 2>/dev/null | head -1); theirs=$(ls "$dir/official/"*.hnr 2>/dev/null | head -1)
if [ -z "$mine" ]; then echo "ok   a client that missed ticks saved no replay"
elif [ -n "$theirs" ] && cmp -s "$mine" "$theirs"; then echo "ok   a client that reconnected saved a replay identical to the server's"
else echo "FAIL: a client that lost its connection saved a replay that is not the server's"; fail=1; fi
[ $fail -eq 0 ] && echo "client replay: the race a client followed is saved as the server's replay"
exit $fail
