#!/usr/bin/env bash
# Installs a HoverNet server package containing Discord notifications, saves the
# webhook URL with mode 600, restarts the service, and verifies it. Run this on
# the server host:
#
#   sudo scripts/enable-discord-notifications.sh /path/to/hovernet-server_VERSION_amd64.deb
#
# The webhook is requested silently from the terminal so it does not enter shell
# history. For automation it may instead be supplied in HOVERNET_DISCORD_WEBHOOK_URL.
set -euo pipefail

package=${1:-}
unit=hovernet-server
environment_file=/etc/hovernet/discord.env

fail() { echo "STOP: $*" >&2; exit 1; }
say() { printf '\n== %s\n' "$*"; }

[[ $EUID -eq 0 ]] || fail "run this script with sudo"
[[ -n $package && -f $package ]] || fail "usage: sudo $0 /path/to/hovernet-server_VERSION_amd64.deb"
package=$(readlink -f "$package")
dpkg-deb --field "$package" Package | grep -Fxq hovernet-server || fail "$package is not a hovernet-server package"

webhook=${HOVERNET_DISCORD_WEBHOOK_URL:-}
if [[ -z $webhook ]]; then
    [[ -r /dev/tty ]] || fail "no terminal is available; set HOVERNET_DISCORD_WEBHOOK_URL"
    read -r -s -p "Discord webhook URL: " webhook </dev/tty
    printf '\n' >/dev/tty
fi
[[ $webhook =~ ^https://(discord\.com|discordapp\.com)/api/webhooks/[0-9]+/[A-Za-z0-9._-]+$ ]] ||
    fail "that does not look like a Discord webhook URL"

if ! command -v curl >/dev/null 2>&1; then
    say "Installing curl"
    apt-get update
    apt-get install -y curl
fi

say "Checking the Discord webhook"
curl --fail --silent --show-error --max-time 15 \
    --header 'Content-Type: application/json' \
    --data-binary '{"content":"✅ HoverNet Discord notifications connected.","allowed_mentions":{"parse":[]}}' \
    "$webhook" >/dev/null || fail "Discord rejected the webhook; nothing was installed"

say "Installing $(basename "$package")"
# Preserve the operator's existing server.conf without opening dpkg's conffile
# pager/prompt. This script is often run through SSH, where an abandoned prompt
# would otherwise keep the package lock indefinitely. Wait briefly for normal
# unattended upgrades rather than failing immediately on their lock.
DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a \
    apt-get -o DPkg::Lock::Timeout=120 -o Dpkg::Options::=--force-confold \
    install -y "$package"
systemctl cat "$unit" | grep -Fq 'EnvironmentFile=-/etc/hovernet/discord.env' ||
    fail "the installed package does not contain Discord support"

say "Saving the webhook securely"
temporary=$(mktemp /etc/hovernet/discord.env.XXXXXX)
cleanup() { rm -f "$temporary"; }
trap cleanup EXIT
chmod 600 "$temporary"
printf 'HOVERNET_DISCORD_WEBHOOK_URL=%s\n' "$webhook" > "$temporary"
chown root:root "$temporary"
if [[ -f $environment_file ]]; then
    cp -a "$environment_file" "$environment_file.bak-$(date +%Y%m%d-%H%M%S)"
fi
mv "$temporary" "$environment_file"
trap - EXIT
unset webhook HOVERNET_DISCORD_WEBHOOK_URL

say "Restarting HoverNet (active races drain first)"
systemctl daemon-reload
systemctl restart "$unit"
systemctl is-active --quiet "$unit" || {
    systemctl --no-pager status "$unit" 2>&1 | head -30
    fail "the HoverNet server did not restart"
}

pid=$(systemctl show -p MainPID --value "$unit")
if [[ -z $pid || $pid == 0 ]]; then fail "the service is active but has no server process"; fi
if ! journalctl -u "$unit" --since "2 minutes ago" --no-pager | grep -Fq 'Discord race notifications enabled'; then
    fail "the server is running, but did not report that Discord notifications are enabled"
fi

say "Live"
echo "Discord will now receive lobby joins, race starts, and race results."
