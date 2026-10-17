#!/usr/bin/env bash
# Runs AS ROOT ON THE HOST (scripts/setup-server-host.sh copies it there and runs it with sudo). Installs a hovernet-server package, makes the
# configuration let the web client in, starts the server, switches on the /auth/ proxy lines in the Apache site, and checks the result.
# Every file it edits is copied to a .bak-<time> file first; a failed Apache configtest puts the old file back.
#
#   setup-server.sh /path/to/hovernet-server_X.Y.Z_amd64.deb [--no-apache]
#   setup-server.sh --configure-only [--no-apache]        (the package is already installed)
#
# For tests, these can be overridden: HN_CONF, HN_APACHE_SITE, HN_ORIGIN, HN_STATUS_PORT, HN_SKIP_SERVICES=1 (edit files only).
set -euo pipefail

deb="${1:-}"; shift || true
apache=yes
for a in "$@"; do [ "$a" = "--no-apache" ] && apache=no; done
install_package=yes
if [ "$deb" = "--configure-only" ]; then install_package=no; deb=""; fi     # the package is already installed (for example by the GitLab deploy)
conf="${HN_CONF:-/etc/hovernet/server.conf}"
site="${HN_APACHE_SITE:-/etc/apache2/sites-available/hovernet.outiva.com.conf}"
origin="${HN_ORIGIN:-https://hovernet.outiva.com}"
status_port="${HN_STATUS_PORT:-9612}"
skip_services="${HN_SKIP_SERVICES:-}"
stamp="$(date +%Y%m%d-%H%M%S)"

say() { printf '\n== %s\n' "$*"; }
backup() { cp -a "$1" "$1.bak-$stamp"; echo "backup: $1.bak-$stamp"; }

# Sets "name value" in the configuration: replaces the active or commented-out line for that option, or appends one.
set_option() {
    local name="$1" value="$2"
    if grep -Eq "^#?[[:space:]]*${name}[[:space:]]" "$conf"; then
        sed -i -E "0,/^#?[[:space:]]*${name}[[:space:]].*/s||${name} ${value}|" "$conf"
    else
        printf '%s %s\n' "$name" "$value" >> "$conf"
    fi
}

if [ -z "$skip_services" ]; then
    [ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 1; }
    was_active=no; systemctl is-active --quiet hovernet-server && was_active=yes || true
    if [ "$install_package" = yes ]; then
        [ -f "$deb" ] || { echo "usage: $0 /path/to/hovernet-server_X.Y.Z_amd64.deb | --configure-only [--no-apache]" >&2; exit 64; }
        dpkg-deb --field "$deb" Package | grep -Fxq hovernet-server || { echo "$deb is not a hovernet-server package" >&2; exit 65; }
        say "Installing $(basename "$deb")"
        apt-get install -y "$deb"
    else
        dpkg-query -W hovernet-server >/dev/null 2>&1 || { echo "hovernet-server is not installed: give a package instead of --configure-only" >&2; exit 1; }
        say "Using the installed $(dpkg-query -W hovernet-server)"
    fi
fi

say "Configuring $conf"
[ -f "$conf" ] || { echo "$conf does not exist (is the package installed?)" >&2; exit 1; }
backup "$conf"
# the lobby (many races) must be on: a bare "lobby" line
if grep -Eq '^lobby[[:space:]]*$' "$conf"; then :
elif grep -Eq '^#[[:space:]]*lobby[[:space:]]*$' "$conf"; then sed -i -E 's/^#[[:space:]]*lobby[[:space:]]*$/lobby/' "$conf"
else echo lobby >> "$conf"; fi
set_option ws-origin "$origin"
set_option status-port "$status_port"
grep -Eq '^bind[[:space:]]+127\.0\.0\.1' "$conf" || echo "NOTE: 'bind' is not 127.0.0.1 in $conf; the web path expects the server on the loopback interface behind Apache." >&2
echo "--- active settings:"; grep -E '^[a-z]' "$conf"

if [ -z "$skip_services" ]; then
    say "Starting the server"
    if [ "${was_active:-no}" = yes ]; then systemctl restart hovernet-server; else systemctl start hovernet-server; fi
    for _ in $(seq 1 30); do [ "$(curl -s -m 2 "http://127.0.0.1:${status_port}/health" || true)" = ok ] && break; sleep 1; done
    health="$(curl -s -m 3 "http://127.0.0.1:${status_port}/health" || true)"
    echo "health: ${health:-no answer}"
    [ "$health" = ok ] || { echo "The server did not come up healthy. See: journalctl -u hovernet-server -n 40" >&2; exit 1; }
fi

if [ "$apache" = yes ]; then
    say "Enabling the /auth/ proxy in $site"
    if [ ! -f "$site" ]; then
        echo "NOTE: $site not found; skipping Apache (add the two ProxyPass /auth/ lines yourself, see docs/operators.md)." >&2
    elif grep -Eq '^[[:space:]]*ProxyPass /auth/' "$site"; then
        echo "already enabled"
    elif grep -Eq '^[[:space:]]*#ProxyPass /auth/' "$site"; then
        backup "$site"
        sed -i -E 's/^([[:space:]]*)#(ProxyPass(Reverse)? \/auth\/)/\1\2/' "$site"
        if [ -z "$skip_services" ]; then
            if apachectl configtest; then systemctl reload apache2; echo "Apache reloaded"
            else echo "configtest failed: restoring the old site file" >&2; cp -a "$site.bak-$stamp" "$site"; exit 1; fi
        fi
    else
        echo "NOTE: the site file has no /auth/ proxy lines (it is older than this repository's copy); copy them from packaging/web/hovernet.outiva.com.conf." >&2
    fi
fi

if [ -z "$skip_services" ] && [ "$apache" = yes ]; then
    say "Checking the browser path through Apache (this host, bypassing HAProxy)"
    code="$(curl -sk -m 8 -o /dev/null -w '%{http_code}' -H "Origin: ${origin}" -H 'Connection: Upgrade' -H 'Upgrade: websocket' \
        -H 'Sec-WebSocket-Version: 13' -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' --resolve "hovernet.outiva.com:443:127.0.0.1" \
        https://hovernet.outiva.com/auth/ || true)"
    echo "upgrade request answered: HTTP ${code:-none}"
    case "$code" in
        101) echo "OK: the browser path works up to Apache. If a browser still cannot connect, check that HAProxy passes WebSocket upgrades on /auth/." ;;
        403) echo "403: the Origin does not match ws-origin in $conf." ;;
        404) echo "404: Apache is not proxying /auth/ (the lines are not enabled)." ;;
        *)   echo "Unexpected answer; see: journalctl -u hovernet-server -n 40  and  /var/log/apache2/error.log" ;;
    esac
fi
say "Done"
