#!/usr/bin/env bash
# The editor's window, headless (SDL's dummy video driver): it draws a track (the picture is not blank and shows the road, the starts and the gates), opens every bundled track
# source, and its "compile and check" gives the real compiler's and validator's verdict, with the editor's own reasons for a track that is broken.
# Everything it does to a track is tested without a window in HoverNetTrackSource (EditorSession); this checks the window shows it.
#   scripts/test-track-editor.sh BUILD_DIR
set -uo pipefail
b="${1:?usage: $0 BUILD_DIR}"
cd "$(dirname "$0")/.."
export SDL_VIDEODRIVER=dummy
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=1; }

# a picture check without any image library: how many different colours, and how much of the picture is the plain background
look() { python3 -I - "$1" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
off = struct.unpack_from("<I", d, 10)[0]; w, h = struct.unpack_from("<ii", d, 18); bpp = struct.unpack_from("<H", d, 28)[0]
assert bpp == 32, bpp
px = [d[off + i:off + i + 4] for i in range(0, abs(h) * w * 4, 4)]
from collections import Counter
c = Counter(px); top = c.most_common(1)[0][1]
print(len(c), round(top / len(px), 3), w, abs(h))
PY
}

"$b/HoverNetTrackEditor" --new-loop --frames 4 --capture "$w/loop.bmp" --size 1280x800 || bad "the editor did not start"
read -r colours background width height < <(look "$w/loop.bmp")
if [ "${colours:-0}" -ge 12 ] && python3 -c "import sys; sys.exit(0 if float('$background') < 0.8 else 1)"; then ok "a new loop is drawn ($colours colours, ${width}x$height, the plain background is $background of it)"; else bad "the new loop's picture looks blank ($colours colours, background $background)"; fi

# the Extend the road tool: a road built from a start room, with the preview of the next room showing (its green)
"$b/HoverNetTrackEditor" --demo-road --frames 5 --capture "$w/road.bmp" --size 1280x800 || bad "the demo road did not draw"
if python3 -I - "$w/road.bmp" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
off = struct.unpack_from("<I", d, 10)[0]; w, h = struct.unpack_from("<ii", d, 18)
px = d[off:off + abs(h) * w * 4]
green = sum(1 for i in range(0, len(px), 4) if px[i + 2] > 90 and px[i + 1] > 190 and px[i] < 190 and px[i + 2] < 140)   # the preview's edge, in BGRA
yellow = sum(1 for i in range(0, len(px), 4) if px[i + 2] > 240 and px[i + 1] > 215 and px[i] < 140)                      # the highlighted open edge
sys.exit(0 if green > 100 and yellow > 100 else 1)
PY
then ok "a road built room by room is drawn with the preview of the next room and the highlighted open edge"; else bad "the extend preview is not drawn"; fi

for src in NetTarget/TrackSources/*.track.txt; do
  name="$(basename "$src" .track.txt)"
  "$b/HoverNetTrackEditor" "$src" --frames 4 --capture "$w/o.bmp" --size 1024x700 && read -r colours background _ < <(look "$w/o.bmp")
  if [ "${colours:-0}" -ge 12 ]; then ok "$name opens and is drawn"; else bad "$name did not draw"; fi
  if "$b/HoverNetTrackEditor" "$src" --check > "$w/check.txt" 2>&1; then ok "$name: compile and check says VALID"; else bad "$name: compile and check failed"; cat "$w/check.txt"; fi
done

sed 's/wall=\(.*\), 1, 52/wall=\1, 1, 999/' NetTarget/TrackSources/Switchback.track.txt > "$w/broken.track.txt"
if "$b/HoverNetTrackEditor" "$w/broken.track.txt" --check > "$w/check.txt" 2>&1; then bad "a broken track passed the check"; else
  grep -q "surface 999 is not an element the game knows" "$w/check.txt" && ok "a broken track fails the check and the editor says what is wrong in plain words" || { bad "the broken track's reasons are missing"; cat "$w/check.txt"; }; fi

printf 'not a track at all' > "$w/junk.track.txt"
"$b/HoverNetTrackEditor" "$w/junk.track.txt" --frames 2 --capture "$w/j.bmp" > /dev/null 2>&1 && ok "a file that is not a track does not crash the editor" || bad "the editor failed on a junk file"
"$b/HoverNetTrackEditor" --check > /dev/null 2>&1; [ $? -eq 2 ] && ok "--check without a file is refused" || bad "--check without a file was not refused"
exit $fail
