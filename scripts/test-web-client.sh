#!/usr/bin/env bash
# Headless-browser tests of a built web client. Usage: scripts/test-web-client.sh <web-dir> [out-dir] [HoverNetAuthServer]
# With a HoverNetAuthServer binary it also plays a race on a HoverNet 3.0 server from the browser over WebSocket.
# <web-dir> holds index.html, HoverNet.js, HoverNet.wasm and HoverNet.data (the CI web artifact, unpacked).
# Needs node (18+, or the one inside Emscripten's emsdk) and Chrome/Chromium; exits 77 (skipped) without them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
web_dir="${1:?usage: $0 <web-dir> [out-dir]}"
out_dir="${2:-web-smoke-output}"
node_bin="$(command -v node || true)"
if [ -z "$node_bin" ] && [ -n "${EMSDK:-}" ]; then node_bin="$(ls "$EMSDK"/node/*/bin/node 2>/dev/null | head -1 || true)"; fi
if [ -z "$node_bin" ]; then echo "SKIP: node not found"; exit 77; fi
"$node_bin" "$here/web-smoke/web-stale-page.mjs" || exit $?
"$node_bin" "$here/web-smoke/web-smoke.mjs" "$web_dir" "$out_dir" || exit $?
if [ -n "${3:-}" ]; then
  "$node_bin" "$here/web-smoke/web-servers-screen.mjs" "$web_dir" "$out_dir/servers" "$3" || exit $?
fi
