#!/usr/bin/env bash
# Installs (or updates, or removes) the Track Market service on the web/race host, from THIS computer, in one command:
#
#     bash packaging/web/install-market.sh                       # john@192.168.10.181
#     bash packaging/web/install-market.sh john@other-host
#     bash packaging/web/install-market.sh --dry-run             # print every step, change nothing
#     bash packaging/web/install-market.sh --uninstall           # stop and disable it and remove the Apache snippet (keeps the data)
#
# What it does: sends the committed source (git archive HEAD, so exactly what is in git) to the host, builds the few programs the checks need there, in
# /opt/hovernet-market (the track packer, validator and replay checker, with the object plug-in), installs the service in /usr/lib/hovernet, creates
# /etc/hovernet/market.env (the session secret is copied from account.env, which is how the market recognises a signed-in author), starts it, adds the Apache
# snippet after testing the Apache configuration (and undoes it if the test fails), and checks the answer locally and through https.
# It will ask for your sudo password on the host (the sudo steps run in one ssh session). No containers are used. Nothing in the game or the race server is touched.
# The market starts CLOSED: nobody can publish until you put account names in /etc/hovernet/market-authors.txt on the host (the last lines tell you how).
set -euo pipefail
host="john@192.168.10.181"; dry=no; uninstall=no
for a in "$@"; do case "$a" in --dry-run) dry=yes;; --uninstall) uninstall=yes;; -*) echo "unknown option $a"; exit 2;; *) host="$a";; esac; done
cd "$(dirname "$0")/../.."
step() { printf '\n==== %s\n' "$*"; }
run()  { echo "+ $*"; [ "$dry" = yes ] || "$@"; }

if [ "$uninstall" = yes ]; then
  step "Removing the Track Market from $host (data in /var/lib/hovernet-market and the published files are kept)"
  run ssh -t "$host" 'sudo bash -c "systemctl disable --now hovernet-market hovernet-market-sync.timer 2>/dev/null; a2disconf hovernet-market 2>/dev/null; apache2ctl configtest && systemctl reload apache2; rm -f /etc/systemd/system/hovernet-market.service /etc/systemd/system/hovernet-market-sync.service /etc/systemd/system/hovernet-market-sync.timer /etc/apache2/conf-available/hovernet-market.conf; systemctl daemon-reload; echo removed"'
  exit 0
fi

step "Checking this computer"
git diff --quiet HEAD -- packaging/web scripts NetTarget CMakeLists.txt || echo "note: you have uncommitted changes; only what is committed (HEAD) is sent"
commit="$(git rev-parse --short HEAD)"; echo "commit to install: $commit ($(git log -1 --format=%s))"
python3 scripts/test-web-market.py > /dev/null && echo "service tests pass here" || { echo "STOP: the service's own tests fail here"; exit 1; }
[ "$dry" = yes ] || ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" true || { echo "STOP: cannot ssh to $host without a password prompt (set up your ssh key)"; exit 1; }

step "1/4 Packing the source ($commit)"
tarball="$(mktemp /tmp/hn-market-src.XXXXXX.tgz)"; trap 'rm -f "$tarball"' EXIT
git archive --format=tar HEAD | gzip -9 > "$tarball"; echo "source: $(du -h "$tarball" | cut -f1)"
run scp -q "$tarball" "$host:/tmp/hn-market-src.tgz"

step "2/4 Building and installing on $host (asks for your sudo password once; the build takes a few minutes and runs niced so races are not disturbed)"
# The check programs read the game's data files from the source folder they were built in (its path is compiled in), and the service cannot see
# anyone's home folder, so the source is built where it will live: /opt/hovernet-market. The old copy is kept until the new one is built.
remote_script="$(cat <<'REMOTE'
set -euo pipefail
me="$(id -un)"; new=/opt/hovernet-market; old=/opt/hovernet-market.old
sudo -v
id hovernet >/dev/null 2>&1 || { echo "the hovernet user does not exist (the account service should have made it)"; exit 1; }
grep -qE '^HOVERNET_SESSION_SECRET=' <(sudo cat /etc/hovernet/account.env) || { echo "no HOVERNET_SESSION_SECRET in /etc/hovernet/account.env: set up the account service first (packaging/web/configure-account-oidc.sh)"; exit 1; }
echo "-- building"
sudo rm -rf "$old"; [ -d "$new" ] && sudo mv "$new" "$old"
sudo install -d -o "$me" -g "$me" -m 0755 "$new"
restore() { echo "build failed: putting the previous version back"; sudo rm -rf "$new"; [ -d "$old" ] && sudo mv "$old" "$new"; }
trap 'restore' ERR
tar xzf /tmp/hn-market-src.tgz -C "$new"
( cd "$new" && nice -n 15 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON > build-configure.log 2>&1 \
  && nice -n 15 cmake --build build -j2 --target HoverNetTrackPack HoverNetTrackValidator HoverNetReplay HoverNetHeadlessObjFac1 HoverNetMazeCompiler HoverNetDriver 2>&1 | tail -3 )
