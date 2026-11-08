#!/usr/bin/env python3
"""Tests packaging/web/hovernet_market.py: who may publish, what happens to a submission (waiting, listed, refused), the catalogue and files it produces,
reports and takedown, new versions, limits, persistence, the HTTP layer, and (when the C++ tools are built) the real checks on a real generated track.
Usage: test-web-market.py [BUILD_DIR]   (BUILD_DIR enables the real-tools part)"""
import base64, gzip, hashlib, http.client, json, os, subprocess, sys, tempfile, threading, time, uuid
from http.server import ThreadingHTTPServer

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "packaging", "web"))
import hovernet_market as hm

ORIGIN = "https://hovernet.outiva.com"
SECRET = "x" * 40
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


class FakeTools:
    """Stands in for the C++ programs: the check passes unless the package says otherwise; the 'package' is JSON describing the track."""
    def check(self, package, replay):
        meta = json.load(open(package))
        if meta.get("fail"):
            return False, ["ok package is well formed", "FAIL %s" % meta["fail"]]
        return True, ["ok all"]

    def inspect(self, package):
        meta = json.load(open(package))
        trk = meta["trk"].encode()
        return {"name": meta["name"], "author": meta["author"], "licence": meta.get("licence", "CC-BY-4.0"), "mode": "race", "starts": "8", "rooms": "24",
                "payload-sha256": hashlib.sha256(trk).hexdigest()}, trk


def pkg(name, author="Alice", trk=None, **extra):
    return json.dumps(dict(name=name, author=author, trk=trk or ("track data of " + name + uuid.uuid4().hex), **extra)).encode()


class Clock:
    def __init__(self): self.now = 1_800_000_000.0
    def __call__(self): return self.now


def cookie_for(session, sub, name, exp=None, clock=None):
    profile = {"sub": sub, "preferred_username": name, "name": name, "email": "", "callsign": ""}
    payload = base64.urlsafe_b64encode(json.dumps({"profile": profile, "exp": exp or (time.time() + 3600)}).encode()).rstrip(b"=").decode()
    return "%s=%s.%s" % (hm.SESSION_COOKIE, payload, session.sign(payload))


work = tempfile.mkdtemp()
allow_file, mod_file = os.path.join(work, "allow.txt"), os.path.join(work, "mods.txt")
open(allow_file, "w").write("# invited\nalice\nbob\ncarol\n")
open(mod_file, "w").write("mod\n")
clock = Clock()
session = hm.Session(SECRET, clock=time.time)


def new_market(state="state", tools=None):
    return hm.Market(os.path.join(work, state), os.path.join(work, state, "public"), tools or FakeTools(), hm.People(allow_file), hm.People(mod_file), "v1", clock)


alice = {"sub": "sub-alice", "preferred_username": "alice"}
bob = {"sub": "sub-bob", "preferred_username": "bob"}
mallory = {"sub": "sub-mallory", "preferred_username": "mallory"}
mod = {"sub": "sub-mod", "preferred_username": "mod"}
market = new_market()
catalogue = lambda: open(os.path.join(market.public_dir, "catalogue.tsv")).read()
rows = lambda: [l.split("\t") for l in catalogue().splitlines() if not l.startswith("#")]

# --- who may publish
check(market.submit(None, pkg("Loop One"), b"r", "v1", "")[0] == 401, "a guest cannot publish")
check(market.submit(mallory, pkg("Loop One"), b"r", "v1", "")[0] == 403, "a signed-in account that is not invited cannot publish (private beta)")
check(market.submit(alice, pkg("Loop One"), b"r", "old-version", "")[0] == 400, "the current author declaration must be accepted")
check(market.submit(alice, b"x" * (hm.MAX_PACKAGE + 1), b"r", "v1", "")[0] == 413, "an oversized package is refused")
check(market.submit(alice, pkg("Loop One"), b"r", "v1", "x" * 201)[0] == 400, "a description over 200 characters is refused")
check(rows() == [], "nothing refused is listed")
empty = hm.Market(os.path.join(work, "closed"), os.path.join(work, "closed", "public"), FakeTools(), hm.People(None), hm.People(None), "v1", clock)
check(empty.submit(alice, pkg("Loop One"), b"r", "v1", "")[0] == 403, "with no allow-list nobody can publish (it fails closed)")

