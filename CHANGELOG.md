# Changelog

All notable changes to HoverNet. Versions are the `vX.Y.Z` git tags the release
pipelines build from. See [Releasing](docs/releasing.md) for how a release is made
and how to roll one back.

## Unreleased

### Added
- Community tracks download on demand: pick one in any track selector and press
  *Download & Start Race*, *Download & Create* or *Download & Join*, or run
  `hovernet --download-community-tracks --all`. Downloads are verified against a
  SHA-256 in the manifest before they are installed.
- `SHA256SUMS` is published with every release.
- Debian packages are reproducible: building the same commit twice gives
  byte-identical packages (`SOURCE_DATE_EPOCH` is honoured).
- Third-party notices and a checked runtime-dependency inventory
  ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

### Fixed
- The `hovernet-raceserver` package now depends on `libstdc++6` and `libgcc-s1`
  (the server could fail to start on a minimal system without them).
- A missing track file on the download server is reported as "not available yet"
  instead of a connection problem.

## 0.3.1 - 2026-10-01

### Added
- A library of about 1,000 classic community tracks, played unmodified and
  listed below the official tracks in every selector, with a search box. Free-play
  tracks (no finish line) show elapsed time instead of laps.
- A lenient `HoverNetTrackValidator --community` level, a manifest generator and
  pack/install scripts.
- The race server accepts community tracks listed in `CommunityTracks.tsv`.
- Linux `-DHOVERNET_WARNINGS=ON` / `-DHOVERNET_SANITIZE=ON` build options and a CI
  job that runs the whole test suite under AddressSanitizer and
  UndefinedBehaviorSanitizer.

### Changed
- Remembered race choices are stored by track name instead of list position
  (older files are still read).
- A level's negative start angle is normalised when it loads.

### Fixed
- Out-of-bounds trig-table reads, a `delete`/`delete[]` mismatch in MazeCompiler,
  32-bit overflows in the wall renderer and in collision and map maths, a divide by
  zero on zero-height walls, bad room links, and leaks on the client's quit path.
- Several community-track loading faults: missing classic object types, a vertex
  clamp that misaligned section data, and an unbounded audible-room count.

## 0.3.0 - 2026-10-01
Tag only; its release run failed while packaging the Windows x64 build (the
GitHub workflow did not stage the new track manifest). Superseded by 0.3.1.

## 0.2.2
Local tag only (never pushed): the GitLab Windows runners locate CMake when it is
not on `PATH`.

## 0.2.1 - 2026-10-01
- Content and tools (roadmap phase 4): seven bundled tracks validated, the
  command-line track compiler and validator, a track-authoring guide, HoverCad
  declared legacy.

## 0.2.0 - 2026-09-29
- Complete 2.0 user experience (roadmap phase 3): one consistent menu, settings,
  pause, lobby and post-race flow; remappable controls and controller support;
  display, audio and persisted settings; an improved HUD; onboarding; accessibility
  options; the shared SDL2/ImGui client on Windows and Linux.
- Installed-Windows acceptance tests for the client and its online lobby.
