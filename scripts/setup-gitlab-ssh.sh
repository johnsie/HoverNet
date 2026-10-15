#!/usr/bin/env bash
# Run on YOUR machine (not the server) to enable key-based SSH to the GitLab host.
#   bash scripts/setup-gitlab-ssh.sh [user] [host]
# Installs your default public key on the server (you type the account password once),
# adds a "gitlab-server" alias to ~/.ssh/config, then verifies passwordless login.
# This gives anything that can use your private key (including Claude Code here)
# the same shell access you have as that account. It does NOT set up sudo.
set -Eeuo pipefail

SSH_USER=${1:-john}
HOST=${2:-192.168.10.46}
ALIAS=gitlab-server
KEY=${HOME}/.ssh/id_ed25519

[[ -f ${KEY} ]] || { echo "Missing ${KEY}; create one with: ssh-keygen -t ed25519" >&2; exit 1; }
[[ -f ${KEY}.pub ]] || ssh-keygen -y -f "${KEY}" > "${KEY}.pub"

echo "== Installing ${KEY}.pub on ${SSH_USER}@${HOST} (enter the account password when asked)"
ssh-copy-id -i "${KEY}.pub" "${SSH_USER}@${HOST}"

CONFIG=${HOME}/.ssh/config
touch "${CONFIG}"; chmod 600 "${CONFIG}"
if ! grep -q "^Host ${ALIAS}\$" "${CONFIG}"; then
  cat >> "${CONFIG}" <<CONF

Host ${ALIAS}
    HostName ${HOST}
    User ${SSH_USER}
    IdentityFile ${KEY}
    IdentitiesOnly yes
CONF
  echo "== Added '${ALIAS}' to ${CONFIG}"
fi

echo "== Verifying key-based login"
ssh -o BatchMode=yes "${ALIAS}" 'hostname; id -un; command -v gitlab-rails || echo "gitlab-rails not on PATH for this user"'
echo "Done. Tell Claude: use 'ssh ${ALIAS}'."
