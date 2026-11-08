#!/usr/bin/env bash
# Signs a file (the catalogue, SHA256SUMS) with your production key and checks the signature straight away.
#   bash sign-file.sh FILE [KEYFILE]       writes FILE.sig next to FILE
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
file="${1:?usage: $0 FILE [KEYFILE]}"; key="${2:-$HOME/hovernet-keys/hovernet-catalogue.key}"
[ -f "$file" ] || { echo "no such file: $file"; exit 1; }; [ -f "$key" ] || { echo "no such key: $key"; exit 1; }
src="$here/signing-src"; [ -d "$src" ] || src="$here/../../NetTarget"
t="$src/LinuxClient/SignTool.cpp"; e="$src/Authority/Ed25519.cpp"
[ -f "$t" ] && [ -f "$e" ] || { echo "cannot find the signing tool sources next to this script"; exit 1; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
g++ -std=c++14 -O2 -o "$work/HoverNetSign" "$t" "$e"
"$work/HoverNetSign" sign "$key" "$file"
"$work/HoverNetSign" verify "$("$work/HoverNetSign" pubkey "$key")" "$file" && echo "signed and verified: $file.sig"
