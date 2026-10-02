#!/usr/bin/env python3
"""Classifies every tracked file by where it came from, for the licence split.

    scripts/audit-provenance.py [OUTPUT.csv]

Classes (a heuristic: a human reviews the "candidate" classes before anything moves):

  third-party        bundled third-party code (own licence)
  licensed-header    carries a GrokkSoft / HoverRace licence header
  baseline           present in the repository's first commit, i.e. imported
                     HoverRace material (even if edited since)
  baseline-asset     as above, and a binary/art/sound/model asset
  added-engine       added later, no licence header, but written against the
                     HoverRace engine (uses its MR_* API or headers)
  added-reference    added later, text that talks about HoverRace internals
  added-asset        added later, binary asset of unrecorded origin
  generated          test output / build output kept in the repository
  candidate          added later, no header, no engine dependence, no HoverRace
                     internals: the only class that may move to a clean project
"""
import csv, os, re, subprocess, sys

def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout

root = git("rev-list", "--max-parents=0", "HEAD").split()[-1]
baseline = set(git("ls-tree", "-r", "--name-only", root).splitlines())
files = git("ls-files").splitlines()

HEADER = re.compile(r"GrokkSoft|Langlois|HoverRace SourceCode License", re.I)
ENGINE = re.compile(r"\bMR_[A-Za-z]|#include\s+\"\.\./(Model|Util|MainCharacter|VideoServices|ObjFacTools|ObjFac1|Game2|Platform)/|MfcCompat|StdAfx")
REFERENCE = re.compile(r"ObjFac1|MFC|HoverRace|fixed-point|MR_[A-Za-z]|rasteri[sz]er|Win32/MFC|CArchive|GrokkSoft", re.I)
ASSET_EXT = {".bmp", ".png", ".jpg", ".wav", ".dat", ".msh", ".mdl", ".avi", ".ico", ".res", ".fbres", ".trk", ".mz", ".pal", ".aps"}
TEXT_EXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".sh", ".ps1", ".md", ".txt", ".yml", ".yaml",
            ".cmake", ".iss", ".tsv", ".json", ".xml", ".rc", ".url", ".bat"}

def text_of(path):
    try:
        with open(path, "rb") as f:
            data = f.read(400_000)
        if b"\0" in data:
            return None
        return data.decode("utf-8", "replace")
    except OSError:
        return None

rows = []
for path in files:
    ext = os.path.splitext(path)[1].lower()
    in_base = path in baseline
    text = text_of(path)
    if path.startswith("NetTarget/ThirdParty/"):
        cls, why = "third-party", "bundled third-party code"
    elif path.startswith(("test_output/", "build/", "dist/")) or path.endswith((".log",)):
        cls, why = "generated", "kept output, not source"
    elif text is not None and HEADER.search(text):
        cls, why = "licensed-header", "carries a GrokkSoft/HoverRace licence header"
    elif in_base and (ext in ASSET_EXT or text is None):
        cls, why = "baseline-asset", "binary present in the first (imported) commit"
    elif in_base:
        cls, why = "baseline", "present in the first (imported) commit"
    elif text is None or ext in ASSET_EXT:
        cls, why = "added-asset", "binary added later, origin not recorded"
    elif ext in {".c", ".cc", ".cpp", ".h", ".hpp"} and ENGINE.search(text):
        cls, why = "added-engine", "written against the HoverRace engine API"
    elif REFERENCE.search(text):
        cls, why = "added-reference", "text that refers to HoverRace internals"
    else:
        cls, why = "candidate", "added later; no header, no engine dependence, no HoverRace internals"
    rows.append((path, cls, why))

out = sys.argv[1] if len(sys.argv) > 1 else "provenance-audit.csv"
with open(out, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["path", "class", "reason"])
    w.writerows(rows)

counts = {}
for _, c, _ in rows:
    counts[c] = counts.get(c, 0) + 1
print(f"{len(rows)} tracked files (baseline commit {root[:8]}, {len(baseline)} files)")
for c, n in sorted(counts.items(), key=lambda kv: -kv[1]):
    print(f"  {c:16} {n}")
