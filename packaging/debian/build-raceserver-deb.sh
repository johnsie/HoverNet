#!/usr/bin/env bash
set -euo pipefail

version="$1"
binary="$2"
package_root="${CI_PROJECT_DIR:-$(pwd)}/dist/debian/hovernet-raceserver_${version}_amd64"

rm -rf "$package_root"
install -Dm755 "$binary" "$package_root/usr/lib/hovernet/RaceServer"
install -Dm644 NetTarget/RaceServer/config.xml "$package_root/etc/hovernet/config.xml"
# Allowlist of community track names the server will relay races for. Operators
# may edit it (it is a conffile, so upgrades keep their changes).
install -Dm644 NetTarget/CommunityTracks.tsv "$package_root/etc/hovernet/CommunityTracks.tsv"
install -Dm644 packaging/debian/hovernet-raceserver.service "$package_root/lib/systemd/system/hovernet-raceserver.service"
install -Dm644 packaging/debian/control "$package_root/DEBIAN/control"
install -Dm755 packaging/debian/postinst "$package_root/DEBIAN/postinst"
install -Dm644 packaging/debian/conffiles "$package_root/DEBIAN/conffiles"

sed -i "s/^Version: .*/Version: ${version}/" "$package_root/DEBIAN/control"
mkdir -p dist
dpkg-deb --build --root-owner-group "$package_root" "dist/hovernet-raceserver_${version}_amd64.deb"