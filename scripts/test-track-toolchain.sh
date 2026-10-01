#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:?build directory required}
source_dir=${2:?source directory required}
work_dir=${3:?work directory required}
mkdir -p "$work_dir"

valid_out="$work_dir/tidal-causeway.trk"
invalid_source="$work_dir/invalid.track.txt"
invalid_out="$work_dir/invalid.trk"

"$build_dir/HoverNetMazeCompiler" "$valid_out" \
  "$source_dir/NetTarget/TrackSources/Tidal Causeway.track.txt"
"$build_dir/HoverNetTrackValidator" "$valid_out" "Tidal Causeway toolchain output"

# A compiler error must produce a non-zero result. This input deliberately has
# more starts than the runtime can store; older code wrote beyond the array and
# then forced the parser result to success.
cp "$source_dir/NetTarget/TrackSources/Tidal Causeway.track.txt" "$invalid_source"
# Tidal Causeway already has eight starts. Add enough blocks to exceed the
# serialized level's MR_NB_MAX_PLAYER limit (32) without duplicating that
# implementation constant in the test's pass/fail logic.
for index in $(seq 1 25); do
  cat >>"$invalid_source" <<'EOF'
[Initial_Position]
Section=1
Position=0,0,1
Orientation=0
Team=0
EOF
done

if "$build_dir/HoverNetMazeCompiler" "$invalid_out" "$invalid_source"; then
  echo "Compiler accepted a track with too many starting positions" >&2
  exit 1
fi

echo "Track compiler and validator regression checks passed"
