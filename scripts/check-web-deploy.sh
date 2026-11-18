#!/usr/bin/env bash
# Post-deploy checks for the web client (docs/web-client-roadmap.md, phase 5 item 7).
# Usage: scripts/check-web-deploy.sh [base-url]   (default https://hovernet.outiva.com)
# Checks: landing page, security headers, asset MIME types and sizes, HTTP -> HTTPS redirect, the
# WebSocket upgrade for allowed and foreign origins, and that the page and the wasm match each other.
# HOVERNET_CHECK_LOCAL=1 checks the Apache on this machine (127.0.0.1, self-signed origin certificate)
# instead of the public name: the CI runner sits on the same private network as the server and cannot
# reach its own public address.
set -uo pipefail
base="${1:-https://hovernet.outiva.com}"
base="${base%/}"
host="${base#*://}"
fail=0
opts=()
if [ "${HOVERNET_CHECK_LOCAL:-}" = "1" ]; then
  opts=(-k --resolve "${host}:443:127.0.0.1" --resolve "${host}:80:127.0.0.1")
fi
ok()  { echo "PASS: $1"; }
bad() { echo "FAIL: $1"; fail=1; }
header() { curl -sI "${opts[@]}" --max-time 20 "$1" | tr -d '\r' | grep -i "^$2:" | head -1 | cut -d: -f2- | sed 's/^ *//'; }
status() { curl -sI "${opts[@]}" --max-time 20 -o /dev/null -w '%{http_code}' "$1"; }

[ "$(status "$base/")" = "200" ] && ok "landing page returns 200" || bad "landing page did not return 200"
html="$(curl -s "${opts[@]}" --max-time 30 "$base/")"
echo "$html" | grep -q "HoverNet.js" && ok "landing page loads HoverNet.js" || bad "landing page does not reference HoverNet.js"

for h in x-content-type-options content-security-policy cross-origin-opener-policy cross-origin-embedder-policy; do
  [ -n "$(header "$base/" "$h")" ] && ok "header $h is set" || bad "header $h is missing"
done
csp="$(header "$base/" content-security-policy)"
# Framing is controlled by the CSP's frame-ancestors (it can name the one site allowed to embed the page, which X-Frame-Options cannot); X-Frame-Options is accepted too.
{ echo "$csp" | grep -q "frame-ancestors" || [ -n "$(header "$base/" x-frame-options)" ]; } && ok "framing is restricted (frame-ancestors or x-frame-options)" || bad "framing is not restricted: no frame-ancestors in the CSP and no x-frame-options"
echo "$csp" | grep -q "wasm-unsafe-eval" && ok "CSP allows WebAssembly" || bad "CSP does not allow WebAssembly"
echo "$csp" | grep -q "'unsafe-eval'" && bad "CSP allows eval" || ok "CSP forbids eval (the client does not use it)"

check_asset() { # file expected-type min-bytes
  local type size
  type="$(header "$base/$1" content-type)"; size="$(header "$base/$1" content-length)"
  [[ "$type" == $2* ]] && ok "$1 is served as $2" || bad "$1 content-type is '$type', expected $2"
  [ "${size:-0}" -ge "$3" ] 2>/dev/null && ok "$1 is ${size} bytes" || bad "$1 is too small (${size:-?} bytes)"
}
check_asset HoverNet.js application/javascript 100000
check_asset HoverNet.wasm application/wasm 1000000
check_asset HoverNet.data application/octet-stream 5000000
# Compression and revalidation (warnings, not failures: they cost download time, they do not break the page). The data file is 26 MB raw and about 17 MB compressed; a browser
# that already has the wasm asks "unchanged?" with the file's ETag and should get a 304, not the 2 MB again (Apache's -gzip ETag suffix broke that: DeflateAlterETag NoChange).
warn() { echo "WARN: $1"; }
enc="$(curl -sI "${opts[@]}" --max-time 20 -H 'Accept-Encoding: gzip' "$base/HoverNet.data" | tr -d '\r' | grep -i '^content-encoding:' | head -1)"
echo "$enc" | grep -qi gzip && ok "HoverNet.data is compressed" || warn "HoverNet.data is served uncompressed (about 25 MB on a first visit instead of about 17; see hovernet.outiva.com.conf)"
etag="$(curl -sI "${opts[@]}" --max-time 20 -H 'Accept-Encoding: gzip' "$base/HoverNet.wasm" | tr -d '\r' | grep -i '^etag:' | head -1 | cut -d: -f2- | sed 's/^ *//')"
if [ -n "$etag" ]; then
  code="$(curl -s "${opts[@]}" --max-time 20 -o /dev/null -w '%{http_code}' -H 'Accept-Encoding: gzip' -H "If-None-Match: $etag" "$base/HoverNet.wasm")"
  [ "$code" = "304" ] && ok "an unchanged HoverNet.wasm answers 304 to its ETag" || warn "HoverNet.wasm answers $code to its own ETag (it should be 304: the browser downloads it again on every visit)"
fi
cc="$(header "$base/HoverNet.wasm" cache-control)"
[ -n "$cc" ] && ok "wasm cache-control: $cc" || bad "wasm has no cache-control"

if [[ "$base" == https://* ]]; then
  [ "$(status "http://$host/")" = "302" ] || [ "$(status "http://$host/")" = "301" ] && ok "http redirects to https" || bad "http does not redirect to https"
  key="dGhlIHNhbXBsZSBub25jZQ=="
  ws() { curl -s "${opts[@]}" --http1.1 --max-time 10 -o /dev/null -w '%{http_code}' -H "Connection: Upgrade" -H "Upgrade: websocket" \
          -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: $key" -H "Origin: $1" "$base/race/"; }
  [ "$(ws "$base")" = "101" ] && ok "WebSocket gateway upgrades for the site's origin" || bad "WebSocket upgrade failed for the site's origin"
  [ "$(ws https://evil.example)" = "403" ] && ok "WebSocket gateway refuses a foreign origin" || bad "WebSocket gateway accepted a foreign origin"
fi
[ "$fail" = 0 ] && echo "web deployment checks passed" || echo "web deployment checks FAILED"
exit "$fail"