# --- first track waits for a moderator; the catalogue and files
code, body = market.submit(alice, pkg("Loop One", trk="TRACK-ONE"), b"replay", "v1", "A first loop")
check(code == 201 and body["status"] == "waiting for review", "an invited author's first track waits for review")
check(rows() == [], "a track waiting for review is not in the catalogue")
check(market.review_queue(alice)[0] == 403 and market.review(alice, body["id"], "approve", "")[0] == 403, "an author cannot review")
check(len(market.review_queue(mod)[1]["queue"]) == 1, "the moderator sees it in the queue")
check(market.review(mod, body["id"], "approve", "")[1]["status"] == "listed", "a moderator approves it")
r = rows()
sha_one = hashlib.sha256(b"TRACK-ONE").hexdigest()
check(len(r) == 1 and r[0][0] == "Loop One" and r[0][6] == sha_one and r[0][10] == "Alice" and r[0][11] == "CC-BY-4.0" and r[0][14] == "listed", "the catalogue lists it with author, licence and hash")
check(gzip.decompress(open(market.asset_path(sha_one), "rb").read()) == b"TRACK-ONE", "the hash-named file unpacks to the track")
check("catalogue-version 1" in catalogue(), "the catalogue version rose")
check(not os.path.exists(os.path.join(market.state_dir, "pending", "1.hnr")), "the replay is not kept")

# --- later tracks of the same author list at once; refusals
code, body = market.submit(alice, pkg("Loop Two", trk="TRACK-TWO"), b"r", "v1", "")
check(code == 201 and body["status"] == "listed", "an author with a listed track lists later tracks automatically")
check(market.submit(alice, pkg("Loop Two", trk="TRACK-TWO"), b"r", "v1", "")[0] == 422, "the very same track cannot be submitted twice")
code, body = market.submit(alice, pkg("Broken", fail="finishing replay does not verify"), b"r", "v1", "")
check(code == 422 and body["status"] == "refused" and "finishing replay does not verify" in body["reasons"][0], "a failed check refuses with the reason in plain words")
check(market.submit(alice, pkg("Tidal Causeway"), b"r", "v1", "")[0] == 422, "the name of a bundled track is reserved")
clock.now += 86400   # refused submissions count toward the daily limit, so move to the next day
check(market.submit(alice, pkg("No licence", licence="CC-BY-NC-4.0"), b"r", "v1", "")[0] == 422, "a licence the market does not accept is refused")
check(market.submit(alice, pkg("<script>"), b"r", "v1", "")[0] == 422, "an unsafe track name is refused")
check(market.submit(alice, pkg("Con"), b"r", "v1", "")[0] == 422 and market.submit(alice, pkg("Trailing dot."), b"r", "v1", "")[0] == 422, "names Windows cannot use as files are refused")
with_inherited = hm.Market(os.path.join(work, "inh"), os.path.join(work, "inh", "public"), FakeTools(), hm.People(allow_file), hm.People(mod_file), "v1", clock, reserved={"old classic"})
check(with_inherited.submit(alice, pkg("Old Classic"), b"r", "v1", "")[0] == 422, "the name of an inherited community track is refused")
check(market.submit(bob, pkg("Impostor", author="alice"), b"r", "v1", "")[0] == 422, "an author name already used by another account is refused")
check(market.submit(bob, pkg("Loop Two", author="Bobby", trk="BOB-LOOP-TWO"), b"r", "v1", "")[0] == 422, "a track name already used by another author is refused (names are unique across the market)")
check(len(rows()) == 2, "refusals never reach the catalogue")

# --- new version replaces the old
code, body = market.submit(alice, pkg("Loop One", trk="TRACK-ONE-V2"), b"r", "v1", "")
check(code == 201 and body["version"] == 2, "a changed track under the same name is version 2")
names = [(x[0], x[13]) for x in rows()]
check(("Loop One", "2") in names and ("Loop One", "1") not in names, "the new version replaces the old in the catalogue")

# --- unlist
check(market.unlist(bob, body["id"])[0] == 404, "someone else cannot unlist an author's track")
check(market.unlist(alice, body["id"])[1]["status"] == "unlisted" and all(x[0] != "Loop One" for x in rows()), "an author can unlist their own track")

# --- reports and takedown
sha_two = hashlib.sha256(b"TRACK-TWO").hexdigest()
check(market.report("1.2.3.4", "nothex", "other", "")[0] == 400, "a malformed report is refused")
check(market.report("1.2.3.4", "0" * 64, "other", "")[0] == 404, "a report about an unknown track is refused")
check(market.report("1.2.3.4", sha_two, "other", "poor taste")[0] == 202 and any(x[6] == sha_two for x in rows()), "an ordinary report keeps the track listed for review")
check(market.report("1.2.3.4", sha_two, "unlawful", "copied")[0] == 202 and all(x[6] != sha_two for x in rows()), "a report of unlawful content hides the track at once")
check(len(market.review_queue(mod)[1]["queue"]) == 1, "the hidden track is in the moderator's queue")
check(market.review(mod, 2, "restore", "")[1]["status"] == "listed" and any(x[6] == sha_two for x in rows()), "a moderator can restore it")
market.report("1.2.3.4", sha_two, "unlawful", "copied")
check(market.review(mod, 2, "revoke", "rights holder asked")[1]["status"] == "revoked", "a moderator can revoke it")
revoked = [x for x in rows() if x[6] == sha_two]
check(len(revoked) == 1 and revoked[0][14] == "revoked" and revoked[0][0] == "-" and revoked[0][10] == "-", "a revoked track stays only as its hash")
check(not os.path.exists(market.asset_path(sha_two)), "a revoked track's file is deleted")
clock.now += 86400
check(market.submit(alice, pkg("Loop Two", trk="TRACK-TWO"), b"r", "v1", "")[0] == 422, "a revoked track cannot be submitted again")

