#!/usr/bin/env python3
"""The HoverNet Track Market: submission service and live catalogue (docs/adr/0004-track-marketplace.md, docs/specs/track-market.md).

Authors publish from the track editor; every player can browse and download. This service is the part on the web host:

    GET  /market/catalogue.tsv        the live catalogue (public, no sign-in)
    POST /market/submit               multipart: package (.hntp), replay (.hnr), declaration, description. Signed-in authors only
    POST /market/submit-source        multipart: source (the editor's .track.txt), name, author, licence, declaration, description. The service compiles it,
                                      packs it, finds a finishing run in the real simulation, then applies exactly the checks above (this is what the editor's
                                      Publish button sends, so a browser author needs no command-line tools)
    GET  /market/info                 what the editor needs before it offers Publish: the current declaration (version and text), the licences, and whether the
                                      person is signed in and invited (public; says nothing about anyone else)
    GET  /market/mine                 the signed-in author's submissions and their state
    POST /market/unlist               {"id": N}: the author takes a track down
    POST /market/report               {"asset": "<sha256>", "reason": "unlawful"|"other", "note": "..."}: anyone may report a listed track
    GET  /market/review               moderators: tracks waiting for review and tracks under report
    POST /market/review               moderators: {"id": N, "action": "approve"|"reject"|"revoke"|"restore", "note": "..."}

The track files themselves are static, hash-named and immutable: <public dir>/<shard>/<asset>.trk.gz, served by Apache (packaging/web/market-tracks.conf).

Who may do what is the project's policy and is checked here, not trusted from the browser:
  * Sign-in is the account service's session cookie (same secret, same cookie): this service never sees a password or a token.
  * Private beta: only accounts on the allow-list may submit (HOVERNET_MARKET_ALLOW_FILE, one account name or id per line). With no file nothing
    is allowed, so a misconfigured service fails closed.
  * An author's first track waits for a moderator; later tracks list as soon as the checks pass.
  * A report that a track is unlawful hides it at once until a moderator decides.
  * Moderators are listed in HOVERNET_MARKET_MODERATORS_FILE (same format).
The checks of a submission are scripts/check-track-submission.sh, the same script the editor runs; this file only decides what to do with the result.

Configuration (environment, normally /etc/hovernet/market.env):
    HOVERNET_SESSION_SECRET            the account service's secret (at least 32 bytes): the session cookie is verified with it
    HOVERNET_ORIGIN                    https://hovernet.outiva.com
    HOVERNET_MARKET_STATE_DIR          /var/lib/hovernet-market      (state.json, pending submissions)
    HOVERNET_MARKET_PUBLIC_DIR         /var/lib/hovernet-market/public   (catalogue.tsv and the .trk.gz files; served by Apache)
    HOVERNET_MARKET_TOOLS_DIR          /usr/lib/hovernet/bin         (HoverNetTrackPack, HoverNetTrackValidator, HoverNetReplay)
    HOVERNET_MARKET_CHECK_SCRIPT       /usr/lib/hovernet/check-track-submission.sh
    HOVERNET_MARKET_ALLOW_FILE         /etc/hovernet/market-authors.txt
    HOVERNET_MARKET_MODERATORS_FILE    /etc/hovernet/market-moderators.txt
    HOVERNET_MARKET_DECLARATION        the current author-declaration version authors must accept (default "draft-0")
    HOVERNET_MARKET_HOST / HOVERNET_MARKET_PORT    127.0.0.1 / 9603
"""
import base64
import gzip
import hashlib
import hmac
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from email.parser import BytesParser
from email.policy import HTTP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SESSION_COOKIE = "__Host-hovernet_session"          # the account service's cookie (packaging/web/hovernet_account.py)
LICENCES = ("CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0")  # docs/adr/0002-content-licence-policy.md
MAX_PACKAGE = 2 * 1024 * 1024
MAX_REPLAY = 1024 * 1024
MAX_SOURCE = 1024 * 1024
MAX_BODY = MAX_PACKAGE + MAX_REPLAY + 64 * 1024
MAX_DESCRIPTION = 200
SUBMISSIONS_PER_DAY = 5
LISTED_PER_ACCOUNT = 50
REPORTS_PER_DAY_PER_ADDRESS = 20
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 '._-]{1,38}[A-Za-z0-9]$")        # 3 to 40 printable characters; the name becomes a file name on every platform
BYLINE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _.-]{0,38}[A-Za-z0-9]$|^[A-Za-z0-9]$")
ASSET = re.compile(r"^[0-9a-f]{64}$")
RESERVED = {"classich", "metro spiral", "steeplechase", "switchback", "the alley2", "the river", "tidal causeway"}   # the bundled tracks
# names Windows refuses as file names (the name is the file name on every platform, so they are refused everywhere)
RESERVED |= {"con", "prn", "aux", "nul"} | {"com%d" % i for i in range(1, 10)} | {"lpt%d" % i for i in range(1, 10)}


