#!/usr/bin/env bash
# Puts the Track Market and the browser track editor on the web/race host, from THIS computer, with ssh and scp:
#
#     bash scripts/owner/deploy-all.sh --dry-run                  # shows every step and every command, changes nothing
#     bash scripts/owner/deploy-all.sh                            # does it (asks for your sudo password on the host, twice)
#     bash scripts/owner/deploy-all.sh --authors alice,bob --moderators john
#     bash scripts/owner/deploy-all.sh john@other-host
#
# 1. builds the browser editor here if it is not built (WebAssembly: it does not depend on the host's libraries);
# 2. installs the Track Market (packaging/web/install-market.sh: sends the committed source, builds the check programs on the host, installs the service,
#    the ten-minute sync and the Apache snippet after testing the Apache configuration);
# 3. puts the editor online at https://hovernet.outiva.com/editor/ (packaging/web/deploy-editor.sh);
# 4. with --authors / --moderators, adds those Outiva account names to the lists on the host (the market starts CLOSED: nobody can publish until named here);
# 5. checks the answers locally and through https and tells you what is still to do.
# What it does NOT do: push anything to GitLab or GitHub, tag or release, touch the race server's program or configuration, or edit the site's own Apache file.
# Only COMMITTED work is sent to the host (git archive HEAD), so commit what you want installed first.
set -euo pipefail
cd "$(dirname "$0")/../.."
host="john@192.168.10.181"; dry=no; authors=""; moderators=""
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) dry=yes; shift;;
    --authors) authors="${2:?--authors needs a comma-separated list}"; shift 2;;
    --moderators) moderators="${2:?--moderators needs a comma-separated list}"; shift 2;;
    -*) echo "unknown option $1"; exit 2;;
    *) host="$1"; shift;;
  esac
done
flag=""; [ "$dry" = yes ] && flag="--dry-run"
step() { printf '\n==== %s\n' "$*"; }
valid_names() { [[ "$1" =~ ^[A-Za-z0-9_.@-]+(,[A-Za-z0-9_.@-]+)*$ ]]; }
[ -z "$authors" ] || valid_names "$authors" || { echo "STOP: --authors takes account names (letters, digits, . _ - @) separated by commas"; exit 2; }
[ -z "$moderators" ] || valid_names "$moderators" || { echo "STOP: --moderators takes account names (letters, digits, . _ - @) separated by commas"; exit 2; }

step "0/5 Looking at this computer and the host"
git diff --quiet HEAD -- . || echo "note: you have uncommitted changes; only what is committed ($(git rev-parse --short HEAD)) is sent"
echo "installing commit $(git rev-parse --short HEAD): $(git log -1 --format=%s)"
if [ "$dry" = no ]; then
  ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" true || { echo "STOP: cannot ssh to $host without a password prompt: set up your ssh key first"; exit 1; }
  echo "ssh to $host works"
fi

step "1/5 The browser editor build"
if [ -s build/web/HoverNetTrackEditor.wasm ] && [ -z "$(find NetTarget/TrackEditor packaging/web/editor-shell.html NetTarget/ThirdParty/imgui -newer build/web/HoverNetTrackEditor.wasm -type f 2>/dev/null | head -1)" ]; then
  echo "build/web/HoverNetTrackEditor.wasm is up to date"
elif [ "$dry" = yes ]; then echo "+ (cd build/web && cmake --build . --target HoverNetTrackEditor)   [it is missing or older than its sources]"
else
  [ -f build/web/CMakeCache.txt ] || { echo "STOP: there is no WebAssembly build folder (build/web). Configure it once with the Emscripten toolchain, as for the game's web build"; exit 1; }
  (cd build/web && cmake --build . --target HoverNetTrackEditor -j4)
fi

step "2/5 The Track Market service"
bash packaging/web/install-market.sh $flag "$host"

step "3/5 The browser track editor"
bash packaging/web/deploy-editor.sh $flag "$host"

step "4/5 Who may publish"
add_names() { # file, comma list
  local file="$1" list="$2"
  [ -n "$list" ] || return 0
  if [ "$dry" = yes ]; then echo "+ ssh -t $host sudo: add $list to /etc/hovernet/$file"; return 0; fi
  ssh -t "$host" "for n in $(echo "$list" | tr ',' ' '); do sudo grep -qxi \"\$n\" /etc/hovernet/$file || echo \"\$n\" | sudo tee -a /etc/hovernet/$file >/dev/null; done; echo \"/etc/hovernet/$file now holds:\"; sudo grep -v '^#' /etc/hovernet/$file"
}
if [ -z "$authors$moderators" ]; then echo "no names given: the market starts closed. Add names with:  bash scripts/owner/deploy-all.sh --authors name1,name2 --moderators yourname"
else add_names market-authors.txt "$authors"; add_names market-moderators.txt "$moderators"; fi

step "5/5 Checking"
if [ "$dry" = yes ]; then echo "+ (would check http://127.0.0.1:9603/market/catalogue.tsv on the host, https://hovernet.outiva.com/market/catalogue.tsv, https://hovernet.outiva.com/editor/ and the market's /market/info)"; else
  ssh "$host" 'curl -s -m 10 http://127.0.0.1:9603/market/catalogue.tsv | head -2' || true
  echo "-- through https:"
  curl -s -m 15 https://hovernet.outiva.com/market/catalogue.tsv | head -2 || true
  curl -s -m 15 https://hovernet.outiva.com/market/info || true; echo
  curl -sI -m 15 https://hovernet.outiva.com/editor/ | tr -d '\r' | grep -i -E '^(HTTP|cross-origin-(opener|embedder)-policy):' || true
fi
note=""; [ "$dry" = yes ] && note=" (dry run: nothing was changed)"
cat <<EOF

Done${note}. Next:
  - open https://hovernet.outiva.com/editor/ in a browser: File > Open from your computer, then Track > Publish to the community (needs an invited Outiva account)
  - moderators review a first track through the market's review endpoint (GET/POST https://hovernet.outiva.com/market/review while signed in as a moderator)
  - to remove everything again:  bash packaging/web/install-market.sh --uninstall  and  bash packaging/web/deploy-editor.sh --remove
EOF
