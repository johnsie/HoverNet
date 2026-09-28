#!/usr/bin/env python3
"""Deterministic black-box fuzzer for RaceServer's framed TCP decoder."""

import os
import random
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time


def frame(message_type, payload):
    return struct.pack("<HB", (message_type & 0x3F) << 10, len(payload)) + payload


def connect(port):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2)
    hello = b"HNET" + struct.pack(">HHHI", 2, 0, 255, 0)
    sock.sendall(frame(59, hello))
    reply = sock.recv(14)
    if len(reply) < 4 or reply[3] != 0:
        raise RuntimeError("protocol negotiation failed")
    return sock


def wait_for_server(port):
    for _ in range(50):
        try:
            sock = connect(port)
            sock.close()
            return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("RaceServer did not start")


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <RaceServer>", file=sys.stderr)
        return 2
    port = 19877
    rng = random.Random(0x484E4554)
    log = tempfile.NamedTemporaryFile(prefix="hovernet-fuzz-", suffix=".log", delete=False)
    log.close()
    server = subprocess.Popen([sys.argv[1], str(port), log.name, "--require-protocol-2"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for_server(port)
        cases = 500
        sent = 0
        while sent < cases:
            try:
                sock = connect(port)
            except (OSError, RuntimeError):
                if server.poll() is not None:
                    raise RuntimeError(f"RaceServer crashed during fuzzing (exit {server.returncode})")
                continue
            try:
                # Stay below the per-connection invalid-message cutoff; reconnects
                # are part of the lifecycle pressure this harness is meant to add.
                for _ in range(min(8, cases - sent)):
                    message_type = rng.randrange(64)
                    payload = bytes(rng.randrange(256) for _ in range(rng.randrange(256)))
                    data = frame(message_type, payload)
                    offset = 0
                    while offset < len(data):
                        chunk = rng.randrange(1, min(17, len(data) - offset) + 1)
                        sock.sendall(data[offset:offset + chunk])
                        offset += chunk
                    sent += 1
            except (BrokenPipeError, ConnectionResetError, socket.timeout):
                pass
            finally:
                sock.close()

        survivor = connect(port)
        survivor.sendall(frame(60, b""))
        response = survivor.recv(258)
        survivor.close()
        if len(response) < 3:
            raise RuntimeError("RaceServer stopped responding after fuzz cases")
        print(f"RaceServer survived {cases} deterministic fragmented fuzz frames")
        return 0
    finally:
        server.send_signal(signal.SIGTERM)
        try:
            if server.wait(timeout=5) != 0:
                raise RuntimeError("RaceServer did not exit cleanly after fuzzing")
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
            raise RuntimeError("RaceServer hung after fuzzing")
        try:
            os.unlink(log.name)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