def community_names(filename):
    """The names of the 1,012 inherited tracks (NetTarget/CommunityTracks.tsv): a market track may not take one, because servers keep both in one folder."""
    names = set()
    try:
        with open(filename, encoding="utf-8") as handle:
            for line in handle:
                if line.strip() and not line.startswith("#"):
                    names.add(line.split("\t")[0].strip().lower())
    except OSError:
        pass
    return names
SHARD = "market"
DECLARATION_TEXT = ("I made this track, or I have the right to publish it. It does not copy someone else's work without permission. I choose the licence shown here "
                    "and I understand that anyone may download, use, copy and share the track under that licence, including HoverNet and the people who run HoverNet "
                    "servers. The name, description and anything else I enter are mine to publish and are not unlawful, abusive or misleading, and contain no private "
                    "information about anyone. I understand the track may be reviewed, unlisted or removed as described in the terms.")
COLUMNS = ["name", "mode", "starts", "rooms", "warnings", "bytes", "sha256", "shard", "asset", "gzip", "author", "licence", "listed", "version", "status"]


def b64url_decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def day(now):
    return time.strftime("%Y-%m-%d", time.gmtime(now))


class People:
    """A list of account names or ids, one per line (# comments allowed), re-read when the file changes."""

    def __init__(self, filename):
        self.filename = filename
        self.stamp = None
        self.names = set()

    def contains(self, profile):
        if not self.filename:
            return False
        try:
            stamp = os.stat(self.filename).st_mtime_ns
        except OSError:
            return False
        if stamp != self.stamp:
            try:
                with open(self.filename, encoding="utf-8") as handle:
                    self.names = {line.split("#")[0].strip().lower() for line in handle if line.split("#")[0].strip()}
                self.stamp = stamp
            except OSError:
                return False
        return any(str(profile.get(key, "")).lower() in self.names for key in ("sub", "preferred_username") if profile.get(key))


class Session:
    """Reads the account service's signed cookie. Never creates one."""

    def __init__(self, secret, clock=time.time):
        if len(secret.encode()) < 32:
            raise ValueError("HOVERNET_SESSION_SECRET must be at least 32 bytes.")
        self.secret = secret.encode()
        self.clock = clock

    def sign(self, payload):
        return base64.urlsafe_b64encode(hmac.new(self.secret, payload.encode(), hashlib.sha256).digest()).rstrip(b"=").decode()

    def profile(self, cookie_header):
        token = ""
        for part in (cookie_header or "").split(";"):
            name, _, value = part.strip().partition("=")
            if name == SESSION_COOKIE:
                token = value
        payload, dot, signature = token.rpartition(".")
        if not dot or not payload or not hmac.compare_digest(signature, self.sign(payload)):
            return None
        try:
            session = json.loads(b64url_decode(payload))
            profile = session.get("profile") or {}
            return profile if session.get("exp", 0) > self.clock() and profile.get("sub") else None
        except (ValueError, TypeError):
            return None


