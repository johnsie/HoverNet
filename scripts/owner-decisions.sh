#!/usr/bin/env bash
# Walks the project owner through the decisions only they can make, and records the answers in docs/owner-decisions.md.
#   scripts/owner-decisions.sh [HoverNetSign]        (default: build/HoverNetSign; run from the repository root)
# It changes files in the working tree and commits, pushes, tags and releases nothing. Press Enter at any question to skip it.
# The secret signing key is created where you say, never inside the repository.
set -uo pipefail
root="${HOVERNET_REPO:-.}"
sign="${1:-build/HoverNetSign}"
log="$root/docs/owner-decisions.md"
adr="$root/docs/adr/0002-content-licence-policy.md"
today="$(date +%Y-%m-%d)"

ask()   { local a; read -r -p "$1 " a; printf '%s' "$a"; }
yesno() { local a; read -r -p "$1 [y/N] " a; [[ "$a" =~ ^[Yy] ]]; }
note()  { [ -f "$log" ] || printf '# Owner decisions\n\nRecorded by scripts/owner-decisions.sh. Each entry is the owner'"'"'s answer on the date shown; nothing here is checked by the build.\n' > "$log"; printf '\n## %s (%s)\n\n%s\n' "$1" "$today" "$2" >> "$log"; echo "  recorded in $log"; }

echo "HoverNet owner decisions. Enter skips a question."

echo; echo "== 1. Release signing key =="
echo "Create the key on a machine that is not a build server and back it up offline (docs/signing.md)."
if yesno "Create a key now?"; then
  if [ ! -x "$sign" ]; then echo "  $sign not found: build it with 'cmake --build build --target HoverNetSign'."; else
    keyfile="$(ask "Where should the SECRET key be written (a path outside the repository, e.g. ~/hovernet-release.key)?")"
    keyfile="${keyfile/#\~/$HOME}"
    if [ -z "$keyfile" ]; then echo "  skipped."
    elif [[ "$(realpath -m "$keyfile")" == "$(realpath -m "$root")"/* ]]; then echo "  refused: that path is inside the repository."
    elif [ -e "$keyfile" ]; then echo "  refused: $keyfile already exists (never overwritten)."
    else
      pub="$("$sign" keygen "$keyfile")" && { echo "  secret key written to $keyfile. BACK IT UP OFFLINE and never put it in CI."; echo "  public key: $pub"
        note "Release signing key" "Public key (Ed25519): \`$pub\`. The secret key is held offline by the owner. Not yet used by the release jobs or compiled into clients."; }
    fi
  fi
fi

echo; echo "== 2. ADR 0002: content licence policy =="
echo "Read $adr (about 2 pages). Not legal advice: have a lawyer review it before outside track submissions are accepted."
if yesno "Accept it as written?"; then
  yesno "Has a lawyer reviewed it?" && review=done || review="still to do before outside submissions"
  if grep -q '^- Status: \*\*Proposed\*\*' "$adr" 2>/dev/null; then
    sed -i "s/^- Status: \*\*Proposed\*\*.*/- Status: **Accepted** ($today) by the project owner. Not legal advice; legal review is $review./" "$adr"
    echo "  status line updated in the ADR."
  else echo "  the ADR's status line is not 'Proposed'; left unchanged."; fi
  note "ADR 0002 (content licence policy)" "Accepted as written."
else
  c="$(ask "What should change (one line, or Enter to leave it undecided)?")"
  [ -n "$c" ] && note "ADR 0002 (content licence policy)" "Changes requested: $c"
fi

echo; echo "== 3. Win32 client retirement =="
echo "Open question: split screen (docs/win32-client-inventory.md): migrate it in Stage 3, or retire it in favour of online play."
split="$(ask "Split screen: type 'migrate' or 'retire' (Enter to skip):")"
if [ -n "$split" ]; then
  notice="$(ask "Date of the deprecation notice in the legacy client (YYYY-MM-DD):")"
  removal="$(ask "Date the Win32 client is removed from 3.0 packages (YYYY-MM-DD):")"
  note "Win32 retirement" "Split screen: **$split**. Deprecation notice: ${notice:-not set}. Removed from 3.0 packages: ${removal:-not set}."
fi

echo; echo "== 4. Accounts and sign-in =="
echo "Answer what you can; the answers decide what gets built (a guest-only product needs none of it)."
acc="$(ask "Accounts: 'none' (guest names only), 'optional' or 'required' (Enter to skip):")"
if [ -n "$acc" ]; then
  how=""; data=""
  if [ "$acc" != none ]; then
    how="$(ask "Sign-in method (email-link, oauth, password):")"
    data="$(ask "Personal data you accept to store (e.g. email address only):")"
  fi
  note "Accounts and sign-in" "Accounts: **$acc**. Method: ${how:-n/a}. Data stored: ${data:-n/a}. A privacy and deletion policy for accounts still has to be written and reviewed."
fi

echo; echo "== 5. Pipelines =="
echo "Push the branch yourself when you are ready (this script never pushes), then check the GitHub Actions and GitLab runs of the"
echo "hovernet-server package jobs (.github/workflows/build.yml, .gitlab-ci.yml). Neither edit has run yet."
if command -v gh >/dev/null 2>&1 && yesno "Show the latest GitHub Actions runs now (read only)?"; then gh run list --limit 5 || true; fi

echo; echo "== 6. Client menus =="
echo "Needs you at the machine: ask Claude to 'do the menus' and check each screen natively and in the browser build."

echo; echo "Done. Review with 'git diff' and commit what you agree with."
