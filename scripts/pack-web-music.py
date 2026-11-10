#!/usr/bin/env python3
"""Pack NetTarget/Music/*.ogg into one file for the web page to download after the game starts.

Format: lines of "name size", an empty line, then the files' bytes one after another (see packaging/web/shell.html).
usage: pack-web-music.py OUTPUT.bin
"""
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent / "NetTarget" / "Music"
files = sorted(root.glob("*.ogg"))
if len(sys.argv) != 2 or not files:
    raise SystemExit(__doc__)
with open(sys.argv[1], "wb") as out:
    out.write("".join(f"{f.name} {f.stat().st_size}\n" for f in files).encode() + b"\n")
    for f in files:
        out.write(f.read_bytes())
print(f"packed {len(files)} files into {sys.argv[1]}")
