"""Runs the game's textures through the AI upscaler (srvgg.py) and writes one PNG per texture.

  python3 -I tools/texture-ai/enhance_all.py <export dir> <model .pth> <output dir> [--scale 4] [--min-size 64]

The export directory comes from `HoverNetTextureExport ObjFac1.dat <dir>` (one <id>.ppm per texture and an index.tsv). Textures smaller than --min-size
(there are a dozen of 32 pixels or fewer: flat fills and tiny details a network cannot improve) are skipped; the game improves them itself.
Writes <id>.png and manifest.tsv (id, source width, source height, output width, output height, sha256 of the PNG).
"""
import hashlib
import os
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import srvgg  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    scale = 4
    min_size = 64
    for i, a in enumerate(sys.argv):
        if a == "--scale":
            scale = int(sys.argv[i + 1]); args.remove(sys.argv[i + 1])
        if a == "--min-size":
            min_size = int(sys.argv[i + 1]); args.remove(sys.argv[i + 1])
    export_dir, model, out_dir = args[:3]
    os.makedirs(out_dir, exist_ok=True)
    weights = srvgg.load_weights(model)
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(export_dir, "index.tsv"))]
    manifest = open(os.path.join(out_dir, "manifest.tsv"), "w")
    done = 0
    started = time.time()
    for r in rows:
        tid, w, h = r[0], int(r[4]), int(r[5])
        if w < min_size or h < min_size or w & (w - 1) or h & (h - 1):
            print(f"skip {tid} ({w}x{h})", flush=True)
            continue
        src = Image.open(os.path.join(export_dir, f"{tid}.ppm")).convert("RGB")
        x = np.asarray(src, dtype=np.float32) / 255.0
        y = srvgg.upscale(weights, x, scale=scale, wrap=True)
        out = Image.fromarray((y * 255.0 + 0.5).astype(np.uint8))
        path = os.path.join(out_dir, f"{tid}.png")
        out.save(path, optimize=True)
        digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
        manifest.write(f"{tid}\t{w}\t{h}\t{out.width}\t{out.height}\t{digest}\n"); manifest.flush()
        done += 1
        print(f"{tid}: {w}x{h} -> {out.width}x{out.height}  ({done} done, {time.time() - started:.0f}s)", flush=True)
    manifest.close()
    print(f"{done} textures written to {out_dir}")


if __name__ == "__main__":
    main()