class Tools:
    """The real checks: the shared script and the package tool. Replaced by a fake in the tests that do not need the C++ programs."""

    def __init__(self, tools_dir, check_script, timeout=300):
        self.tools_dir, self.check_script, self.timeout = os.path.abspath(tools_dir), os.path.abspath(check_script), timeout   # the tools run in a work folder, so paths must not be relative

    def check(self, package, replay):
        try:
            done = subprocess.run(["bash", self.check_script, "--tools", self.tools_dir, package, replay], capture_output=True, text=True, timeout=self.timeout)
        except subprocess.TimeoutExpired:
            return False, ["FAIL timeout: the checks took too long"]
        lines = [line for line in done.stdout.splitlines() if line.strip()]
        return done.returncode == 0, lines

    def build(self, source, name, author, licence, description, work):
        """Compile the author's track source with the real compiler and pack it. Returns (ok, reason, package path)."""
        trk, hntp = os.path.join(work, "track.trk"), os.path.join(work, "track.hntp")
        try:
            done = subprocess.run([os.path.join(self.tools_dir, "HoverNetMazeCompiler"), trk, source], capture_output=True, text=True, timeout=90, cwd=work)
        except subprocess.TimeoutExpired:
            return False, "the compiler took too long on this track", None
        if done.returncode != 0 or not os.path.exists(trk):
            lines = [l.strip() for l in done.stdout.splitlines() if "ERROR" in l or "WARNING" in l]
            return False, "the compiler refused the track" + (": " + lines[0] if lines else ""), None
        packed = subprocess.run([os.path.join(self.tools_dir, "HoverNetTrackPack"), "pack", trk, hntp, "--name", name, "--author", author, "--licence", licence,
                                 "--description", description[:MAX_DESCRIPTION]], capture_output=True, text=True, timeout=60, cwd=work)
        if packed.returncode != 0 or not os.path.exists(hntp):
            return False, "the track could not be packed: " + (packed.stdout + packed.stderr).strip().splitlines()[-1][:160] if (packed.stdout + packed.stderr).strip() else "the track could not be packed", None
        return True, "", hntp

    def prove(self, package, replay, work):
        """Find a run that finishes a lap in the real simulation (the game's own driver), as the proof the track can be driven round. Returns (ok, note)."""
        trk = os.path.join(work, "proof.trk")
        unpack = subprocess.run([os.path.join(self.tools_dir, "HoverNetTrackPack"), "unpack", package, trk], capture_output=True, timeout=60, cwd=work)
        if unpack.returncode != 0:
            return False, "the package cannot be read"
        for seed in (7, 1, 2, 3, 4, 5):
            try:
                done = subprocess.run([os.path.join(self.tools_dir, "HoverNetDriver"), trk, replay, "--laps=1", "--seed=%d" % seed, "--width=300"],
                                      capture_output=True, text=True, timeout=60, cwd=work)
            except subprocess.TimeoutExpired:
                continue
            if "finished 1 lap" in done.stdout and os.path.exists(replay):
                return True, "found a finishing run (search %d)" % seed
        return False, "no way of driving a lap round this track was found: check that the road is wide and connected from the start to the finish, and that both checkpoints are on the route"

    def inspect(self, package):
        """The manifest of the package and its original track bytes."""
        pack = os.path.join(self.tools_dir, "HoverNetTrackPack")
        info = subprocess.run([pack, "info", package], capture_output=True, text=True, timeout=60)
        if info.returncode != 0:
            raise ValueError("the package cannot be read")
        manifest = dict(line.split("=", 1) for line in info.stdout.splitlines() if "=" in line)
        with tempfile.TemporaryDirectory() as work:
            track = os.path.join(work, "track.trk")
            subprocess.run([pack, "unpack", package, track], capture_output=True, timeout=60, check=True)
            with open(track, "rb") as handle:
                data = handle.read()
        return manifest, data


