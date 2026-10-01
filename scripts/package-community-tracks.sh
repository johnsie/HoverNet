#!/usr/bin/env bash
# Builds the separately distributed community track pack from a folder of .trk
# files, without modifying any track.
#
#   scripts/package-community-tracks.sh VERSION TRACK_DIR [OUT_DIR]
#
# Produces, in OUT_DIR (default dist/):
#   community-tracks-hosting/<shard>/<asset>.trk.gz   what the game downloads on
#                                              demand; upload each <shard> folder as
#                                              the assets of a release of that name
#                                              (see community-tracks-hosting/UPLOAD.txt)
#   hovernet-community-tracks_VERSION.zip      (+ .sha256)   offline install: unzip into
#                                              NetTarget/CommunityTracks, or use
#                                              scripts/install-community-tracks.*
#   hovernet-community-tracks_VERSION_all.deb                Debian/Ubuntu (if dpkg-deb exists)
#
# Every track named in NetTarget/CommunityTracks.tsv must be present and match
# the SHA-256 recorded there, so a pack can never silently lack, or differ from,
# something the game lists or will verify.
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

missing=0; listed=0; mismatched=0
while IFS=$'\t' read -r name mode starts rooms warnings bytes sha shard asset gzbytes; do
  [[ -z $name || $name == \#* ]] && continue
  listed=$((listed + 1))
  if [[ ! -f "$track_dir/$name.trk" ]]; then
    echo "missing from $track_dir: $name.trk" >&2
    missing=$((missing + 1))
  elif [[ -n ${sha:-} && $(sha256sum "$track_dir/$name.trk" | cut -d' ' -f1) != "$sha" ]]; then
    echo "does not match the manifest hash: $name.trk" >&2
    mismatched=$((mismatched + 1))
  fi
done <"$manifest"
[[ $missing -eq 0 ]] || { echo "$missing listed track(s) missing; refusing to build an incomplete pack" >&2; exit 1; }
[[ $mismatched -eq 0 ]] || { echo "$mismatched track(s) differ from the manifest; regenerate it with scripts/generate-community-manifest.sh" >&2; exit 1; }

# Per-track downloads, grouped by release shard.
hosting="$out_dir/community-tracks-hosting"
rm -rf "$hosting"; mkdir -p "$hosting"
while IFS=$'\t' read -r name mode starts rooms warnings bytes sha shard asset gzbytes; do
  [[ -z $name || $name == \#* || -z ${asset:-} ]] && continue
  mkdir -p "$hosting/$shard"
  gzip -n -9 -c "$track_dir/$name.trk" >"$hosting/$shard/$asset.trk.gz"
done <"$manifest"
{
  echo "Upload each shard folder as the assets of a GitHub release whose tag is the folder name."
  echo "The game downloads <base>/<shard>/<asset>.trk.gz, where <base> defaults to"
  echo "https://github.com/johnsie/HoverNet/releases/download. For example:"
  echo
  for shard_dir in "$hosting"/*/; do
    shard=$(basename "$shard_dir")
    echo "  gh release create $shard --repo johnsie/HoverNet --title \"Community tracks ($shard)\" \\"
    echo "     --notes \"Community track downloads for HoverNet; fetched automatically by the game.\" \\"
    echo "     $shard_dir*.trk.gz"
    echo
  done
} >"$hosting/UPLOAD.txt"
echo "built hosting files in $hosting ($(find "$hosting" -name '*.trk.gz' | wc -l) tracks; see UPLOAD.txt)"

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
