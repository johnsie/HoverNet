#!/usr/bin/env bash
# The browser path for protocol 3.0, end to end with nothing but the Python standard library as the "browser": a lobby server started
# with --ws-origin accepts a WebSocket upgrade from the allowed origin only, carries the protocol's Hello / race list / host / join
# frames in binary messages, and refuses a foreign origin, a wrong path and a text message. Run from the repository root.
#   scripts/test-websocket.sh build/HoverNetAuthServer
set -uo pipefail
server="${1:?usage: $0 <server>}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
"$server" --lobby --port 0 --ws-origin https://hovernet.example --limit-ticks 200 --start-delay-ms 300 > "$dir/server.txt" 2>&1 &
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
python3 -I - "$port" <<'PY'
import base64, os, socket, struct, sys, time
port = int(sys.argv[1])
fail = 0
def check(ok, what):
    global fail
    if not ok:
        print("FAIL:", what); fail = 1

def connect(origin="https://hovernet.example", path="/auth/"):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    head = "GET %s HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n" % (path, key)
    if origin: head += "Origin: %s\r\n" % origin
    s.sendall((head + "\r\n").encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        d = s.recv(4096)
        if not d: break
        buf += d
    return s, buf

def send(s, opcode, payload):
    mask = os.urandom(4)
    n = len(payload)
    hdr = bytes([0x80 | opcode]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n)) + mask
    s.sendall(hdr + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

def recv_message(s):
    def need(n):
        b = b""
        while len(b) < n:
            d = s.recv(n - len(b))
            if not d: raise EOFError
            b += d
        return b
    h = need(2)
    n = h[1] & 0x7F
    if n == 126: n = struct.unpack(">H", need(2))[0]
    return h[0] & 0x0F, need(n)

def frame(ftype, payload): return struct.pack("<HB", len(payload) + 1, ftype) + payload
MAGIC = b"HN3\x00"
def hello(name):
    nm = name.encode()
    return frame(1, MAGIC + struct.pack("<HHIH", 3, 0, 0, 1200) + bytes([len(nm)]) + nm)
def host_race(track, players, laps):
    t = track.encode()
    return frame(22, bytes([len(t)]) + t + bytes([players, laps, 1]))
def frames_in(blob):
    out = []
    while len(blob) >= 3:
        n = struct.unpack("<H", blob[:2])[0]
        if len(blob) < 2 + n: break
        out.append((blob[2], blob[3:2 + n])); blob = blob[2 + n:]
    return out
def types_until(s, wanted, limit=20):
    seen = []
    for _ in range(limit):
        op, data = recv_message(s)
        if op != 2: return seen
        seen += [t for t, _ in frames_in(data)]
        if wanted in seen: break
    return seen

s, r = connect()
check(r.startswith(b"HTTP/1.1 101"), "the allowed origin is upgraded")
send(s, 2, hello("browser1"))
check(21 in types_until(s, 21), "a Hello in a binary message gets the race list back")
send(s, 2, host_race("ClassicH", 2, 1))
seen = types_until(s, 2)
check(2 in seen, "hosting a race over the WebSocket gets a HelloAck")
send(s, 9, b"hi")
op, data = recv_message(s)
while op == 2: op, data = recv_message(s)
check(op == 10 and data == b"hi", "a ping is answered with a pong")
send(s, 1, b"text is not the protocol")
closed = False
try:
    for _ in range(10):
        op, data = recv_message(s)
        if op == 8: closed = data[:2] == struct.pack(">H", 1003); break
except (EOFError, ConnectionError, socket.timeout):
    pass
check(closed, "a text message is answered with close code 1003")
s.close()
for origin, path, code in (("https://evil.example", "/auth/", b"403"), (None, "/auth/", b"403"), ("https://hovernet.example", "/nope/", b"404")):
    s, r = connect(origin, path)
    check(r.startswith(b"HTTP/1.1 " + code), "refused with %s: %s" % (code.decode(), r[:40]))
    s.close()
# The plain TCP protocol still works on the same port after all that.
t = socket.create_connection(("127.0.0.1", port), timeout=5)
t.sendall(hello("tcp1"))
data = b""
deadline = time.time() + 5
while time.time() < deadline and 21 not in [x for x, _ in frames_in(data)]:
    data += t.recv(4096)
check(21 in [x for x, _ in frames_in(data)], "plain TCP clients still get the race list on the same port")
sys.exit(fail)
PY
rc=$?
[ $rc -eq 0 ] && echo "websocket: browser path works end to end (upgrade rules, hello, host, ping, text refused), TCP unaffected"
exit $rc