class Market:
    def __init__(self, state_dir, public_dir, tools, allow, moderators, declaration="draft-0", clock=time.time, reserved=()):
        self.reserved = RESERVED | set(reserved)
        self.state_dir, self.public_dir, self.tools = state_dir, public_dir, tools
        self.allow, self.moderators, self.declaration, self.clock = allow, moderators, declaration, clock
        self.lock = threading.RLock()
        self.heavy = threading.Semaphore(1)       # one track is compiled and searched at a time, so the host is never swamped
        os.makedirs(os.path.join(state_dir, "pending"), exist_ok=True)
        os.makedirs(public_dir, exist_ok=True)
        self.path = os.path.join(state_dir, "state.json")
        self.state = {"next_id": 1, "catalogue_version": 0, "subs": {}, "bylines": {}, "reports": [], "report_counts": {}}
        if os.path.exists(self.path):
            with open(self.path, encoding="utf-8") as handle:
                self.state.update(json.load(handle))
        self.write_catalogue()

    # ---- storage ---------------------------------------------------------------------------------------------------------------------
    def save(self):
        temp = self.path + ".tmp"
        with open(temp, "w", encoding="utf-8") as handle:
            json.dump(self.state, handle, indent=1, sort_keys=True)
        os.replace(temp, self.path)

    def get(self, sid):
        return self.state["subs"].get(str(sid))

    def listed_by(self, sub):
        return [s for s in self.state["subs"].values() if s["sub"] == sub and s["status"] == "listed"]

    # ---- the catalogue ---------------------------------------------------------------------------------------------------------------
    def write_catalogue(self):
        rows = []
        for s in self.state["subs"].values():
            if s["status"] == "listed":
                rows.append([s["name"], s["mode"], s["starts"], s["rooms"], 0, s["bytes"], s["sha256"], SHARD, s["sha256"], s["gzip"], s["author"],
                             s["licence"], s["listed"], s["version"], "listed"])
            elif s["status"] == "revoked":     # only the hash survives: clients and servers delete or refuse it
                rows.append(["-", "-", 0, 0, 0, 0, s["sha256"], SHARD, s["sha256"], 0, "-", "-", s["revoked"], s["version"], "revoked"])
        rows.sort(key=lambda r: (r[8], r[13]))
        text = "# HoverNet Track Market catalogue (docs/specs/track-market.md)\n# catalogue-version %d\n# %s\n" % (self.state["catalogue_version"], "\t".join(COLUMNS))
        text += "".join("\t".join(str(c) for c in row) + "\n" for row in rows)
        temp = os.path.join(self.public_dir, "catalogue.tsv.tmp")
        with open(temp, "w", encoding="utf-8") as handle:
            handle.write(text)
        os.replace(temp, os.path.join(self.public_dir, "catalogue.tsv"))
        return text

    def changed(self):
        self.state["catalogue_version"] += 1
        self.save()
        self.write_catalogue()

    def asset_path(self, sha):
        return os.path.join(self.public_dir, SHARD, sha + ".trk.gz")

    def publish_file(self, sub):
        path = self.asset_path(sub["sha256"])
        if not os.path.exists(path):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(os.path.join(self.state_dir, "pending", "%d.trk" % sub["id"]), "rb") as handle:
                data = handle.read()
            temp = path + ".tmp"
            with open(temp, "wb") as handle:
                handle.write(gzip.compress(data, mtime=0))
            os.replace(temp, path)

    def drop_pending(self, sid):
        for suffix in ("hntp", "hnr", "trk"):
            try:
                os.remove(os.path.join(self.state_dir, "pending", "%d.%s" % (sid, suffix)))
            except OSError:
                pass

    # ---- submitting -------------------------------------------------------------------------------------------------------------------
    def submit(self, profile, package, replay, declaration, description):
        """Returns (http status, body dict). Fails closed on every rule."""
        if not profile:
            return 401, {"error": "sign in to publish"}
        if not self.allow.contains(profile):
            return 403, {"error": "the Track Market is in private beta: publishing is open to invited authors only"}
        if declaration != self.declaration:
            return 400, {"error": "accept the current author declaration (version %s)" % self.declaration}
        if not package or len(package) > MAX_PACKAGE or not replay or len(replay) > MAX_REPLAY:
            return 413, {"error": "the package must be at most 2 MB and the finishing replay at most 1 MB"}
        if len(description) > MAX_DESCRIPTION or any(ord(c) < 32 for c in description):
            return 400, {"error": "the description must be plain text of at most %d characters" % MAX_DESCRIPTION}
        with self.lock:
            now = self.clock()
            sub = profile["sub"]
            recent = [s for s in self.state["subs"].values() if s["sub"] == sub and s["submitted_day"] == day(now)]
            if len(recent) >= SUBMISSIONS_PER_DAY:
                return 429, {"error": "at most %d submissions a day" % SUBMISSIONS_PER_DAY}
            if len(self.listed_by(sub)) >= LISTED_PER_ACCOUNT:
                return 429, {"error": "at most %d listed tracks per account: unlist one first" % LISTED_PER_ACCOUNT}
            sid = self.state["next_id"]
            self.state["next_id"] += 1
            pending = os.path.join(self.state_dir, "pending")
            package_path, replay_path = [os.path.join(pending, "%d.%s" % (sid, e)) for e in ("hntp", "hnr")]
            for path, data in ((package_path, package), (replay_path, replay)):
                with open(path, "wb") as handle:
                    handle.write(data)
            record = {"id": sid, "sub": sub, "submitted": now, "submitted_day": day(now), "declaration": declaration, "status": "checking", "reasons": [],
                      "description": description, "version": 1}
            self.state["subs"][str(sid)] = record

            def refuse(reasons, code=422):
                record["status"], record["reasons"] = "refused", reasons
                self.drop_pending(sid)
                self.save()
                return code, {"id": sid, "status": "refused", "reasons": reasons}

            ok, lines = self.tools.check(package_path, replay_path)
            if not ok:
                return refuse([line for line in lines if line.startswith("FAIL")] or ["the checks did not pass"])
            try:
                manifest, trk = self.tools.inspect(package_path)
            except (ValueError, subprocess.SubprocessError, OSError):
                return refuse(["the package cannot be read"])
            name, author, licence = manifest.get("name", ""), manifest.get("author", ""), manifest.get("licence", "")
            if licence not in LICENCES:
                return refuse(["the licence %s is not accepted (use one of %s)" % (licence, ", ".join(LICENCES))])
            if not NAME.match(name) or name.lower() in self.reserved:
                return refuse(["the track name must be 3 to 40 letters, digits and simple punctuation, and not the name of a bundled or inherited community track"])
            if any(o["sub"] != sub and o.get("name", "").lower() == name.lower() and o["status"] in ("listed", "waiting", "unlisted", "reported", "revoked") for o in self.state["subs"].values()):
                return refuse(["a track called %s already exists: names are unique across the market, please choose another" % name])
            if not BYLINE.match(author):
                return refuse(["the author name must be 1 to 40 letters, digits, spaces and . _ -"])
            owner = self.state["bylines"].get(author.lower())
            if owner not in (None, sub):
                return refuse(["the author name %s is already used by someone else" % author])
            sha = hashlib.sha256(trk).hexdigest()
            if sha != manifest.get("payload-sha256"):
                return refuse(["the package's track does not match its manifest"])
            if any(s["sha256"] == sha and s["status"] in ("listed", "waiting", "unlisted", "reported", "revoked") for s in self.state["subs"].values() if "sha256" in s):
                return refuse(["this exact track has already been submitted"])
            versions = [s["version"] for s in self.state["subs"].values() if s["sub"] == sub and s.get("name", "").lower() == name.lower() and s["status"] in ("listed", "waiting", "unlisted")]
            record.update(name=name, author=author, licence=licence, mode=manifest.get("mode", "race"), starts=int(manifest.get("starts", 0)), rooms=int(manifest.get("rooms", 0)),
                          bytes=len(trk), sha256=sha, gzip=len(gzip.compress(trk, mtime=0)), version=(max(versions) + 1) if versions else 1)
            self.state["bylines"][author.lower()] = sub
            with open(os.path.join(pending, "%d.trk" % sid), "wb") as handle:
                handle.write(trk)
            if self.listed_by(sub):
                self.list_it(record, now)
                body_status = "listed"
            else:
                record["status"] = "waiting"
                body_status = "waiting for review"
                self.save()
            try:
                os.remove(replay_path)       # the replay was only the proof; it is not kept
            except OSError:
                pass
            return 201, {"id": sid, "status": body_status, "name": name, "version": record["version"]}

    def submit_source(self, profile, source, name, author, licence, declaration, description):
        """Publish from the editor: the author's track source in, the same decision as submit() out. The service compiles and packs it and finds a finishing run
        itself, with the same tools the checks use, so the browser editor needs nothing but the text."""
        if not profile:
            return 401, {"error": "sign in to publish"}
        if not self.allow.contains(profile):
            return 403, {"error": "the Track Market is in private beta: publishing is open to invited authors only"}
        if declaration != self.declaration:
            return 400, {"error": "accept the current author declaration (version %s)" % self.declaration}
        if not source or len(source) > MAX_SOURCE:
            return 413, {"error": "the track source must be at most 1 MB"}
        if len(description) > MAX_DESCRIPTION or any(ord(c) < 32 for c in description):
            return 400, {"error": "the description must be plain text of at most %d characters" % MAX_DESCRIPTION}
        if licence not in LICENCES:
            return 400, {"error": "choose one of the licences: %s" % ", ".join(LICENCES)}
        if not NAME.match(name or "") or not BYLINE.match(author or ""):
            return 400, {"error": "the track name must be 3 to 40 letters, digits and simple punctuation, and the author name 1 to 40 letters, digits, spaces and . _ -"}
        with self.lock:
            now = self.clock()
            recent = [x for x in self.state["subs"].values() if x["sub"] == profile["sub"] and x["submitted_day"] == day(now)]
            if len(recent) >= SUBMISSIONS_PER_DAY:
                return 429, {"error": "at most %d submissions a day" % SUBMISSIONS_PER_DAY}
        if not self.heavy.acquire(blocking=False):
            return 503, {"error": "the market is checking another track: try again in a minute"}
        try:
            with tempfile.TemporaryDirectory(dir=self.state_dir) as work:
                source_path = os.path.join(work, "track.track.txt")
                with open(source_path, "wb") as handle:
                    handle.write(source)
                ok, reason, package_path = self.tools.build(source_path, name, author, licence, description, work)
                proof = os.path.join(work, "proof.hnr")
                if ok:
                    ok, reason = self.tools.prove(package_path, proof, work)
                if not ok:
                    with self.lock:     # a refusal costs one of the day's submissions, so the CPU cannot be spent without limit
                        sid = self.state["next_id"]
                        self.state["next_id"] += 1
                        self.state["subs"][str(sid)] = {"id": sid, "sub": profile["sub"], "submitted": self.clock(), "submitted_day": day(self.clock()), "declaration": declaration,
                                                          "status": "refused", "reasons": [reason], "description": description, "version": 1, "name": name}
                        self.save()
                    return 422, {"id": sid, "status": "refused", "reasons": [reason]}
                with open(package_path, "rb") as handle:
                    package = handle.read()
                with open(proof, "rb") as handle:
                    replay = handle.read()
        finally:
            self.heavy.release()
        return self.submit(profile, package, replay, declaration, description)

    def list_it(self, record, now):
        record["status"], record["listed"] = "listed", day(now)
        for older in self.state["subs"].values():      # a new version replaces the older one of the same name by the same author
            if older is not record and older["sub"] == record["sub"] and older.get("name", "").lower() == record["name"].lower() and older["status"] == "listed" and older["version"] < record["version"]:
                older["status"] = "unlisted"
        self.publish_file(record)
        self.changed()

    # ---- authors ---------------------------------------------------------------------------------------------------------------------
    def mine(self, profile):
        if not profile:
            return 401, {"error": "sign in"}
        keys = ("id", "name", "version", "status", "reasons", "licence", "listed", "sha256")
        return 200, {"submissions": [{k: s.get(k) for k in keys} for s in self.state["subs"].values() if s["sub"] == profile["sub"]]}

    def unlist(self, profile, sid):
        if not profile:
            return 401, {"error": "sign in"}
        with self.lock:
            s = self.get(sid)
            if not s or s["sub"] != profile["sub"]:
                return 404, {"error": "no such submission"}
            if s["status"] not in ("listed", "waiting"):
                return 409, {"error": "it is not listed"}
            was = s["status"]
            s["status"] = "unlisted"
            self.changed() if was == "listed" else self.save()
            return 200, {"id": s["id"], "status": "unlisted"}

    # ---- reports and moderation -----------------------------------------------------------------------------------------------------------
    def report(self, address, asset, reason, note):
        if not ASSET.match(asset or "") or reason not in ("unlawful", "other") or len(note or "") > 300:
            return 400, {"error": "a report needs the track's asset hash, a reason (unlawful or other) and a note of at most 300 characters"}
        with self.lock:
            key = "%s|%s" % (address, day(self.clock()))
            counts = self.state["report_counts"]
            if counts.get(key, 0) >= REPORTS_PER_DAY_PER_ADDRESS:
                return 429, {"error": "too many reports today"}
            counts[key] = counts.get(key, 0) + 1
            target = next((s for s in self.state["subs"].values() if s.get("sha256") == asset and s["status"] in ("listed", "reported")), None)
            if not target:
                return 404, {"error": "that track is not listed"}
            self.state["reports"].append({"id": target["id"], "reason": reason, "note": note or "", "at": self.clock()})
            if reason == "unlawful" and target["status"] == "listed":     # hidden at once while a moderator looks
                target["status"] = "reported"
                self.changed()
            else:
                self.save()
            return 202, {"received": True}

    def review_queue(self, profile):
        if not self.moderators.contains(profile or {}):
            return 403, {"error": "moderators only"}
        keys = ("id", "name", "author", "version", "licence", "description", "status", "sha256")
        waiting = [{k: s.get(k) for k in keys} for s in self.state["subs"].values() if s["status"] in ("waiting", "reported")]
        reports = [r for r in self.state["reports"] if (self.get(r["id"]) or {}).get("status") == "reported"]
        return 200, {"queue": waiting, "reports": reports}

    def review(self, profile, sid, action, note):
        if not self.moderators.contains(profile or {}):
            return 403, {"error": "moderators only"}
        with self.lock:
            s = self.get(sid)
            if not s:
                return 404, {"error": "no such submission"}
            now = self.clock()
            if action == "approve" and s["status"] == "waiting":
                self.list_it(s, now)
            elif action == "reject" and s["status"] == "waiting":
                s["status"], s["reasons"] = "refused", [note or "not accepted by a moderator"]
                self.drop_pending(s["id"])
                self.save()
            elif action == "restore" and s["status"] == "reported":
                s["status"] = "listed"
                self.changed()
            elif action == "revoke" and s["status"] in ("listed", "reported", "waiting", "unlisted"):
                s["status"], s["revoked"] = "revoked", day(now)
                try:
                    os.remove(self.asset_path(s["sha256"]))
                except OSError:
                    pass
                self.drop_pending(s["id"])
                self.changed()      # the revocation line is itself a change clients must see (and a listed track leaves the list)
            else:
                return 409, {"error": "%s is not possible while the track is %s" % (action, s["status"])}
            return 200, {"id": s["id"], "status": s["status"]}


