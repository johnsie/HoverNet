#!/usr/bin/env bash
# Makes the HoverNet 3.0 server on the host reachable from the internet on port 9610 for the Linux and Windows clients (docs/operators.md), encrypted only:
# it listens on all addresses, creates the server's identity key and refuses plain TCP connections. The web client is not affected (Apache still reaches the
# server on loopback and the WebSocket path is handled before the encryption rule). Run from this computer:
#   scripts/open-server-port.sh            (host john@192.168.10.181; HOVERNET_SERVER_HOST=user@host overrides)
# It shows what it will change and asks before restarting the server (a restart waits for races under way, up to the drain time-out).
set -euo pipefail
HOST="${HOVERNET_SERVER_HOST:-john@192.168.10.181}"
ssh -t "$HOST" 'set -e
  CONF=/etc/hovernet/server.conf
  echo "--- current network lines in $CONF"; grep -nE "^[# ]*(bind|port|identity-file|require-encryption|ws-origin)\b" "$CONF" || true
  echo; read -r -p "Listen on all addresses, create the identity key and require encryption, then restart hovernet-server? [y/N] " answer
  [ "$answer" = "y" ] || { echo "nothing changed"; exit 1; }
  sudo cp "$CONF" "$CONF.before-open-port"
  sudo sed -i -E "s/^[# ]*bind .*/bind 0.0.0.0/" "$CONF"
  grep -qE "^identity-file" "$CONF" || echo "identity-file /var/lib/hovernet/identity.key" | sudo tee -a "$CONF" >/dev/null
  grep -qE "^require-encryption" "$CONF" || echo "require-encryption" | sudo tee -a "$CONF" >/dev/null
  sudo sed -i -E "s|^# *identity-file .*|identity-file /var/lib/hovernet/identity.key|; s|^# *require-encryption|require-encryption|" "$CONF"
  if command -v ufw >/dev/null && sudo ufw status | grep -q "Status: active"; then sudo ufw allow 9610/tcp; fi
  sudo systemctl restart hovernet-server
  sleep 3
  systemctl is-active hovernet-server
  echo; echo "--- the server key (give this to me; it is public, the identity file is the secret):"
  journalctl -u hovernet-server -n 60 --no-pager | grep -i "server key" | tail -n 1
  echo; ss -ltn | grep ":9610 " || echo "NOT LISTENING on 9610: check journalctl -u hovernet-server"
  echo "(a copy of the old configuration is in $CONF.before-open-port)"'
echo; echo "From here, test: timeout 5 bash -c 'echo > /dev/tcp/hovernet.outiva.com/9610' && echo reachable"
