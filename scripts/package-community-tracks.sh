#!/usr/bin/env bash
# Builds the separately distributed community track pack from a folder of .trk
# files, without modifying any track.
#
#   scripts/package-community-tracks.sh VERSION TRACK_DIR [OUT_DIR]
#
# Produces, in OUT_DIR (default dist/):
#   hovernet-community-tracks_VERSION.zip      (+ .sha256)   any platform; unzip into
#                                              NetTarget/CommunityTracks, or use
#                                              scripts/install-community-tracks.*
#   hovernet-community-tracks_VERSION_all.deb                Debian/Ubuntu (if dpkg-deb exists)
#
# Every track named in NetTarget/CommunityTracks.tsv must be present, so a pack
# can never silently lack something the game lists.
set -euo pipefail

version="${1:?version required, e.g. 2.0.0}"; version="${version#v}"
track_dir=$(realpath "${2:?directory of .trk files required}")
out_dir=$(realpath -m "${3:-dist}")
manifest=NetTarget/CommunityTracks.tsv
readme_source=docs/community-tracks-pack-readme.txt

case "$version" in
  ""|*[!0-9A-Za-z.+:~\-]*) echo "Invalid version: $version" >&2; exit 2 ;;
esac
test -f "$manifest"
test -f "$readme_source"

missing=0; listed=0
while IFS=$'\t' read -r name _; do
  [[ -z $name || $name == \#* ]] && continue
  listed=$((listed + 1))
  if [[ ! -f "$track_dir/$name.trk" ]]; then
    echo "missing from $track_dir: $name.trk" >&2
    missing=$((missing + 1))
  fi
done <"$manifest"
[[ $missing -eq 0 ]] || { echo "$missing listed track(s) missing; refusing to build an incomplete pack" >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
root="$stage/hovernet-community-tracks"
mkdir -p "$root/CommunityTracks" "$out_dir"
while IFS=$'\t' read -r name _; do
  [[ -z $name || $name == \#* ]] && continue
  cp -p "$track_dir/$name.trk" "$root/CommunityTracks/$name.trk"
done <"$manifest"
cp "$readme_source" "$root/README.txt"
cp "$manifest" "$root/CommunityTracks.tsv"

zip_path="$out_dir/hovernet-community-tracks_${version}.zip"
rm -f "$zip_path"
(cd "$stage" && zip -q -r -X "$zip_path" hovernet-community-tracks)
(cd "$out_dir" && sha256sum "$(basename "$zip_path")" >"$(basename "$zip_path").sha256")
echo "built $zip_path ($listed tracks)"

if command -v dpkg-deb >/dev/null 2>&1; then
  packaging/debian/build-community-tracks-deb.sh "$version" "$root/CommunityTracks" "$out_dir"
fi