# ---- HTTP ---------------------------------------------------------------------------------------------------------------------------------
def parse_multipart(content_type, body):
    message = BytesParser(policy=HTTP).parsebytes(b"Content-Type: " + content_type.encode("latin-1", "replace") + b"\r\nMIME-Version: 1.0\r\n\r\n" + body)
    if not message.is_multipart():
        raise ValueError("not multipart")
    fields = {}
    for part in message.iter_parts():
        name = part.get_param("name", header="content-disposition")
        if name:
            fields[name] = part.get_payload(decode=True) or b""
    return fields


def make_handler(market, session, origin):
    class Handler(BaseHTTPRequestHandler):
        server_version = "HoverNetMarket"
        sys_version = ""

        def log_message(self, fmt, *args):
            pass

        def send_json(self, status, body, headers=None):
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def profile(self):
            return session.profile(self.headers.get("Cookie"))

        def address(self):
            peer = self.client_address[0]
            forwarded = (self.headers.get("X-Forwarded-For") or "").split(",")[0].strip()
            return forwarded if peer in ("127.0.0.1", "::1") and forwarded else peer

        def same_origin(self):
            return self.headers.get("Origin") == origin

        def read_body(self, limit):
            try:
                length = int(self.headers.get("Content-Length") or 0)
            except ValueError:
                return None
            if length < 0 or length > limit:
                return None
            return self.rfile.read(length)

        def do_GET(self):
            if self.path == "/market/catalogue.tsv":
                with market.lock:
                    data = market.write_catalogue().encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/tab-separated-values; charset=utf-8")
                self.send_header("Cache-Control", "public, max-age=60")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                return self.wfile.write(data)
            if self.path == "/market/info":
                who = self.profile()
                return self.send_json(200, {"declaration": market.declaration, "declarationText": DECLARATION_TEXT, "licences": list(LICENCES), "privateBeta": True,
                                            "you": {"signedIn": bool(who), "invited": bool(who) and market.allow.contains(who)}})
            if self.path == "/market/mine":
                return self.send_json(*market.mine(self.profile()))
            if self.path == "/market/review":
                return self.send_json(*market.review_queue(self.profile()))
            return self.send_json(404, {"error": "not found"})

        def do_POST(self):
            if not self.same_origin():
                return self.send_json(403, {"error": "wrong origin"})
            if self.path == "/market/submit":
                body = self.read_body(MAX_BODY)
                if body is None:
                    return self.send_json(413, {"error": "too large"})
                try:
                    fields = parse_multipart(self.headers.get("Content-Type", ""), body)
                except ValueError:
                    return self.send_json(400, {"error": "send multipart form data"})
                text = lambda key: fields.get(key, b"").decode("utf-8", "replace").strip()
                return self.send_json(*market.submit(self.profile(), fields.get("package", b""), fields.get("replay", b""), text("declaration"), text("description")))
            if self.path == "/market/submit-source":
                body = self.read_body(MAX_SOURCE + 64 * 1024)
                if body is None:
                    return self.send_json(413, {"error": "too large"})
                try:
                    fields = parse_multipart(self.headers.get("Content-Type", ""), body)
                except ValueError:
                    return self.send_json(400, {"error": "send multipart form data"})
                text = lambda key: fields.get(key, b"").decode("utf-8", "replace").strip()
                return self.send_json(*market.submit_source(self.profile(), fields.get("source", b""), text("name"), text("author"), text("licence"), text("declaration"), text("description")))
            body = self.read_body(4096)
            try:
                data = json.loads(body or b"{}")
            except ValueError:
                return self.send_json(400, {"error": "send JSON"})
            if self.path == "/market/unlist":
                return self.send_json(*market.unlist(self.profile(), data.get("id")))
            if self.path == "/market/report":
                return self.send_json(*market.report(self.address(), str(data.get("asset", "")), str(data.get("reason", "")), str(data.get("note", ""))))
            if self.path == "/market/review":
                return self.send_json(*market.review(self.profile(), data.get("id"), str(data.get("action", "")), str(data.get("note", ""))))
            return self.send_json(404, {"error": "not found"})

    return Handler


