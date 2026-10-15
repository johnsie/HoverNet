#!/usr/bin/env bash
# One-time tuning for the shared 8-vCPU / 16-GB HoverNet host (jm-home-st07).
# Idempotent: safe to run again. Run as root: sudo bash packaging/web/tune-build-host.sh
#
#   1. Lower CI build priority so builds cannot starve RaceServer or the other services.
#   2. Cap the systemd journal (it had grown to ~4 GB).
#   3. Install ccache for the Linux/sanitizer builds.
#   4. Confirm runner concurrency is 2 and report swap, disk and I/O pressure.
set -Eeuo pipefail

JOURNAL_MAX=500M
DROPIN_DIR=/etc/systemd/system/gitlab-runner.service.d
RUNNER_CONFIG=/etc/gitlab-runner/config.toml

if [[ ${EUID} -ne 0 ]]; then
  echo "Run as root: sudo bash $0" >&2
  exit 1
fi

echo "== 1. Runner priority drop-in"
mkdir -p "${DROPIN_DIR}"
cat > "${DROPIN_DIR}/10-hovernet-priority.conf" <<'CONF'
[Service]
Nice=10
CPUWeight=50
IOWeight=50
IOSchedulingClass=idle
CONF
systemctl daemon-reload

echo "== 2. Journal size cap (${JOURNAL_MAX})"
mkdir -p /etc/systemd/journald.conf.d
cat > /etc/systemd/journald.conf.d/10-hovernet-size.conf <<CONF
[Journal]
SystemMaxUse=${JOURNAL_MAX}
CONF
systemctl restart systemd-journald
journalctl --vacuum-size="${JOURNAL_MAX}" >/dev/null
journalctl --disk-usage

echo "== 3. ccache"
if ! command -v ccache >/dev/null; then
  apt-get install -y ccache
fi
ccache --version | head -1

echo "== 4. Runner concurrency"
if [[ -f ${RUNNER_CONFIG} ]]; then
  grep -E '^\s*concurrent\s*=' "${RUNNER_CONFIG}" || echo "concurrent is not set; run packaging/web/configure-runner-concurrency.sh"
else
  echo "${RUNNER_CONFIG} not found" >&2
fi

# Restart only when no CI job is mid-run, otherwise the job would be killed.
if pgrep -u gitlab-runner -f 'gitlab-runner-helper|emcc|cmake|ninja|make' >/dev/null; then
  echo "A CI job appears to be running; priority change applies after the next runner restart:"
  echo "  sudo systemctl restart gitlab-runner"
else
  systemctl restart gitlab-runner
  echo "gitlab-runner restarted with new priority limits."
fi

echo "== Status"
systemctl show gitlab-runner -p Nice -p CPUWeight -p IOWeight -p IOSchedulingClass
nproc
free -h | sed -n 1,3p
swapon --show
df -h / | tail -1
head -2 /proc/pressure/io
