#!/usr/bin/env bash
# Lets a HoverNet server host community tracks: copies the sync script and the community manifest to the server, fills the folder the packaged service
# reads (/var/lib/hovernet/community-tracks; about 700 MB from the GitHub releases, every file checked against the manifest's size and SHA-256) and reports
# whether the running service is set to read it. Run it from your own machine in the repository (it asks for the sudo password of the server once):
#   scripts/deploy-server-community-tracks.sh                  # server john@192.168.10.181
#   scripts/deploy-server-community-tracks.sh john@other-host
# Safe to run again (what is already right is kept; new tracks are fetched). The server does not need a restart for new files, but the service only reads
# the folder once it has the --community-dir option, which comes with the hovernet-server package of this version (the tag deploy installs it).
set -euo pipefail
cd "$(dirname "$0")/.."
host="${1:-john@192.168.10.181}"
stage="/tmp/hovernet-server-community-tracks"

echo "-- copying to $host:$stage"
ssh "$host" "rm -rf '$stage' && mkdir -p '$stage'"
scp -q scripts/sync-server-community-tracks.sh NetTarget/CommunityTracks.tsv "$host:$stage/"

echo "-- on the server (sudo password, once; this takes a few minutes the first time)"
ssh -t "$host" "sudo bash -c '
  set -euo pipefail
  dest=/var/lib/hovernet/community-tracks
  if id hovernet >/dev/null 2>&1; then install -d -m 755 -o hovernet -g hovernet \$dest; else install -d -m 755 \$dest; fi
  bash $stage/sync-server-community-tracks.sh $stage/CommunityTracks.tsv \$dest
  if id hovernet >/dev/null 2>&1; then chown -R hovernet:hovernet \$dest; fi
  echo \"files in \$dest: \$(find \$dest -name \"*.trk\" | wc -l), \$(du -sh \$dest | cut -f1)\"
'"
echo "-- the service"
ssh "$host" "rm -rf '$stage'; if systemctl cat hovernet-server >/dev/null 2>&1; then
  if systemctl cat hovernet-server | grep -q -- '--community-dir'; then echo 'ok: hovernet-server is set to read the community folder'; else echo 'NOTE: the installed hovernet-server does not have --community-dir yet; it arrives with the package of this version (the tag deploy), no further step needed'; fi
  systemctl is-active hovernet-server
else echo 'NOTE: no hovernet-server service on this host'; fi"
