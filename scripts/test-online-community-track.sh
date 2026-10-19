#!/usr/bin/env bash
# A race on a community track through the HoverNet lobby, end to end with real clicks: the client does not have the track, so the create button says
# "Download & Create"; clicking it downloads the track (from a small local web server standing in for the track host, checked against the manifest's
# hash), then creates the race; the server hosts it from its community folder. Two runs: a server that has the track must host it, a server without a
# community folder must refuse it (so the first run proves the server side too).
#   scripts/test-online-community-track.sh build/HoverNetAuthServer build/HoverNetGame2Player
set -uo pipefail
server="${1:?usage: $0 <server> <client>}"; client="${2:?}"
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
name="Zz Online Sprint [1-1]"
mkdir -p "$dir/srv" "$dir/www/test-shard" "$dir/client-tracks" "$dir/config/hovernet"
cp NetTarget/Tracks/ClassicH.trk "$dir/srv/$name.trk"
sha=$(sha256sum "$dir/srv/$name.trk" | cut -d' ' -f1); asset=${sha:0:16}
gzip -9 -n -c "$dir/srv/$name.trk" > "$dir/www/test-shard/$asset.trk.gz"
printf '%s\trace\t10\t12\t0\t%s\t%s\ttest-shard\t%s\t%s\n' "$name" "$(stat -c %s "$dir/srv/$name.trk")" "$sha" "$asset" "$(stat -c %s "$dir/www/test-shard/$asset.trk.gz")" > "$dir/manifest.tsv"
printf '%s\n3 1\n' "$name" > "$dir/config/hovernet/local_race_prefs"      # the track the Host dialog opens on (remembered by name)
(cd "$dir/www" && exec python3 -u -m http.server 0 --bind 127.0.0.1 > "$dir/www.txt" 2>&1) &
for _ in $(seq 1 50); do grep -q "Serving HTTP" "$dir/www.txt" 2>/dev/null && break; sleep 0.1; done
webport=$(sed -n 's/.*port \([0-9]*\).*/\1/p' "$dir/www.txt" | head -1)
[ -n "$webport" ] || { echo "FAIL: the stand-in track host did not start"; cat "$dir/www.txt"; exit 1; }

run() {   # $1 = label, then the server's extra arguments; prints the client's CLICKTEST lines, returns 0 when the race was created
  local label="$1"; shift
  rm -f "$dir/client-tracks"/*.trk
  "$server" --lobby --port 0 --limit-ticks 500 "$@" > "$dir/server-$label.txt" 2>&1 &
  local pid=$!
  for _ in $(seq 1 100); do grep -q "listening on" "$dir/server-$label.txt" 2>/dev/null && break; sleep 0.1; done
  local port; port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server-$label.txt" | head -1)
  [ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server-$label.txt"; return 2; }
  HOME="$dir/home" XDG_CONFIG_HOME="$dir/config" HOVERNET_COMMUNITY_MANIFEST="$dir/manifest.tsv" HOVERNET_COMMUNITY_TRACKS_DIR="$dir/client-tracks" \
    HOVERNET_TRACK_DOWNLOAD_URL="http://127.0.0.1:$webport" HOVERNET_TEST_MENU=authserver HOVERNET_TEST_AUTH_CLICKS="127.0.0.1:$port" \
    timeout 120 "$client" --frames 100000 > "$dir/client-$label.txt" 2>&1
  kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  grep -q "CLICKTEST ok" "$dir/client-$label.txt"
}
mkdir -p "$dir/home"
fail=0
if run with --community-dir "$dir/srv"; then
  if cmp -s "$dir/client-tracks/$name.trk" "$dir/srv/$name.trk"; then echo "ok   the client downloaded the track it did not have (checked against the manifest) and the server hosted the race on it"
  else echo "FAIL: the race was created but the track was not downloaded intact"; fail=1; fi
else echo "FAIL: with the track on the server, 'Download & Create' did not end in a hosted race"; grep -h "CLICKTEST\|ownload" "$dir/client-with.txt" | tail -5; fail=1; fi
if run without; then echo "FAIL: a server without a community folder hosted a community track"; fail=1
else echo "ok   a server without a community folder refuses a community track"; fi
[ $fail -eq 0 ] && echo "online community track: download, create and host work together"
exit $fail
