#!/usr/bin/env bash
# Tune the existing shell runner for a 8-core / 16-GB HoverNet host.
set -Eeuo pipefail

CONFIG=/etc/gitlab-runner/config.toml
SLOTS=${SLOTS:-3}

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi
if [[ ! -f ${CONFIG} ]]; then
  echo "GitLab Runner configuration was not found at ${CONFIG}" >&2
  exit 2
fi

BACKUP="${CONFIG}.before-hovernet-concurrency-$(date -u +%Y%m%dT%H%M%SZ)"
cp -a -- "${CONFIG}" "${BACKUP}"

python3 - "${CONFIG}" "${SLOTS}" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
slots = int(sys.argv[2])
text = path.read_text()

global_setting = f"concurrent = {slots}"
if re.search(r"(?m)^\s*concurrent\s*=.*$", text):
    text = re.sub(r"(?m)^\s*concurrent\s*=.*$", global_setting, text, count=1)
else:
    text = global_setting + "\n" + text

runner_start = text.find("[[runners]]")
if runner_start < 0:
    raise SystemExit("No [[runners]] section exists in config.toml")
runner_end = text.find("[[runners]]", runner_start + len("[[runners]]"))
if runner_end < 0:
    runner_end = len(text)

section = text[runner_start:runner_end]
for key in ("limit", "request_concurrency"):
    setting = f"  {key} = {slots}"
    pattern = rf"(?m)^\s*{key}\s*=.*$"
    if re.search(pattern, section):
        section = re.sub(pattern, setting, section, count=1)
    else:
        newline = section.find("\n") + 1
        section = section[:newline] + setting + "\n" + section[newline:]

path.write_text(text[:runner_start] + section + text[runner_end:])
PY

if ! gitlab-runner verify; then
  cp -a -- "${BACKUP}" "${CONFIG}"
  echo "Runner verification failed; restored ${BACKUP}" >&2
  exit 3
fi

systemctl restart gitlab-runner.service
systemctl is-active --quiet gitlab-runner.service
echo "GitLab Runner now allows ${SLOTS} concurrent jobs."
echo "Backup: ${BACKUP}"
grep -E '^[[:space:]]*(concurrent|limit|request_concurrency)[[:space:]]*=' "${CONFIG}"
