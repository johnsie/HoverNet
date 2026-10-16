# Changelog

All notable changes to HoverNet. Versions are the `vX.Y.Z` git tags the release
pipelines build from. See [Releasing](docs/releasing.md) for how a release is made
and how to roll one back.

## 2.0.15 - 2026-10-16

### Changed
- Web client: tablets (iPad and similar) keep their desktop menu layouts and are no longer asked
  to rotate in portrait; only phones are.
- Web client on phones: Settings and Controls have a slim header, and Settings keeps Save and
  Cancel pinned under the scrolling options.
- Web client on phones: the wide race frame is nudged so the whole hovercraft stays in view.

### Fixed
- Web client: the on-screen race controls no longer float over the screens opened from the pause
  menu (New Local Race, Settings, Controls) or the results screen.
- README describes the browser, phone, tablet and VR play.

## 2.0.14 - 2026-10-16

### Added
- Web client: VR mode for the Meta Quest browser. An ENTER VR button (shown only in a headset
  browser) puts the game on a big virtual screen with real stereo 3D in races -- the game renders
  one view per eye -- and a controller to drive, fire, jump and switch between the chase and
  cockpit views; menus are pointed at and clicked with the controller. Desktop and phone
  browsers are unaffected.

### Fixed
- A hovercraft could stop dead against a wall or platform instead of bouncing (most often at
  high speed): the bounce is triggered by a contact ring slightly wider than the space the craft
  may move in, so it could rest just outside it. A craft whose moves keep being refused is now
  bounced back.

## 2.0.13 - 2026-10-16

### Fixed
- Web client: after a browser stall (hidden tab, long pause, slow phone frame) the race no longer
  replays the whole gap at once, which threw hovercraft into walls and platforms and let
  missiles skip past targets.

## 2.0.12 - 2026-10-15

### Changed
- Web client on phones: dragging the steering knob up jumps (the JUMP button still works).

## 2.0.11 - 2026-10-15

### Changed
- Web client on phones: the two steering arrows are replaced by a steering slider -- drag the
  knob left or right to steer. The other race controls are unchanged.

### Fixed
- Web client on phones: the pause menu's top buttons (Resume, New Local Race, Online
  Multiplayer) could not be tapped because the race controls floated over them; the race
  controls are now hidden while paused.

## 2.0.10 - 2026-10-15

### Added
- Web client (desktop): a "Game Size" setting sizes the gameplay area inside the browser
  window (Fit, or 800x600 up to 1920x1080) without resizing the browser. Remembered per browser.

### Changed
- Web client (phones): the race frame matches the screen's shape (960 wide, about 2.2:1 in
  landscape) instead of a letterboxed 4:3 frame.

### Fixed
- Wide framebuffers were stretched horizontally because the 3D projection assumed a 4:3 screen;
  pixels are now square at every resolution (4:3 output is unchanged).
- Web client on phones: less flicker -- frames are paced to the display and the canvas is no
  longer resized when iOS toolbars slide in and out.

## 2.0.9 - 2026-10-15

### Changed
- Web client on phones: holding the phone in portrait now shows a full-screen "Rotate your
  phone" prompt on every screen (including loading and the menus), with a
  "Continue in portrait" option for the rest of the session.

## 2.0.8 - 2026-10-15

### Changed
- Phone layouts for the web client: the main menu (portrait stack and compact landscape),
  the Driver Briefing (auto-sized cards, touch-control page on touch devices), the Internet
  Meeting Room (tabs for games/race, chat and users) and Local Race Setup (slim header,
  reachable buttons).

### Fixed
- The main menu footer showed the version as "vv2.0.7".

## 2.0.7 - 2026-10-15

### Fixed
- Web client on phones no longer gets stuck on the loading screen: startup used `eval`, which
  the site's Content-Security-Policy blocks, so the game stopped before drawing its first frame.
- Windows x64 installer packaging in GitHub Actions now stages every client asset
  (tutorial image, fonts and licences) and the package validator checks for them.

## 2.0.6 - 2026-10-15

### Fixed
- Web client on phones: real startup errors are no longer overwritten by "Still loading…",
  stray page errors no longer break the loader, the last startup log lines are shown while
  loading, and a browser that draws no frame within 25 seconds falls back to the software
  renderer automatically.

