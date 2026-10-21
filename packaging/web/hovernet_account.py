#!/usr/bin/env python3
"""Optional Outiva sign-in for the HoverNet browser client.

The same design as NetPanzer's account service: the browser never handles a token. This small server is the OpenID Connect client
(a confidential client, Authorization Code + PKCE), keeps the client secret, and gives the browser one signed HttpOnly session cookie on
hovernet.outiva.com. The page (and the copy framed inside outiva.com, which is the same site) asks /account/me who is signed in.

    GET  /account/login?prompt=none&mode=redirect   start the sign-in (silent when the user already has an Outiva session)
    GET  /account/callback                          Keycloak returns here; sets the cookie, then goes back to outiva.com (mode=redirect)
                                                    or to the game
    GET  /account/me                                200 {sub,name,preferred_username,email,callsign} or 204 for a guest
    GET  /outiva-status.json                        {"status":"available"|"degraded","players":N} for the Outiva games console (no sign-in needed)
    PUT  /account/callsign                          {"callsign": "..."} remembers the display name for this account
    POST /account/logout                            forgets the session

Everything is optional: with no configuration (or Keycloak down) the game is played as a guest, exactly as before. The server never
authorises anything in a race: a signed-in name is a convenience, not proof to the race servers (see docs/online-services.md).

Configuration (environment, normally /etc/hovernet/account.env, written by configure-account-oidc.sh):
    HOVERNET_OIDC_ISSUER           https://accounts.outiva.com/realms/outiva
    HOVERNET_OIDC_BACKCHANNEL_URL  http://127.0.0.1:8180/realms/outiva   (optional: where this server reaches Keycloak directly)
    HOVERNET_OIDC_CLIENT_ID        hovernet-web
    HOVERNET_OIDC_CLIENT_SECRET
    HOVERNET_SESSION_SECRET        at least 32 bytes, signs the session cookie
    HOVERNET_ORIGIN                https://hovernet.outiva.com
    HOVERNET_OUTIVA_ORIGIN         https://outiva.com
    HOVERNET_CALLSIGN_FILE         /var/lib/hovernet-account/callsigns.json
    HOVERNET_ACCOUNT_HOST / HOVERNET_ACCOUNT_PORT   127.0.0.1 / 9602
    HOVERNET_STATUS_URL            http://127.0.0.1:9612   (the race server's loopback monitoring port; only loopback addresses are accepted)
"""

import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SESSION_COOKIE = "__Host-hovernet_session"
SESSION_SECONDS = 8 * 60 * 60
STATE_TTL_SECONDS = 5 * 60
MAX_PENDING = 1000
CALLSIGN = re.compile(r"^[A-Za-z0-9 _-]{1,20}$")   # the game keeps names in a 24-byte field


def b64url(data):
    if isinstance(data, str):
        data = data.encode()
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def b64url_decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def random_value(size=32):
    return b64url(secrets.token_bytes(size))


def jwt_claims(token):
    parts = (token or "").split(".")
    if len(parts) != 3:
        raise ValueError("The identity provider returned an invalid ID token.")
    return json.loads(b64url_decode(parts[1]))


def safe_profile(profile):
    def text(key):
        value = profile.get(key)
        return value[:254] if isinstance(value, str) else ""
    return {"sub": text("sub"), "name": text("name"), "preferred_username": text("preferred_username"), "email": text("email")}


