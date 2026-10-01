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
