#!/usr/bin/env bash
# Run from your own machine: installs a hovernet-server package on the race-server host, lets the web client in, starts the server and
# checks the browser path. It copies packaging/server/setup-server.sh and the package to the host, then runs it there with sudo (you are asked
# for the host's sudo password once). Safe to run again: it backs up every file it edits and changes nothing that is already right.
#
#   scripts/setup-server-host.sh --version 2.1.0            # downloads the release's package and checks it against SHA256SUMS
#   scripts/setup-server-host.sh --deb ~/Downloads/hovernet-server_2.1.0_amd64.deb      # a package you already have
#
# Options: --host USER@HOST (default john@192.168.10.181)   --no-apache (leave the web server alone)   --dry-run (print, do nothing)
#
# Use a package built by CI (the GitHub release, or the GitLab job's artifact), not one built on this machine: the host's libraries are older.
set -euo pipefail

host="john@192.168.10.181"; version=""; deb=""; apache_flag=""; dry=no
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host="${2:?}"; shift 2 ;;
    --version) version="${2:?}"; shift 2 ;;
    --deb) deb="${2:?}"; shift 2 ;;
    --no-apache) apache_flag="--no-apache"; shift ;;
    --dry-run) dry=yes; shift ;;
    -h|--help) sed -n 2,12p "$0"; exit 0 ;;
    *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
  esac
done
here="$(cd "$(dirname "$0")" && pwd)"
remote_script="$here/../packaging/server/setup-server.sh"
[ -f "$remote_script" ] || { echo "cannot find $remote_script" >&2; exit 1; }
[ -n "$version" ] || [ -n "$deb" ] || { echo "give --version X.Y.Z or --deb FILE (see --help)" >&2; exit 2; }
run() { echo "+ $*"; [ "$dry" = yes ] || "$@"; }

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
if [ -n "$version" ]; then
  [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must look like 2.1.0" >&2; exit 2; }
  base="https://github.com/johnsie/HoverNet/releases/download/v${version}"
  name="hovernet-server_${version}_amd64.deb"
  echo "Downloading $name and SHA256SUMS from the v${version} release"
  if [ "$dry" = yes ]; then echo "+ curl -fL $base/$name ; curl -fL $base/SHA256SUMS ; sha256sum -c --ignore-missing"; deb="$work/$name"
  else
    curl -fL -o "$work/$name" "$base/$name"
    curl -fL -o "$work/SHA256SUMS" "$base/SHA256SUMS"
    (cd "$work" && sha256sum -c --ignore-missing SHA256SUMS | grep -F "$name: OK") || { echo "the download does not match SHA256SUMS: not installing" >&2; exit 1; }
    deb="$work/$name"
  fi
fi
[ "$dry" = yes ] || [ -f "$deb" ] || { echo "no such file: $deb" >&2; exit 1; }
[ "$dry" = yes ] || dpkg-deb --field "$deb" Package | grep -Fxq hovernet-server || { echo "$deb is not a hovernet-server package" >&2; exit 1; }

stamp="$(date +%Y%m%d-%H%M%S)"; remote_dir="hn-setup-$stamp"
echo "Host: $host   package: $deb"
run ssh "$host" "mkdir -p ~/$remote_dir"
run scp "$remote_script" "$deb" "$host:~/$remote_dir/"
run ssh -t "$host" "sudo bash ~/$remote_dir/setup-server.sh ~/$remote_dir/$(basename "$deb") $apache_flag"
run ssh "$host" "rm -rf ~/$remote_dir"
echo
echo "Finished. Open the web client, choose HOVERNET SERVERS and Connect (the default address is wss://hovernet.outiva.com/auth/)."
