#!/usr/bin/env bash
# Community track support, end to end.
#
#   scripts/test-community-tracks.sh BUILD_DIR SOURCE_DIR WORK_DIR
#
# 1. Manifest sanity (always): every name is unique, safe for the filesystem and
#    the network protocol, and every mode is race|freeplay.
# 2. Selector regression (always, using a throw-away pack built from a bundled
#    track): the local-setup screen runs with a community track in the catalog,
#    remembers it BY NAME across runs, still reads the old numeric preference
#    format, and renders the open dropdown.
# 3. Full pack (only if the real community pack is installed in
#    NetTarget/CommunityTracks or HOVERNET_COMMUNITY_TRACKS_DIR): every listed
#    track passes the community validator and starts in the real game client.
#
# Exits 77 (ctest "skipped") only if there is nothing to test, which cannot happen
# for parts 1-2, so a pass always means something was verified.
set -euo pipefail

build_dir=$(realpath "${1:?build directory required}")
source_dir=$(realpath "${2:?source directory required}")
work_dir=$(realpath -m "${3:?work directory required}")
rm -rf "$work_dir"; mkdir -p "$work_dir"
cd "$source_dir"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

manifest=NetTarget/CommunityTracks.tsv
fail() { echo "FAIL: $*" >&2; exit 1; }

# ---- 1. manifest sanity -----------------------------------------------------
python3 - "$manifest" <<'PY'
import sys
seen, count = set(), 0
official = {"classich","steeplechase","switchback","the alley2","the river","tidal causeway","metro spiral"}
for number, raw in enumerate(open(sys.argv[1], encoding="utf-8"), 1):
    line = raw.rstrip("\n")
    if not line or line.startswith("#"):
        continue
    fields = line.split("\t")
    if len(fields) < 5:
        sys.exit(f"manifest line {number}: expected 5 fields, got {len(fields)}")
    name, mode, starts, rooms, warnings = fields[:5]
    if mode not in ("race", "freeplay"):
        sys.exit(f"manifest line {number}: bad mode {mode!r}")
    if not (1 <= int(starts) <= 10) or int(rooms) < 1 or int(warnings) < 0:
        sys.exit(f"manifest line {number}: bad numbers")
    if not name or name.startswith(".") or any(c in name for c in '/\\:') or any(ord(c) < 32 for c in name):
        sys.exit(f"manifest line {number}: unsafe name {name!r}")
    if len(fields) >= 10:
        import re
        _, _, _, _, _, size, sha, shard, asset, gz = fields[:10]
        if not (re.fullmatch(r"[0-9a-f]{64}", sha) and re.fullmatch(r"[0-9a-f]{16,64}", asset)
                and re.fullmatch(r"[A-Za-z0-9._-]{1,64}", shard) and int(size) > 0 and int(gz) > 0):
            sys.exit(f"manifest line {number}: bad download columns")
        if not sha.startswith(asset):
            sys.exit(f"manifest line {number}: asset name is not derived from the hash")
    if len(name.encode("utf-8")) > 63:
        sys.exit(f"manifest line {number}: name over 63 bytes")
    key = name.lower()
    if key in seen or key in official:
        sys.exit(f"manifest line {number}: duplicate or official name {name!r}")
    seen.add(key); count += 1
if count == 0:
    sys.exit("manifest lists no tracks")
print(f"manifest ok: {count} community tracks")
PY

# ---- 2. selector regression with a throw-away pack ---------------------------
pack="$work_dir/pack"; mkdir -p "$pack"
cp NetTarget/Tracks/"Tidal Causeway.trk" "$pack/Zz Test Arena.trk"
cp NetTarget/Tracks/ClassicH.trk "$pack/Aa Test Sprint.trk"
printf 'Zz Test Arena\tfreeplay\t8\t40\t0\nAa Test Sprint\trace\t10\t12\t0\n' >"$work_dir/manifest.tsv"
export HOVERNET_COMMUNITY_TRACKS_DIR="$pack" HOVERNET_COMMUNITY_MANIFEST="$work_dir/manifest.tsv"
export XDG_CONFIG_HOME="$work_dir/config"; mkdir -p "$XDG_CONFIG_HOME/hovernet"
prefs="$XDG_CONFIG_HOME/hovernet/local_race_prefs"
game="$build_dir/HoverNetGame2Player"

