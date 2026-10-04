# HoverNet 3.0 Roadmap

*Draft for review, 2 October 2026. Revised after HoverNet 2.0 shipped.*

HoverNet 3.0 replaces the 1990s-era technical constraints that 2.0 deliberately left
alone, with a deterministic, server-authoritative racing platform, while keeping what
players love: fast handling, compact tracks, weapons, and the visual identity. It also
turns the community track library and the tooling built in 2.0 into a proper creator
ecosystem.

3.0 starts only after 2.0 has run in production without incident and its release
criteria still hold. Durations below are relative to kickoff and are estimates, not
commitments: about 16 to 22 months for a small team. The 2.0 foundations (a single
cross-platform client, a hardened server, release engineering, and a lot of automated
checking) remove several stages of work the first draft of this roadmap assumed.

## Where 2.0 leaves us

### What 2.0 delivered, and 3.0 builds on

| Area | State at the end of 2.0 |
| --- | --- |
| Client | One shared SDL2/ImGui client for Windows x64 and Linux: menus, settings, remapping, controllers, display and audio options, accessibility options, onboarding. The legacy Win32/MFC client still ships as a compatibility fallback. |
| Network | Versioned protocol 2.0 with explicit field encoding (no native struct layouts), negotiation, rate limits, input validation, fuzzing, lifecycle tests, and cross-platform races verified Windows to Linux in both directions. The server is a relay; clients run physics locally. |
| Content | Seven validated bundled tracks, about 1,000 community tracks played unmodified, a command-line compiler and validator, a manifest with sizes and SHA-256 hashes, and a verified on-demand downloader. |
| Quality gates | Sanitizer builds (ASan, UBSan), a headless screenshot-test pattern for UI, Windows acceptance in CI, a compile-time guard that stops platform-dependent types in file formats, an x64 audit, and a runtime-dependency check. |
| Release engineering | Reproducible packages, `SHA256SUMS`, pre-release handling, changelog, third-party notices, and written upgrade and rollback procedures. |

### What 2.0 deliberately left, and 3.0 must address

- **Simulation is client-side and relayed.** Nothing is authoritative; the server
  cannot verify a result. The server-side simulation scaffolding in the repository is
  unfinished and unused.
- **Determinism is unproven.** The physics is fixed-point integer maths, which helps,
  but 2.0's sanitizer work found real signed-overflow and undefined behaviour that only
  worked by accident, and one file (the software rasterizer) is still exempt from the
  overflow check. Whether two platforms stay bit-identical over a long race has never
  been measured.
- **Rendering and gameplay are entangled,** and the renderer is a 1990s software
  rasterizer with 4096-scaled coordinates.
- **File formats are unversioned.** Tracks and resources are serialized archives with no
  version number, plus a plug-in model (`ObjFac1`) built on dynamically loaded C++ classes.
- **No replays, spectating, accounts or ratings,** and HoverCad is a legacy tool.
- **Open operational items from 2.0:** the 24-hour server soak (started 2 October 2026,
  still running), the production-server migration rehearsal, Windows reproducibility and
  dependency checks, and a line-by-line review of the MSVC narrowing warnings.
- **The Win32 client is retained,** which means two clients exist until a retirement
  decision is made with evidence.

## Product goals

HoverNet 3.0 should deliver:

- deterministic, server-authoritative online racing;
- a modern renderer and UI on the shared client, with no legacy client at runtime;
- first-class replays, spectating, and time trials;
- a supported visual track editor and a versioned content pipeline, with every classic
  track converted or explained;
- safe, minimal social features at launch (names, private lobbies, mute and report),
  with ratings and tournaments following once the foundations are proven;
- sustainable automated testing, observability, and live operations;
- a first-class browser/WebAssembly client that races on the same servers as native clients and maintains continuous feature and user-experience parity with the shared SDL2 client.

It should not become a generic game engine. Every investment must serve racing, content
creation, multiplayer integrity, or maintainability.

## Principles carried over from 2.0

These are what made 2.0 work; keep them.

