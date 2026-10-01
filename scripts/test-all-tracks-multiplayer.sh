#!/usr/bin/env bash
set -euo pipefail

build_dir=$(realpath "${1:?build directory required}")
source_dir=$(realpath "${2:?source directory required}")
work_dir=$(realpath -m "${3:?work directory required}")
mkdir -p "$work_dir"

tracks=("ClassicH" "Steeplechase" "Switchback" "The Alley2" "The River" "Tidal Causeway" "Metro Spiral")
port=19920
server_pid=
host_pid=

cleanup() {
  if [[ -n ${host_pid:-} ]]; then kill "$host_pid" 2>/dev/null || true; fi
  if [[ -n ${server_pid:-} ]]; then kill "$server_pid" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

cd "$source_dir"
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

for track in "${tracks[@]}"; do
  safe_name=${track// /-}
  "$build_dir/RaceServer" "$port" "$work_dir/$safe_name-server.log" --require-protocol-2 &
  server_pid=$!
  sleep 0.4
  timeout --kill-after=5s 25s "$build_dir/HoverNetGame2Player" --lobby 127.0.0.1 "$port" \
    --track "$track" --online-race-host-smoke --frames 60 >"$work_dir/$safe_name-host.log" 2>&1 &
  host_pid=$!
  sleep 0.4
  timeout --kill-after=5s 25s "$build_dir/HoverNetGame2Player" --lobby 127.0.0.1 "$port" \
    --online-race-join-smoke --frames 60 >"$work_dir/$safe_name-join.log" 2>&1
  wait "$host_pid"
  host_pid=
  kill -TERM "$server_pid"
  wait "$server_pid"
  server_pid=
  port=$((port + 1))
  echo "Multiplayer playthrough passed: $track"
done
