#!/usr/bin/env bash
# Snapshot property test on the seven bundled tracks at two snapshot points each: restore in place and in a new session must
# replay the rest of the race with identical state hashes (HoverNetSnapshotTest), and the snapshot bytes themselves must be
# identical on every platform (digests in docs/snapshot-golden.txt). Run from the repository root.
#   scripts/test-snapshot.sh build/HoverNetSnapshotTest [--update]
set -uo pipefail
cmd="${1:?usage: $0 <HoverNetSnapshotTest> [--update]}"
golden="docs/snapshot-golden.txt"
tracks=("ClassicH" "Metro Spiral" "Steeplechase" "Switchback" "The Alley2" "The River" "Tidal Causeway")
out="$(mktemp)"; fail=0
for track in "${tracks[@]}"; do
  for tick in 400 1400; do
    if ! line="$($cmd "$track" $tick $((tick + 2000)) 2>/dev/null | tail -1)" || [ -z "$line" ]; then echo "FAIL: $track at tick $tick"; fail=1; continue; fi
    echo "$line" >> "$out"
  done
done
if [ $fail -ne 0 ]; then rm -f "$out"; exit 1; fi
if [ "${2:-}" = "--update" ]; then cp "$out" "$golden"; echo "updated $golden"; rm -f "$out"; exit 0; fi
if diff -u "$golden" "$out"; then echo "snapshot: all $(wc -l < "$out") snapshots restore exactly and match the golden digests"; rm -f "$out"; exit 0; fi
echo "snapshot: DIGEST MISMATCH (the snapshot bytes changed; if intended, bump the rules version and --update)"; rm -f "$out"; exit 1