# A community track named in the saved preference survives a run.
printf 'Zz Test Arena\n4 1\n' >"$prefs"
"$game" --play --local-setup-screen --frames 4 >"$work_dir/setup.log" 2>&1 || { cat "$work_dir/setup.log"; fail "local setup screen failed with a community track selected"; }
[[ $(head -n1 "$prefs") == "Zz Test Arena" ]] || fail "community track was not remembered by name (got: $(head -n1 "$prefs"))"
[[ $(sed -n 2p "$prefs") == "4 1" ]] || fail "laps/weapons were not remembered"

# The previous "index laps weapons" format still selects an official track.
printf '2 7 0\n' >"$prefs"
"$game" --play --local-setup-screen --frames 4 >"$work_dir/setup.log" 2>&1 || fail "legacy preference file broke the setup screen"
[[ $(head -n1 "$prefs") == "Switchback" ]] || fail "legacy index 2 should map to Switchback (got: $(head -n1 "$prefs"))"

# A preference naming a track that is not installed falls back to the first official track.
printf 'No Such Track\n3 1\n' >"$prefs"
"$game" --play --local-setup-screen --frames 4 >"$work_dir/setup.log" 2>&1 || fail "unknown saved track broke the setup screen"
[[ $(head -n1 "$prefs") == "ClassicH" ]] || fail "unknown saved track should fall back to ClassicH (got: $(head -n1 "$prefs"))"

# The open dropdown renders: capture it and require the official and community
# headings (the coral heading colour) and a list body in the capture.
HOVERNET_TEST_OPEN_TRACK_DROPDOWN=1 HOVERNET_CAPTURE_FRAME="$work_dir/dropdown.bmp" \
  "$game" --play --local-setup-screen --frames 8 >"$work_dir/dropdown.log" 2>&1 || fail "open dropdown run failed"
