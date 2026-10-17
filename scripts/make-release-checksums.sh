#!/usr/bin/env bash
# Writes DIR/SHA256SUMS covering every file in DIR (sorted, relative names, so
# `sha256sum -c SHA256SUMS` works from the download folder) and verifies it.
# If HOVERNET_SIGN_TOOL (HoverNetSign) and HOVERNET_SIGN_KEY (the secret key file) are set, also writes and checks SHA256SUMS.sig
# (docs/signing.md). Without them nothing is signed, and nothing should be described as signed.
#
#   scripts/make-release-checksums.sh DIR
set -euo pipefail

dir=${1:?release asset directory required}
cd "$dir"
rm -f SHA256SUMS SHA256SUMS.sig
# NUL-delimited throughout so unusual file names (spaces, newlines) are safe.
find . -maxdepth 1 -type f ! -name SHA256SUMS ! -name SHA256SUMS.sig -printf '%f\0' | LC_ALL=C sort -z | xargs -0 --no-run-if-empty sha256sum >SHA256SUMS
[[ -s SHA256SUMS ]] || { echo "no release assets in $dir" >&2; exit 1; }
sha256sum --check --quiet SHA256SUMS
echo "SHA256SUMS: $(wc -l <SHA256SUMS) file(s)"
if [[ -n "${HOVERNET_SIGN_TOOL:-}" && -n "${HOVERNET_SIGN_KEY:-}" ]]; then
  "$HOVERNET_SIGN_TOOL" sign "$HOVERNET_SIGN_KEY" SHA256SUMS
  "$HOVERNET_SIGN_TOOL" verify "$("$HOVERNET_SIGN_TOOL" pubkey "$HOVERNET_SIGN_KEY")" SHA256SUMS >/dev/null
  echo "SHA256SUMS.sig: signed and verified"
fi
