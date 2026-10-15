#!/usr/bin/env bash
# Install an origin TLS certificate for HAProxy -> Apache HTTPS traffic.
# Run on 192.168.10.181 with: sudo bash fix-lifehub-backend-tls.sh
set -Eeuo pipefail

CERT=/etc/ssl/certs/lifehub-origin.crt
KEY=/etc/ssl/private/lifehub-origin.key
HAD_CERT=0
HAD_KEY=0

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi
command -v openssl >/dev/null
getent group ssl-cert >/dev/null

BACKUP=$(mktemp -d /root/lifehub-tls-backup.XXXXXX)

if [[ -e ${CERT} ]]; then cp -a "${CERT}" "${BACKUP}/lifehub-origin.crt"; HAD_CERT=1; fi
if [[ -e ${KEY} ]]; then cp -a "${KEY}" "${BACKUP}/lifehub-origin.key"; HAD_KEY=1; fi

restore_on_error() {
  status=$?
  if [[ ${status} -ne 0 ]]; then
    if [[ ${HAD_CERT} -eq 1 ]]; then cp -a "${BACKUP}/lifehub-origin.crt" "${CERT}"; else rm -f "${CERT}"; fi
    if [[ ${HAD_KEY} -eq 1 ]]; then cp -a "${BACKUP}/lifehub-origin.key" "${KEY}"; else rm -f "${KEY}"; fi
    echo "TLS installation failed; the previous certificate and key state was restored." >&2
  fi
  rm -rf "${BACKUP}"
  exit "${status}"
}
trap restore_on_error EXIT

umask 077
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 3650 \
  -subj "/CN=lifehub.outiva.com" \
  -addext "subjectAltName=DNS:lifehub.outiva.com,IP:192.168.10.181" \
  -keyout "${BACKUP}/lifehub-origin.key.new" \
  -out "${BACKUP}/lifehub-origin.crt.new"
openssl x509 -in "${BACKUP}/lifehub-origin.crt.new" -noout \
  -checkhost lifehub.outiva.com -checkip 192.168.10.181

install -o root -g root -m 0644 "${BACKUP}/lifehub-origin.crt.new" "${CERT}"
install -o root -g ssl-cert -m 0640 "${BACKUP}/lifehub-origin.key.new" "${KEY}"
apache2ctl configtest
systemctl reload apache2
echo "Installed self-signed origin TLS certificate and reloaded Apache. Configure HAProxy to trust ${CERT} when verifying its HTTPS backend."
