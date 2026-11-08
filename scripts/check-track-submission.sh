#!/usr/bin/env bash
# The checks every Track Market submission passes (docs/specs/track-market.md, section 2; docs/adr/0004-track-marketplace.md decision 1). The editor runs this before
# Publish and the submission service runs the same script, so a track cannot pass in one place and fail in the other.
#   scripts/check-track-submission.sh [--tools DIR] PACKAGE.hntp FINISHING.hnr
# DIR holds HoverNetTrackPack, HoverNetTrackValidator and HoverNetReplay (default: build). One line per check, "ok NAME" or "FAIL NAME: reason"; exit 1 on any failure.
set -uo pipefail
tools="build"
if [ "${1:-}" = "--tools" ]; then tools="${2:?--tools needs a directory}"; shift 2; fi
package="${1:?usage: $0 [--tools DIR] PACKAGE.hntp FINISHING.hnr}"; replay="${2:?}"
pack="$tools/HoverNetTrackPack"; validator="$tools/HoverNetTrackValidator"; replaytool="$tools/HoverNetReplay"
for tool in "$pack" "$validator" "$replaytool"; do [ -x "$tool" ] || { echo "FAIL tools: $tool is not there"; exit 1; }; done
[ -f "$package" ] || { echo "FAIL package: $package does not exist"; exit 1; }
[ -f "$replay" ] || { echo "FAIL replay: $replay does not exist"; exit 1; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
fail=0
step() { # name, command... ; the command's output is kept for the failure message
  local name="$1"; shift
  if "$@" > "$work/out.txt" 2>&1; then echo "ok $name"; return 0; fi
  echo "FAIL $name: $(grep -v '^\s*$' "$work/out.txt" | tail -2 | tr '\n' ' ')"; fail=1; return 1
}
step "package is well formed and admissible (named author, approved licence)" "$pack" verify "$package" --catalogue || exit 1
step "track can be unpacked" "$pack" unpack "$package" "$work/track.trk" || exit 1
step "strict validator: loads, starts and is a race" bash -c "'$validator' '$work/track.trk' submission | grep -q '^VALID .* mode=race .*warnings=0'"
step "strict lint: no authoring warnings" "$pack" lint "$work/track.trk" --strict
step "finishing replay matches this track and re-simulates exactly" "$replaytool" verify "$replay" --track="$work/track.trk"
step "replay finishes at least one lap" bash -c "'$replaytool' info '$replay' | grep -q 'checkpoints' && '$replaytool' events '$replay' | grep -qi 'finish'"
exit $fail
