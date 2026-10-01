#!/usr/bin/env bash
set -euo pipefail

version="${1#v}"
tracks="${2:?directory of .trk files required}"
out_dir="${3:-dist}"
package_root="$(mktemp -d)/hovernet-community-tracks_${version}_all"
trap 'rm -rf "$(dirname "$package_root")"' EXIT

mkdir -p "$package_root/usr/share/games/hovernet/NetTarget/CommunityTracks"
for track in "$tracks"/*.trk; do
  install -Dm644 "$track" "$package_root/usr/share/games/hovernet/NetTarget/CommunityTracks/$(basename "$track")"
done
install -Dm644 docs/community-tracks-pack-readme.txt "$package_root/usr/share/doc/hovernet-community-tracks/README"
install -Dm644 packaging/debian/community-tracks-control "$package_root/DEBIAN/control"
sed -i "s/^Version: .*/Version: ${version}/" "$package_root/DEBIAN/control"
mkdir -p "$out_dir"
dpkg-deb --build --root-owner-group "$package_root" "$out_dir/hovernet-community-tracks_${version}_all.deb"
