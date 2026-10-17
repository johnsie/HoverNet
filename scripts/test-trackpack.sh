#!/usr/bin/env bash
# Track package round trip and defences: every bundled track packs, verifies and unpacks to the identical bytes; damaged,
# truncated and re-labelled packages are refused; the decoder survives mutation fuzzing; and a file that is not a track is
# refused with a reason. Run from the repository root.   scripts/test-trackpack.sh build/HoverNetTrackPack
set -uo pipefail
tool="${1:?usage: $0 <HoverNetTrackPack>}"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
for t in ClassicH "Metro Spiral" Steeplechase Switchback "The Alley2" "The River" "Tidal Causeway"; do
  src="NetTarget/Tracks/$t.trk"
  "$tool" pack "$src" "$dir/p.hntp" --author "HoverRace" --licence "GrokkSoft HoverRace SourceCode License v1.0" --created 2026-10-17 >/dev/null 2>&1 || { echo "FAIL: pack $t"; fail=1; continue; }
  "$tool" verify "$dir/p.hntp" >/dev/null 2>&1 || { echo "FAIL: verify $t"; fail=1; }
  "$tool" unpack "$dir/p.hntp" "$dir/u.trk" >/dev/null 2>&1 && cmp -s "$src" "$dir/u.trk" || { echo "FAIL: unpack $t is not byte-identical"; fail=1; }
  # Packing twice gives the same bytes (reproducible).
  "$tool" pack "$src" "$dir/p2.hntp" --author "HoverRace" --licence "GrokkSoft HoverRace SourceCode License v1.0" --created 2026-10-17 >/dev/null 2>&1
  cmp -s "$dir/p.hntp" "$dir/p2.hntp" || { echo "FAIL: packing $t twice differs"; fail=1; }
done
p="$dir/p.hntp"; size=$(stat -c %s "$p")
cp "$p" "$dir/flip.hntp"; printf '\x55' | dd of="$dir/flip.hntp" bs=1 seek=$((size / 2)) conv=notrunc 2>/dev/null
"$tool" verify "$dir/flip.hntp" >/dev/null 2>&1 && { echo "FAIL: a damaged package verified"; fail=1; }
for n in 0 5 40 100 $((size - 1)); do head -c "$n" "$p" > "$dir/cut.hntp"; "$tool" verify "$dir/cut.hntp" >/dev/null 2>&1 && { echo "FAIL: a package cut at $n verified"; fail=1; }; done
{ printf 'XXXX'; tail -c +5 "$p"; } > "$dir/magic.hntp"; "$tool" verify "$dir/magic.hntp" >/dev/null 2>&1 && { echo "FAIL: wrong magic verified"; fail=1; }
echo "not a track" > "$dir/junk.trk"; "$tool" pack "$dir/junk.trk" "$dir/j.hntp" >/dev/null 2>&1 && { echo "FAIL: junk was imported"; fail=1; }
"$tool" fuzz "$p" 5000 >/dev/null 2>&1 || { echo "FAIL: fuzz"; fail=1; }
info=$("$tool" info "$p"); echo "$info" | grep -q '^licence=GrokkSoft' || { echo "FAIL: manifest lacks the licence"; fail=1; }
# Catalogue admission: a named author and an approved licence pass; unknown authors, unspecified or unlisted licences do not.
src="NetTarget/Tracks/ClassicH.trk"
"$tool" pack "$src" "$dir/ok.hntp" --author "Ada" --licence CC-BY-4.0 >/dev/null 2>&1 && "$tool" verify "$dir/ok.hntp" --catalogue >/dev/null 2>&1 || { echo "FAIL: an attributed CC-BY-4.0 track was refused"; fail=1; }
for args in "" "--author Ada" "--licence CC-BY-4.0" "--author Ada --licence All-rights-reserved" "--author unknown --licence CC0-1.0"; do
  "$tool" pack "$src" "$dir/no.hntp" $args >/dev/null 2>&1
  "$tool" verify "$dir/no.hntp" --catalogue >/dev/null 2>&1 && { echo "FAIL: admitted with [$args]"; fail=1; }
  "$tool" verify "$dir/no.hntp" >/dev/null 2>&1 || { echo "FAIL: plain verify rejected [$args]"; fail=1; }
done
[ $fail -eq 0 ] && echo "trackpack: 7 tracks round trip byte for byte, reproducible; damaged files refused; decoder fuzzed"
exit $fail
