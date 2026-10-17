#!/usr/bin/env bash
# HoverNetSign against an independent Ed25519 (scripts/ed25519_reference.py, written from RFC 8032): same public keys, identical
# signatures, each side verifies the other's, tampering and a wrong key are refused. Run from the repository root.
#   scripts/test-signing.sh build/HoverNetSign
set -uo pipefail
tool="${1:?usage: $0 <HoverNetSign>}"
dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
fail=0
for round in 1 2 3 4 5 6; do
  "$tool" keygen "$dir/k$round" >"$dir/pub$round.txt" || { echo "FAIL: keygen"; fail=1; continue; }
  head -c $(( (round - 1) * 53 )) /dev/urandom > "$dir/m$round"
  "$tool" sign "$dir/k$round" "$dir/m$round" || { echo "FAIL: sign"; fail=1; continue; }
  "$tool" verify "$(cat "$dir/pub$round.txt")" "$dir/m$round" >/dev/null || { echo "FAIL: own signature $round does not verify"; fail=1; }
  python3 -I - "$dir" "$round" "$(dirname "$0")" <<'PY' || { echo "FAIL: round $round disagrees with the reference"; fail=1; }
import sys
sys.path.insert(0, sys.argv[3])
import ed25519_reference as ref
d, r = sys.argv[1], sys.argv[2]
seed = bytes.fromhex(open("%s/k%s" % (d, r)).read().strip())
msg = open("%s/m%s" % (d, r), "rb").read()
pub = bytes.fromhex(open("%s/pub%s.txt" % (d, r)).read().strip())
sig = bytes.fromhex(open("%s/m%s.sig" % (d, r)).read().strip())
assert ref.public_key(seed) == pub, "public key differs"
assert ref.sign(seed, msg) == sig, "signature differs"
assert ref.verify(pub, msg, sig)
open("%s/ref%s.sig" % (d, r), "w").write(ref.sign(seed, msg + b"!").hex() + "\n")
PY
  # The reference's signature of (message + "!") must verify in the tool for that message, and not for the original.
  { cat "$dir/m$round"; printf '!'; } > "$dir/mx$round"
  "$tool" verify "$(cat "$dir/pub$round.txt")" "$dir/mx$round" "$dir/ref$round.sig" >/dev/null || { echo "FAIL: the reference's signature $round was refused"; fail=1; }
  "$tool" verify "$(cat "$dir/pub$round.txt")" "$dir/m$round" "$dir/ref$round.sig" >/dev/null 2>&1 && { echo "FAIL: a signature for another message verified"; fail=1; }
done
"$tool" verify "$(cat "$dir/pub2.txt")" "$dir/m1" "$dir/m1.sig" >/dev/null 2>&1 && { echo "FAIL: verified under the wrong key"; fail=1; }
"$tool" keygen "$dir/k1" >/dev/null 2>&1 && { echo "FAIL: keygen overwrote an existing key"; fail=1; }
[ "$(stat -c %a "$dir/k1")" = 600 ] || { echo "FAIL: the key file is not owner-only"; fail=1; }
first=$(head -c1 "$dir/m3.sig"); [ "$first" = 0 ] && new=1 || new=0
sed -i "s/^./$new/" "$dir/m3.sig"; "$tool" verify "$(cat "$dir/pub3.txt")" "$dir/m3" >/dev/null 2>&1 && { echo "FAIL: a damaged signature verified"; fail=1; }
[ $fail -eq 0 ] && echo "signing: public keys and signatures identical to the reference on 6 messages, cross-verified, tampering refused"
exit $fail
