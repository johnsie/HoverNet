#!/usr/bin/env bash
# Puts the Track Market's listed tracks on a race server and takes revoked or unlisted ones off, so every listed track can be hosted online
# (docs/specs/track-market.md, section 5). The server looks for "<track name>.trk" in its community folder each time someone hosts a race, so no restart is needed.
#
#   sync-market-tracks.sh [--catalogue FILE|URL] [--assets DIR|URL] [--dest DIR]
#   defaults: the catalogue the market service writes, the published files next to it, and the folder the packaged server reads:
#     /var/www/hovernet.outiva.com/market-tracks/catalogue.tsv   /var/www/hovernet.outiva.com/market-tracks   /var/lib/hovernet/community-tracks
#
# Safe to run again and every ten minutes (hovernet-market-sync.timer): every file is unpacked and checked against the catalogue's size and SHA-256 before it is
# installed; a track that fails is skipped and reported; it never touches a file it did not install (the 1,012 inherited community tracks live in the same folder,
# and a market track may not take one of their names); only tracks it installed are removed, and only when they left the catalogue's listed set.
set -euo pipefail
catalogue="/var/www/hovernet.outiva.com/market-tracks/catalogue.tsv"; assets="/var/www/hovernet.outiva.com/market-tracks"; dest="/var/lib/hovernet/community-tracks"
while [ $# -gt 0 ]; do
  case "$1" in --catalogue) catalogue="${2:?}"; shift 2;; --assets) assets="${2:?}"; shift 2;; --dest) dest="${2:?}"; shift 2;; *) echo "unknown option $1" >&2; exit 2;; esac
done
for tool in gzip sha256sum flock; do command -v "$tool" >/dev/null || { echo "STOP: $tool is needed" >&2; exit 1; }; done
fetch() { # source -> stdout
  case "$1" in http://*|https://*) curl --fail --location --silent --show-error --retry 3 --connect-timeout 20 --max-time 300 "$1";; *) cat -- "$1";; esac
}
mkdir -p "$dest"
exec 9> "$dest/.market-sync.lock"; flock -n 9 || { echo "another sync is running"; exit 0; }
state="$dest/.market-installed"; touch "$state"
cat_file="$(mktemp "$dest/.catalogue.XXXXXX")"; trap 'rm -f "$cat_file" "$dest"/.part.* 2>/dev/null || true' EXIT
fetch "$catalogue" > "$cat_file" || { echo "STOP: cannot read the catalogue $catalogue (nothing was changed)" >&2; exit 1; }
grep -q '^# catalogue-version ' "$cat_file" || { echo "STOP: $catalogue is not a Track Market catalogue (nothing was changed)" >&2; exit 1; }
name_ok='^[A-Za-z0-9][A-Za-z0-9 ._'"'"'-]{1,38}[A-Za-z0-9]$'
have=0; got=0; removed=0; bad=0
declare -A listed=()      # name -> sha, for every valid listed row
declare -A failed=()      # names whose download failed this time: what is installed stays as it is
declare -A installed=()   # name -> sha, what this script put there before
while IFS=$'\t' read -r n s; do [ -n "${n:-}" ] && installed["$n"]="$s"; done < "$state"
while IFS=$'\t' read -r name _mode _starts _rooms _warn bytes sha shard asset _gz _author _licence _listed _version status; do
  case "$name" in ''|'#'*) continue;; esac
  [ "$status" = "listed" ] || continue
  if [[ ! "$name" =~ $name_ok || ! "$sha" =~ ^[0-9a-f]{64}$ || "$asset" != "$sha" || "$shard" != "market" || ! "$bytes" =~ ^[0-9]+$ || "$bytes" -gt 4000000 ]]; then
    echo "skip (bad catalogue line): $name" >&2; bad=$((bad+1)); continue
  fi
  file="$dest/$name.trk"
  if [ -f "$file" ] && [ "${installed[$name]:-}" = "" ]; then echo "skip: $name.trk is already there and is not a market track" >&2; bad=$((bad+1)); continue; fi
  listed["$name"]="$sha"
  if [ -f "$file" ] && [ "${installed[$name]:-}" = "$sha" ] && [ "$(stat -c %s "$file")" = "$bytes" ] && [ "$(sha256sum "$file" | cut -d' ' -f1)" = "$sha" ]; then have=$((have+1)); continue; fi
  tmp="$(mktemp "$dest/.part.XXXXXX")"
  if fetch "$assets/$shard/$asset.trk.gz" 2>/dev/null | gzip -dc > "$tmp" 2>/dev/null && [ "$(stat -c %s "$tmp")" = "$bytes" ] && [ "$(sha256sum "$tmp" | cut -d' ' -f1)" = "$sha" ]; then
    chmod 644 "$tmp"; mv "$tmp" "$file"; installed["$name"]="$sha"; got=$((got+1))
  else
    rm -f "$tmp"; echo "FAILED: $name (the download did not match the catalogue)" >&2; bad=$((bad+1)); failed["$name"]=1; unset 'listed[$name]'
  fi
done < "$cat_file"
# take off what this script installed and the catalogue no longer lists (unlisted, revoked, or replaced by a new version)
for n in "${!installed[@]}"; do
  s="${installed[$n]}"
  [ -z "${failed[$n]+x}" ] || continue
  if [ "${listed[$n]:-}" != "$s" ]; then
    f="$dest/$n.trk"
    if [ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$s" ]; then rm -f "$f"; removed=$((removed+1)); fi
    unset 'installed[$n]'
  fi
done
tmp_state="$(mktemp "$dest/.part.XXXXXX")"
for n in "${!installed[@]}"; do printf '%s\t%s\n' "$n" "${installed[$n]}"; done | sort > "$tmp_state"; mv "$tmp_state" "$state"
echo "market tracks for the server: $have already right, $got fetched, $removed removed, $bad problems ($dest)"
[ "$bad" -eq 0 ]
