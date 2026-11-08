# Track Market: submission and catalogue (specification, draft)

Status: **draft; built and tested on Linux: the checks (`scripts/check-track-submission.sh`, shared by the editor and the service) and the submission service with the live catalogue (`packaging/web/hovernet_market.py`, `scripts/test-web-market.py`, installed by `packaging/web/install-market.sh`); not yet built: the Track Market screen in the game, the Publish button in the editor, the server-side sync of listed tracks.** Decisions are in [ADR 0004](../adr/0004-track-marketplace.md); this file says exactly what is sent, checked and published so the editor, the service, the clients and the servers can be written against it independently.

## 1. A submission

An authenticated author (ADR 0004 decision 2) sends one request with two files and a few fields.

| Part | Content |
| --- | --- |
| `package` | the track package, `.hntp` ([track-package.md](track-package.md)), at most 2 MB, with `author` (the display name to publish) and `licence` (`CC0-1.0`, `CC-BY-4.0` or `CC-BY-SA-4.0`) in its manifest |
| `replay` | a finishing replay (`.hnr`, [replay-format.md](replay-format.md)) of the track in the package, one player, at least one lap |
| `declaration` | the version number of the author declaration the author accepted (text lives in the terms of use; the service records the version, time and account) |
| `description` | at most 200 characters, shown in the market (the package manifest's description is used when empty) |

### 1b. Publishing from the editor (`POST /market/submit-source`)

The editor does not make packages or replays. It sends the track source (the `.track.txt`, at most 1 MB) with `name`, `author`, `licence`, `declaration` and `description`. The service compiles it with the real compiler, packs it, finds a finishing run in the real simulation (the game's own driver, tried with several search seeds, at most one track at a time on the host) and then applies the checks of section 2 to the result. A source the compiler refuses, or that no finishing run is found for, is turned down with the reason and counts toward the day's limit. Finding a run is a search: a track it fails on may still be drivable, so the editor's own playtest (a replay of a finishing lap, submitted through `/market/submit`) remains the other way in.

## 2. The checks (all must pass; the same script runs in the editor and on the server)

`scripts/check-track-submission.sh PACKAGE REPLAY` runs, with the programs in the same release as the game:

1. `HoverNetTrackPack verify PACKAGE --catalogue`: the package is well formed, hashes match, and it has a named author and an approved licence.
2. `HoverNetTrackPack unpack` to a temporary `.trk`, then `HoverNetTrackValidator` at the strict level: loads, starts, is a race (finish line and both checkpoints, at least eight starts).
3. `HoverNetTrackPack lint --strict`: no authoring warnings (for example no power-ups or fuel).
4. `HoverNetReplay verify REPLAY --track=TRACK`: the replay's recorded track hash equals the submitted track and re-simulating it reproduces every checkpoint hash, and it finishes at least one lap.
5. Limits the scripts above do not cover, enforced by the service: package size, description length, the background image is one of the game's bundled backgrounds (compared by hash), name rules (printable text, unique per author, not a bundled track's name), per-account rate and quota.

The script prints one line per check, `ok <name>` or `FAIL <name>: <reason in plain words>`, and exits non-zero on any failure, so an editor can show the lines and a service can store them.

## 3. States of a submission

`checking` then one of `refused` (a check failed; the reasons are kept), `waiting for review` (passed; this author has no listed track yet), or `listed`. A listed track can become `unlisted` (the author's choice, or a moderator while a report is looked at) and `revoked` (takedown). Only `listed` tracks appear in the catalogue as available.

## 4. The catalogue

One file, served at a fixed URL on the web host over TLS (and signed once the production key exists, `docs/signing.md`), UTF-8, tab-separated, one track per line, sorted by `asset`. The first line is a header with the column names. It extends the columns of `NetTarget/CommunityTracks.tsv`:

| Column | Meaning |
| --- | --- |
| `name` | display name, unique per author |
| `mode` | `race` or `freeplay` |
| `starts`, `rooms`, `warnings` | as in the community manifest |
| `bytes`, `sha256` | size and SHA-256 of the `.trk` |
| `shard`, `asset`, `gzip` | where the immutable hash-named `.trk.gz` is and its size |
| `author` | the published display name |
| `licence` | the SPDX id |
| `listed` | date listed, `YYYY-MM-DD` |
| `version` | 1 for the first version of this name by this author, then 2, 3... (an update replaces the older version once it is listed) |
| `status` | `listed` or `revoked` |

A `revoked` line keeps `sha256` and `asset` and drops everything else that identifies the content; clients delete a downloaded track with that hash, servers remove it, and neither offers it again. A header line `catalogue-version N` rises with every change so a client can tell it is up to date, and a client keeps the last good catalogue when a fetch fails.

## 5. What a client and a server do with it

- **Client** (desktop and browser, one code base): fetch the catalogue when the main menu opens and merge its tracks into the one Community tracks list (they are community tracks); show name, author, licence and description; download `<base>/<shard>/<asset>.trk.gz`, unpack, check size and SHA-256, and only then install (exactly as community tracks are installed today); show attribution in the track selector and on the results screen; a Report button sends the track's hash and a reason to the service.
- **Official race server:** sync every `listed` track and remove every `revoked` one on a schedule; a track it does not have is "cannot be hosted here", as today.
- **Private servers:** unchanged (`--community-dir`); they may run the same sync.

## 6. Not specified yet

Ratings, play counts and tags; the exact terms-of-use text and declaration; the moderator console's track screens; the update flow's wording; the signing key's public value. None changes the formats above.
