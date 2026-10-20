#!/usr/bin/env python3
"""Tests packaging/web/hovernet_account.py against a stand-in Keycloak: the whole sign-in round trip, the cookie, the callsign, and every
way a sign-in can go wrong (which must always end as a guest, never as a signed-in stranger or a crash)."""
import base64, hashlib, http.client, json, os, sys, tempfile, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "packaging", "web"))
import hovernet_account as ha

ISSUER = "https://accounts.outiva.com/realms/outiva"
ORIGIN = "https://hovernet.outiva.com"
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


def b64(data):
    return base64.urlsafe_b64encode(json.dumps(data).encode()).rstrip(b"=").decode()


class FakeKeycloak(BaseHTTPRequestHandler):
    codes = {}          # code -> dict(nonce, verifier_challenge, sub, aud, iss, userinfo_sub, token_status)
    token_requests = []

    def log_message(self, *a): pass

    def do_POST(self):
        body = urllib.parse.parse_qs(self.rfile.read(int(self.headers["Content-Length"])).decode())
        FakeKeycloak.token_requests.append({k: v[0] for k, v in body.items()})
        entry = FakeKeycloak.codes.get(body.get("code", [""])[0])
        if entry is None or entry.get("token_status", 200) != 200 or body.get("client_secret") != ["s3cret"]:
            self.send_response(entry.get("token_status", 400) if entry else 400); self.send_header("Content-Length", "0"); self.end_headers(); return
        claims = {"iss": entry.get("iss", ISSUER), "aud": entry.get("aud", "hovernet-account"), "sub": entry["sub"], "nonce": entry["nonce"], "exp": int(time.time()) + 300}
        out = json.dumps({"id_token": "x.%s.y" % b64(claims), "access_token": "tok-" + entry["sub"]}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)

    def do_GET(self):
        sub = self.headers["Authorization"].split("tok-")[1]
        entry = next(e for e in FakeKeycloak.codes.values() if e["sub"] == sub)
        out = json.dumps({"sub": entry.get("userinfo_sub", sub), "name": "Pat Racer", "preferred_username": "pat", "email": "pat@example.com"}).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)


def serve(handler):
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


kc = serve(FakeKeycloak)
tmp = tempfile.mkdtemp()
callsign_file = os.path.join(tmp, "callsigns.json")
now = [time.time()]
account = ha.Account(ISSUER, "hovernet-account", "s3cret", "x" * 40, ORIGIN, "https://outiva.com",
                     "http://127.0.0.1:%d" % kc.server_address[1], callsign_file, clock=lambda: now[0])
app = serve(ha.make_handler(account))
guest_app = serve(ha.make_handler(None))


def call(server, method, path, headers=None, body=None):
    connection = http.client.HTTPConnection("127.0.0.1", server.server_address[1])
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    data = response.read()
    return response.status, {k.lower(): v for k, v in response.getheaders()}, data


def sign_in(mode="redirect", prompt="none", **entry):
    """Runs login -> (Keycloak) -> callback and returns the callback response."""
    status, headers, _ = call(app, "GET", "/account/login?prompt=%s&mode=%s" % (prompt, mode))
    location = urllib.parse.urlsplit(headers["location"])
    q = {k: v[0] for k, v in urllib.parse.parse_qs(location.query).items()}
    code = "code-%d" % len(FakeKeycloak.codes)
    FakeKeycloak.codes[code] = dict({"nonce": q["nonce"], "sub": "sub-1", "challenge": q["code_challenge"]}, **entry)
    return q, code, lambda extra="": call(app, "GET", "/account/callback?state=%s&code=%s%s" % (q["state"], code, extra))


# 0. not configured: guests only
check(call(guest_app, "GET", "/account/me")[0] == 204, "unconfigured service: /account/me says guest")
check(call(guest_app, "GET", "/account/login")[0] == 404, "unconfigured service: no login")

# 1. the login redirect
q, code, callback = sign_in()
status, headers, _ = call(app, "GET", "/account/login?prompt=none&mode=redirect")
location = urllib.parse.urlsplit(headers["location"])
check(status == 302 and location.path == "/realms/outiva/protocol/openid-connect/auth", "login redirects to Keycloak's authorization endpoint")
check(q["client_id"] == "hovernet-account" and q["redirect_uri"] == ORIGIN + "/account/callback" and q["code_challenge_method"] == "S256" and q["prompt"] == "none" and q["response_type"] == "code", "authorization request: client, exact redirect_uri, PKCE S256, silent")
status, headers, _ = call(app, "GET", "/account/login?mode=redirect")
check("prompt" not in urllib.parse.parse_qs(urllib.parse.urlsplit(headers["location"]).query), "without prompt=none the request is not silent")

# 2. the callback
status, headers, _ = callback()
cookie = headers.get("set-cookie", "")
check(status == 302 and headers["location"] == "https://outiva.com/?outiva_sso=complete#/play/hovernet", "callback in redirect mode returns to the Outiva console's player")
check(cookie.startswith("__Host-hovernet_session=") and all(f in cookie for f in ("HttpOnly", "Secure", "SameSite=Lax", "Path=/")) and "Domain" not in cookie, "session cookie is __Host-, HttpOnly, Secure, SameSite=Lax, no Domain")
sent = FakeKeycloak.token_requests[-1]
check(base64.urlsafe_b64encode(hashlib.sha256(sent["code_verifier"].encode()).digest()).rstrip(b"=").decode() == q["code_challenge"], "the PKCE verifier sent to the token endpoint matches the challenge")
session = cookie.split(";")[0]

