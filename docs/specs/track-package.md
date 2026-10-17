# Track package v1

Status: **implemented and tested** (`NetTarget/Authority/TrackPackage.*`, the importer and packager `HoverNetTrackPack`, ctest
`HoverNetTrackPack`). Stage 4 of [roadmap-3.0.md](../roadmap-3.0.md): a versioned, portable wrapper that gives every classic `.trk`
track an identity, an author, a licence and a description without changing a byte of the track itself.

## Why a wrapper

The classic `.trk` archive has no version number, no author, no licence and no declared contents. Rewriting 1,000 community tracks
into a new level format is risky and unnecessary for 3.0: the simulation loads the classic archive exactly. The package keeps the
original bytes as its payload (so a track can always be unpacked to the exact file the creator made) and adds what was missing.
A future native track format can be a new package `format` value; the wrapper, the manifest and the tools stay the same.

## Layout (little-endian)

| Field | Size | Meaning |
| --- | ---: | --- |
| magic | 4 | ASCII `HNTP` |
| version | 2 | `1`; any other value is refused |
| flags | 2 | `0`; anything else is refused |
| manifest length | 4 | at most 16 KB |
| manifest | n | UTF-8 lines `key=value` (keys `a-z`, `0-9`, `-`; sorted; unique; each value one line of valid text, at most 200 bytes) |
| payload length | 4 | at most 64 MB, and exactly the bytes that remain before the CRC |
| payload | n | the classic `.trk` file, unmodified |
| crc32 | 4 | CRC-32 (IEEE) of everything before it |

## Manifest keys

Required: `format` (`1` = classic `.trk` payload), `name`, `author` (`unknown` when not known), `licence` (`unspecified` when not known),
`payload-sha256`, `payload-bytes`, `mode` (`race` or `freeplay`), `rooms`, `starts`. Written by the importer: `importer`, `elements`,
`finish-lines`, `checkpoints`, `power-ups`, `bumper-gates`, `fuel-zones`, `speed-zones`. Optional: `description`, `created`. Packing the
same track with the same metadata always gives the same bytes (there is no timestamp unless you pass `--created`).

`mode` is `race` when the track has a finish line and both checkpoints, otherwise `freeplay`: no laps can be completed, the HUD shows
elapsed time, and the authoritative server runs the race to a time limit (10 minutes by default) instead of waiting for a finish.

## The importer

`HoverNetTrackPack pack IN.trk OUT.hntp --author A --licence L [--name N] [--description D] [--created DATE]` loads the track into the
simulation with the game's own loader and refuses it, with the exact reason, if the game could not play it: the archive cannot be
opened or loaded, sections of it could not be read, there are no rooms, no start position, more starts than the game supports, or a
start outside the track. `verify` re-checks the CRC, the manifest rules, the payload hash and size, and re-runs the importer on the
payload to confirm the manifest still describes it. `unpack` writes the original bytes back. `corpus DIR OUTDIR REPORT.tsv` imports a
whole directory and writes one report line per track (result, mode, rooms, starts, size, reason); it exits non-zero if fewer than 95%
import or any package fails to verify.

## Results on the community library (17 October 2026)

All **1,012** of 1,012 tracks import and verify (805 races, 207 free play); none failed, so there are no failure reasons to list. The
mode of every track agrees with the community manifest (`NetTarget/CommunityTracks.tsv`) that the downloader uses. The library has no
author or licence information, so packages made from it say `author=unknown`, `licence=unspecified`; recording real authorship and
licences is a content-curation task (below), not something the importer can invent.

## Rules for adding a track to the official catalogue

1. A package must name its author and a licence that allows HoverNet to distribute it; `unknown` and `unspecified` are acceptable only
   for tracks that were already part of the community library when 3.0 began and are kept for archival compatibility.
2. `verify` must pass, and the track must also pass the validator at the level the catalogue requires.
3. Removal on request: a creator can ask for a track to be withdrawn; the catalogue manifest then drops it and clients stop offering it.

## Not built yet

A signed catalogue with review, reporting and revocation; per-track dependency lists (the classic archive has none); the visual editor
and one-click playtesting; a native (non-classic) level format.
