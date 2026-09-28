# HoverNet 3.0 Roadmap

HoverNet 3.0 begins only after HoverNet 2.0 has shipped and its release criteria remain healthy in production. Its purpose is to replace the remaining 1990s-era technical constraints with a modern, authoritative, extensible racing platform while preserving HoverRace's fast handling, compact tracks, weapons, and visual identity.

This is approximately an 18-to-24-month programme for a small team. Dates should be assigned only after 2.0 is complete; durations below are relative to the 3.0 kickoff.

## Product goals

HoverNet 3.0 should deliver:

- deterministic, server-authoritative online racing;
- a modern cross-platform client architecture;
- secure accounts, matchmaking, rankings, and moderation;
- first-class replays, spectating, tournaments, and community tracks;
- a supported visual track editor and modern content pipeline;
- compatibility with classic tracks through conversion or an isolated legacy mode;
- sustainable automated testing, observability, and live operations.

It should not become a generic game engine. Every technical investment must support racing, content creation, multiplayer integrity, or maintainability.

## Stage 0 — Discovery and technical prototypes

**Months 0–3**

- Measure 2.0 gameplay, network behaviour, player retention, popular modes, hardware, and failure rates.
- Record a physics reference suite from 2.0: acceleration, steering, jumping, collisions, weapons, pickups, and representative full races.
- Specify the 3.0 simulation, protocol, replay, asset, and track formats before production implementation.
- Prototype deterministic simulation across Windows and Linux.
- Prototype client prediction, server reconciliation, interpolation, and lag compensation under artificial loss, latency, and jitter.
- Evaluate the rendering and platform layer with a narrow criterion: preserve gameplay while reducing platform-specific code.
- Decide whether to evolve the existing C++ codebase or build a new client around a reusable simulation core.
- Produce migration plans for classic tracks, resources, settings, player identities, and hosted servers.

**Exit gate:** A two-player prototype produces matching simulation hashes across platforms and remains playable at the agreed network latency target.

## Stage 1 — Shared deterministic simulation core

**Months 3–7**

- Separate gameplay state from rendering, audio, UI, input, and networking.
- Implement fixed-step deterministic simulation with explicit numeric and randomness rules.
- Port hovercraft movement, collisions, checkpoints, lap counting, weapons, pickups, and race rules into the shared core.
- Add versioned state snapshots and deterministic input streams.
- Build golden tests comparing important 2.0 behaviours against 3.0.
- Add simulation hashes, divergence reports, property tests, fuzz tests, and long replay tests.
- Run the same core in the client, dedicated server, replay viewer, and headless test runner.

**Exit gate:** Representative races replay identically on supported platforms, and gameplay differences from 2.0 are either fixed or documented design decisions.

## Stage 2 — Authoritative networking and dedicated server

**Months 6–11**

- Design and implement a versioned 3.0 protocol independent of native C++ memory layouts.
- Use authenticated encryption and secure session establishment.
- Make the server authoritative for starts, movement, collisions, weapons, pickups, laps, finishes, and results.
- Implement prediction, reconciliation, interpolation, reconnect, and spectator snapshots.
- Add bandwidth budgets, packet prioritisation, compression, rate limiting, and denial-of-service protections.
- Support regional server pools and private dedicated servers.
- Build network simulation into tests for latency, loss, duplication, reordering, and disconnects.
- Provide server health, metrics, tracing, crash reporting, configuration validation, and rolling deployment support.

**Exit gate:** Eight-player races remain stable and fair at the defined latency and packet-loss targets, with no client able to authoritatively alter race results.

## Stage 3 — Modern client and presentation

**Months 7–13**

- Replace remaining Win32/MFC-specific runtime paths with a shared cross-platform application layer.
- Introduce a maintainable renderer with widescreen, high-DPI, windowed, borderless, and fullscreen support.
- Preserve a classic visual preset while adding modern lighting, particles, effects, and scalable quality options.
- Build a unified UI system for menus, HUD, lobby, settings, results, replays, and editor workflows.
- Add robust controller support, rebinding, input glyphs, vibration, and multiple controller layouts.
- Add accessibility options including scalable UI, colour-safe indicators, reduced flashing, camera controls, and configurable assists.
- Establish performance budgets and automated frame captures for visual regression testing.

**Exit gate:** The client completes all core game flows on every supported platform without legacy platform-specific UI dependencies.

## Stage 4 — Identity, matchmaking, rankings, and moderation

**Months 10–15**

