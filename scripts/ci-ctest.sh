#!/usr/bin/env bash
# Runs ctest for a CI job and, when tests fail, names each failed test and its first failure lines as an annotation of the job (a "::error::" workflow
# command), so the failure can be read from the run's annotations without opening the log (the API shows annotations to everyone, logs only to a signed-in
# user). Exit status is ctest's.
#   scripts/ci-ctest.sh <build dir> [other ctest arguments]
set -uo pipefail
dir="${1:?usage: $0 <build dir> [ctest arguments]}"; shift
ctest --test-dir "$dir" --output-on-failure "$@"
rc=$?
if [ $rc -ne 0 ]; then
  failed="$dir/Testing/Temporary/LastTestsFailed.log"
  log="$dir/Testing/Temporary/LastTest.log"
  if [ -f "$failed" ]; then
    while IFS=: read -r _ name; do
      [ -n "$name" ] || continue
      # The lines of that test's output that say what failed (the first few), from the ctest log.
      why="$(awk -v t="$name" '$0 ~ "^[0-9]+/[0-9]+ Test *#[0-9]+: " t " " {on=1; next} on && /^[0-9]+\/[0-9]+ Test / {on=0} on && (/FAIL|DIVERGED|expected|STOP|MISMATCH|Assertion|Segmentation|ERROR|Error/ || /^[-+][A-Za-z0-9]/) {print}' "$log" 2>/dev/null | head -6 | tr '\n' '|' | cut -c1-600)"
      echo "::error title=Test failed: $name::$name | ${why:-see the log}"
    done < "$failed"
  fi
fi
exit $rc
