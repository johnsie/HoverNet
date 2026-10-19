#!/usr/bin/env bash
# Makes the web client's community-track download work on the web host: copies the sync script, the manifest and the Apache snippet there, fills the
# track folder (about 151 MB, from the GitHub releases), enables the snippet, tests the Apache configuration and reloads Apache, then checks the result
# from here. Run it from your own machine in the repository (it asks for the sudo password of the host once, on the terminal):
#   scripts/deploy-web-community-tracks.sh                      # host john@192.168.10.181
#   scripts/deploy-web-community-tracks.sh john@other-host
# Safe to run again (files already there are kept; the snippet is replaced by the same text). Nothing else on the host is touched, and if Apache's
# configuration test fails the snippet is removed again and Apache is not reloaded.
set -euo pipefail
cd "$(dirname "$0")/.."
host="${1:-john@192.168.10.181}"
site="${HOVERNET_WEB_URL:-https://hovernet.outiva.com}"
stage="/tmp/hovernet-community-tracks"

echo "-- copying to $host:$stage"
ssh "$host" "rm -rf '$stage' && mkdir -p '$stage'"
scp -q scripts/sync-web-community-tracks.sh NetTarget/CommunityTracks.tsv packaging/web/community-tracks.conf "$host:$stage/"

echo "-- on the host (sudo password, once)"
ssh -t "$host" "sudo bash -c '
  set -euo pipefail
  bash $stage/sync-web-community-tracks.sh $stage/CommunityTracks.tsv /var/www/hovernet.outiva.com/community-tracks
  install -m 644 $stage/community-tracks.conf /etc/apache2/conf-available/hovernet-community-tracks.conf
  a2enconf hovernet-community-tracks >/dev/null
  if apache2ctl configtest; then
    systemctl reload apache2
    echo \"apache reloaded\"
  else
    a2disconf hovernet-community-tracks >/dev/null
    rm -f /etc/apache2/conf-available/hovernet-community-tracks.conf
    echo \"STOP: the Apache configuration test failed; the snippet was removed again and Apache was not reloaded\" >&2
    exit 1
  fi
'"
ssh "$host" "rm -rf '$stage'"

echo "-- checking from here"
asset="community-tracks-2/ca0e33693215c782.trk.gz"   # ZRT's Nightmare!!!
headers="$(curl -s -I -m 30 "$site/community-tracks/$asset")"
echo "$headers" | head -1
echo "$headers" | grep -qi '^HTTP/[0-9.]* 200' || { echo "FAIL: $site/community-tracks/$asset did not answer 200" >&2; exit 1; }
if echo "$headers" | grep -qi '^content-encoding: *\(x-\)\?gzip'; then echo "FAIL: the file is served with Content-Encoding gzip: the browser would unpack it" >&2; exit 1; fi
size="$(curl -s -m 60 "$site/community-tracks/$asset" | wc -c)"
[ "$size" = "109097" ] || { echo "FAIL: the file is $size bytes, expected 109097" >&2; exit 1; }
echo "ok: $site/community-tracks/ serves the tracks (plain gzip, 109097 bytes for the check file)"
echo "Now release a web build that downloads from there (this fix is not in the web client that is live now)."
