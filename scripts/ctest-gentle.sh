#!/usr/bin/env bash
# Run the ctest suite without swamping the machine: two tests at a time, lowest CPU and disk priority,
# a per-test time limit, and a wait while free memory is low. Usage: scripts/ctest-gentle.sh [ctest args]
# Environment: HN_JOBS (parallel tests, default 2), HN_MIN_FREE_MB (default 1500), HN_TEST_TIMEOUT (seconds, default 300).
set -u
cd "$(dirname "$0")/.."
jobs=${HN_JOBS:-2}
min_free_mb=${HN_MIN_FREE_MB:-1500}
for _ in $(seq 1 60); do
  avail=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
  [ "$avail" -ge "$min_free_mb" ] && break
  echo "waiting: only ${avail} MB free (need ${min_free_mb})"; sleep 5
done
exec nice -n 19 ionice -c3 ctest --test-dir build/linux -j"$jobs" --timeout "${HN_TEST_TIMEOUT:-300}" --output-on-failure "$@"
