#!/usr/bin/env bash
# Community tracks on the Leaderboards: a track of the community library has its own per-craft boards in All Records (picked like any other track), its
# broken records are listed in Records Broken, and WATCH on one of its times downloads the track first when this computer does not have it (from a small
# local web server standing in for the track host, checked against the manifest's hash) and then plays the replay to its verified hash.
#   scripts/test-leaderboards-community.sh build/HoverNetAuthServer build/HoverNetReplay build/HoverNetGame2Player
set -uo pipefail
server="$(realpath "${1:?usage: $0 <server> <replay tool> <client>}")"; tool="$(realpath "${2:?}")"; client="$(realpath "${3:?}")"
cd "$(dirname "$0")/.."
root="$(pwd)"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
name="Zz Board Track [1-1]"
mkdir -p "$dir/work/NetTarget/Tracks" "$dir/replays" "$dir/www/test-shard" "$dir/client-tracks" "$dir/home"
cp NetTarget/Tracks/ClassicH.trk "$dir/work/NetTarget/Tracks/$name.trk"      # the tool finds a track by name under NetTarget/Tracks of its working directory
( cd "$dir/work" && "$tool" record "$name" 700 "$dir/replays/a-1-100.hnr" --players=2 > /dev/null 2>&1 && "$tool" record "$name" 800 "$dir/replays/b-2-200.hnr" --players=2 > /dev/null 2>&1 ) || { echo "FAIL: could not record replays on the community track"; exit 1; }
H='rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n'
printf "$H"'1\t0\t1\t1\t90000\t90000\tAnna\t0\n2\t1\t1\t1\t95000\t95000\tBen\t2\n' > "$dir/replays/a-1-100.results.tsv"
printf "$H"'1\t1\t1\t1\t85000\t85000\tDee\t0\n2\t0\t1\t1\t99000\t99000\tEli\t0\n' > "$dir/replays/b-2-200.results.tsv"
touch -d '2 hours ago' "$dir/replays/a-1-100.hnr"
want=$(cd "$dir/work" && "$tool" verify "$dir/replays/b-2-200.hnr" 2>/dev/null | sed -n 's/.*final hash \([0-9a-f]*\).*/\1/p')
sha=$(sha256sum "$dir/work/NetTarget/Tracks/$name.trk" | cut -d' ' -f1); asset=${sha:0:16}
gzip -9 -n -c "$dir/work/NetTarget/Tracks/$name.trk" > "$dir/www/test-shard/$asset.trk.gz"
printf '%s\trace\t10\t12\t0\t%s\t%s\ttest-shard\t%s\t%s\n' "$name" "$(stat -c %s "$dir/work/NetTarget/Tracks/$name.trk")" "$sha" "$asset" "$(stat -c %s "$dir/www/test-shard/$asset.trk.gz")" > "$dir/manifest.tsv"
(cd "$dir/www" && exec python3 -u -m http.server 0 --bind 127.0.0.1 > "$dir/www.txt" 2>&1) &
for _ in $(seq 1 50); do grep -q "Serving HTTP" "$dir/www.txt" 2>/dev/null && break; sleep 0.1; done
webport=$(sed -n 's/.*port \([0-9]*\).*/\1/p' "$dir/www.txt" | head -1)
"$server" --lobby --port 0 --replay-dir "$dir/replays" > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ -n "$webport" ] || { echo "FAIL: the server or the stand-in track host did not start"; exit 1; }
export HOME="$dir/home"; mkdir -p "$HOME"; unset XDG_CONFIG_HOME
fail=0
shot() { env HOVERNET_COMMUNITY_MANIFEST="$dir/manifest.tsv" HOVERNET_COMMUNITY_TRACKS_DIR="$dir/client-tracks" HOVERNET_TRACK_DOWNLOAD_URL="http://127.0.0.1:$webport" \
  HOVERNET_TEST_AUTH_SERVER="127.0.0.1:$port" HOVERNET_TEST_MENU=leaderboards HOVERNET_TEST_MENU_FORCE=1 HOVERNET_TEST_REPLAY_LIBRARY=1 HOVERNET_TEST_RECORDS_TRACK="$name" "$@" \
  timeout -s KILL 90 "$client" --play --replay-hash --frames 1500 2>&1; }
out=$(shot HOVERNET_TEST_LEADERBOARD_TAB=3)
if grep -qF "BREAKS 3 | $name Basic lap: Dee 85000 beat Anna 90000 | $name Bi-Turbo lap: Ben 95000 set the first record | $name Basic lap: Anna 90000 set the first record" <<< "$out"; then echo "ok   Records Broken lists the records broken and set on a community track"; else echo "FAIL: records broken on a community track"; grep BREAKS <<< "$out"; fail=1; fi
out=$(shot HOVERNET_TEST_LEADERBOARD_PICK=1)
if grep -qF "LEADERBOARDS $name laps 0 boards 2 | Basic: Dee 85000 | Bi-Turbo: Ben 95000" <<< "$out"; then echo "ok   All Records shows the community track's boards per craft"; else echo "FAIL: the community track's records"; grep LEADERBOARDS <<< "$out"; fail=1; fi
got=$(sed -n 's/^replay hash \([0-9a-f]*\) ticks.*/\1/p' <<< "$out")
if [ -n "$want" ] && [ "$want" = "$got" ] && cmp -s "$dir/client-tracks/$name.trk" "$dir/work/NetTarget/Tracks/$name.trk"; then echo "ok   WATCH downloaded the community track first, then played the record to the verified hash ($got)"
else echo "FAIL: watching a time on a community track that was not installed (hash '$got', wanted '$want')"; tail -5 <<< "$out"; fail=1; fi
[ $fail -eq 0 ] && echo "community leaderboards: records, broken records and watching, with the download"
exit $fail
