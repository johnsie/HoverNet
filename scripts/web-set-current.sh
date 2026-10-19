#!/usr/bin/env bash
# Points the live web client (https://hovernet.outiva.com) at one of the releases already installed on the web host, the way
# /usr/local/sbin/hovernet-web-deploy does it: the symlink is replaced atomically, nothing is copied, nothing is deleted.
# Use it when a stale build became "current" (a late CI job for an old commit deployed after the newer release).
#
# Run it ON THE WEB HOST (192.168.10.181) as root:
#   sudo scripts/web-set-current.sh            # lists the installed releases and what is current
#   sudo scripts/web-set-current.sh v2.3.20    # makes v2.3.20 current
# From your own machine (copy it first: sudo needs the terminal to ask for the password, so the script cannot be piped in on stdin):
#   scp scripts/web-set-current.sh john@192.168.10.181:/tmp/
#   ssh -t john@192.168.10.181 'sudo bash /tmp/web-set-current.sh v2.3.20'
set -euo pipefail

root="/var/www/hovernet.outiva.com"
releases="$root/releases"
[ "$(id -u)" -eq 0 ] || { echo "STOP: run this as root (sudo)." >&2; exit 1; }
[ -d "$releases" ] || { echo "STOP: $releases does not exist: is this the web host?" >&2; exit 1; }

echo "current: $(readlink "$root/current" 2>/dev/null || echo none)"
echo "installed releases (newest first):"
ls -1t --time-style=long-iso -l "$releases" | awk 'NR>1 && $NF !~ /^\.staging-/ {print "  " $NF "   (" $6 " " $7 ")"}'

want="${1:-}"
[ -n "$want" ] || { echo; echo "Say which one: $0 <release name, for example v2.3.20>"; exit 0; }
[[ "$want" =~ ^[A-Za-z0-9._+-]{1,100}$ ]] || { echo "STOP: '$want' is not a release name." >&2; exit 1; }
dir="$releases/$want"
[ -d "$dir" ] || { echo "STOP: no installed release called $want." >&2; exit 1; }
for f in index.html HoverNet.js HoverNet.wasm HoverNet.data; do
  [ -s "$dir/$f" ] || { echo "STOP: $dir/$f is missing or empty: not a complete web build." >&2; exit 1; }
done

ln -sfn "$dir" "$root/.current-next"
mv -T "$root/.current-next" "$root/current"      # atomic: the site never has a moment without a build
echo
echo "current is now: $(readlink "$root/current")"
echo "check: curl -sI https://hovernet.outiva.com/HoverNet.wasm | grep -i last-modified   (and reload with Ctrl+Shift+R)"
