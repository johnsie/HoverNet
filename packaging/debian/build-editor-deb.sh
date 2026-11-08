#!/usr/bin/env bash
# Builds the hovernet-editor package: the track editor, the compiler and validator it runs, the track backgrounds the compiler reads, the bundled track sources as
# examples, and a launcher. It needs the hovernet-game package (the game it playtests on, the object plug-in, the fonts), which it declares as a dependency.
#   packaging/debian/build-editor-deb.sh VERSION [BUILD_DIR]        BUILD_DIR defaults to build/linux
set -euo pipefail

version="${1#v}"
# shellcheck source=packaging/debian/reproducible.sh
source "$(dirname "$0")/reproducible.sh"
file_version="$version"
version="$(debian_version "$version")"
build="${2:-build/linux}"
architecture="$(dpkg --print-architecture)"
package_root="${CI_PROJECT_DIR:-$(pwd)}/dist/debian/hovernet-editor_${version}_${architecture}"

case "$version" in
  ""|*[!0-9A-Za-z.+:~\-]*) echo "Invalid Debian package version: $version" >&2; exit 2;;
esac
for f in HoverNetTrackEditor HoverNetMazeCompiler HoverNetTrackValidator; do test -x "$build/$f" || { echo "missing $build/$f" >&2; exit 1; }; done
test -f NetTarget/mazes/BG-CITY.pcx
test -d NetTarget/TrackSources

rm -rf "$package_root"
for f in HoverNetTrackEditor HoverNetMazeCompiler HoverNetTrackValidator; do
  install -Dm755 "$build/$f" "$package_root/usr/lib/hovernet/$f"
done
install -Dm755 packaging/debian/hovernet-editor "$package_root/usr/games/hovernet-editor"
install -Dm644 packaging/debian/hovernet-editor.desktop "$package_root/usr/share/applications/hovernet-editor.desktop"
# the pictures a track source names as its background (the compiler reads them relative to the data folder)
for picture in NetTarget/mazes/*.pcx; do
  install -Dm644 "$picture" "$package_root/usr/share/games/hovernet/NetTarget/mazes/$(basename "$picture")"
done
# the bundled track sources, to open and learn from
for source in NetTarget/TrackSources/*.track.txt; do
  install -Dm644 "$source" "$package_root/usr/share/games/hovernet/NetTarget/TrackSources/$(basename "$source")"
done
install -Dm644 docs/track-authoring.md "$package_root/usr/share/doc/hovernet-editor/track-authoring.md"
install -Dm644 packaging/debian/editor-control "$package_root/DEBIAN/control"
sed -i "s/^Version: .*/Version: ${version}/; s/^Architecture: .*/Architecture: ${architecture}/" "$package_root/DEBIAN/control"
install -Dm644 LICENSE "$package_root/usr/share/doc/hovernet-editor/copyright"
install -Dm644 THIRD_PARTY_NOTICES.md "$package_root/usr/share/doc/hovernet-editor/THIRD_PARTY_NOTICES.md"
install -Dm644 CHANGELOG.md "$package_root/usr/share/doc/hovernet-editor/CHANGELOG.md"
normalize_package_tree "$package_root"
mkdir -p dist
dpkg-deb --build --root-owner-group "$package_root" "dist/hovernet-editor_${file_version}_${architecture}.deb"
