#!/usr/bin/env bash
# Run ONCE, as root, on the host that will run the HoverNet 3.0 server (the same host as the race server, 192.168.10.181):
#
#   sudo packaging/server/install-deploy-helper.sh
#
# It installs the root-owned deploy wrapper and lets the GitLab runner account run that wrapper, and nothing else, with sudo. Run it from a
# checkout of this repository. It changes no running service.
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "Run this as root (sudo)." >&2; exit 1; fi
here="$(cd "$(dirname "$0")" && pwd)"
wrapper="$here/../debian/hovernet-server-deploy"
[[ -f $wrapper ]] || { echo "Cannot find $wrapper: run this from a checkout of the repository." >&2; exit 1; }
id gitlab-runner >/dev/null 2>&1 || { echo "There is no gitlab-runner account on this host; nothing installed." >&2; exit 1; }

install -o root -g root -m 0755 "$wrapper" /usr/local/sbin/hovernet-server-deploy
rule=/etc/sudoers.d/hovernet-server-deploy
printf '%s\n' 'gitlab-runner ALL=(root) NOPASSWD: /usr/local/sbin/hovernet-server-deploy' > "$rule.tmp"
chmod 0440 "$rule.tmp"
visudo -cf "$rule.tmp"          # refuses a broken rule before it is put in place
mv "$rule.tmp" "$rule"
echo "Installed /usr/local/sbin/hovernet-server-deploy and $rule."
echo "Next: install the first hovernet-server package by hand, as described in docs/operators.md (\"Deploying from GitLab\")."