[[ -s "$work_dir/dropdown.bmp" ]] || fail "dropdown capture was not written"
python3 - "$work_dir/dropdown.bmp" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
width, height = struct.unpack_from("<ii", data, 18)
bpp = struct.unpack_from("<H", data, 28)[0]
height = abs(height); step = bpp // 8
stride = (width * step + 3) & ~3
# kHoverNetCoral heading text rows: count rows that contain coral-ish pixels
# (strong red, mid green/blue) in the left half where the dropdown is drawn.
rows = 0
for y in range(height):
    base = offset + y * stride
    hit = 0
    for x in range(0, width // 2):
        b, g, r = data[base + x * step: base + x * step + 3]
        if r > 200 and 70 < g < 140 and 70 < b < 150:
            hit += 1
    if hit > 8:
        rows += 1
if rows < 6:
    sys.exit(f"dropdown capture shows no section headings (coral rows: {rows})")
print(f"dropdown capture ok ({rows} heading rows)")
PY
echo "selector regression ok"

# ---- 2b. online race on community tracks through the real RaceServer ---------
# Covers the server's manifest allowlist (a name outside it is refused), the
# 32-byte race-name limit (the long name below is 41 bytes), and the joiner
# loading the same community track by name.
long_name="Zz Long Community Track Name For Test"
cp NetTarget/Tracks/Switchback.trk "$pack/$long_name.trk"
printf '%s\trace\t10\t12\t0\n' "$long_name" >>"$work_dir/manifest.tsv"
export HOVERNET_TRACK_MANIFEST="$work_dir/manifest.tsv"
port=19950
run_online_race() {
  local track=$1 expect=$2 server_pid host_pid
  "$build_dir/RaceServer" "$port" "$work_dir/server-$port.log" --require-protocol-2 >/dev/null 2>&1 &
  server_pid=$!
  sleep 0.5
  timeout 40 "$game" --lobby 127.0.0.1 "$port" --track "$track" --online-race-host-smoke --frames 60 \
    >"$work_dir/host-$port.log" 2>&1 &
  host_pid=$!
  sleep 0.5
  local join_rc=0 host_rc=0
  timeout 40 "$game" --lobby 127.0.0.1 "$port" --online-race-join-smoke --frames 60 \
    >"$work_dir/join-$port.log" 2>&1 || join_rc=$?
  wait "$host_pid" || host_rc=$?
  kill -TERM "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  port=$((port + 1))
  if [[ $expect == ok ]]; then
    [[ $join_rc -eq 0 && $host_rc -eq 0 ]] || { tail -n 20 "$work_dir/host-$((port - 1)).log" "$work_dir/join-$((port - 1)).log" >&2; fail "online race on '$track' failed (host=$host_rc join=$join_rc)"; }
  fi
}
run_online_race "Zz Test Arena" ok
run_online_race "$long_name" ok
echo "online community races ok"

# ---- 2c. downloading tracks on demand (needs curl and gzip; served over file://) --
if command -v curl >/dev/null 2>&1 && command -v gzip >/dev/null 2>&1; then
  hosting="$work_dir/hosting/test-shard"; mkdir -p "$hosting"
  dl_manifest="$work_dir/dl-manifest.tsv"
  : >"$dl_manifest"
  for name in "Zz Test Arena" "Aa Test Sprint" "$long_name"; do
    file="$pack/$name.trk"
    sha=$(sha256sum "$file" | cut -d' ' -f1)
    gzip -n -9 -c "$file" >"$hosting/${sha:0:16}.trk.gz"
    printf '%s\trace\t10\t12\t0\t%s\t%s\ttest-shard\t%s\t%s\n' \
      "$name" "$(stat -c %s "$file")" "$sha" "${sha:0:16}" "$(stat -c %s "$hosting/${sha:0:16}.trk.gz")" >>"$dl_manifest"
  done
  export HOVERNET_TRACK_DOWNLOAD_URL="file://$work_dir/hosting"
  export HOVERNET_COMMUNITY_MANIFEST="$dl_manifest" HOVERNET_TRACK_MANIFEST="$dl_manifest"

  # Command line: everything missing is downloaded, verified and byte-identical.
  d1="$work_dir/dl1"
  HOVERNET_COMMUNITY_TRACKS_DIR="$d1" "$game" --play --download-community-tracks --all >"$work_dir/dl1.log" 2>&1 \
    || { cat "$work_dir/dl1.log"; fail "--download-community-tracks --all failed"; }
  for name in "Zz Test Arena" "Aa Test Sprint" "$long_name"; do
    cmp -s "$d1/$name.trk" "$pack/$name.trk" || fail "downloaded '$name' differs from the original"
  done
  [[ -z $(find "$d1" -name '*.part' -o -name '*.tmp') ]] || fail "download left temporary files behind"
  HOVERNET_COMMUNITY_TRACKS_DIR="$d1" "$game" --play --download-community-tracks --all 2>&1 | grep -q "Nothing to download" \
    || fail "a second --all run should find nothing to do"

  # --track NAME fetches a missing track by itself and then plays it.
  d2="$work_dir/dl2"
  HOVERNET_COMMUNITY_TRACKS_DIR="$d2" "$game" --play --autoplay --track "Zz Test Arena" --frames 20 >"$work_dir/dl2.log" 2>&1 \
    || { cat "$work_dir/dl2.log"; fail "--track did not download and load a missing community track"; }
  [[ -f "$d2/Zz Test Arena.trk" ]] || fail "--track did not install the track"

  # The local setup screen's "Download & Start Race" installs the track and confirms.
  d3="$work_dir/dl3"
  printf 'Zz Test Arena\n4 1\n' >"$prefs"
  HOVERNET_COMMUNITY_TRACKS_DIR="$d3" HOVERNET_TEST_AUTO_START=1 "$game" --play --local-setup-screen --frames 600 \
    >"$work_dir/dl3.log" 2>&1 || { cat "$work_dir/dl3.log"; fail "Download & Start Race failed"; }
  [[ -f "$d3/Zz Test Arena.trk" ]] || fail "Download & Start Race did not install the track"

  # A track that does not match its recorded hash is refused and nothing is installed.
  bad_manifest="$work_dir/bad-manifest.tsv"
  sed -E 's/\t[0-9a-f]{64}\t/\t0000000000000000000000000000000000000000000000000000000000000000\t/' "$dl_manifest" >"$bad_manifest"
  d4="$work_dir/dl4"
  if HOVERNET_COMMUNITY_MANIFEST="$bad_manifest" HOVERNET_COMMUNITY_TRACKS_DIR="$d4" \
       "$game" --play --download-community-tracks "Zz Test Arena" >"$work_dir/dl4.log" 2>&1; then
    fail "a download that fails verification must not succeed"
  fi
  [[ ! -e "$d4/Zz Test Arena.trk" ]] || fail "a tampered download was installed"

  # Online: the host has the track, the joiner does not. The joiner downloads it
  # when joining and then plays the same race.
  joiner_dir="$work_dir/joiner"
  "$build_dir/RaceServer" 19960 "$work_dir/server-19960.log" --require-protocol-2 >/dev/null 2>&1 &
  server_pid=$!
  sleep 0.5
  HOVERNET_COMMUNITY_TRACKS_DIR="$pack" timeout 60 "$game" --lobby 127.0.0.1 19960 --track "Zz Test Arena" \
    --online-race-host-smoke --frames 80 >"$work_dir/host-19960.log" 2>&1 &
  host_pid=$!
  sleep 0.5
  join_rc=0
  HOVERNET_COMMUNITY_TRACKS_DIR="$joiner_dir" timeout 60 "$game" --lobby 127.0.0.1 19960 \
    --online-race-join-smoke --frames 80 >"$work_dir/join-19960.log" 2>&1 || join_rc=$?
  host_rc=0; wait "$host_pid" || host_rc=$?
  kill -TERM "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true
  [[ $join_rc -eq 0 && $host_rc -eq 0 ]] || { tail -n 20 "$work_dir/host-19960.log" "$work_dir/join-19960.log" >&2; fail "joiner without the track could not download and join"; }
  [[ -f "$joiner_dir/Zz Test Arena.trk" ]] || fail "the joiner did not download the host's track"
  echo "on-demand downloads ok"
  unset HOVERNET_TRACK_DOWNLOAD_URL
else
  echo "curl/gzip not available; skipped the download checks"
fi

# ---- 3. the real pack, if installed -----------------------------------------
unset HOVERNET_COMMUNITY_MANIFEST
real_pack=${REAL_COMMUNITY_PACK:-}
if [[ -z $real_pack ]]; then
  for candidate in "${ORIGINAL_COMMUNITY_TRACKS_DIR:-}" NetTarget/CommunityTracks; do
    [[ -n $candidate && -d $candidate ]] && real_pack=$candidate && break
  done
fi
if [[ -z $real_pack ]]; then
  echo "community pack not installed; skipped full-pack validation (manifest and selector checks passed)"
  exit 0
fi
real_pack=$(realpath "$real_pack")
export HOVERNET_COMMUNITY_TRACKS_DIR="$real_pack"
checked=0; missing=0
while IFS=$'\t' read -r name mode starts rooms warnings; do
  [[ -z $name || $name == \#* ]] && continue
  [[ -f "$real_pack/$name.trk" ]] || { missing=$((missing + 1)); continue; }
  "$build_dir/HoverNetTrackValidator" --community "$real_pack/$name.trk" "$name" >/dev/null 2>"$work_dir/validate.err" \
    || { cat "$work_dir/validate.err" >&2; fail "'$name' no longer passes the community validator"; }
  checked=$((checked + 1))
done <"$manifest"
echo "full pack: $checked validated, $missing not present"
[[ $checked -gt 0 ]] || fail "pack directory $real_pack holds none of the manifest tracks"