# 3. /account/me
status, _, body = call(app, "GET", "/account/me", {"Cookie": session})
profile = json.loads(body)
check(status == 200 and profile["sub"] == "sub-1" and profile["name"] == "Pat Racer" and profile["callsign"] == "", "/account/me returns the signed-in profile")
check(call(app, "GET", "/account/me")[0] == 204, "no cookie: guest")
name, _, value = session.partition("=")
payload, _, signature = value.rpartition(".")
forged = base64.urlsafe_b64encode(json.dumps({"profile": {"sub": "someone-else"}, "exp": now[0] + 999}).encode()).rstrip(b"=").decode()
check(call(app, "GET", "/account/me", {"Cookie": "%s=%s.%s" % (name, forged, signature)})[0] == 204, "a forged cookie payload is rejected")
check(call(app, "GET", "/account/me", {"Cookie": "%s=%s.%s" % (name, payload, "A" * len(signature))})[0] == 204, "a bad signature is rejected")
now[0] += ha.SESSION_SECONDS + 1
check(call(app, "GET", "/account/me", {"Cookie": session})[0] == 204, "an expired session is rejected")
now[0] -= ha.SESSION_SECONDS + 1

# 4. callsign
put = lambda cookie_header, origin, body, ctype="application/json": call(app, "PUT", "/account/callsign", {k: v for k, v in {"Cookie": cookie_header, "Origin": origin, "Content-Type": ctype}.items() if v}, body)
check(put(None, ORIGIN, '{"callsign":"Pat"}')[0] == 401, "callsign: signed-out is refused")
check(put(session, "https://evil.example", '{"callsign":"Pat"}')[0] == 403, "callsign: a foreign Origin is refused")
check(put(session, ORIGIN, '{"callsign":"Pat"}', "text/plain")[0] == 403, "callsign: a non-JSON content type is refused")
check(put(session, ORIGIN, '{"callsign":"bad<name>"}')[0] == 400 and put(session, ORIGIN, '{"callsign":"x' + "y" * 30 + '"}')[0] == 400 and put(session, ORIGIN, "not json")[0] == 400, "callsign: invalid names are refused")
check(put(session, ORIGIN, '{"callsign":"Pat R"}')[0] == 204, "callsign: a valid name is saved")
check(json.loads(call(app, "GET", "/account/me", {"Cookie": session})[2])["callsign"] == "Pat R", "callsign comes back on /account/me")
check(ha.Callsigns(callsign_file).get("sub-1") == "Pat R" and oct(os.stat(callsign_file).st_mode & 0o777) == "0o600", "callsign survives a restart and the file is private (0600)")

# 5. every failure ends as a guest
def guest(what, status_expected=302, **kw):
    global callback
    before = len(FakeKeycloak.token_requests)
    q, code, cb = sign_in(**{k: v for k, v in kw.items() if k not in ("extra",)})
    status, headers, _ = cb(kw.get("extra", ""))
    check(status == status_expected and "set-cookie" not in headers and headers["location"].startswith("https://outiva.com/"), what)

guest("Keycloak says login_required (no Outiva session): back to the console as a guest", extra="&error=login_required")
guest("an ID token for a different client is not accepted", aud="someone-elses-client")
guest("an ID token from a different issuer is not accepted", iss="https://evil.example/realms/outiva")
guest("a userinfo that disagrees with the ID token is not accepted", sub="sub-mismatch", userinfo_sub="other-user")
guest("a failing token endpoint is survived", token_status=500)
q, code, cb = sign_in()
first = cb()
replay = call(app, "GET", "/account/callback?state=%s&code=%s" % (q["state"], code))
check("set-cookie" in first[1] and "set-cookie" not in replay[1], "a state value works once (replay ends as a guest)")
status, headers, _ = call(app, "GET", "/account/callback?state=never-issued&code=x")
check(status == 302 and "set-cookie" not in headers, "an unknown state ends as a guest")
q, code, cb = sign_in(mode="")
status, headers, _ = cb()
check(headers["location"] == ORIGIN + "/" and "set-cookie" in headers, "without mode=redirect it signs in and returns to the game itself")
real_clock = account.clock
account.clock = lambda: now[0] + 10_000
status, headers, _ = cb()
account.clock = real_clock
check("set-cookie" not in headers, "(state already used: still a guest)")
q, code, cb = sign_in()
account.clock = lambda: now[0] + ha.STATE_TTL_SECONDS + 5
status, headers, _ = cb()
account.clock = real_clock
check(status == 302 and "set-cookie" not in headers, "a sign-in left open for more than five minutes ends as a guest")

# 6. logout
check(call(app, "POST", "/account/logout", {"Origin": "https://evil.example"})[0] == 403, "logout: a foreign Origin is refused")
status, headers, _ = call(app, "POST", "/account/logout", {"Origin": ORIGIN})
check(status == 204 and "Max-Age=0" in headers["set-cookie"], "logout clears the cookie")
check(call(app, "GET", "/account/nope")[0] == 404, "unknown paths are 404")

print("%d failure(s)" % failures if failures else "web account service: all checks passed")
sys.exit(1 if failures else 0)
