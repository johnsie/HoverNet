#!/usr/bin/env bash
# Writes DIR/SHA256SUMS covering every file in DIR (sorted, relative names, so
# `sha256sum -c SHA256SUMS` works from the download folder) and verifies it.
#
#   scripts/make-release-checksums.sh DIR
set -euo pipefail

dir=${1:?release asset directory required}
cd "$dir"
rm -f SHA256SUMS
# NUL-delimited throughout so unusual file names (spaces, newlines) are safe.
find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' | LC_ALL=C sort -z | xargs -0 --no-run-if-empty sha256sum >SHA256SUMS
[[ -s SHA256SUMS ]] || { echo "no release assets in $dir" >&2; exit 1; }
sha256sum --check --quiet SHA256SUMS
echo "SHA256SUMS: $(wc -l <SHA256SUMS) file(s)"
