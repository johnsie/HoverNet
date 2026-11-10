#!/usr/bin/env bash
# Copies the web deploy script to the web host and installs it (needed before CI_VARIABLE HOVERNET_WEB_DEFERRED_MUSIC=ON; docs/operators.md).
# Run from this computer:  scripts/update-web-host.sh            (host defaults to john@192.168.10.181; HOVERNET_WEB_HOST=user@host overrides)
# It asks for the host's sudo password once. It does NOT replace the Apache site file (that one holds the host's TLS settings): the last lines show
# how to compare it with the repository's copy.
set -euo pipefail
HOST="${HOVERNET_WEB_HOST:-john@192.168.10.181}"
here="$(cd "$(dirname "$0")/.." && pwd)"
scp "$here/packaging/web/hovernet-web-deploy" "$here/packaging/web/hovernet.outiva.com.conf" "$HOST:/tmp/"
ssh -t "$HOST" 'set -e
  sudo install -o root -g root -m 0755 /tmp/hovernet-web-deploy /usr/local/sbin/hovernet-web-deploy
  python3 -m py_compile /usr/local/sbin/hovernet-web-deploy && echo "deploy script installed"
  grep -n "OPTIONAL" /usr/local/sbin/hovernet-web-deploy | head -2
  echo; echo "Apache: differences between the live site file and the repository copy (review before copying anything):"
  diff /etc/apache2/sites-available/hovernet.outiva.com.conf /tmp/hovernet.outiva.com.conf || true
  rm -f /tmp/hovernet-web-deploy'
echo
echo "Then, to enable compression yourself after reviewing the diff, on the host:"
echo "  sudo a2enmod deflate && sudo cp /tmp/hovernet.outiva.com.conf /etc/apache2/sites-available/hovernet.outiva.com.conf && sudo apache2ctl configtest && sudo systemctl reload apache2"
echo "Last, set the GitLab CI variable HOVERNET_WEB_DEFERRED_MUSIC=ON (Settings > CI/CD > Variables)."