class Callsigns:
    """The display name each account chose, kept by the immutable OIDC subject, in one small JSON file."""

    def __init__(self, filename):
        self.filename = filename
        self.lock = threading.Lock()
        self.players = {}
        if filename:
            try:
                with open(filename, encoding="utf-8") as handle:
                    for sub, entry in (json.load(handle).get("players") or {}).items():
                        if isinstance(entry, dict) and CALLSIGN.match(str(entry.get("callsign", ""))):
                            self.players[sub] = entry
            except FileNotFoundError:
                pass

    def get(self, sub):
        with self.lock:
            return self.players.get(sub, {}).get("callsign", "")

    def set(self, sub, callsign):
        with self.lock:
            self.players[sub] = {"callsign": callsign, "updatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
            if not self.filename:
                return
            os.makedirs(os.path.dirname(self.filename), mode=0o750, exist_ok=True)
            temporary = "%s.%d.tmp" % (self.filename, os.getpid())
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
                json.dump({"version": 1, "players": self.players}, handle, indent=2)
                handle.write("\n")
            os.replace(temporary, self.filename)


class Account:
    def __init__(self, issuer, client_id, client_secret, session_secret, origin, outiva_origin="https://outiva.com",
                 backchannel_url=None, callsign_file=None, opener=None, clock=time.time):
        if len(session_secret.encode()) < 32:
            raise ValueError("HOVERNET_SESSION_SECRET must be at least 32 bytes.")
        self.issuer = issuer.rstrip("/")
        self.backchannel = (backchannel_url or issuer).rstrip("/")
        self.client_id = client_id
        self.client_secret = client_secret
        self.secret = session_secret.encode()
        self.origin = origin.rstrip("/")
        self.outiva_origin = outiva_origin.rstrip("/")
        self.redirect_uri = self.origin + "/account/callback"
        self.callsigns = Callsigns(callsign_file)
        self.opener = opener or (lambda request: urllib.request.urlopen(request, timeout=8))
        self.clock = clock
        self.pending = {}
        self.lock = threading.Lock()

    # -- session cookie: base64url(json{profile,exp}).hmac -------------------------------------------------------------------------
    def sign(self, payload):
        return b64url(hmac.new(self.secret, payload.encode(), hashlib.sha256).digest())

    def session_cookie(self, profile):
        payload = b64url(json.dumps({"profile": safe_profile(profile), "exp": self.clock() + SESSION_SECONDS}))
        return "%s=%s.%s; Path=/; Max-Age=%d; HttpOnly; Secure; SameSite=Lax" % (SESSION_COOKIE, payload, self.sign(payload), SESSION_SECONDS)

    @staticmethod
    def clearing_cookie():
        return "%s=; Path=/; Max-Age=0; HttpOnly; Secure; SameSite=Lax" % SESSION_COOKIE

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
            return safe_profile(profile) if session.get("exp", 0) > self.clock() and profile.get("sub") else None
        except (ValueError, TypeError):
            return None

    # -- OpenID Connect -------------------------------------------------------------------------------------------------------------
    def login_url(self, query):
        now = self.clock()
        with self.lock:
            for key in [k for k, v in self.pending.items() if now - v["created"] > STATE_TTL_SECONDS]:
                del self.pending[key]
            while len(self.pending) >= MAX_PENDING:
                del self.pending[next(iter(self.pending))]
            state, nonce, verifier = random_value(), random_value(), random_value(64)
            self.pending[state] = {"nonce": nonce, "verifier": verifier, "created": now, "redirect": (query.get("mode") or [""])[0] == "redirect"}
        parameters = {
            "client_id": self.client_id, "redirect_uri": self.redirect_uri, "response_type": "code", "response_mode": "query",
            "scope": "openid profile email", "state": state, "nonce": nonce,
            "code_challenge": b64url(hashlib.sha256(verifier.encode()).digest()), "code_challenge_method": "S256",
        }
        if (query.get("prompt") or [""])[0] == "none":
            parameters["prompt"] = "none"
        return self.issuer + "/protocol/openid-connect/auth?" + urllib.parse.urlencode(parameters)

    def _post(self, path, fields):
        request = urllib.request.Request(self.backchannel + path, data=urllib.parse.urlencode(fields).encode(),
                                         headers={"Content-Type": "application/x-www-form-urlencoded"})
        with self.opener(request) as response:
            return json.loads(response.read())

    def _get(self, path, token):
        request = urllib.request.Request(self.backchannel + path, headers={"Authorization": "Bearer " + token})
        with self.opener(request) as response:
            return json.loads(response.read())

    def finish_login(self, query):
        """Returns (profile or None, redirect_to_outiva). A guest result is never an error page: the game is playable without an account."""
        state = (query.get("state") or [""])[0]
        with self.lock:
            request = self.pending.pop(state, None) if state else None
        if request is None:
            return None, False
        if self.clock() - request["created"] > STATE_TTL_SECONDS or "error" in query:
            return None, request["redirect"]
        try:
            tokens = self._post("/protocol/openid-connect/token", {
                "grant_type": "authorization_code", "client_id": self.client_id, "client_secret": self.client_secret,
                "redirect_uri": self.redirect_uri, "code": (query.get("code") or [""])[0], "code_verifier": request["verifier"]})
            claims = jwt_claims(tokens.get("id_token"))
            audiences = claims.get("aud") if isinstance(claims.get("aud"), list) else [claims.get("aud")]
            if (claims.get("iss") != self.issuer or self.client_id not in audiences or claims.get("nonce") != request["nonce"]
                    or claims.get("exp", 0) <= self.clock() or not claims.get("sub")):
                raise ValueError("Invalid identity claims.")
            user = safe_profile(self._get("/protocol/openid-connect/userinfo", tokens.get("access_token", "")))
            if not user["sub"] or user["sub"] != claims["sub"]:
                raise ValueError("User profile did not match the ID token.")
            return user, request["redirect"]
        except (OSError, ValueError, KeyError, TypeError) as error:
            print("OIDC callback failed: %s" % error, file=sys.stderr)
            return None, request["redirect"]

STATUS_PATH = "/outiva-status.json"
PLAYERS_GAUGE = re.compile(r"^hovernet_players_active ([0-9]{1,6})$", re.M)
LOOPBACK_HOSTS = ("127.0.0.1", "localhost", "::1")


class StatusAdapter:
    """The small public view of the race server for the Outiva console: availability and how many players are driving.

    It reads the loopback monitoring port (/health, /metrics) and passes on nothing else: no raw metrics, names, results or device ids.
    A draining or unreachable server is "degraded" (local play and the web assets still work); the count is the explicit
    hovernet_players_active gauge, which excludes spectators and lobby visitors, and is 0 when it cannot be read."""

    def __init__(self, base_url, timeout=2.0, fetch=None, clock=time.time, cache_seconds=5):
        parts = urllib.parse.urlsplit(base_url)
        try:
            port = parts.port
        except ValueError:
            port = None
        if (parts.scheme != "http" or parts.hostname not in LOOPBACK_HOSTS or parts.username is not None or parts.password is not None
                or parts.path not in ("", "/") or parts.query or parts.fragment or port is None):
            raise ValueError("the status URL must be http:// on a loopback address (the monitoring port is never public)")
        self.base_url = "%s://%s" % (parts.scheme, parts.netloc)
        self.timeout = timeout
        self.fetch = fetch or self.http_get
        self.clock = clock
        self.cache_seconds = cache_seconds
        self.cached = None
        self.lock = threading.Lock()
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def http_get(self, path):
        try:
            with self.opener.open(self.base_url + path, timeout=self.timeout) as response:
                return response.status, response.read(65536).decode("utf-8", "replace")
        except urllib.error.HTTPError as error:   # /health answers 503 while draining
            return error.code, error.read(65536).decode("utf-8", "replace")

    def compute(self):
        try:
            health, _ = self.fetch("/health")
        except (OSError, ValueError):
            return {"status": "degraded", "players": 0}
        players = None
        if health in (200, 503):
            try:
                code, text = self.fetch("/metrics")
                found = PLAYERS_GAUGE.search(text) if code == 200 else None
                players = int(found.group(1)) if found else None
            except (OSError, ValueError):
                pass
        available = health == 200 and players is not None
        return {"status": "available" if available else "degraded", "players": players or 0}

    def status(self):
        with self.lock:
            now = self.clock()
            if self.cached is None or now - self.cached[0] >= self.cache_seconds or now < self.cached[0]:
                self.cached = (now, self.compute())
            return dict(self.cached[1])


def make_handler(account, status=None, console_origin="https://outiva.com"):
    class Handler(BaseHTTPRequestHandler):
        server_version = "HoverNetAccount"
        sys_version = ""

        def log_message(self, fmt, *args):   # request lines carry state and codes: do not log them
            pass

        def send(self, status, headers=None, body=b""):
            self.send_response(status)
            if "Cache-Control" not in (headers or {}):
                self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            for key, value in (headers or {}).items():
                self.send_header(key, value)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            url = urllib.parse.urlsplit(self.path)
            query = urllib.parse.parse_qs(url.query)
            if url.path == STATUS_PATH:
                if status is None:
                    return self.send(404)
                return self.send(200, {"Content-Type": "application/json; charset=utf-8", "Cache-Control": "public, max-age=15",
                                       "Access-Control-Allow-Origin": console_origin, "Vary": "Origin"},
                                 json.dumps(status.status(), separators=(",", ":")).encode())
            if account is None:   # not configured: everyone is a guest
                return self.send(204) if url.path == "/account/me" else self.send(404)
            if url.path == "/account/me":
                current = account.profile(self.headers.get("Cookie"))
                if not current:
                    return self.send(204)
                current["callsign"] = account.callsigns.get(current["sub"])
                return self.send(200, {"Content-Type": "application/json; charset=utf-8"}, json.dumps(current).encode())
            if url.path == "/account/login":
                return self.send(302, {"Location": account.login_url(query)})
            if url.path == "/account/callback":
                user, to_outiva = account.finish_login(query)
                headers = {"Location": account.outiva_origin + "/?outiva_sso=complete#/play/hovernet" if to_outiva else account.origin + "/"}
                if user:
                    headers["Set-Cookie"] = account.session_cookie(user)
                return self.send(302, headers)
            return self.send(404)

        def do_PUT(self):
            if self.path != "/account/callsign" or account is None:
                return self.send(404)
            current = account.profile(self.headers.get("Cookie"))
            if not current:
                return self.send(401)
            if self.headers.get("Origin") != account.origin or not (self.headers.get("Content-Type") or "").startswith("application/json"):
                return self.send(403)
            try:
                length = int(self.headers.get("Content-Length") or 0)
                if length > 1024:
                    return self.send(413)
                callsign = str(json.loads(self.rfile.read(length)).get("callsign", "")).strip()
            except (ValueError, AttributeError):
                return self.send(400)
            if not CALLSIGN.match(callsign):
                return self.send(400)
            account.callsigns.set(current["sub"], callsign)
            return self.send(204)

        def do_POST(self):
            if self.path != "/account/logout" or account is None:
                return self.send(404)
            if self.headers.get("Origin") != account.origin:
                return self.send(403)
            return self.send(204, {"Set-Cookie": account.clearing_cookie()})

    return Handler


def account_from_environment(env=os.environ):
    keys = ["HOVERNET_OIDC_ISSUER", "HOVERNET_OIDC_CLIENT_ID", "HOVERNET_OIDC_CLIENT_SECRET", "HOVERNET_SESSION_SECRET"]
    configured = [key for key in keys if env.get(key)]
    if not configured:
        return None
    if len(configured) != len(keys):
        raise SystemExit("OIDC needs " + ", ".join(keys) + " all set (or none, for guest-only play).")
    return Account(env["HOVERNET_OIDC_ISSUER"], env["HOVERNET_OIDC_CLIENT_ID"], env["HOVERNET_OIDC_CLIENT_SECRET"],
                   env["HOVERNET_SESSION_SECRET"], env.get("HOVERNET_ORIGIN", "https://hovernet.outiva.com"),
                   env.get("HOVERNET_OUTIVA_ORIGIN", "https://outiva.com"), env.get("HOVERNET_OIDC_BACKCHANNEL_URL"),
                   env.get("HOVERNET_CALLSIGN_FILE", "/var/lib/hovernet-account/callsigns.json"))


def main():
    account = account_from_environment()
    host = os.environ.get("HOVERNET_ACCOUNT_HOST", "127.0.0.1")
    port = int(os.environ.get("HOVERNET_ACCOUNT_PORT", "9602"))
    try:
        status = StatusAdapter(os.environ.get("HOVERNET_STATUS_URL", "http://127.0.0.1:9612"))
    except ValueError as error:
        raise SystemExit("HOVERNET_STATUS_URL: %s" % error)
    console = os.environ.get("HOVERNET_OUTIVA_ORIGIN", "https://outiva.com")
    server = ThreadingHTTPServer((host, port), make_handler(account, status, console))
    print("HoverNet account service on %s:%d (OIDC %s)" % (host, port, "enabled" if account else "disabled: guests only"), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