[ -x "$new/build/HoverNetTrackPack" ] && [ -x "$new/build/HoverNetReplay" ] && [ -x "$new/build/HoverNetTrackValidator" ] && [ -x "$new/build/HoverNetMazeCompiler" ] && [ -x "$new/build/HoverNetDriver" ] && [ -f "$new/build/ObjFac1.so" ] || { echo "the check programs were not built: see $new/build-configure.log"; false; }
echo "-- the checks work on this machine"
sample="$(mktemp -d)"
"$new/build/HoverNetTrackPack" pack "$new/NetTarget/Tracks/Switchback.trk" "$sample/s.hntp" --name Switchback --author Check --licence CC0-1.0 >/dev/null
"$new/build/HoverNetTrackPack" verify "$sample/s.hntp" --catalogue >/dev/null && echo "package tool ok"
"$new/build/HoverNetTrackValidator" "$new/NetTarget/Tracks/Switchback.trk" Switchback | grep -q '^VALID' && echo "validator ok"
"$new/build/HoverNetMazeCompiler" "$sample/s.trk" "$new/NetTarget/TrackSources/Switchback.track.txt" >/dev/null 2>&1 && cmp -s "$sample/s.trk" "$new/NetTarget/Tracks/Switchback.trk" && echo "compiler ok (a bundled source compiles to its committed track, byte for byte)"
rm -rf "$sample"
trap - ERR
echo "-- installing"
sudo chown -R root:root "$new"; sudo chmod -R go-w "$new"; sudo rm -rf "$old"
sudo install -m 0644 "$new/packaging/web/hovernet_market.py" /usr/lib/hovernet/hovernet_market.py
sudo install -m 0644 "$new/packaging/web/hovernet-market.service" /etc/systemd/system/hovernet-market.service
sudo install -d -o hovernet -g hovernet -m 0755 /var/www/hovernet.outiva.com/market-tracks
sudo install -d -m 0750 -o root -g hovernet /etc/hovernet
if [ ! -f /etc/hovernet/market.env ]; then
  { sudo grep -E '^HOVERNET_SESSION_SECRET=' /etc/hovernet/account.env | head -1; printf 'HOVERNET_ORIGIN=https://hovernet.outiva.com\nHOVERNET_MARKET_DECLARATION=draft-0\n'; } | sudo tee /etc/hovernet/market.env >/dev/null
  sudo chown root:hovernet /etc/hovernet/market.env; sudo chmod 0640 /etc/hovernet/market.env
fi
for f in market-authors.txt market-moderators.txt; do
  [ -f "/etc/hovernet/$f" ] || { printf '# one Outiva account name (or account id) per line; lines starting with # are ignored\n' | sudo tee "/etc/hovernet/$f" >/dev/null; sudo chown root:hovernet "/etc/hovernet/$f"; sudo chmod 0640 "/etc/hovernet/$f"; }
done
sudo install -m 0644 "$new/packaging/web/market-tracks.conf" /etc/apache2/conf-available/hovernet-market.conf
sudo a2enconf hovernet-market >/dev/null
if sudo apache2ctl configtest 2>&1 | tail -1 | grep -q "Syntax OK"; then sudo systemctl reload apache2; echo "apache: configuration ok, reloaded"
else echo "apache: the configuration test FAILED, undoing the snippet"; sudo a2disconf hovernet-market >/dev/null; sudo apache2ctl configtest || true; exit 1; fi
sudo install -m 0644 "$new/packaging/web/hovernet-market-sync.service" /etc/systemd/system/hovernet-market-sync.service
sudo install -m 0644 "$new/packaging/web/hovernet-market-sync.timer" /etc/systemd/system/hovernet-market-sync.timer
sudo install -d -o hovernet -g hovernet -m 0755 /var/lib/hovernet/community-tracks
sudo systemctl daemon-reload
sudo systemctl enable hovernet-market >/dev/null 2>&1
sudo systemctl restart hovernet-market
sleep 2
sudo systemctl enable --now hovernet-market-sync.timer >/dev/null 2>&1 && echo "sync timer: every 10 minutes (listed tracks reach the race server's community folder)"
sudo systemctl is-active --quiet hovernet-market && echo "service: active" || { echo "service: NOT active"; sudo journalctl -u hovernet-market -n 15 --no-pager; exit 1; }
rm -f /tmp/hn-market-src.tgz
REMOTE
)"
if [ "$dry" = yes ]; then echo "+ ssh -t $host <build in /opt/hovernet-market, then install the service, the Apache snippet and /etc/hovernet/market*>"; else ssh -t "$host" "$remote_script"; fi

step "3/4 Checking it answers"
run ssh "$host" 'curl -s -m 10 http://127.0.0.1:9603/market/catalogue.tsv | head -3'
run bash -c "curl -s -m 15 https://hovernet.outiva.com/market/catalogue.tsv | head -3"

step "4/4 Done. The market is installed and CLOSED."
cat <<EOF
Open the private beta by adding invited Outiva account names (and your own moderator name) on the host:

  ssh -t $host 'sudo nano /etc/hovernet/market-authors.txt'        # who may publish
  ssh -t $host 'sudo nano /etc/hovernet/market-moderators.txt'     # who reviews first tracks and reports
  (changes are picked up at once; no restart)

Logs:      ssh $host 'journalctl -u hovernet-market -n 50 --no-pager'
Remove:    bash packaging/web/install-market.sh --uninstall
Update:    commit, then run this script again (it replaces the files and restarts the service; the state in /var/lib/hovernet-market is kept)
EOF
