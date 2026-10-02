# HoverRace licence compliance

HoverNet contains source derived from HoverRace and distributed under the
GrokkSoft HoverRace SourceCode License v1.0. The complete licence is in
[`LICENSE`](../LICENSE).

## Modified-file notices

The licence requires modified files to carry prominent notices stating that the
files were changed and the date of the change. HoverNet provides
`scripts/audit-hoverrace-modification-notices.py` to make that requirement
auditable without inventing dates or marking untouched upstream files as changed.

The audit deliberately requires a **pristine upstream HoverRace checkout**. This
prevents the tool from assuming that every licensed file in HoverNet was modified.
It compares matching files byte-for-byte and obtains the latest HoverNet change
date from `git log`.

Example:

```bash
git clone https://github.com/HoverRace/HoverRace.git /tmp/hoverrace-upstream
python3 scripts/audit-hoverrace-modification-notices.py \
  --upstream-dir /tmp/hoverrace-upstream
```

After reviewing the report, insert/update generated notices with:

```bash
python3 scripts/audit-hoverrace-modification-notices.py \
  --upstream-dir /tmp/hoverrace-upstream --fix
```

Review the resulting diff before committing it. Files that do not have a matching
upstream path are reported as `UNRESOLVED` and are intentionally not modified
automatically; their provenance must be checked manually.

A generated notice has this form:

```cpp
// HoverNet modification notice: Modified for HoverNet; latest recorded change YYYY-MM-DD.
// See repository history for authorship and detailed changes.
```

The date is the latest recorded change to that path in the HoverNet git history.
The repository history remains the detailed record of what changed and who made
the change.

## Release checklist

Before a public release:

1. Run the audit against a known pristine upstream checkout.
2. Resolve every `UNRESOLVED` item manually.
3. Run the tool with `--fix`, review the diff, and commit the notices.
4. Run the audit again without `--fix`; it should report no missing/outdated
   notices and no unresolved files.
5. Include the GrokkSoft licence conditions and disclaimer with binary
   distributions as required by `LICENSE`.
6. Confirm separately that any planned commercial activity has the prior written
   permission required by the GrokkSoft licence.

This process is a repository compliance aid, not a change to the licence terms and
not a substitute for establishing rights to assets whose provenance is unknown.
