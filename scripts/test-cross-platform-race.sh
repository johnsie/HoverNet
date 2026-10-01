#!/usr/bin/env bash
# Windows x64 <-> Linux multiplayer, both directions, through a Linux RaceServer.
#
#   SSH_OPTS="-p 2222" scripts/test-cross-platform-race.sh \
#       BUILD_DIR WINDOWS_SSH_TARGET WINDOWS_EXE WINDOWS_DATA_DIR LINUX_IP
#
#   BUILD_DIR          Linux build tree (RaceServer, HoverNetGame2Player)
#   WINDOWS_SSH_TARGET user@host of a Windows machine with OpenSSH and PowerShell
#   WINDOWS_EXE        path on that machine to HoverNet.exe
#   WINDOWS_DATA_DIR   path on that machine to the data tree (contains NetTarget\)
#   LINUX_IP           address of THIS machine as the Windows machine sees it
#
# Run 1: the Windows client hosts, the Linux client joins. Run 2: the reverse.
# Needs a TCP port (default 19990+) open between the machines. Meant for release
# rehearsals: the two operating systems cannot share one CI job.
set -euo pipefail

build_dir=$(realpath "${1:?Linux build directory required}")
target=${2:?Windows ssh target required}
win_exe=${3:?path to HoverNet.exe on the Windows machine required}
win_data=${4:?data directory on the Windows machine required}
linux_ip=${5:?address of this machine, as seen from Windows, required}
port=${PORT:-19990}
read -r -a ssh_opts <<<"${SSH_OPTS:-}"
work=$(mktemp -d); trap 'rm -rf "$work"; [[ -n ${server_pid:-} ]] && kill "$server_pid" 2>/dev/null || true' EXIT
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy XDG_CONFIG_HOME="$work/config"
mkdir -p "$XDG_CONFIG_HOME"

# Runs HoverNet.exe on Windows with the given arguments; prints its exit code.
windows_client() {
  local args=$1 out=$2
  ssh "${ssh_opts[@]}" -o BatchMode=yes "$target" powershell -NoProfile -Command - >"$out" 2>&1 <<PS
\$env:SDL_VIDEODRIVER='dummy'; \$env:SDL_AUDIODRIVER='dummy'; \$env:HOVERNET_DATA_DIR='$win_data'
\$env:APPDATA = Join-Path \$env:TEMP 'hovernet-xos-appdata'; New-Item -ItemType Directory -Force -Path \$env:APPDATA | Out-Null
\$p = Start-Process -FilePath '$win_exe' -ArgumentList '$args' -WorkingDirectory '$win_data' -PassThru -NoNewWindow -RedirectStandardOutput "\$env:TEMP\\xos.out" -RedirectStandardError "\$env:TEMP\\xos.err"
\$null = \$p.Handle
if (-not \$p.WaitForExit(150000)) { Stop-Process -Id \$p.Id -Force; 'TIMEOUT' } else { 'EXIT=' + \$p.ExitCode }
Get-Content "\$env:TEMP\\xos.out" -ErrorAction SilentlyContinue | Select-Object -Last 3
PS
}

run_pair() {
  local label=$1 host_side=$2
  echo "== $label"
  "$build_dir/RaceServer" "$port" "$work/server-$port.log" --require-protocol-2 >/dev/null 2>&1 &
  server_pid=$!
  sleep 1
  local win_args linux_rc=0
  if [[ $host_side == windows ]]; then
    windows_client "--lobby $linux_ip $port --track Switchback --online-race-host-smoke --frames 400" "$work/win-$port.out" &
    local win_pid=$!
    sleep 6
    timeout 150 "$build_dir/HoverNetGame2Player" --lobby 127.0.0.1 "$port" --online-race-join-smoke --frames 400 \
      >"$work/linux-$port.log" 2>&1 || linux_rc=$?
  else
    timeout 150 "$build_dir/HoverNetGame2Player" --lobby 127.0.0.1 "$port" --track Switchback --online-race-host-smoke --frames 400 \
      >"$work/linux-$port.log" 2>&1 &
    local linux_pid=$!
    sleep 4
    windows_client "--lobby $linux_ip $port --online-race-join-smoke --frames 400" "$work/win-$port.out" &
    local win_pid=$!
    wait "$linux_pid" || linux_rc=$?
  fi
  wait "$win_pid" || true
  kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; server_pid=
  local win_exit
  win_exit=$(grep -o 'EXIT=[0-9-]*' "$work/win-$port.out" | head -n1 || true)
  local players
  players=$(grep -o 'players=[0-9]*' "$work/server-$port.log" | sort -u | tr '\n' ' ')
  echo "   linux client exit=$linux_rc, windows client ${win_exit:-no result}, server saw: $players"
  if [[ $linux_rc -ne 0 || $win_exit != EXIT=0 ]] || ! grep -q 'players=2' "$work/server-$port.log"; then
    echo "FAILED: $label" >&2
    tail -n 5 "$work/win-$port.out" "$work/linux-$port.log" >&2 || true
    return 1
  fi
  port=$((port + 1))
}

run_pair "Windows x64 client hosts, Linux client joins (Linux server)" windows
run_pair "Linux client hosts, Windows x64 client joins (Linux server)" linux
echo "cross-platform races passed in both directions"
