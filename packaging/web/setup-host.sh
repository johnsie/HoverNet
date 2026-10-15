#!/usr/bin/env bash
# Run from this checkout on 192.168.10.181 as root:
#   sudo bash packaging/web/setup-host.sh [--disable-broken-lifehub]
set -Eeuo pipefail

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SITE="hovernet.outiva.com"
WEBROOT="/var/www/${SITE}"
APACHE_SITE="/etc/apache2/sites-available/${SITE}.conf"
TEMP_SITE="/etc/apache2/sites-available/${SITE}-acme.conf"
TLS_TMP=""

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0 [--disable-broken-lifehub]" >&2
  exit 1
fi

DISABLE_LIFEHUB=0
if [[ ${1:-} == "--disable-broken-lifehub" ]]; then DISABLE_LIFEHUB=1; fi

PUBLIC_SITE_IP=$(dig +short @1.1.1.1 "${SITE}" A | awk '/^[0-9]+(\.[0-9]+){3}$/ { print; exit }')
if [[ -z ${PUBLIC_SITE_IP} ]]; then
  echo "Public DNS for ${SITE} does not resolve. Configure its public A record and NAT/firewall first." >&2
  exit 3
fi

# The current host has an enabled lifehub.conf that references a missing TLS
# private key. Never disable an unrelated vhost without an explicit flag.
if [[ -e /etc/apache2/sites-enabled/lifehub.conf && ! -e /etc/ssl/private/lifehub-origin.key ]]; then
  if [[ ${DISABLE_LIFEHUB} -ne 1 ]]; then
    cat >&2 <<'EOF'
Apache has an enabled lifehub.conf that references missing
/etc/ssl/private/lifehub-origin.key. Restore the intended key, or rerun this
script with --disable-broken-lifehub if that site is obsolete and may be taken
offline. The site will be disabled reversibly with a2dissite.
EOF
    exit 2
  fi
  a2dissite lifehub.conf
fi

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y apache2 openssl python3-websockets

for module in ssl headers expires proxy proxy_http proxy_wstunnel; do a2enmod "${module}"; done
install -d -o root -g www-data -m 0755 "${WEBROOT}/releases" "${WEBROOT}/acme"

# The public certificate belongs to the HAProxy frontend. Apache presents a
# local self-signed certificate only on the HTTPS backend connection.
TLS_TMP=$(mktemp -d /root/hovernet-origin-cert.XXXXXX)
trap 'rm -rf "${TLS_TMP}"' EXIT
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 3650 \
  -subj "/CN=${SITE}" \
  -addext "subjectAltName=DNS:${SITE},IP:192.168.10.181" \
  -keyout "${TLS_TMP}/hovernet-origin.key" \
  -out "${TLS_TMP}/hovernet-origin.crt"
install -o root -g root -m 0644 "${TLS_TMP}/hovernet-origin.crt" /etc/ssl/certs/hovernet-origin.crt
install -o root -g ssl-cert -m 0640 "${TLS_TMP}/hovernet-origin.key" /etc/ssl/private/hovernet-origin.key
rm -rf "${TLS_TMP}"
trap - EXIT

if ! id hovernet >/dev/null 2>&1; then
  echo "The existing RaceServer package account 'hovernet' is missing; install/fix that service first." >&2
  exit 4
fi
if ! systemctl list-unit-files hovernet-raceserver.service --no-legend | grep -q hovernet-raceserver; then
  echo "hovernet-raceserver.service is missing; the WebSocket gateway requires the existing RaceServer." >&2
  exit 5
fi

install -d -o root -g root -m 0755 /usr/lib/hovernet
install -o root -g root -m 0644 "${SOURCE_DIR}/websocket_gateway.py" /usr/lib/hovernet/websocket_gateway.py
install -o root -g root -m 0644 "${SOURCE_DIR}/hovernet-websocket-gateway.service" /etc/systemd/system/hovernet-websocket-gateway.service
install -o root -g root -m 0755 "${SOURCE_DIR}/hovernet-web-deploy" /usr/local/sbin/hovernet-web-deploy
install -o root -g root -m 0644 "${SOURCE_DIR}/hovernet.outiva.com.conf" "${APACHE_SITE}"
rm -f "${TEMP_SITE}"
if [[ -L "/etc/apache2/sites-enabled/${SITE}-acme.conf" ]]; then a2dissite "${SITE}-acme.conf"; fi
a2ensite "${SITE}.conf"

if id gitlab-runner >/dev/null 2>&1; then
  printf '%s\n' 'gitlab-runner ALL=(root) NOPASSWD: /usr/local/sbin/hovernet-web-deploy' \
    >/etc/sudoers.d/hovernet-web-deploy
  chmod 0440 /etc/sudoers.d/hovernet-web-deploy
  visudo -cf /etc/sudoers.d/hovernet-web-deploy
else
  echo "gitlab-runner account missing; skipped the scoped deployment sudo rule." >&2
fi

apache2ctl configtest
systemctl daemon-reload
systemctl enable --now hovernet-websocket-gateway.service
systemctl reload apache2
echo "HoverNet web host is configured. Deploy a build artifact through the protected GitLab deploy:web job."
