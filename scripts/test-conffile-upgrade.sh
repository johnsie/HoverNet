#!/usr/bin/env bash
# The server package must upgrade without asking anybody a question. dpkg asks (and, with no terminal, FAILS, leaving the package unpacked and the old
# server running) when a package ships a new version of a configuration file the operator has edited. This is how the live server stopped upgrading
# for six releases. The test installs the previous server package's configuration, edits it as an operator does, and upgrades to the current one with no
# terminal: the shipped configuration file must therefore stay byte for byte what the first version shipped (packaging/server/hovernet-server.conf).
#   scripts/test-conffile-upgrade.sh
set -uo pipefail
conf="${HOVERNET_TEST_CONF:-packaging/server/hovernet-server.conf}"
conffiles="packaging/debian/server-conffiles"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
admin="$dir/admin"; root="$dir/root"; mkdir -p "$admin/info" "$admin/updates" "$root"; : > "$admin/status"
dp() { dpkg --admindir="$admin" --instdir="$root" --force-not-root --force-script-chrootless --no-triggers --log="$dir/dpkg.log" "$@" < /dev/null; }
build() { # version, config file to ship
  local d="$dir/pkg-$1"; mkdir -p "$d/DEBIAN" "$d/etc/hovernet"
  printf 'Package: hovernet-server\nVersion: %s\nArchitecture: all\nMaintainer: test <t@example.org>\nDescription: conffile upgrade test\n' "$1" > "$d/DEBIAN/control"
  cp "$conffiles" "$d/DEBIAN/conffiles"; cp "$2" "$d/etc/hovernet/server.conf"
  dpkg-deb --build --root-owner-group "$d" "$dir/hovernet-server_$1.deb" > /dev/null
}
first="$dir/first.conf"
git show d27b404:packaging/server/hovernet-server.conf > "$first" 2>/dev/null || { echo "SKIP: the repository history with the first server package is not available"; exit 77; }
build 1.0 "$first"
dp -i "$dir/hovernet-server_1.0.deb" > "$dir/out1.txt" 2>&1 || { echo "FAIL: the first install failed"; cat "$dir/out1.txt"; exit 1; }
printf '\nws-origin https://example.org\nstatus-port 9612\n' >> "$root/etc/hovernet/server.conf"     # the operator's edits
build 2.0 "$conf"
if dp -i "$dir/hovernet-server_2.0.deb" > "$dir/out2.txt" 2>&1; then
  if grep -q "status-port 9612" "$root/etc/hovernet/server.conf"; then echo "ok   the current package upgrades over an edited configuration without a question, and keeps the edits"; exit 0; fi
  echo "FAIL: the upgrade replaced the operator's configuration"; exit 1
fi
echo "FAIL: upgrading over an edited configuration needs an answer (the pipeline cannot give one):"; tail -6 "$dir/out2.txt"
echo "The packaged configuration must not change: put new defaults in the service file or in code, not in $conf."
exit 1
