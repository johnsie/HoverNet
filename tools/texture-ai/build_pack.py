"""Builds the texture pack (ObjFac1.textures) from the PNGs that enhance_all.py wrote.

  python3 -I tools/texture-ai/build_pack.py <png directory> <output pack>

Format (little-endian): "HNTXPK1\\n", u32 count, then per image: u32 id, u32 width, u32 height, u32 PNG size, the PNG. The reader is NetTarget/ObjFacTools/TexturePack.cpp.
"""
import os
import struct
import sys

from PIL import Image


def main():
    png_dir, out = sys.argv[1:3]
    entries = []
    for line in open(os.path.join(png_dir, "manifest.tsv")):
        tid, sw, sh, w, h, _digest = line.rstrip("\n").split("\t")
        data = open(os.path.join(png_dir, f"{tid}.png"), "rb").read()
        im = Image.open(os.path.join(png_dir, f"{tid}.png"))
        assert im.size == (int(w), int(h)), (tid, im.size, w, h)
        entries.append((int(tid), int(w), int(h), data))
    entries.sort()
    with open(out, "wb") as f:
        f.write(b"HNTXPK1\n")
        f.write(struct.pack("<I", len(entries)))
        for tid, w, h, data in entries:
            f.write(struct.pack("<IIII", tid, w, h, len(data)))
            f.write(data)
    print(f"{len(entries)} images, {os.path.getsize(out) / 1e6:.1f} MB -> {out}")


if __name__ == "__main__":
    main()
