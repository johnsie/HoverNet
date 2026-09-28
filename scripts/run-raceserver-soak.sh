#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <build-directory> <duration-seconds> [results-log]" >&2
    echo "Phase 2 gate: $0 build/linux 86400 phase2-soak.log" >&2
    exit 64
fi

build_dir=$(readlink -f -- "$1")
duration_sec=$2
results_log=${3:-phase2-soak.log}
[[ $duration_sec =~ ^[1-9][0-9]*$ ]] || { echo "duration must be a positive integer" >&2; exit 64; }
[[ -x "$build_dir/HoverNetRaceServerClientSmoke" && -x "$build_dir/RaceServer" ]] || {
    echo "RaceServer smoke binaries are missing from $build_dir" >&2
    exit 66
}

start_epoch=$(date +%s)
deadline=$((start_epoch + duration_sec))
iterations=0
: > "$results_log"
while (( $(date +%s) < deadline )); do
    "$build_dir/HoverNetRaceServerClientSmoke" "$build_dir/RaceServer" >> "$results_log" 2>&1
    "$build_dir/HoverNetRaceServerLifecycleSmoke" "$build_dir/RaceServer" >> "$results_log" 2>&1
    python3 scripts/raceserver_protocol_fuzz.py "$build_dir/RaceServer" >> "$results_log" 2>&1
    iterations=$((iterations + 1))
    printf 'iteration=%d elapsed_sec=%d\n' "$iterations" "$(( $(date +%s) - start_epoch ))" | tee -a "$results_log"
done

printf 'result=pass iterations=%d duration_sec=%d\n' "$iterations" "$(( $(date +%s) - start_epoch ))" | tee -a "$results_log"
