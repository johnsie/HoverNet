#!/usr/bin/env bash
# Everything the owner can do from this computer, in one command (run it from the repository):
#
#     bash scripts/owner/owner-setup.sh
#
# It: (1) checks the release is live (read-only, over ssh to john@192.168.10.181); (2) makes the production signing key if you do not have one yet (never
# overwrites), and saves the public key beside it; (3) packs the legal-review documents into one file with a cover note; (4) shows what is not pushed.
# Options (all optional):
#   --backup-to DIR   also copy the secret key to DIR (a USB stick) and check the copy matches
#   --push            then push main to GitLab (deploys the web client) and GitHub, after you type "push" to confirm
#   --version X.Y.Z   the release to check (default 2.3.26)
# Environment: HOVERNET_KEYS_DIR (default ~/hovernet-keys), RACE_HOST (default john@192.168.10.181).
# Safe to run again: each step skips what is already done.
set -uo pipefail
cd "$(dirname "$0")/../.."
backup=""; push=no; version="2.3.26"
while [ $# -gt 0 ]; do
  case "$1" in
    --backup-to) backup="${2:?--backup-to needs a folder}"; shift 2;;
    --push) push=yes; shift;;
    --version) version="${2:?}"; shift 2;;
    *) echo "unknown option: $1"; exit 2;;
  esac
done
keys="${HOVERNET_KEYS_DIR:-$HOME/hovernet-keys}"; key="$keys/hovernet-catalogue.key"; pub="$keys/hovernet-catalogue.pub"
todo=()
line() { printf '\n==== %s\n' "$*"; }

line "1/4 Is release $version live?"
if bash scripts/owner/check-production.sh "$version"; then :; else todo+=("A production check failed: read the FAIL lines above (a GitLab pipeline may still be running: https://gitlab.outiva.com/devteam/hovernet/-/pipelines)"); fi

line "2/4 Signing key"
if [ -f "$key" ]; then
  echo "A key already exists at $key: leaving it alone."
  [ -f "$pub" ] || echo "(no public-key file beside it: the public key can be printed with the signing tool's  pubkey  command)"
else
  if bash scripts/owner/make-signing-key.sh "$key" > "$keys.log" 2>&1; then
    pubkey="$(grep -E '^\s+[0-9a-f]{64}\s*$' "$keys.log" | tr -d ' ' | head -1)"
    [ -n "$pubkey" ] && { printf '%s\n' "$pubkey" > "$pub"; chmod 644 "$pub"; }
    rm -f "$keys.log"
    echo "Made the secret key: $key"
  else
    cat "$keys.log"; rm -f "$keys.log"; todo+=("The signing key could not be made: see the message above"); 
  fi
fi
if [ -f "$key" ]; then
  [ -f "$pub" ] && echo "Public key (safe to share, 64 characters): $(cat "$pub")" && echo "  saved in $pub"
  if [ -n "$backup" ]; then
    mkdir -p "$backup" && cp "$key" "$backup/hovernet-catalogue.key" && chmod 600 "$backup/hovernet-catalogue.key"
    if [ "$(sha256sum < "$key")" = "$(sha256sum < "$backup/hovernet-catalogue.key")" ]; then echo "Backup copied to $backup and the copy matches."; else todo+=("The backup in $backup does NOT match the key: copy it again"); fi
  else todo+=("BACK UP the secret key offline now (a USB stick kept away from this computer): rerun with  --backup-to /path/to/usb  or copy $key yourself")
  fi
  todo+=("Send me ONLY the public key (the 64 characters above, or the file $pub). Never the secret key.")
fi

line "3/4 Legal review pack"
mkdir -p dist
pack="dist/hovernet-legal-review-pack"; rm -rf "$pack"; mkdir -p "$pack"
for d in docs/track-market-terms-draft.md docs/adr/0002-content-licence-policy.md docs/adr/0004-track-marketplace.md docs/account-privacy-policy-draft.md docs/specs/track-market.md; do cp "$d" "$pack/"; done
cat > "$pack/COVER-NOTE.txt" <<'EOT'
Subject: HoverNet Track Market: please review the draft terms before we open it to outside authors

HoverNet is a free hovercraft racing game. We are adding a Track Market where players publish tracks (small data files made with our editor) for all players to download.
Before we let anyone outside a small invited group publish, we would like your review of the attached draft.

Start with track-market-terms-draft.md: the terms of use, the author declaration, the licence choice and the takedown procedure. The questions we most need answered
are listed at its end. The other files are background: our licence and authorship policy (0002), the market's design (0004 and track-market.md) and the draft
account privacy policy. The draft is ours, not legal advice, and is not in force.

Where players and servers are: [OWNER: countries]. The service is run by: [OWNER: name].
EOT
tar -C dist -czf dist/hovernet-legal-review-pack.tar.gz hovernet-legal-review-pack && echo "Made dist/hovernet-legal-review-pack.tar.gz (documents + a cover note you can paste into an email)."
todo+=("Fill in the [OWNER: ...] placeholders in $pack/COVER-NOTE.txt and docs/track-market-terms-draft.md, then send dist/hovernet-legal-review-pack.tar.gz to your lawyer")
todo+=("Make the list of Outiva account names you will invite to publish first (one per line) and keep it for me")

line "4/4 Work not pushed yet"
git fetch -q origin 2>/dev/null; git fetch -q gitlab 2>/dev/null
ahead_o="$(git rev-list --count origin/main..main 2>/dev/null || echo '?')"; ahead_g="$(git rev-list --count gitlab/main..main 2>/dev/null || echo '?')"
echo "main is $ahead_o commit(s) ahead of GitHub and $ahead_g ahead of GitLab."
git log --oneline origin/main..main 2>/dev/null | head -8
if [ "$push" = yes ]; then
  echo; echo "Pushing main to GitLab (this deploys the web client) and GitHub (this runs the Actions build). No tag, so the race server does not change."
  read -r -p 'Type  push  to continue: ' typed
  if [ "$typed" = push ]; then git push gitlab main && git push origin main || todo+=("A push failed: see above"); else echo "Not pushed."; fi
else
  [ "$ahead_o" != 0 ] || [ "$ahead_g" != 0 ] && todo+=("When you want it live: rerun with --push (or: git push gitlab main && git push origin main)")
fi

line "What is left for you"
n=0; for t in "${todo[@]}"; do n=$((n + 1)); echo "  $n. $t"; done
[ $n -gt 0 ] || echo "  Nothing."