1. **Measure before claiming.** Every "it works" has a test or a captured result. Exit
   gates below are numbers and commands, not intentions.
2. **Sanitizers and fuzzing are part of the definition of done,** and exemptions (like
   the rasterizer's) are tracked and removed, not forgotten.
3. **Verify UI changes visually.** Headless screenshot tests catch what reasoning misses.
4. **Real machines in the loop.** Cross-platform behaviour is tested on real Windows and
   Linux, and that rehearsal is automated wherever possible.
5. **Release hygiene is not optional:** reproducible builds, checksums, a changelog, and
   rehearsed rollback for every release.
6. **Honest scope.** Unproven foundations are never hidden behind features.
7. **Browser parity is mandatory.** The web client is another target of the shared client,
   not a reduced edition. Every player-facing feature added to the SDL2 client must ship
   in the browser in the same release unless an explicit, documented browser-platform
   limitation makes it impossible. Conversely, browser-only gameplay features are avoided.
   A release with unexplained SDL2/web feature drift fails its release gate.
8. **SDL2 is the UI reference.** The web UI must look and behave as close to the shared
   SDL2/ImGui client as practical: same information architecture, screens, controls,
   terminology, ordering, HUD, menus, dialogs, states and interaction flows. Browser-native
   differences are limited to things the platform requires, such as permission prompts,
   fullscreen behaviour, downloads, storage and connection errors.

## Stage 0: Prove the foundations

**Months 0 to 3**

Run the experiments that decide the architecture before building on it.

- **Cross-platform determinism experiment (first task).** Record the inputs of full races
  (all seven bundled tracks plus a sample of community tracks), hash the whole game state
  every frame, and replay the same inputs on Linux, Windows x64, and Win32. Report the
  first frame where any hash differs. Audit the likely causes: platform maths-library
  results in table construction, floating-point use in the simulation, uninitialised
  state, and signed overflow.
- **Remove the rasterizer's sanitizer exemption** and make the simulation free of
  undefined behaviour under UBSan across the whole corpus of 1,009 community tracks.
- **Golden physics suite.** Capture reference behaviour from 2.0: acceleration, steering,
  jumps, collisions, weapons, pickups, and full races, as inputs plus expected hashes.
- **Netcode prototype** under artificial latency, loss, jitter and reordering:
  prediction, reconciliation, interpolation, and lag compensation for weapons.
- **Architecture decision (an ADR),** based on the data above: evolve the existing C++
  code behind a clean simulation boundary, or build a new simulation core. Decide how the
  renderer is handled separately.
- **Win32 inventory.** List every feature only the Win32 client has, and assign each to
  migrate, replace, or retire, so its retirement is a dated decision.
- **Format and protocol specifications** for the simulation state, replay, track, and
  network protocol 3.0 (versioned, explicit, portable).
- **Browser transport proof.** Prove browser -> secure WebSocket -> RaceServer using the
  existing protocol framing. The proof must negotiate protocol 2, enter the lobby, list,
  host and join races, while native clients continue using the same server.
- **WebAssembly compile and determinism proof.** Build the portable C++ simulation with
  Emscripten, inventory browser-incompatible dependencies, and add WASM to recorded-input
  state-hash comparisons. Do not reimplement physics or race rules in JavaScript.
- **UI parity baseline.** Capture the SDL2 client screen inventory and reference screenshots
  for every player-facing state. Create a parity matrix covering navigation, lobby, race
  setup, loading, HUD, pause/settings, results, chat, errors, controls, accessibility and
  content flows. This matrix becomes a maintained test/release artifact.
- **Agree the targets** that later gates use. Proposal to confirm: playable at 120 ms round
  trip with 2% loss, and stable at 250 ms with 5%; eight players per race.

**Exit gate:** a two-player prototype produces identical state hashes across all supported
platforms for the recorded races, or the divergences are fully explained with a fix plan;
the architecture decision is written down; the simulation is clean under UBSan; browser protocol/WASM feasibility is demonstrated; and the SDL2/web parity baseline exists.

## Stage 1: Shared deterministic simulation core

**Months 3 to 7**

- Separate gameplay state from rendering, audio, UI, input, and networking. The 2.0 library
  split (model, main character, video, resources) is the starting seam.
- Fixed-step simulation with explicit numeric and randomness rules, and no platform maths
  in the simulation path (integer or fixed-point tables generated once and committed).
- Port movement, collisions, checkpoints, laps, weapons, pickups, and race rules into the
  shared core, with versioned state snapshots and deterministic input streams.
- Golden tests against 2.0 behaviour; every difference is fixed or recorded as a design
  decision.
- Divergence reports, property tests, fuzz tests, and long-replay tests in CI on every
  supported platform.
- The same core runs in the SDL2 client, browser/WASM client, dedicated server, replay
  viewer, and headless test runner.
- Shared presentation/view-model code supplies UI state and commands to both SDL2 and web
  targets wherever practical, preventing business rules and screen behaviour from drifting.
  Platform adapters handle only genuinely platform-specific input, networking, storage,
  audio, rendering and lifecycle behaviour.

**Exit gate:** representative races, including every bundled track and a fixed sample of
community tracks, replay bit-identically on all supported platforms in CI, and remaining
differences from 2.0 are documented design decisions.

## Stage 2: Authoritative networking and dedicated server

**Months 6 to 11**

- A versioned protocol 3.0 independent of native memory layouts, building on 2.0's explicit
  encoding, with authenticated encryption and secure session establishment.
- The server is authoritative for starts, movement, collisions, weapons, pickups, laps,
  finishes, and results. Prediction, reconciliation, interpolation, reconnect, and
  spectator snapshots on the client.
- Bandwidth budgets, packet prioritisation, compression, rate limiting, and abuse
  protection (extending 2.0's limits and fuzzing).
- Regional server pools and private dedicated servers; **server observability** (health,
  metrics, tracing, crash reports) and rolling deployment.
- A network-condition test harness (latency, loss, duplication, reordering, disconnects)
  in CI, plus **automated soak and load tests** replacing 2.0's manual 24-hour run.
- Protocol compatibility policy and a negotiated fallback path: 3.0 clients can still talk
  to a 2.0 relay for a defined period, and the reverse is refused with a clear message.
- **WebSocket transport** is a first-class RaceServer transport. Secure WebSocket frames
  terminate in a thin adapter feeding the same validated protocol/session layer as native
  clients. There is no browser-specific race protocol.
- Mixed SDL2/browser tests cover lobby, host/join, race start, gameplay, reconnect,
  malformed traffic, slow clients, protocol mismatch and race completion.

**Exit gate:** eight-player races stay stable and fair at the Stage 0 latency and loss
targets over a multi-day soak, no client can alter a result, and a game-state tampering
suite fails to change any official outcome. Mixed SDL2/browser races must pass the same gate.

## Stage 3: Modern client and presentation

**Months 7 to 13**

2.0 already converged on one shared client; this stage modernises it and removes the legacy
one.

- A maintainable renderer for widescreen and high-DPI, with a classic visual preset
  that matches 2.0 output (verified by automated frame captures) and optional modern
  lighting, particles and quality tiers. Replace the software rasterizer behind the
  simulation boundary, so rendering can change without touching gameplay.
- A unified UI toolkit for menus, HUD, lobby, replays, and editor, using the visual test
  pattern from 2.0, and full controller navigation and glyphs.
- Accessibility carried forward and extended: colour-safe indicators, reduced flashing,
  camera controls, configurable assists, scalable text.
- Performance budgets with automated frame-time and frame-capture regression tests.
- **Win32 client retirement,** per the Stage 0 inventory: migrate or retire each exclusive
  feature, run a deprecation period with a visible notice, and remove it from 3.0
  packages. The final 2.x release stays downloadable as the legacy build.

### Browser client and SDL2 parity

The browser client is a supported target of the shared HoverNet client. It must never
become a cut-down "web edition".

- Compile the shared deterministic C++ simulation and reusable resource/content code to
  WebAssembly with Emscripten. JavaScript/TypeScript is browser integration glue, not a
  second implementation of HoverNet.
- Add browser adapters for secure WebSocket networking, keyboard, Gamepad API, pointer,
  Web Audio, persistent settings, timers, focus/visibility, fullscreen, high-DPI,
  downloads/content cache and lifecycle/error handling.
- Bootstrap rendering may present the existing indexed/paletted framebuffer through
  Canvas/WebGL to get a playable client early. The eventual renderer remains shared in
  visual intent with SDL2 and must preserve the same race presentation.
- **SDL2 visual fidelity is the default.** For every screen, maintain reference SDL2
  screenshots and compare browser captures at agreed viewport sizes. Fonts, spacing,
  panels, button order, labels, icons, colours/palette, HUD placement, dialogs, loading
  states and results presentation should match within defined tolerances. Responsive
  adaptation may rearrange only where viewport constraints require it.
- **Interaction parity is equally important.** Keyboard/controller navigation order,
  default focus, escape/back behaviour, confirmations, validation, disabled states,
  tooltips/help, settings semantics and error flows must correspond between SDL2 and web.
- Maintain a machine-readable or reviewable **feature-parity matrix**. Each player-facing
  feature has SDL2 and Web status plus automated/manual parity tests. New features are not
  considered complete until both targets are complete.
- PRs that alter SDL2 UI or player-facing behaviour must update the web target in the same
  change or carry an explicitly approved temporary parity exception with an owner, reason
  and expiry release. CI reports outstanding exceptions. Stable releases require zero
  unexplained exceptions.
- Add automated browser screenshot tests corresponding to the SDL2 screenshot suite and
  behavioural tests that drive equivalent user journeys on both targets. CI flags visual
  and functional drift.
- Test the complete equivalent flow: launch, identity/onboarding, settings, lobby,
  host/join, track/options, content acquisition, loading, race HUD/gameplay, pause,
  chat/social controls, results, reconnect, replay/spectating and accessibility as those
  features become available.
- Native-only concepts such as filesystem pickers are represented with the closest browser
  equivalent without changing the surrounding HoverNet workflow. Required differences are
  documented in the parity matrix.
- Browser suspension/background throttling, refresh, lost focus and network interruption
  must fail/reconnect cleanly without leaving ghost race sessions.
- Establish budgets for WASM download/startup, track load, memory, frame time, input
  latency and network processing. Test cold and warm caches.
- CI builds and serves the web client, runs supported browser engines, executes golden
  deterministic replays, runs mixed native/web races and captures parity screenshots.
- Web content is data only; the browser never downloads and executes community native
  plug-ins. Apply CSP, origin controls, bounded parsers, dependency auditing and normal
  web supply-chain protections.

**Parity rule:** if a player can do it in the current supported SDL2 client, they can do
it in the supported web client, with substantially the same UI and flow. If a feature
cannot meet that rule because of a genuine browser restriction, it must be documented,
tested and explicitly accepted rather than silently omitted.

**Exit gate:** every supported flow works on every supported platform with no legacy UI
dependency, the classic preset matches 2.0 captures within an agreed tolerance, and
frame-time budgets hold. A clean browser profile can open HoverNet, use the SDL2-equivalent
UI, join the same server, race native players and complete the same post-race flow. The
feature-parity matrix has no unexplained gaps and the SDL2/web visual regression suite
passes.

## Stage 4: Content pipeline and the HoverCad successor

**Months 4 to 14, in parallel with the networking stages**

Content is HoverNet's strength: there are already about a thousand community tracks. This
stage makes them durable and makes new ones easy.

- **Versioned, portable formats** for tracks, materials, meshes, sounds, spawn points,
  checkpoints, and gameplay objects, replacing the unversioned archive, with a documented
  migration path and a compile-time guard against platform-dependent fields (carried over
  from 2.0).
- **Deterministic importers** for classic `.trk` files. The 1,009-track library is the
  conversion test corpus: every track converts, or is listed with the exact reason it
  cannot.
- A supported **visual editor**: validation as you work, undo and redo, preview, object
  placement, checkpoint tooling, and one-click playtesting. Free-play tracks (no finish
  line) become a first-class mode, not an accident of missing objects.
- Automated checks for topology, collision, checkpoint order, spawn safety, missing assets,
  and performance, building on 2.0's validator and its strict and community levels.
- **Browser-safe content parity.** Native and web clients consume the same logical tracks,
  meshes, gameplay objects and metadata and verify the same hashes/validation rules.
  Legacy native-code plug-ins are converted to safe data-driven equivalents or explicitly
  handled by the compatibility plan; they are never silently missing from web races.
- **Content distribution on 2.0's foundations:** packages with metadata, licence, author,
  dependencies, hashes, and format version; the verified downloader and manifest become a
  signed catalogue with review, reporting, revocation, and server allowlists. Record
  authorship and licence for every new track (the 2.0 library has none).
- Authoring documentation and example content under clear licences.

**Exit gate:** a creator can build, validate, package, share, and play a new track without
touching the source tree; all bundled tracks and at least 95% of the community library
convert, with every failure explained.

## Stage 5: Replays, spectating, and time trials

**Months 10 to 16**

Depends on Stage 1's determinism.

- Compact deterministic replays (versioned inputs, events, periodic snapshots) that
  re-verify against the server's recorded result.
- Seeking, camera controls, timeline markers, and export-friendly playback.
- Delayed live spectating (protects competitive integrity) using the server's spectator
  snapshots.
- Ghosts, time trials, personal bests, track leaderboards, and shareable replay identifiers.

**Exit gate:** a race can be saved, shared, replayed, and independently verified from the
stored data, on every supported platform.

## Stage 6: Online services, in two steps

**Months 11 to 18 (6a), after the foundations are proven (6b)**

2.0 intentionally has no accounts, rankings or moderation, and these carry the heaviest
operational and legal load. They are split so the launch is not held up by them.

**6a, in 3.0:** safe basics.
- Display names, optional sign-in, privacy controls, and a guest path.
- Parties, invitations, private and public lobbies, and region selection.
- Mute, block, and report with a minimal moderator workflow and an audit log.
- Retention and deletion policy for personal data, chat, and telemetry.

**6b, 3.1 unless staffing and a business case justify pulling it in:**
skill-aware matchmaking, seasonal and permanent ratings with auditable result ingestion,
sanctions and appeals, tournaments (brackets, heats, seeded grids, race administration),
and administrative tooling with least privilege.

**Exit gate (6a):** a player can find and join an appropriate race, and every moderation
action is authenticated and auditable. **6b** has its own review.

## Stage 7: Migration, compatibility, and ecosystem beta

**Months 16 to 20**

- Conversion tools and reports for every bundled and community track (Stage 4).
- Preserve 2.x as an installable legacy release; keep its servers visibly separate.
- Dedicated-server images, configuration schemas, and upgrade guidance, using the packaging
  and rollback process already proven in 2.0.
- Public beta cycles for creators, server operators, competitive players, and accessibility,
  run with 2.0's beta plan (entry and exit criteria, severity triage) as the template.
- Test rollback, regional failover, protocol-version retirement, and account migration.
- Freeze formats and public interfaces before release candidates.
- Run a browser deployment/rollback rehearsal covering HTTPS, WebSocket routing, caching,
  old open tabs, bundle versioning and rollback without disrupting native clients.
- Beta triage treats SDL2/web visual or functional parity regressions as product defects,
  not optional browser polish.

**Exit gate:** bundled and community content is migrated, creator and operator workflows are
documented, and beta participants operate without developer help.

## Stage 8: Launch readiness

**Months 19 to 22**

- Large-scale load, soak, security, recovery, and abuse-response exercises (automated, and
  repeated for every release candidate).
- An independent security review of the native client, web client/WebSocket endpoint, services, updater, content ingestion, and administrative tools.
- Restore, failover, key-rotation, rollback, and emergency protocol-shutdown drills.
- Signed packages and a signed auto-updater (2.0 has checksums but no signatures), crash
  reporting, support documentation, and release notes.
- Service-level objectives and on-call procedures; staged cohort release before general
  availability.

**Exit gate:** launch, rollback, recovery, and incident response have all been exercised
successfully, and the soak and load results are published.

## HoverNet 3.0 release criteria

Ship 3.0 only when:

- the simulation is bit-identical on every supported platform, proven by CI replays and by
  a clean UBSan run with no exemptions;
- the server is authoritative for every result-affecting action, backed by a tampering suite;
- the eight-player target passes latency, loss, multi-day soak, and adversarial tests;
- the complete game runs without the legacy client or any legacy UI dependency;
- replays reproduce official results and remain readable across supported patch versions;
- every bundled track is converted and validated, and at least 95% of the community library
  converts, with each failure explained;
- creators can produce and distribute content through supported tools, with authorship and
  licence recorded;
- the 6a online services are production-ready (6b is not required for launch);
- dedicated-server operators have stable, signed packages, documentation, and observability;
- accessibility, performance, security, recovery, and rollback gates have all passed;
- the browser client passes mixed-client races, WASM determinism tests and web security
  review;
- the SDL2/web feature-parity matrix contains no unexplained gaps and automated visual and
  behavioural parity tests pass.

## Scope controls

Outside 3.0 unless additional staffing and a separate business case justify them:

- an open-ended general-purpose engine;
- user-supplied native-code plug-ins (the 2.0 `ObjFac1` model is closed and retired, not extended);
- peer-to-peer authoritative multiplayer (traffic keeps routing through servers);
- pay-to-win progression or gameplay-affecting purchases;
- virtual reality, and mobile or console releases before desktop operations are stable;
- procedurally generated tracks at the expense of the supported editor;
- unlimited backward compatibility inside the new simulation;
- ratings, matchmaking, tournaments, and sanctions (Stage 6b), unless pulled in deliberately.

## Programme checkpoints and risks

At the end of Stages 0, 2, 4, and 7, hold a formal continue, revise, or stop review. The
critical path is determinism, then authoritative networking. Identity, visual upgrades,
content distribution, and competitive features must not hide a failure to prove those
foundations.

| Risk | Mitigation |
| --- | --- |
| The 1990s simulation cannot be made deterministic cheaply | Stage 0 measures it first; the ADR allows a new simulation core |
| Prediction feels worse than the relay model | prototype under real network conditions in Stage 0; keep the relay path until Stage 2 passes |
| Scope creep from social and competitive features | Stage 6 split; 6b gated by a review |
| Community content cannot all be converted | a defined 95% target with explained failures, and a legacy mode for the rest |
| Unknown licensing of community content | record authorship and licence for all new content; legal review before wider distribution of the old library |
| Browser/WASM simulation diverges | WASM is in Stage 0 determinism and every golden replay gate; never maintain separate browser physics |
| SDL2 and web UI drift | SDL2 reference screenshots, shared presentation state, parity matrix, paired journey tests and zero unexplained gaps at release |
| Web becomes a reduced client | definition of done requires both targets; temporary exceptions are explicit, owned and expire |
| WebSocket becomes a second protocol | thin transport adapter feeds the same validated server protocol/session layer |
| Small-team capacity | stage browser work from protocol/WASM proof to full UI; each stage gate must pass before dependent work expands |

## Decisions to make before kickoff

- Confirm the latency and loss targets and the eight-player limit.
- Decide who owns operations (on-call, moderation) before Stage 6a starts.
- Choose the licence and authorship policy for community content.
- Agree the Win32 deprecation timeline once the Stage 0 inventory exists.
- Confirm SDL2 as the canonical UI/UX reference for browser parity and define screenshot
  tolerances/viewports for the visual regression suite.
- Confirm the browser support matrix and the production secure-WebSocket deployment model.
