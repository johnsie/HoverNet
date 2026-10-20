#!/usr/bin/env bash
# Creates/updates HoverNet's confidential Keycloak client (hovernet-account) and installs the account service with its secrets.
# Run as root ON THE HOST THAT RUNS KEYCLOAK AND THE WEB SITE (the same host as hovernet.outiva.com), from this checkout:
#   sudo bash packaging/web/configure-account-oidc.sh
# The secrets are written to /etc/hovernet/account.env (root:hovernet 0640) and never printed. Safe to run again: the client secret and the
# session secret are kept. Modelled on NetPanzer's deploy/outiva/configure-oidc.sh.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
CLIENT_JSON="$HERE/keycloak/hovernet-account-client.json"
KEYCLOAK_ENV=/etc/keycloak/keycloak.env
ACCOUNT_ENV=/etc/hovernet/account.env
KCADM=/opt/keycloak/bin/kcadm.sh

[ "$(id -u)" -eq 0 ] || { echo "Run this as root (sudo $0)." >&2; exit 1; }
{ [ -x "$KCADM" ] && [ -r "$KEYCLOAK_ENV" ] && [ -r "$CLIENT_JSON" ]; } || { echo "The Outiva Keycloak installation or the client definition was not found." >&2; exit 1; }
getent group hovernet >/dev/null || { echo "The hovernet account does not exist: install the HoverNet server first." >&2; exit 1; }

set -a; . "$KEYCLOAK_ENV"; set +a
: "${KC_BOOTSTRAP_ADMIN_USERNAME:?Missing Keycloak administrator username}"
: "${KC_BOOTSTRAP_ADMIN_PASSWORD:?Missing Keycloak administrator password}"

admin_config=$(mktemp); trap 'rm -f "$admin_config"' EXIT; chmod 0600 "$admin_config"
"$KCADM" config credentials --config "$admin_config" --server http://127.0.0.1:8180 --realm master \
  --user "$KC_BOOTSTRAP_ADMIN_USERNAME" --password "$KC_BOOTSTRAP_ADMIN_PASSWORD"

client_uuid=$("$KCADM" get clients --config "$admin_config" -r outiva -q clientId=hovernet-account --fields id --format csv --noquotes | head -n 1)
if [ -n "$client_uuid" ]; then "$KCADM" update "clients/$client_uuid" --config "$admin_config" -r outiva -f "$CLIENT_JSON"
else client_uuid=$("$KCADM" create clients --config "$admin_config" -r outiva -f "$CLIENT_JSON" -i); fi

client_secret=$("$KCADM" get "clients/$client_uuid/client-secret" --config "$admin_config" -r outiva --fields value --format csv --noquotes)
if [ -z "$client_secret" ]; then
  client_secret=$("$KCADM" create "clients/$client_uuid/client-secret" --config "$admin_config" -r outiva -o | python3 -c 'import json,sys; print(json.load(sys.stdin)["value"])')
fi
if [ -r "$ACCOUNT_ENV" ]; then session_secret=$(sed -n 's/^HOVERNET_SESSION_SECRET=//p' "$ACCOUNT_ENV"); fi
session_secret=${session_secret:-$(openssl rand -hex 48)}

install -d -o root -g hovernet -m 0750 /etc/hovernet
umask 0077
secrets_tmp=$(mktemp /etc/hovernet/account.env.XXXXXX); trap 'rm -f "$admin_config" "$secrets_tmp"' EXIT
{
  echo 'HOVERNET_OIDC_ISSUER=https://accounts.outiva.com/realms/outiva'
  echo 'HOVERNET_OIDC_BACKCHANNEL_URL=http://127.0.0.1:8180/realms/outiva'
  echo 'HOVERNET_OIDC_CLIENT_ID=hovernet-account'
  printf 'HOVERNET_OIDC_CLIENT_SECRET=%s\n' "$client_secret"
  printf 'HOVERNET_SESSION_SECRET=%s\n' "$session_secret"
  echo 'HOVERNET_ORIGIN=https://hovernet.outiva.com'
  echo 'HOVERNET_OUTIVA_ORIGIN=https://outiva.com'
} > "$secrets_tmp"
chown root:hovernet "$secrets_tmp"; chmod 0640 "$secrets_tmp"; mv -f "$secrets_tmp" "$ACCOUNT_ENV"; trap 'rm -f "$admin_config"' EXIT

install -d -o root -g root -m 0755 /usr/lib/hovernet
install -o root -g root -m 0644 "$HERE/hovernet_account.py" /usr/lib/hovernet/hovernet_account.py
install -o root -g root -m 0644 "$HERE/hovernet-account.service" /etc/systemd/system/hovernet-account.service
systemctl daemon-reload
systemctl enable --now hovernet-account.service
systemctl restart hovernet-account.service
echo "HoverNet account service is running. Secrets are in $ACCOUNT_ENV (not printed)."
echo "Next: install the Apache site from packaging/web/hovernet.outiva.com.conf (it proxies /account/ to 127.0.0.1:9602), test it, reload Apache."
