#!/usr/bin/env bash
# Builds the hovernet-server package (the 3.0 authoritative server) from a build directory holding HoverNetAuthServer, HoverNetModTool and
# HoverNetReplay.   packaging/debian/build-authserver-deb.sh VERSION BUILD_DIR
set -euo pipefail

version="$1"
build_dir="$2"
# shellcheck source=packaging/debian/reproducible.sh
source "$(dirname "$0")/reproducible.sh"
file_version="$version"
version="$(debian_version "$version")"
package_root="${CI_PROJECT_DIR:-$(pwd)}/dist/debian/hovernet-server_${version}_amd64"

rm -rf "$package_root"
install -Dm755 "$build_dir/HoverNetAuthServer" "$package_root/usr/bin/HoverNetAuthServer"
install -Dm755 "$build_dir/HoverNetModTool" "$package_root/usr/bin/HoverNetModTool"
install -Dm755 "$build_dir/HoverNetReplay" "$package_root/usr/bin/HoverNetReplay"
# The server reads the official tracks relative to its working directory (/usr/share/hovernet in the unit file).
for track in NetTarget/Tracks/*.trk; do
  install -Dm644 "$track" "$package_root/usr/share/hovernet/NetTarget/Tracks/$(basename "$track")"
done
install -Dm644 packaging/server/hovernet-server.conf "$package_root/etc/hovernet/server.conf"
install -Dm644 packaging/server/hovernet-server.service "$package_root/lib/systemd/system/hovernet-server.service"
install -Dm644 packaging/debian/server-control "$package_root/DEBIAN/control"
install -Dm755 packaging/debian/server-postinst "$package_root/DEBIAN/postinst"
install -Dm755 packaging/debian/server-prerm "$package_root/DEBIAN/prerm"
install -Dm644 packaging/debian/server-conffiles "$package_root/DEBIAN/conffiles"
sed -i "s/^Version: .*/Version: ${version}/" "$package_root/DEBIAN/control"
install -Dm644 docs/operators.md "$package_root/usr/share/doc/hovernet-server/operators.md"
install -Dm644 LICENSE "$package_root/usr/share/doc/hovernet-server/copyright"
install -Dm644 THIRD_PARTY_NOTICES.md "$package_root/usr/share/doc/hovernet-server/THIRD_PARTY_NOTICES.md"
install -Dm644 CHANGELOG.md "$package_root/usr/share/doc/hovernet-server/CHANGELOG.md"
normalize_package_tree "$package_root"
mkdir -p dist
dpkg-deb --build --root-owner-group "$package_root" "dist/hovernet-server_${file_version}_amd64.deb"
