#!/usr/bin/env bash
# Makes the production signing key for the Track Market catalogue and the release checksums (docs/signing.md). Run it on a machine you trust that is NOT a
# build server or CI runner, ideally one that is offline. It builds the small signing tool from source here (so no binary has to be copied from anywhere),
# creates the key, proves that signing and verifying work, and prints the public key to publish.
#   bash make-signing-key.sh [KEYFILE]        default KEYFILE: $HOME/hovernet-keys/hovernet-catalogue.key
# It never overwrites an existing key. Back the key file up offline (a USB stick in a drawer, a printed copy in a safe). Anyone who has the file can sign as the project.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
key="${1:-$HOME/hovernet-keys/hovernet-catalogue.key}"
if [ -n "${CI:-}" ] || [ -n "${GITLAB_CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; then echo "STOP: this looks like a CI runner. Run it on your own machine."; exit 1; fi
[ ! -e "$key" ] || { echo "STOP: $key already exists. I will not overwrite a key. Move it away first if you really mean to replace it."; exit 1; }
src="$here/signing-src"; [ -d "$src" ] || src="$here/../../NetTarget"   # the kit carries a copy of the two source files with the same folder layout
tool_src="$src/LinuxClient/SignTool.cpp"; ed_src="$src/Authority/Ed25519.cpp"
[ -f "$tool_src" ] && [ -f "$ed_src" ] || { echo "STOP: cannot find the signing tool sources next to this script"; exit 1; }
command -v g++ >/dev/null || { echo "STOP: g++ is needed to build the signing tool (sudo apt install g++)"; exit 1; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
echo "Building the signing tool from source..."
g++ -std=c++14 -O2 -o "$work/HoverNetSign" "$tool_src" "$ed_src"
mkdir -p "$(dirname "$key")"; chmod 700 "$(dirname "$key")"
"$work/HoverNetSign" keygen "$key" > "$work/pub.txt"
chmod 600 "$key"
pub="$("$work/HoverNetSign" pubkey "$key")"
# prove it works: sign a file, verify it, and check that a changed file fails
printf 'HoverNet signing self-test\n' > "$work/t.txt"
"$work/HoverNetSign" sign "$key" "$work/t.txt"
"$work/HoverNetSign" verify "$pub" "$work/t.txt" && echo "self-test: a signature verifies"
printf 'changed\n' > "$work/t.txt"
if "$work/HoverNetSign" verify "$pub" "$work/t.txt" 2>/dev/null; then echo "STOP: a changed file verified. Do not use this key."; exit 1; else echo "self-test: a changed file is refused"; fi
cat <<EOF

DONE.
  Secret key (keep private, back it up offline):  $key
  Public key (safe to publish, 64 characters):
  $pub

Next:
  1. Copy the key file to your offline backup NOW, then check the copy: sha256sum "$key"
  2. Send me (or commit to the repository) ONLY the public key line above. Never the key file.
  3. Sign a file with:  HoverNetSign sign "$key" FILE   (or run sign-file.sh FILE)
EOF
