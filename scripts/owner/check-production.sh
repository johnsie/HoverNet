#!/usr/bin/env bash
# Is the release really live? Read-only checks you can run from your own machine (nothing is changed anywhere).
#   bash check-production.sh [VERSION]            default: the version below
#   RACE_HOST=john@192.168.10.181 bash check-production.sh 2.3.26
# Checks: the GitHub release and its eight files, the web site answers over TLS, and (if you can ssh there) the race server package and service.
set -uo pipefail
version="${1:-2.3.26}"; tag="v$version"
race_host="${RACE_HOST:-john@192.168.10.181}"
web_url="${WEB_URL:-https://hovernet.outiva.com}"
fail=0
ok()   { echo "ok    $*"; }
bad()  { echo "FAIL  $*"; fail=1; }
note() { echo "note  $*"; }

# 1. GitHub release (public API)
json="$(curl -s -m 20 "https://api.github.com/repos/johnsie/HoverNet/releases/tags/$tag" || true)"
assets="$(printf '%s' "$json" | python3 -c 'import sys,json
try: d=json.load(sys.stdin)
except Exception: d={}
print(" ".join(sorted(a["name"] for a in d.get("assets",[]))))' 2>/dev/null)"
if [ -n "$assets" ]; then
  ok "GitHub release $tag exists with: $assets"
  for need in "HoverNet-$version-windows-x64-setup.exe" "HoverNet-$version-win32-setup.exe" "hovernet-game_${version}_amd64.deb" "hovernet-raceserver_${version}_amd64.deb" "hovernet-server_${version}_amd64.deb" "SHA256SUMS"; do
    case " $assets " in *" $need "*) ;; *) bad "release is missing $need";; esac
  done
else bad "no GitHub release for $tag (the Actions run may have failed or is still going: https://github.com/johnsie/HoverNet/actions)"; fi

# 2. The web site
code="$(curl -s -m 20 -o /dev/null -w '%{http_code}' "$web_url/" || true)"
if [ "$code" = "200" ]; then ok "$web_url answers 200"; else bad "$web_url answered '$code'"; fi
modified="$(curl -sI -m 20 "$web_url/" | tr -d '\r' | grep -i '^last-modified:' || true)"
[ -n "$modified" ] && note "web page $modified (compare with the time of the GitLab web deploy job)"
note "open $web_url in a browser: the main menu shows the new hovercraft picture and the version (v$version) in its bottom-left corner"

# 3. The race server (needs your ssh key; skipped if it cannot connect)
if ssh -o BatchMode=yes -o ConnectTimeout=8 "$race_host" true 2>/dev/null; then
  installed="$(ssh -o BatchMode=yes "$race_host" "dpkg-query -W -f='\${Version}' hovernet-raceserver 2>/dev/null" || true)"
  active="$(ssh -o BatchMode=yes "$race_host" "systemctl is-active hovernet-raceserver 2>/dev/null" || true)"
  [ -n "$installed" ] && note "race server package installed: $installed"
  case "$installed" in "$version"*) ok "race server package is $version";; *) bad "race server package is '$installed', not $version (the GitLab deploy:raceserver job may not have run)";; esac
  [ "$active" = "active" ] && ok "hovernet-raceserver service is active" || bad "hovernet-raceserver service is '$active'"
  ssh -o BatchMode=yes "$race_host" "ss -ltn 2>/dev/null | grep -q ':9600 '" && ok "port 9600 is listening" || bad "nothing is listening on port 9600"
else note "cannot ssh to $race_host without a password: the race-server checks were skipped (set RACE_HOST=user@host)"; fi

echo; [ $fail -eq 0 ] && echo "All checks passed." || echo "Some checks failed: see the FAIL lines above."
exit $fail
