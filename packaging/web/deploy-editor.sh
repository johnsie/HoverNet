#!/usr/bin/env bash
# Puts the browser track editor on the web host, from THIS computer, in one command:
#
#     bash packaging/web/deploy-editor.sh                  # john@192.168.10.181, the files from build/web
#     bash packaging/web/deploy-editor.sh --dry-run        # show every step, change nothing
#     bash packaging/web/deploy-editor.sh --remove         # take it down again (the Apache snippet and the files)
#     bash packaging/web/deploy-editor.sh john@other-host --from path/to/folder
#
# Build the files first (they are WebAssembly, so they do not depend on the host's libraries):
#     cd build/web && cmake --build . --target HoverNetTrackEditor
# It copies HoverNetTrackEditor.html/.js/.wasm to /var/www/hovernet.outiva.com/editor/ (the page is index.html there), enables the Apache snippet after testing the
# Apache configuration (undoing it if the test fails), and checks the page answers over https with the headers the editor needs. One sudo password prompt on the host.
# Nothing in the game, the race server, the account service or the site's own Apache file is touched.
set -euo pipefail
host="john@192.168.10.181"; dry=no; remove=no; from="build/web"
while [ $# -gt 0 ]; do
  case "$1" in --dry-run) dry=yes; shift;; --remove) remove=yes; shift;; --from) from="${2:?--from needs a folder}"; shift 2;; -*) echo "unknown option $1"; exit 2;; *) host="$1"; shift;; esac
done
cd "$(dirname "$0")/../.."
run() { echo "+ $*"; [ "$dry" = yes ] || "$@"; }
if [ "$remove" = yes ]; then
  run ssh -t "$host" 'sudo bash -c "a2disconf hovernet-editor 2>/dev/null; apache2ctl configtest && systemctl reload apache2; rm -rf /var/www/hovernet.outiva.com/editor /etc/apache2/conf-available/hovernet-editor.conf; echo removed"'
  exit 0
fi
for f in HoverNetTrackEditor.html HoverNetTrackEditor.js HoverNetTrackEditor.wasm; do [ -s "$from/$f" ] || { echo "STOP: $from/$f is missing: build it first (see the top of this script)"; exit 1; }; done
echo "editor build: $(ls -l --time-style=+%F\ %T "$from/HoverNetTrackEditor.wasm" | awk '{print $6, $7}'), $(du -h "$from/HoverNetTrackEditor.wasm" | cut -f1) of WebAssembly"
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
cp "$from/HoverNetTrackEditor.js" "$from/HoverNetTrackEditor.wasm" "$stage/"; cp "$from/HoverNetTrackEditor.html" "$stage/index.html"
cp packaging/web/editor.conf "$stage/hovernet-editor.conf"
# the page names its script and wasm by their built names: keep them, the page is index.html
run ssh "$host" 'rm -rf /tmp/hn-editor-up && mkdir -p /tmp/hn-editor-up'
run scp -q "$stage/index.html" "$stage/HoverNetTrackEditor.js" "$stage/HoverNetTrackEditor.wasm" "$stage/hovernet-editor.conf" "$host:/tmp/hn-editor-up/"
remote='set -euo pipefail
sudo -v
sudo install -d -m 0755 /var/www/hovernet.outiva.com/editor
sudo install -m 0644 /tmp/hn-editor-up/index.html /tmp/hn-editor-up/HoverNetTrackEditor.js /tmp/hn-editor-up/HoverNetTrackEditor.wasm /var/www/hovernet.outiva.com/editor/
sudo install -m 0644 /tmp/hn-editor-up/hovernet-editor.conf /etc/apache2/conf-available/hovernet-editor.conf
sudo a2enconf hovernet-editor >/dev/null
if sudo apache2ctl configtest 2>&1 | tail -1 | grep -q "Syntax OK"; then sudo systemctl reload apache2; echo "apache: configuration ok, reloaded"
else echo "apache: the configuration test FAILED, undoing the snippet"; sudo a2disconf hovernet-editor >/dev/null; sudo apache2ctl configtest || true; exit 1; fi
rm -rf /tmp/hn-editor-up'
if [ "$dry" = yes ]; then echo "+ ssh -t $host <install the files in /var/www/hovernet.outiva.com/editor, enable the Apache snippet after a configuration test>"; else ssh -t "$host" "$remote"; fi
echo; echo "Checking https://hovernet.outiva.com/editor/ ..."
run bash -c "curl -sI -m 20 https://hovernet.outiva.com/editor/ | tr -d '\r' | grep -i -E '^(HTTP|content-type|cross-origin-(opener|embedder)-policy):'"
echo; echo "Open https://hovernet.outiva.com/editor/ in a browser: it starts with a new loop track; File > Open from your computer / Save to your computer."