# --- the catalogue this service writes is what the server sync reads (the whole chain: service -> catalogue and files -> race server folder)
dest = os.path.join(work, "server-community-tracks")
sync = [os.path.join(here, "..", "packaging", "web", "sync-market-tracks.sh"), "--catalogue", os.path.join(market.public_dir, "catalogue.tsv"), "--assets", market.public_dir, "--dest", dest]
done = subprocess.run(["bash"] + sync, capture_output=True, text=True)
files = sorted(os.listdir(dest)) if os.path.isdir(dest) else []
check(done.returncode == 0 and "Loop Two.trk" not in files and "Loop One.trk" not in files, "the server sync installs only what is listed (a revoked and an unlisted track stay off)")
carol = {"sub": "sub-carol", "preferred_username": "carol"}
market.submit(carol, pkg("Chain Seed", author="Carol", trk="CHAIN-SEED"), b"r", "v1", "")
market.review(mod, max(int(k) for k in market.state["subs"]), "approve", "")   # her first track waits for a moderator
market.submit(carol, pkg("Chain Test", author="Carol", trk="CHAIN-TRACK"), b"r", "v1", "")
subprocess.run(["bash"] + sync, capture_output=True, text=True)
check(open(os.path.join(dest, "Chain Test.trk"), "rb").read() == b"CHAIN-TRACK", "a track listed by the service reaches the race server folder byte for byte")
market.unlist(carol, max(int(k) for k, v in market.state["subs"].items() if v.get("name") == "Chain Test"))
subprocess.run(["bash"] + sync, capture_output=True, text=True)
check(not os.path.exists(os.path.join(dest, "Chain Test.trk")), "an unlisted track is taken off the race server")

# --- limits
limited = new_market("limit")
codes = [limited.submit(bob, pkg("Name %d" % i, author="bob"), b"r", "v1", "")[0] for i in range(6)]
check(codes[:5] == [201] * 5 and codes[5] == 429, "the sixth submission in a day is refused")
clock.now += 86400
check(limited.submit(bob, pkg("Name next", author="bob"), b"r", "v1", "")[0] == 201, "the next day it works again")
for _ in range(hm.REPORTS_PER_DAY_PER_ADDRESS):
    market.report("9.9.9.9", sha_one, "other", "")
check(market.report("9.9.9.9", sha_one, "other", "")[0] == 429, "one address cannot flood reports")

# --- persistence
again = new_market()
check(again.write_catalogue() == catalogue() and again.state["next_id"] == market.state["next_id"], "the catalogue and ids survive a restart")

# --- sessions
check(session.profile(cookie_for(session, "s", "alice"))["sub"] == "s", "a valid session cookie is accepted")
check(session.profile(cookie_for(session, "s", "alice")[:-3] + "xyz") is None, "a forged cookie is refused")
check(session.profile(cookie_for(session, "s", "alice", exp=time.time() - 10)) is None, "an expired cookie is refused")
check(session.profile(cookie_for(hm.Session("y" * 40), "s", "alice")) is None, "a cookie signed with another secret is refused")

# --- the HTTP layer
http_market = new_market("http")
server = ThreadingHTTPServer(("127.0.0.1", 0), hm.make_handler(http_market, session, ORIGIN))
threading.Thread(target=server.serve_forever, daemon=True).start()
port = server.server_address[1]


