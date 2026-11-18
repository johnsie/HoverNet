#!/usr/bin/env bash
# Replace ONLY the Apache site file of hovernet.outiva.com on the web host, from this machine (not the whole of setup-host.sh: no packages, no new certificate).
#   scripts/push-web-conf.sh            show the difference, ask, then install and reload
#   scripts/push-web-conf.sh --yes      no question (still backs up, tests, and restores on a failed test)
# Needs: ssh key access to the host (default john@192.168.10.181) and sudo there (you type the password once; ssh -t gives it a terminal).
# Afterwards scripts/check-web-deploy.sh https://hovernet.outiva.com should show "HoverNet.data is compressed" and a 304 for the wasm.
set -Eeuo pipefail

HOST="${HOVERNET_WEB_HOST:-john@192.168.10.181}"
SITE="hovernet.outiva.com"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SRC="${HERE}/../packaging/web/${SITE}.conf"
LIVE="/etc/apache2/sites-available/${SITE}.conf"
STAMP="$(date +%Y%m%d-%H%M%S)"
ASSUME_YES=0
[ "${1:-}" = "--yes" ] && ASSUME_YES=1

[ -f "$SRC" ] || { echo "missing $SRC" >&2; exit 1; }

echo "== difference: live ($HOST:$LIVE) -> this checkout (lines starting - are on the host now, + are what will be installed)"
ssh "$HOST" "cat '$LIVE'" | diff -u --label "live" --label "repo" - "$SRC" || true
echo "=="
if [ "$ASSUME_YES" -ne 1 ]; then
  read -r -p "Install this on $HOST and reload Apache? [y/N] " answer
  [ "$answer" = "y" ] || [ "$answer" = "Y" ] || { echo "nothing changed"; exit 0; }
fi

scp -q "$SRC" "${HOST}:/tmp/${SITE}.conf.new"
# One sudo session: back up, install, test, restore on a failed test, reload only after a good test.
ssh -t "$HOST" "sudo bash -c '
set -e
cp -p \"$LIVE\" \"$LIVE.bak-$STAMP\"
install -o root -g root -m 0644 /tmp/${SITE}.conf.new \"$LIVE\"
if apache2ctl configtest; then
  systemctl reload apache2
  echo \"reloaded; the previous file is $LIVE.bak-$STAMP\"
else
  echo \"configtest FAILED: putting the previous file back, nothing reloaded\" >&2
  cp -p \"$LIVE.bak-$STAMP\" \"$LIVE\"
  exit 1
fi
rm -f /tmp/${SITE}.conf.new
'"

echo "== checking the public site"
"${HERE}/check-web-deploy.sh" "https://${SITE}" || true
