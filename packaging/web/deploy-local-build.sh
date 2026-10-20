#!/usr/bin/env bash
# Deploy a previously built local Emscripten bundle to the configured Apache host.
# Run from the repository checkout as the normal SSH user; sudo prompts on the server.
set -Eeuo pipefail

REMOTE="${1:-john@192.168.10.181}"
BUILD_DIR="${HOVERNET_WEB_BUILD_DIR:-/tmp/hovernet-web-build}"
RELEASE_ID="manual-$(date -u +%Y%m%dT%H%M%SZ)"
ARCHIVE_NAME="hovernet-web-${RELEASE_ID}.tar.gz"
STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hovernet-web-deploy.XXXXXX")"
ARCHIVE_PATH="${STAGE_DIR}/${ARCHIVE_NAME}"
REMOTE_TMP="/tmp/${ARCHIVE_NAME}"
REMOTE_DEPLOYER_TMP="/tmp/hovernet-web-deploy"
REMOTE_RUNNER_TMP="/tmp/hovernet-web-deploy-run.sh"
REMOTE_ARTIFACT="/home/gitlab-runner/builds/manual/${ARCHIVE_NAME}"

cleanup() { rm -rf -- "${STAGE_DIR}"; }
trap cleanup EXIT

for required in HoverNet.html HoverNet.js HoverNet.wasm HoverNet.data; do
  if [[ ! -f "${BUILD_DIR}/${required}" ]]; then
    echo "Missing ${BUILD_DIR}/${required}; build the web target first." >&2
    exit 2
  fi
done

cp -- "${BUILD_DIR}/HoverNet.html" "${STAGE_DIR}/index.html"
sed -i "s/__HN_BUILD__/${RELEASE_ID}/; s/__HN_ORDER__/$(git log -1 --format=%ct)/" "${STAGE_DIR}/index.html"   # build id the page checks against the server (shell.html)
cp -- "${BUILD_DIR}/HoverNet.js" "${BUILD_DIR}/HoverNet.wasm" "${BUILD_DIR}/HoverNet.data" "${BUILD_DIR}/outiva-auth.js" "${STAGE_DIR}/"
tar -czf "${ARCHIVE_PATH}" -C "${STAGE_DIR}" index.html HoverNet.js HoverNet.wasm HoverNet.data outiva-auth.js

echo "Uploading ${ARCHIVE_NAME} to ${REMOTE}..."
scp -F /dev/null -- "${ARCHIVE_PATH}" "${REMOTE}:${REMOTE_TMP}"
scp -F /dev/null -- "$(dirname "$0")/hovernet-web-deploy" "${REMOTE}:${REMOTE_DEPLOYER_TMP}"
cat >"${STAGE_DIR}/remote-deploy.sh" <<'REMOTE_SCRIPT'
#!/usr/bin/env bash
set -Eeuo pipefail
archive_name="$1"
remote_tmp="$2"
deployer_tmp="$3"
runner_artifact="$4"

if [[ ! "${archive_name}" =~ ^hovernet-web-manual-[0-9]{8}T[0-9]{6}Z\.tar\.gz$ ]]; then
  echo "Unexpected release archive name." >&2
  exit 2
fi
if [[ "${remote_tmp}" != "/tmp/${archive_name}" || "${deployer_tmp}" != "/tmp/hovernet-web-deploy" || "${runner_artifact}" != "/home/gitlab-runner/builds/manual/${archive_name}" ]]; then
  echo "Unexpected deployment path." >&2
  exit 2
fi

install -o root -g root -m 0755 "${deployer_tmp}" /usr/local/sbin/hovernet-web-deploy
install -d -o gitlab-runner -g gitlab-runner -m 0750 /home/gitlab-runner/builds/manual
install -o gitlab-runner -g gitlab-runner -m 0640 "${remote_tmp}" "${runner_artifact}"
sudo -u gitlab-runner sudo -n /usr/local/sbin/hovernet-web-deploy "${runner_artifact}"
rm -f -- "${remote_tmp}" "${deployer_tmp}" "${runner_artifact}" /tmp/hovernet-web-deploy-run.sh
REMOTE_SCRIPT
chmod 0755 "${STAGE_DIR}/remote-deploy.sh"
scp -F /dev/null -- "${STAGE_DIR}/remote-deploy.sh" "${REMOTE}:${REMOTE_RUNNER_TMP}"

ssh -F /dev/null -tt "${REMOTE}" "sudo bash ${REMOTE_RUNNER_TMP} ${ARCHIVE_NAME} ${REMOTE_TMP} ${REMOTE_DEPLOYER_TMP} ${REMOTE_ARTIFACT}"

echo "Deployment complete. Check https://hovernet.outiva.com/"
