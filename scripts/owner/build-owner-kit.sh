#!/usr/bin/env bash
# Packs everything the project owner needs to do the owner-only jobs into ONE file you can copy with scp: the scripts, a copy of the signing tool's two
# source files (so nothing has to be built here), and the documents to read or send. Run it from the repository.
#   bash scripts/owner/build-owner-kit.sh        writes dist/hovernet-owner-kit-YYYYMMDD.tar.gz and a .sha256 beside it, and prints the scp commands
set -euo pipefail
cd "$(dirname "$0")/../.."
stamp="$(date +%Y%m%d)"; name="hovernet-owner-kit-$stamp"
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
kit="$stage/$name"; mkdir -p "$kit/signing-src/LinuxClient" "$kit/signing-src/Authority" "$kit/docs"
cp scripts/owner/check-production.sh scripts/owner/make-signing-key.sh scripts/owner/sign-file.sh "$kit/"
cp NetTarget/LinuxClient/SignTool.cpp "$kit/signing-src/LinuxClient/"
cp NetTarget/Authority/Ed25519.cpp NetTarget/Authority/Ed25519.h "$kit/signing-src/Authority/"
for d in docs/owner-actions.md docs/track-market-terms-draft.md docs/adr/0002-content-licence-policy.md docs/adr/0004-track-marketplace.md docs/specs/track-market.md \
         docs/signing.md docs/account-privacy-policy-draft.md docs/licence-compliance.md; do cp "$d" "$kit/docs/"; done
cp docs/owner-actions.md "$kit/README-OWNER.md"
chmod +x "$kit"/*.sh
mkdir -p dist
tar -C "$stage" -czf "dist/$name.tar.gz" "$name"
( cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )
echo "Built dist/$name.tar.gz ($(du -h "dist/$name.tar.gz" | cut -f1)); sha256 in dist/$name.tar.gz.sha256"
cat <<EOF

Copy it to the machine you will use (replace USER@HOST), check it arrived intact, and unpack it:

  scp dist/$name.tar.gz dist/$name.tar.gz.sha256 USER@HOST:~/
  ssh USER@HOST 'cd ~ && sha256sum -c $name.tar.gz.sha256 && tar xzf $name.tar.gz && cd $name && ls'

Then follow README-OWNER.md inside (the same text as docs/owner-actions.md).
EOF