def request(method, path, body=None, headers=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse(); data = r.read(); c.close()
    return r.status, data


def multipart(fields):
    boundary = "----b" + uuid.uuid4().hex
    out = b""
    for name, value in fields.items():
        out += ("--%s\r\nContent-Disposition: form-data; name=\"%s\"%s\r\n\r\n" % (boundary, name, "; filename=\"f.bin\"" if name in ("package", "replay") else "")).encode() + (value if isinstance(value, bytes) else value.encode()) + b"\r\n"
    return out + ("--%s--\r\n" % boundary).encode(), "multipart/form-data; boundary=" + boundary


body, ctype = multipart({"package": pkg("Http Loop", trk="HTTP-TRACK"), "replay": b"r", "declaration": "v1", "description": "over http"})
ck = cookie_for(session, "sub-alice", "alice")
check(request("GET", "/market/catalogue.tsv")[0] == 200, "the catalogue is public")
check(request("POST", "/market/submit", body, {"Content-Type": ctype, "Cookie": ck})[0] == 403, "a submission without the site's Origin header is refused (cross-site)")
check(request("POST", "/market/submit", body, {"Content-Type": ctype, "Origin": ORIGIN})[0] == 401, "a submission with no sign-in is refused")
status, data = request("POST", "/market/submit", body, {"Content-Type": ctype, "Origin": ORIGIN, "Cookie": ck})
check(status == 201 and json.loads(data)["status"] == "waiting for review", "a signed-in invited author submits over HTTP")
status, data = request("GET", "/market/mine", headers={"Cookie": ck})
check(status == 200 and json.loads(data)["submissions"][0]["name"] == "Http Loop", "the author sees their submissions")
check(request("GET", "/market/review", headers={"Cookie": ck})[0] == 403, "the review queue is closed to authors")
status, data = request("GET", "/market/review", headers={"Cookie": cookie_for(session, "sub-mod", "mod")})
check(status == 200 and len(json.loads(data)["queue"]) == 1, "the moderator sees the queue over HTTP")
check(request("POST", "/market/report", json.dumps({"asset": "0" * 64, "reason": "other"}), {"Origin": ORIGIN})[0] == 404, "reports are open to anyone (no sign-in needed)")
check(request("POST", "/market/submit", b"not multipart", {"Content-Type": "text/plain", "Origin": ORIGIN, "Cookie": ck})[0] == 400, "a body that is not a form is refused")
check(request("GET", "/market/other")[0] == 404, "unknown paths are 404")
server.shutdown()

# --- the real checks on a real generated track (needs the built programs)
build = sys.argv[1] if len(sys.argv) > 1 else ""
need = ["HoverNetTrackSource", "HoverNetMazeCompiler", "HoverNetTrackPack", "HoverNetTrackValidator", "HoverNetDriver", "HoverNetReplay"]
if build and all(os.path.exists(os.path.join(build, n)) for n in need):
    b = lambda n: os.path.join(build, n)
    d = tempfile.mkdtemp()
    run = lambda *a: subprocess.run(a, capture_output=True, text=True)
    ok = run(b("HoverNetTrackSource"), "new", "ring", d + "/t.track.txt", "--name=Market Loop").returncode == 0
    ok = ok and run(b("HoverNetMazeCompiler"), d + "/t.trk", d + "/t.track.txt").returncode == 0
    finished = False
    for seed in (7, 1, 2, 3, 4, 5):
        if "finished 1 lap" in run(b("HoverNetDriver"), d + "/t.trk", d + "/t.hnr", "--laps=1", "--seed=%d" % seed, "--width=300").stdout:
            finished = True; break
    ok = ok and finished and run(b("HoverNetTrackPack"), "pack", d + "/t.trk", d + "/t.hntp", "--name", "Market Loop", "--author", "Realauthor", "--licence", "CC-BY-4.0").returncode == 0
    check(ok, "real tools: made a track, a finishing replay and a package")
    real = hm.Market(os.path.join(work, "real"), os.path.join(work, "real", "public"),
                     hm.Tools(build, os.path.join(here, "check-track-submission.sh")), hm.People(allow_file), hm.People(mod_file), "v1", clock)
    code, body = real.submit({"sub": "sub-real", "preferred_username": "alice"}, open(d + "/t.hntp", "rb").read(), open(d + "/t.hnr", "rb").read(), "v1", "real")
    check(code == 201 and body["status"] == "waiting for review", "real tools: a genuine submission passes every check and waits for review")
    real.review(mod, body["id"], "approve", "")
    trk = open(d + "/t.trk", "rb").read()
    row = [l.split("\t") for l in open(os.path.join(real.public_dir, "catalogue.tsv")).read().splitlines() if not l.startswith("#")]
    check(len(row) == 1 and row[0][6] == hashlib.sha256(trk).hexdigest() and row[0][10] == "Realauthor", "real tools: listed with the true hash and author")
    check(gzip.decompress(open(real.asset_path(row[0][6]), "rb").read()) == trk, "real tools: the published file is the author's track, byte for byte")
    code, body = real.submit({"sub": "sub-real2", "preferred_username": "bob"}, open(d + "/t.hntp", "rb").read(), b"garbage replay", "v1", "")
    check(code == 422 and body["status"] == "refused", "real tools: a package with a bad replay is refused")
else:
    print("note the real-tools part was skipped (give the build directory as an argument)")

sys.exit(1 if failures else 0)
