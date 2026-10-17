#!/usr/bin/env bash
# Abuse of a live lobby server over real sockets, plain TCP and WebSocket: garbage, truncated and oversized frames, half-open upgrades,
# connection floods, bad masks. Afterwards the server must be alive, healthy, and still serve a normal client; under ASan/UBSan a
# single memory error would have aborted it. Run from the repository root.
#   scripts/test-abuse.sh build/HoverNetAuthServer
set -uo pipefail
server="${1:?usage: $0 <server>}"
dir="$(mktemp -d)"; trap 'kill $(jobs -p) 2>/dev/null; rm -rf "$dir"' EXIT
"$server" --lobby --port 0 --status-port 0 --ws-origin https://hovernet.example --max-connections 64 > "$dir/server.txt" 2>&1 &
spid=$!
for _ in $(seq 1 100); do grep -q "listening on" "$dir/server.txt" 2>/dev/null && break; sleep 0.1; done
port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\),.*/\1/p' "$dir/server.txt" | head -1)
sport=$(sed -n 's/.*status on 127.0.0.1:\([0-9]*\) .*/\1/p' "$dir/server.txt" | head -1)
[ -n "$port" ] && [ -n "$sport" ] || { echo "FAIL: the server did not start"; cat "$dir/server.txt"; exit 1; }
python3 -I - "$port" <<'PY'
import base64, os, random, socket, struct, sys, time
port = int(sys.argv[1]); rnd = random.Random(7)
def conn():
    s = socket.create_connection(("127.0.0.1", port), timeout=2)
    return s
def hello():
    nm = b"abuse"
    pl = b"HN3\x00" + struct.pack("<HHIH", 3, 0, 0, 1200) + bytes([len(nm)]) + nm
    return struct.pack("<HB", len(pl) + 1, 1) + pl
def upgrade(origin="https://hovernet.example"):
    return ("GET /auth/ HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: %s\r\n\r\n"
            % (base64.b64encode(os.urandom(16)).decode(), origin)).encode()
def masked(op, payload, fin=True, lie=None):
    mask = os.urandom(4); n = len(payload) if lie is None else lie
    h = bytes([(0x80 if fin else 0) | op])
    h += bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n) if n < 65536 else bytes([0x80 | 127]) + struct.pack(">Q", n)
    return h + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
def send(s, data):
    try: s.sendall(data)
    except OSError: pass
attacks = 0
for i in range(120):
    try: s = conn()
    except OSError: continue
    kind = i % 12
    if kind == 0: send(s, os.urandom(rnd.randint(1, 3000)))
    elif kind == 1: send(s, struct.pack("<HB", 65000, 9) + os.urandom(100))                    # a frame longer than allowed
    elif kind == 2: send(s, hello()[:rnd.randint(1, 10)])                                       # truncated hello
    elif kind == 3: send(s, hello() * 200)                                                       # a hello flood
    elif kind == 4: send(s, b"GET /auth/ HTTP/1.1\r\nHost: x\r\n")                               # half-open upgrade, never finished
    elif kind == 5: send(s, b"GET / HTTP/1.1\r\n" + b"X: " + b"a" * 9000 + b"\r\n\r\n")           # giant header
    elif kind == 6: send(s, upgrade("https://evil.example"))
    elif kind == 7: send(s, upgrade() + masked(2, os.urandom(500)))                              # random binary after a good upgrade
    elif kind == 8: send(s, upgrade() + masked(2, b"x", lie=126)[:4])                             # a frame that promises more than it sends
    elif kind == 9: send(s, upgrade() + masked(2, b"y" * 70000, lie=70000))                      # a 70 KB message
    elif kind == 10: send(s, upgrade() + os.urandom(300))                                        # unmasked noise after the upgrade
    else: send(s, upgrade() + b"".join(masked(0, os.urandom(40), fin=False) for _ in range(300)))  # endless continuation fragments
    attacks += 1
    if kind in (4, 8): continue            # leave these open on purpose
    s.close()
print("sent %d attacks" % attacks)
PY
sleep 1
fail=0
kill -0 "$spid" 2>/dev/null || { echo "FAIL: the server died"; tail -5 "$dir/server.txt"; exit 1; }
[ "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$sport/health")" = 200 ] || { echo "FAIL: /health is not 200 after the abuse"; fail=1; }
python3 -I - "$port" <<'PY' || { echo "FAIL: a normal client was not served after the abuse"; fail=1; }
import socket, struct, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=5)
nm = b"normal"
pl = b"HN3\x00" + struct.pack("<HHIH", 3, 0, 0, 1200) + bytes([len(nm)]) + nm
s.sendall(struct.pack("<HB", len(pl) + 1, 1) + pl)
data = b""; end = time.time() + 5
while time.time() < end and b"" == data[:0] and (len(data) < 3 or data[2] != 21):
    d = s.recv(4096)
    if not d: break
    data += d
assert len(data) >= 3 and data[2] == 21, data[:8]
PY
kill -TERM "$spid"; wait "$spid"; rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the server exited with $rc after the abuse"; fail=1; }
[ $fail -eq 0 ] && echo "abuse: 120 hostile connections (garbage, oversized, truncated, half-open, bad WebSocket frames), then the server was alive, healthy and still served a normal client"
exit $fail