- Introduce optional accounts while retaining a low-friction guest path where practical.
- Implement secure identity, display names, sessions, privacy controls, and account recovery.
- Add parties, invitations, public and private lobbies, matchmaking regions, and skill-aware queues.
- Add seasonal and permanent leaderboards with auditable race-result ingestion.
- Implement reports, blocks, mutes, moderator actions, sanctions, and appeal records.
- Define retention and deletion policies for personal data, chat, telemetry, and reports.
- Add administrative tools with least-privilege access and complete audit logs.

**Exit gate:** Players can reliably find appropriate races, and every moderation or ranking change is authenticated and auditable.

## Stage 5 — Replays, spectating, competition, and social play

**Months 12–17**

- Store compact deterministic replays based on versioned inputs, events, and periodic snapshots.
- Add replay seeking, camera controls, timeline markers, and export-friendly playback.
- Add delayed live spectating to protect competitive integrity.
- Support tournament brackets, qualifying, heats, seeded grids, custom rules, and race administration.
- Add ghosts, time trials, personal bests, and track leaderboards.
- Provide shareable race and replay identifiers.

**Exit gate:** A completed competitive event can be administered, spectated, replayed, and independently verified from stored race data.

## Stage 6 — Content pipeline and HoverCad successor

**Months 11–18**

- Define versioned, portable formats for tracks, materials, meshes, sounds, spawn points, checkpoints, and gameplay objects.
- Build deterministic importers for classic `.trk` files and supported legacy resources.
- Create a supported visual editor with validation, undo/redo, preview, object placement, checkpoint tooling, and one-click playtesting.
- Add automated checks for topology, collision, checkpoint order, spawn safety, missing assets, and performance budgets.
- Package community tracks with metadata, licence, dependencies, hashes, and format version.
- Design a curated distribution path with review, reporting, revocation, and server allowlists.
- Publish authoring documentation and example content under clear licences.

**Exit gate:** A creator can build, validate, package, share, and play a new track without modifying the source tree or using unsupported tools.

## Stage 7 — Migration, compatibility, and ecosystem beta

**Months 17–21**

- Ship conversion tools and reports for all bundled 2.0 tracks.
- Preserve 2.0 as an installable legacy release and keep its servers visibly separate from 3.0.
- Provide clear handling for content that cannot be converted exactly.
- Publish dedicated-server images, configuration schemas, upgrade guidance, and compatibility policy.
- Run creator, server-operator, competitive-player, and accessibility betas.
- Test account migration, rollback, server-region failover, and protocol-version retirement.
- Freeze formats and public interfaces before release candidate builds.

**Exit gate:** Bundled content is migrated, community workflows are documented, and beta participants can operate without developer intervention.

## Stage 8 — Launch readiness

**Months 21–24**

- Run large-scale load, soak, security, recovery, and abuse-response exercises.
- Complete an independent security review of clients, services, updater, account flows, content ingestion, and administrative tools.
- Exercise database restoration, regional failover, compromised-key rotation, rollback, and emergency protocol shutdown.
- Complete platform packaging, signing, automatic updates, crash reporting, support documentation, and release notes.
- Establish service-level objectives and on-call procedures for launch.
- Release to staged cohorts before general availability.

**Exit gate:** Production launch, rollback, recovery, and incident-response procedures have all been exercised successfully.

## HoverNet 3.0 release criteria

Ship 3.0 only when:

- the shared simulation is deterministic on every supported platform;
- the server is authoritative for every result-affecting action;
- the eight-player network target passes latency, loss, soak, and adversarial tests;
- the complete game is playable without MFC or other legacy UI dependencies;
- accounts, matchmaking, rankings, moderation, and privacy workflows are production-ready;
- replays reproduce official race results and remain readable across supported patch versions;
- every bundled track has been converted and validated;
- creators can produce and distribute content through supported tools;
- dedicated-server operators have stable packages, documentation, and observability;
- accessibility, performance, security, recovery, and rollback gates have passed.

## Scope controls

The following should remain outside 3.0 unless additional staffing and a separate business case justify them:

- an open-ended general-purpose engine;
- user-supplied native-code plugins;
- peer-to-peer authoritative multiplayer;
- pay-to-win progression or gameplay-affecting purchases;
- virtual-reality support;
- mobile and console releases before the desktop architecture and operations are stable;
- procedurally generated tracks at the expense of the supported editor;
- unlimited backward compatibility inside the new simulation.

## Programme checkpoints

At the end of Stages 0, 2, 4, and 7, conduct a formal continue, revise, or stop review. The critical path is deterministic simulation followed by authoritative networking. Identity services, visual upgrades, content distribution, and competitive features must not conceal failure to prove those foundations.