## 2.0.5 - 2026-10-15

### Fixed
- Web client: sliders and clicks now follow the pointer at any window size (SDL no longer
  rescales mouse events into 1024x768 while a frame sleeps).

### Changed
- The online lobby screen is titled "Internet Meeting Room".

## 2.0.4 - 2026-10-15

### Fixed
- Web client: menus, sliders and text boxes are no longer laggy. The browser build now
  renders with WebGL instead of the CPU software renderer (falling back to software if
  WebGL is unavailable), and frames are paced to a 16 ms budget.

## 2.0.3 - 2026-10-15

### Fixed
- Web client: the desktop GUI no longer appears stretched, and phones no longer show a
  blank screen after the download (the loader stays up until the first frame is drawn,
  and startup errors are shown on the page).
- The sanitizer job runs only from a nightly schedule.

## 2.0.2 - 2026-10-15

### Changed
- Every CI deploy is automatic: version tags deploy both the web client and the RaceServer.
- The web build is queued ahead of native builds, and the runner allows three concurrent jobs.

## 2.0.1 - 2026-10-15

### Fixed
- Web client on phones: menu text boxes (username, chat, server) can now be typed
  into on iOS; the race controls appear only during a race; the page fits above the
  on-screen keyboard and inside the notch/safe area; phones default to a tighter UI scale.

### Changed
- CI builds run in parallel, the web build is incremental, and docs-only changes skip it.

## 2.0.0 - 2026-10-02

HoverNet 2.0, the first stable release of the 2.0 line (it follows the public beta).
See the [release notes](docs/release-notes.md).

### Changed
- The redesigned lobby: no header bar; the Users list now includes you and everyone
  else in the lobby; the game details show the map, track, laps, weapons, players,
  style and status; the waiting room shows all eight players without scrolling; the
  chat keeps 200 lines, scrolls, and stays on the newest message; only the main
  action is a red button.
- A build made from files that differ from its commit now shows `+local` after the
  version (for example `v2.0.0+local`).
- Stable tags publish with `docs/release-notes.md`, which invites feedback through
  GitHub issues.

### Known
- The 24-hour race-server soak test is still running for this build.

## 2.0.0-beta.2 - 2026-10-01

Replaces beta.1, whose `.deb` file names GitHub rewrote (`~` became `.`), so
`SHA256SUMS` could not verify them.

### Fixed
- Debian package file names use the plain tag version (`hovernet-game_2.0.0-beta.2_amd64.deb`)
  while the package's own version stays `2.0.0~beta.2`, so checksums verify and a
  beta still sorts before the final release.

## 2.0.0-beta.1 - 2026-10-01 (superseded by beta.2)

First public beta of HoverNet 2.0, published as a GitHub *pre-release*. Everything
in 0.4.x, now treated as the 2.0 feature set; see the
[beta notes](docs/beta-release-notes.md) and [test plan](docs/beta-test-plan.md).

### Changed
- Pre-release tags (`vX.Y.Z-beta.N`) are published as GitHub pre-releases, and the
  Debian packages use `X.Y.Z~beta.N`, so a beta always sorts before the final
  release on upgrade.
- Decision: the 32-bit Windows client stays in 2.0 as the compatibility fallback.

## 0.4.1 - 2026-10-01

### Fixed
- Windows x64 readiness audit: an uninitialised loop condition in collision force
  maths, a missing virtual destructor on bitmap resources, type-punned pointer and
  enum access, bad `printf`/`sscanf` arguments, and a `memset` of a class object.
  Track data loads identically (all 1,009 community tracks and the 7 bundled ones).
- File archives now reject, at compile time, any field type whose size differs
  between Win32, Windows x64 and Linux, so shared file formats cannot silently
  diverge.

### Added
- Windows client acceptance in both CI pipelines: a local race on every bundled
  track and the community-track downloader (verified on a real Windows x64 runner).
- `scripts/test-cross-platform-race.sh`: Windows x64 <-> Linux races through a Linux
  race server, both directions (release rehearsal).
- [x64 audit](docs/x64-audit.md): method, MSVC `/W4` results (no pointer-truncation
  warnings) and what is not covered.

## 0.4.0 - 2026-10-01

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
