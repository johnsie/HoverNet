#!/usr/bin/env bash
# After the HoverNet server package is installed on the race-server host, checks that the NEW server is what is running: the package is fully installed
# (not left "unpacked"), and the running process was started after the installed binary (it does not run a replaced, deleted file) with the replay folder
# argument the unit passes. An upgrade drains first (races under way finish, up to 11 minutes), so this waits. Runs unprivileged on that host.
#   scripts/verify-server-deploy.sh [wait-seconds]
set -uo pipefail
limit="${1:-840}"
unit=hovernet-server
deadline=$(( $(date +%s) + limit ))
problem=""
while :; do
  problem=""
  status=$(dpkg-query -W -f='${db:Status-Abbrev}' hovernet-server 2>/dev/null | tr -d ' ')
  [ "$status" = "ii" ] || problem="the package is not fully installed (dpkg status '$status')"
  if systemctl is-active --quiet "$unit"; then
    pid=$(systemctl show -p MainPID --value "$unit")
    if [ -n "$pid" ] && [ "$pid" != 0 ] && [ -r "/proc/$pid/cmdline" ]; then
      exe=$(readlink "/proc/$pid/exe" 2>/dev/null || true)
      case "$exe" in *"(deleted)"*) problem="${problem:+$problem; }the running server (pid $pid) is an old build: its program file has been replaced";; esac
      tr '\0' ' ' < "/proc/$pid/cmdline" | grep -q -- "--replay-dir" || problem="${problem:+$problem; }the running server was started without --replay-dir, so it saves no replays"
    else
      problem="${problem:+$problem; }the service is active but has no main process yet"
    fi
  else
    problem="${problem:+$problem; }the service is not active (state: $(systemctl is-active "$unit" 2>&1))"
  fi
  [ -z "$problem" ] && break
  [ "$(date +%s)" -ge "$deadline" ] && break
  echo "waiting (a restart drains races under way): $problem"
  sleep 10
done
if [ -n "$problem" ]; then echo "FAIL: $problem"; systemctl --no-pager status "$unit" 2>&1 | head -12; exit 1; fi
health=$(curl -s -m 5 http://127.0.0.1:9612/health 2>/dev/null || true)
echo "server is the new build, started $(ps -o lstart= -p "$pid" | sed 's/  */ /g'), health: ${health:-unknown}"
