#!/usr/bin/env bash
# Fills the folder the web client downloads community tracks from (https://hovernet.outiva.com/community-tracks/<shard>/<asset>.trk.gz).
# A browser cannot download them from GitHub's releases (the downloads redirect to another host and send no CORS header), so the web host serves its own
# copy: the same gzip files, named by hash, that the native game downloads from GitHub. Safe to run again: files that are already there with the right
# size are kept, a new track in the manifest is fetched, nothing is deleted.
#
# Run it ON THE WEB HOST as root (the folder is next to the web releases, not inside one, so a new web release does not lose it):
#   sudo scripts/sync-web-community-tracks.sh [manifest] [folder]
#   defaults: NetTarget/CommunityTracks.tsv   /var/www/hovernet.outiva.com/community-tracks
# About 151 MB for the 1,009 tracks. Override the source with HOVERNET_TRACK_DOWNLOAD_URL (default: the GitHub releases).
set -euo pipefail
cd "$(dirname "$0")/.."
manifest="${1:-NetTarget/CommunityTracks.tsv}"
dest="${2:-/var/www/hovernet.outiva.com/community-tracks}"
base="${HOVERNET_TRACK_DOWNLOAD_URL:-https://github.com/johnsie/HoverNet/releases/download}"; base="${base%/}"
[ -f "$manifest" ] || { echo "STOP: no manifest at $manifest" >&2; exit 1; }
mkdir -p "$dest"
have=0; got=0; bad=0
while IFS=$'\t' read -r name _mode _starts _rooms _warn _bytes _sha shard asset gz _rest; do
  case "$name" in ''|'#'*) continue ;; esac
  [[ "$shard" =~ ^community-tracks-[0-9]+$ && "$asset" =~ ^[0-9a-f]{16,64}$ && "$gz" =~ ^[0-9]+$ ]] || { echo "skip (bad manifest line): $name" >&2; bad=$((bad+1)); continue; }
  file="$dest/$shard/$asset.trk.gz"
  if [ -f "$file" ] && [ "$(stat -c %s "$file")" = "$gz" ]; then have=$((have+1)); continue; fi
  mkdir -p "$dest/$shard"
  tmp="$(mktemp "$dest/.part.XXXXXX")"
  if curl --fail --location --silent --show-error --retry 3 --connect-timeout 20 --max-time 300 --output "$tmp" "$base/$shard/$asset.trk.gz" \
     && [ "$(stat -c %s "$tmp")" = "$gz" ] && gzip -t "$tmp" 2>/dev/null; then
    chmod 644 "$tmp"; mv "$tmp" "$file"; got=$((got+1))
  else
    rm -f "$tmp"; echo "FAILED: $name ($shard/$asset)" >&2; bad=$((bad+1))
  fi
done < "$manifest"
find "$dest" -type d -exec chmod 755 {} +
echo "community tracks for the web client: $have already there, $got fetched, $bad problems ($dest)"
[ "$bad" -eq 0 ]
