#!/usr/bin/env bash
set -euo pipefail

version="${1#v}"
binary="${2:-build/linux/HoverNetGame2Player}"
plugin="${3:-build/linux/ObjFac1.so}"
architecture="$(dpkg --print-architecture)"
package_root="${CI_PROJECT_DIR:-$(pwd)}/dist/debian/hovernet-game_${version}_${architecture}"

case "$version" in
  ""|*[!0-9A-Za-z.+:~\-]*)
    echo "Invalid Debian package version: $version" >&2
    exit 2
    ;;
esac

test -x "$binary"
test -f "$plugin"
test -f NetTarget/ObjFac1.dat
test -d NetTarget/Tracks

rm -rf "$package_root"
install -Dm755 "$binary" "$package_root/usr/lib/hovernet/HoverNetGame2Player"
install -Dm755 "$plugin" "$package_root/usr/lib/hovernet/ObjFac1.so"
install -Dm755 packaging/debian/hovernet-game "$package_root/usr/games/hovernet"
install -Dm644 packaging/debian/hovernet-game.desktop "$package_root/usr/share/applications/hovernet.desktop"
install -Dm644 NetTarget/ObjFac1.dat "$package_root/usr/share/games/hovernet/NetTarget/ObjFac1.dat"
for track in NetTarget/Tracks/*.trk; do
  install -Dm644 "$track" "$package_root/usr/share/games/hovernet/NetTarget/Tracks/$(basename "$track")"
done
install -Dm644 packaging/debian/game-control "$package_root/DEBIAN/control"
sed -i "s/^Version: .*/Version: ${version}/; s/^Architecture: .*/Architecture: ${architecture}/" "$package_root/DEBIAN/control"
mkdir -p dist
dpkg-deb --build --root-owner-group "$package_root" "dist/hovernet-game_${version}_${architecture}.deb"
