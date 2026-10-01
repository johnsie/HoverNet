#!/usr/bin/env bash
# Installs the community track pack for the current user (no root needed).
#
#   scripts/install-community-tracks.sh PACK.zip|TRACK_DIR
#
# The game looks for tracks in $XDG_CONFIG_HOME/hovernet/CommunityTracks
# (default ~/.config/hovernet/CommunityTracks), beside the game in
# NetTarget/CommunityTracks, or in the hovernet-community-tracks package.
set -euo pipefail

source=${1:?pack zip or a folder of .trk files required}
target="${XDG_CONFIG_HOME:-$HOME/.config}/hovernet/CommunityTracks"
mkdir -p "$target"

if [[ -d $source ]]; then
  cp -p "$source"/*.trk "$target"/
else
  work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
  unzip -q "$source" -d "$work"
  tracks=$(find "$work" -type d -name CommunityTracks | head -n1)
  [[ -n $tracks ]] || tracks=$(find "$work" -type d -name tracks | head -n1)
  [[ -n $tracks ]] || { echo "no CommunityTracks (or tracks) folder found inside $source" >&2; exit 1; }
  cp -p "$tracks"/*.trk "$target"/
fi
echo "installed $(find "$target" -maxdepth 1 -name '*.trk' | wc -l) community tracks into $target"
