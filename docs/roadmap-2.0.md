# HoverNet 2.0 Roadmap

HoverNet 2.0 should be the first polished, cross-platform release rather than a full engine rewrite. The existing foundation includes Windows and Linux clients, working multiplayer, automated packaging, four bundled tracks, the Manta hovercraft, and a broad Linux smoke-test suite.

Assuming one or two regular contributors, this is a realistic six-to-eight-month roadmap beginning in October 2026.

## Phase 0 — Define the 2.0 contract

**October 2026 · 1–2 weeks**

- Write a concise product specification covering local play, multiplayer, supported platforms, and compatibility.
- Inventory Windows and Linux differences.
- Document the existing network protocol and identify which RaceServer code paths are active versus unfinished scaffolding.
- Decide the compatibility policy:
  - 2.0 clients must reject incompatible servers cleanly.
  - Existing tracks and resource files remain supported.
  - Server-authoritative physics is deferred unless anti-cheat becomes a requirement.
- Convert the roadmap into tracked issues with owners and acceptance criteria.

**Exit gate:** The intended 2.0 player experience and protocol compatibility rules are unambiguous.

## Phase 1 — Stabilize the existing game

**October–November 2026 · 4–6 weeks**

- Finish and commit the current Windows rendering and input cleanup.
- Remove disabled diagnostic logging and machine-specific paths.
- Run systematic playtesting for:
  - local races;
  - Windows to Windows;
  - Linux to Linux;
  - Windows to Linux;
  - hosting, joining, leaving, reconnecting, and race completion.
- Fix crashes, hangs, focus problems, and resource-loading failures.
- Add clean handling for missing tracks, missing object libraries, server outages, and malformed settings.
- Establish a repeatable frame-time and memory baseline.

**Exit gate:** Two-hour local and online sessions complete on both platforms without crashes, hangs, or growing memory use.

## Phase 2 — Harden multiplayer and RaceServer

**November–December 2026 · 5–7 weeks**

- Formalize message framing, size limits, endianness, and protocol-version negotiation.
- Replace silent or indefinite network waits with explicit timeouts and errors.
- Validate player names, race settings, chat messages, and state packets.
- Add connection, chat, and race-creation rate limits.
- Complete configuration loading rather than shipping placeholder XML handling.
- Add graceful shutdown, structured logs, health reporting, and useful server metrics.
- Add automated scenarios for:
  - multiple simultaneous races;
  - disconnect during lobby, countdown, or race;
  - host departure;
  - duplicate names;
  - partial and coalesced TCP messages;
  - malformed and oversized packets;
  - server restart and capacity limits.
- Perform protocol fuzzing and dependency/security scanning.

**Exit gate:** A 24-hour soak test with repeated joins, races, disconnects, and malformed traffic produces no crash, deadlock, or orphaned race.

## Phase 3 — Complete the 2.0 user experience

**January–February 2027 · 6–8 weeks**

- Consolidate the main menu, settings, pause menu, lobby, local setup, and post-race flow into one consistent ImGui design.
- Add remappable controls and controller support.
- Add display mode, resolution, scaling, fullscreen, audio, and volume settings.
- Persist all settings in a documented per-user location.
- Improve the HUD:
  - position and lap progress;
  - race timer and results;
  - weapon and ammunition state;
  - connection quality;
  - clearer damage and pickup feedback.
- Add an onboarding and control reference reachable in-game.
- Improve keyboard navigation, text scale, colour contrast, and window resizing.
- Ensure Windows and Linux expose the same core menus and options.

**Exit gate:** A new player can install, configure, start a local race, and join an online race without external documentation.

## Phase 4 — Content and tools

**February–March 2027 · 4–6 weeks**

- Validate every bundled track for starts, checkpoints, finish detection, collision, and multiplayer consistency.
- Add at least two release-quality tracks.
- Balance hovercraft, missiles, mines, pickups, and weapon-enabled versus weapon-disabled races.
- Finish the track compiler's highest-impact TODOs.
- Create a command-line track validator suitable for CI.
- Publish a minimal track-authoring guide and example project.
- Decide whether HoverCad is supported, experimental, or explicitly legacy.

**Exit gate:** All bundled tracks pass automated validation and a complete multiplayer playthrough.

## Phase 5 — Release engineering and beta

**March–April 2027 · 4–6 weeks**

- Remove generated binaries and build artefacts from source control; enforce this through `.gitignore` and CI.
- Add compiler warnings and sanitizers on Linux.
- Add Windows smoke tests beyond compilation.
- Produce reproducible versioned packages from a clean checkout.
- Add checksums, dependency and licence inventory, changelog, migration notes, and rollback instructions.
- Run:
  - an internal alpha;
  - a two-week public beta;
  - a release-candidate freeze;
  - a production-server migration rehearsal.
- Verify clean installation, upgrade, uninstall, and retained server configuration.

**Exit gate:** The release candidate passes CI, cross-platform playtesting, packaging checks, and the production deployment rehearsal.

## HoverNet 2.0 release criteria

Ship 2.0 only when:

- Windows and Linux can host and join each other reliably.
- All supported race flows have automated coverage or a documented manual test.
- There are no known crash, hang, data-loss, or remotely exploitable defects.
- The server survives the 24-hour soak test.
- Clean installations contain everything needed to play.
- Settings and error messages do not require source-tree knowledge.
- Every bundled track completes correctly in local and online play.
- Release and rollback procedures have both been exercised.

## Explicitly deferred to 2.1 or later

To keep 2.0 achievable, defer:

- fully server-authoritative physics;
- accounts, rankings, and persistent progression;
- matchmaking;
- replay and spectator infrastructure;
- a complete rendering-engine rewrite;
- mobile and console ports;
- a completely rewritten visual track editor.

The most important architectural decision is avoiding a half-finished authoritative simulation in 2.0. The repository contains unfinished server simulation, lap detection, UDP, and dispatcher code, while the working multiplayer path is primarily relay-based. Hardening and documenting the proven path is substantially safer; an authoritative protocol can then be designed deliberately for a later release.
