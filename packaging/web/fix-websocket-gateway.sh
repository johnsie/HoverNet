#!/usr/bin/env bash
# Install the Python 3.10+ compatibility fix and restart the browser gateway.
set -Eeuo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
install -o root -g root -m 0644 \
  "${SOURCE_DIR}/websocket_gateway.py" \
  /usr/lib/hovernet/websocket_gateway.py
systemctl restart hovernet-websocket-gateway.service
systemctl is-active --quiet hovernet-websocket-gateway.service
echo "HoverNet WebSocket gateway restarted successfully."
journalctl -u hovernet-websocket-gateway.service -n 10 --no-pager
