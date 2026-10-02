#!/usr/bin/env python3
"""Audit/fix HoverRace SourceCode License v1.0 modified-file notices.

The GrokkSoft licence requires modified files to carry prominent notices stating
that they were changed and the date of the change. This script identifies source
files carrying the GrokkSoft HoverRace SourceCode License header and compares each
file with the corresponding path in a supplied pristine upstream HoverRace tree.

Use --upstream-dir with a checkout of the original HoverRace source. In --fix mode,
a notice is inserted only when the current file differs from the upstream file;
the date is derived from this repository's git history, never guessed.
"""
from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MARKER = "GrokkSoft HoverRace SourceCode License v1.0"
NOTICE_TAG = "HoverNet modification notice:"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".rc"}
SKIP_PARTS = {".git", ".vs", "ThirdParty"}


def candidates():
    for p in ROOT.rglob("*"):
        if not p.is_file() or p.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if any(part in SKIP_PARTS for part in p.parts):
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="strict")
        except (UnicodeDecodeError, OSError):
            continue
        if MARKER in text:
            yield p, text


def last_change_date(rel: pathlib.Path) -> str:
    proc = subprocess.run(
        ["git", "log", "-1", "--format=%cs", "--", rel.as_posix()],
        cwd=ROOT, text=True, capture_output=True, check=True,
    )
    value = proc.stdout.strip()
    if not value:
        raise RuntimeError(f"No git change date found for {rel}")
    # Validate before writing a legal-compliance notice.
    dt.date.fromisoformat(value)
    return value


def notice_for(date: str) -> str:
    return (
        f"// {NOTICE_TAG} Modified for HoverNet; latest recorded change {date}.\n"
        "// See repository history for authorship and detailed changes.\n"
    )


def insert_notice(text: str, notice: str) -> str:
    # Keep the notice prominent: immediately before the existing licence marker's
    # comment block when possible, otherwise at the beginning of the file.
    pos = text.find(MARKER)
    line_start = text.rfind("\n", 0, pos) + 1
    # Walk upward through adjacent // comment lines to the beginning of that block.
    start = line_start
    while start > 0:
        prev_end = start - 1
        prev_start = text.rfind("\n", 0, prev_end) + 1
        if not text[prev_start:prev_end].lstrip().startswith("//"):
            break
        start = prev_start
    return text[:start] + notice + text[start:]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream-dir", type=pathlib.Path, required=True,
                    help="pristine checkout of the original HoverRace source")
    ap.add_argument("--fix", action="store_true",
                    help="insert/update required notices for files changed from upstream")
    args = ap.parse_args()
    upstream = args.upstream_dir.resolve()
    if not upstream.is_dir():
        ap.error("--upstream-dir must be an existing directory")

    changed = missing = fixed = 0
    errors = []
    for path, text in candidates():
        rel = path.relative_to(ROOT)
        original = upstream / rel
        if not original.is_file():
            # A licensed file absent upstream cannot safely be classified automatically.
            errors.append(f"UNRESOLVED {rel}: no matching upstream file")
            continue
        try:
            upstream_text = original.read_text(encoding="utf-8", errors="strict")
        except (UnicodeDecodeError, OSError) as exc:
            errors.append(f"UNRESOLVED {rel}: cannot read upstream: {exc}")
            continue
        if text == upstream_text:
            continue
        changed += 1
        date = last_change_date(rel)
        required = f"{NOTICE_TAG} Modified for HoverNet; latest recorded change {date}."
        has_current = required in text
        if has_current:
            continue
        missing += 1
        if args.fix:
            # Replace an older generated notice rather than accumulating notices.
            lines = text.splitlines(keepends=True)
            lines = [ln for ln in lines if NOTICE_TAG not in ln and
                     "See repository history for authorship and detailed changes." not in ln]
            text = "".join(lines)
            path.write_text(insert_notice(text, notice_for(date)), encoding="utf-8", newline="")
            fixed += 1
            print(f"FIXED {rel} ({date})")
        else:
            print(f"MISSING {rel} (latest change {date})")

    for msg in errors:
        print(msg, file=sys.stderr)
    print(f"Licensed files changed from upstream: {changed}; missing/outdated notices: {missing}; fixed: {fixed}; unresolved: {len(errors)}")
    if errors:
        return 2
    if missing and not args.fix:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
