#!/usr/bin/env bash
# Fills the folder a HoverNet server hosts community tracks from (HoverNetAuthServer --community-dir): one "<track name>.trk" file per track of the
# community manifest, as the game keeps them. Every file is downloaded from the GitHub releases, unpacked and checked against the manifest's size and SHA-256
# before it is installed, so a damaged or altered download never becomes a track. Safe to run again: files that are already right are kept, new tracks are
# fetched, nothing is deleted. About 700 MB when complete.
#
# Run it ON THE SERVER as root (or the user that owns the folder):
#   sudo scripts/sync-server-community-tracks.sh [manifest] [folder]
#   defaults: NetTarget/CommunityTracks.tsv   /var/lib/hovernet/community-tracks   (the folder the packaged service reads)
# HOVERNET_TRACK_DOWNLOAD_URL overrides the source (default: the GitHub releases).
set -euo pipefail
cd "$(dirname "$0")/.."
manifest="${1:-NetTarget/CommunityTracks.tsv}"
dest="${2:-/var/lib/hovernet/community-tracks}"
base="${HOVERNET_TRACK_DOWNLOAD_URL:-https://github.com/johnsie/HoverNet/releases/download}"; base="${base%/}"
[ -f "$manifest" ] || { echo "STOP: no manifest at $manifest" >&2; exit 1; }
for tool in curl gzip sha256sum; do command -v "$tool" >/dev/null || { echo "STOP: $tool is needed" >&2; exit 1; }; done
mkdir -p "$dest"
have=0; got=0; bad=0
while IFS=$'\t' read -r name _mode _starts _rooms _warn bytes sha shard asset gz _rest; do
  case "$name" in ''|'#'*) continue ;; esac
  if [[ "$name" == */* || "$name" == *\\* || "$name" == .* || ${#name} -gt 63 || ! "$sha" =~ ^[0-9a-f]{64}$ || ! "$shard" =~ ^community-tracks-[0-9]+$ || ! "$asset" =~ ^[0-9a-f]{16,64}$ || ! "$bytes" =~ ^[0-9]+$ ]]; then
    echo "skip (bad manifest line): $name" >&2; bad=$((bad+1)); continue
  fi
  file="$dest/$name.trk"
  if [ -f "$file" ] && [ "$(stat -c %s "$file")" = "$bytes" ] && [ "$(sha256sum "$file" | cut -d' ' -f1)" = "$sha" ]; then have=$((have+1)); continue; fi
  tmp="$(mktemp "$dest/.part.XXXXXX")"
  if curl --fail --location --silent --show-error --retry 3 --connect-timeout 20 --max-time 300 "$base/$shard/$asset.trk.gz" | gzip -dc > "$tmp" 2>/dev/null \
     && [ "$(stat -c %s "$tmp")" = "$bytes" ] && [ "$(sha256sum "$tmp" | cut -d' ' -f1)" = "$sha" ]; then
    chmod 644 "$tmp"; mv "$tmp" "$file"; got=$((got+1))
  else
    rm -f "$tmp"; echo "FAILED: $name" >&2; bad=$((bad+1))
  fi
done < "$manifest"
echo "community tracks for the server: $have already right, $got fetched, $bad problems ($dest)"
[ "$bad" -eq 0 ]