def main():
    env = os.environ
    secret = env.get("HOVERNET_SESSION_SECRET", "")
    if len(secret.encode()) < 32:
        raise SystemExit("HOVERNET_SESSION_SECRET (the account service's secret, at least 32 bytes) is required.")
    state = env.get("HOVERNET_MARKET_STATE_DIR", "/var/lib/hovernet-market")
    public = env.get("HOVERNET_MARKET_PUBLIC_DIR", os.path.join(state, "public"))
    tools = Tools(env.get("HOVERNET_MARKET_TOOLS_DIR", "/usr/lib/hovernet/bin"), env.get("HOVERNET_MARKET_CHECK_SCRIPT", "/usr/lib/hovernet/check-track-submission.sh"))
    reserved = community_names(env.get("HOVERNET_MARKET_COMMUNITY_MANIFEST", os.path.join(os.path.dirname(env.get("HOVERNET_MARKET_TOOLS_DIR", "/opt/hovernet-market/build")), "NetTarget", "CommunityTracks.tsv")))
    market = Market(state, public, tools, People(env.get("HOVERNET_MARKET_ALLOW_FILE", "/etc/hovernet/market-authors.txt")),
                    People(env.get("HOVERNET_MARKET_MODERATORS_FILE", "/etc/hovernet/market-moderators.txt")), env.get("HOVERNET_MARKET_DECLARATION", "draft-0"), reserved=reserved)
    host, port = env.get("HOVERNET_MARKET_HOST", "127.0.0.1"), int(env.get("HOVERNET_MARKET_PORT", "9603"))
    server = ThreadingHTTPServer((host, port), make_handler(market, Session(secret), env.get("HOVERNET_ORIGIN", "https://hovernet.outiva.com")))
    market.allow.contains({})       # load the list so the count below is real
    print("HoverNet Track Market on %s:%d (private beta: %d allow-listed names)" % (host, port, len(market.allow.names)), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
