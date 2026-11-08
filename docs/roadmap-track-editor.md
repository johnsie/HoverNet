# HoverNet track editor roadmap (the HoverCad replacement)

*Draft, 8 November 2026. Decisions made by the owner's delegation are recorded as ADRs 0003 and 0004. The Progress table says what is built; the "Where we start" section says what existed before.*

HoverCad is the 1990s Windows-only (MFC) track designer that came with HoverRace. HoverNet needs a supported replacement that:

1. runs **natively on Windows and on Linux**,
2. runs **in the browser** at hovernet.outiva.com, and
3. **always stays at parity with the game client**: whatever the game can load, show and play, the editor can create, show and test, on every platform, in the same release,

and it is for **making new tracks, not only opening old ones**, and for **sharing them**: a finished track is published from the editor to one **Track Market** that every player can browse and download from, in the desktop and browser clients alike ([ADR 0004](adr/0004-track-marketplace.md)).

This is the "visual editor" and "HoverCad successor" of [Stage 4 of the 3.0 roadmap](roadmap-3.0.md#stage-4-content-pipeline-and-the-hovercad-successor); this document is the detailed plan for it. The working name is **HoverNet Track Editor**.

## Progress

*Updated 8 November 2026. "Done" means built and run on Linux only, unless it says more.*

| Item | State |
| --- | --- |
| ADR for decisions 1 to 3 | [ADR 0003](adr/0003-track-editor.md) **accepted** by delegation (8 November 2026) |
| Element and texture catalogue and its parity test (rule 1) | Done on Linux: `HoverNetTrackElementCatalogue`, 46 entries (29 surfaces, 7 placed elements). It found that id 12 (the ball) exists only in the legacy Windows factory, not the shared client's; it is left out. Shown to fail when an entry is removed. Not yet run on Windows or WebAssembly |
| Compiler ignores an `[Editor]` section | Verified on Linux (start and end of file, byte-identical output). Windows and WebAssembly not run |
| Compiler in the browser, same bytes as native (rule 2) | Done on Linux and under Node: the compiler builds as WebAssembly (16 MB stack) and compiles all three bundled sources to the committed `.trk` bytes. Found and fixed a real cross-platform difference (two `qsort` calls, unstable on equal levels; now a stable sort, committed tracks unchanged). `scripts/test-compiler-parity.sh`, ctest `HoverNetCompilerParity` (native half; the WebAssembly half is run by hand and **not yet in CI**). Windows not run. Validator and linter in WebAssembly not started |
| HoverCad feature inventory, `.TR` format spec, `.trk` to source spike | Not started |
| Track source core: lossless text layer, typed model, canonical writer, problems list (Phase 1) | Done on Linux: `HoverNetTrackSource`, ctests `HoverNetTrackSource` (self-test) and `HoverNetTrackSourceRoundTrip` (all three bundled sources read, written back identically, and compile to the same bytes). In WebAssembly (under Node) the self-test passes and the canonical text of all three sources is identical to native. Windows not run |
| New tracks from nothing (Phase 2 "New track") | Started on Linux: `MakeRingTrack` makes a loop track with eight starts, finish and checkpoints; ctest `HoverNetNewTrack` shows it compiles, passes the strict validator and lint, is finished by the game's driver (replay verifies), and is admissible to the catalogue. In WebAssembly the template writes the identical file and it compiles to identical bytes (`scripts/test-editor-wasm-parity.sh`, run by hand, **not yet in CI**); that check found and fixed coordinates that differed in the 17th digit between C libraries (now whole millimetres). More templates and the New Track dialog are not started |
| Track Market (ADR 0004) | **Built and tested on Linux, not installed or used by anyone yet.** *Checks:* `scripts/check-track-submission.sh` (shared by editor and service; ctest `HoverNetTrackSubmissionGate`). *Service:* `packaging/web/hovernet_market.py` (invited authors only, first track reviewed by a moderator, reports hide unlawful content at once, revocations, versions, limits; ctest `HoverNetWebMarket`, 67 checks incl. the real tools on a real generated track). *Servers:* `sync-market-tracks.sh` + timer put listed tracks into the race server's community folder and remove revoked ones (ctest `HoverNetMarketSync`). *Game:* lists published tracks inside the one Community tracks list with author and licence, downloads them from the market, deletes revoked ones from the player's folder (ctest `HoverNetTrackMarketClient`); the WebAssembly build compiles, **not run in a browser**. *Install:* `packaging/web/install-market.sh` (dry run shown, **never run on the host**; the build recipe was proven on a pristine copy here). Not built: the editor's Publish button and 2D interface, moderator screens beyond the JSON endpoints, ratings. Launch gates only the owner can clear: the signing key and the legal review of the terms |
| Phases 2 (UI) to 6 | Not started |

## Where we start

Checked in the repository on 8 November 2026 (read, not run unless stated):

- **HoverCad** (`HoverCad/`) is an MFC application, Windows only, built with a Visual Studio project. It saves binary `.TR` files (MFC archives; `HCad1.TR` and `test.TR` are the only samples in the tree). To compile, it writes a temporary text file and calls `MazeCompiler.exe` ([track-format.md](track-format.md)). It reads a GrokkSoft registration key from the Windows registry. Its source carries the GrokkSoft licence header, so [CLAUDE.md](../CLAUDE.md) rule 5 applies to anything derived from it.
- **The supported 2.0 workflow is already text-first** ([track-authoring.md](track-authoring.md)): a `.track.txt` source, `HoverNetMazeCompiler`, `HoverNetTrackValidator` (strict and community levels), `HoverNetTrackPack lint | pack | verify`. `NetTarget/TrackSources/Tidal Causeway.track.txt` is the maintained example. HoverCad is documented as legacy and not required.
- **The content pipeline exists**: the track package v1 wrapper ([specs/track-package.md](specs/track-package.md)) and 1,012 tracks that import and verify with `HoverNetTrackPack corpus`.
- **The game client is one shared SDL2 + Dear ImGui program** that builds for Windows x64, Linux and, with Emscripten, the browser, with golden tests that check native and WebAssembly agree ([determinism.md](determinism.md), [render-golden.md](render-golden.md), [web-parity-matrix.md](web-parity-matrix.md)).
- **What does not exist**: any visual editor other than HoverCad; a reader for `.TR` outside HoverCad; a way to turn a compiled `.trk` back into editable source (`HoverNetTrackPack` imports and packages `.trk` files but does not decompile them: **not checked further**, see Decision 4); the compiler, validator and linter built as a WebAssembly library (**not checked**).

## What "parity with the game client" means

"Parity" is easy to say and easy to let rot, so this roadmap defines it as five rules, each with a test that fails when it is broken. The editor is not finished at any phase unless all five hold for everything that phase delivers.

1. **Content parity.** Every element, texture, room feature and track rule the game can load, the editor can create and edit. The editor reads one catalogue of elements and textures, `NetTarget/TrackEditor/TrackElementCatalogue.h`, and the ctest `HoverNetTrackElementCatalogue` checks it against the game's real object factories in both directions (an id the factory can create but the catalogue lacks fails; so does a catalogued id the factory cannot create or creates as the wrong kind). The factory itself is a `switch` that nothing can list, which is why the catalogue is a table with a test rather than a read-out. Done 8 November 2026 (see Progress).
2. **Output parity.** The editor does not contain its own compiler or its own idea of "valid". It links the same `MazeCompiler`/`LevelBuilder` code, the same validator (strict and community levels) and the same linter that the command-line tools and CI use, as a library. Round trip: all 1,012 tracks (and the bundled `.track.txt` sources) go editor model, then export, then compile, and come out identical to what the command-line tools produce, or are listed with the exact reason.
3. **Preview parity.** The editor's 3D preview and "playtest" run the game's renderer and simulation, not an approximation. A playtest is a real local race of the shared client code. A render test compares an editor preview frame with the game's frame for the same camera and track (same mechanism as the render golden frames).
4. **Release parity.** The editor ships in the same release, with the same version and the same simulation rules version (`kSimulationRules` in `NetTarget/Authority/ReplayFormat.h`), as the game. A rules bump or a new element in the game cannot be merged until the editor catalogue, the editor's tests and the parity matrix are updated in the same change (a CI check, not a convention). The web editor is deployed with the web client from the same pipeline.
5. **Platform parity.** Windows x64, Linux and the browser editors are one program (one UI code base, one core library) and are held to one **editor parity matrix**, in the style of [web-parity-matrix.md](web-parity-matrix.md): every row verified on every platform, with the evidence named, or listed as an accepted browser limit (file access, storage, upload). The same scripted tests (headless SDL dummy driver natively, headless Chrome for the browser) run on every platform.

## Decisions to make first

Each needs an owner decision (an ADR in `docs/adr/`) before Phase 1 code. Recommendations are marked.

1. **One UI code base or three.** *Recommended: one, in SDL2 + Dear ImGui, the same stack as the game client.* It already builds for Windows, Linux and the browser, so rule 5 holds by construction and the editor can embed the game's renderer for rule 3. The cost is a less "native-looking" interface (menus, file dialogs); native file dialogs and the browser's file picker are handled behind one small interface. The alternative (Qt or Win32 for native plus a separate web UI) triples the work and lets the platforms drift; it is not recommended.
2. **What an editor project is.** *Recommended: the text source (`.track.txt`) stays the single source of truth,* with editor-only data (view, layers, grid, notes) in a clearly marked section the compiler is verified to ignore (**to verify** in Phase 0; if it does not ignore unknown sections, a small side file is used). Text diffs in git, matches the maintained example, and needs no new format. A native level format is a separate 3.0 decision; the editor core keeps its model format-agnostic so that can be added as a reader/writer later.
3. **Licence handling.** Is the new editor written clean (from the documented formats and the game's code) or may it reuse HoverCad's algorithms? Anything derived from HoverCad carries the modification notice and licence conditions ([licence-compliance.md](licence-compliance.md)). *Recommended: write the editor new, use HoverCad only as a behaviour reference and for the `.TR` reader, and record the provenance of each file.* New third-party libraries (a file dialog helper, for example) go in `THIRD_PARTY_NOTICES.md` before use.
4. **Editing the existing library.** Most of the 1,009 community tracks exist only as compiled `.trk` files. Editing one needs a `.trk` to source reader. *Recommended: a 2-week spike in Phase 0* to see whether geometry, starts and elements can be recovered exactly; if not, "open a community track" is read-only (view, playtest, copy elements) and the roadmap says so.
5. **Browser limits.** *Recommended: desktop browsers first (Chrome, Edge, Firefox); tablets best effort; phones out of scope for editing* (the game plays on phones; a track editor needs a pointer and a keyboard). Saving uses the browser's file picker or a download, with an autosave to IndexedDB so a closed tab loses nothing.
6. **Where sharing goes.** Decided: [ADR 0004](adr/0004-track-marketplace.md). Authors publish from the editor to a shared Track Market (a live catalogue, one Track Market screen in the game for desktop and browser, official servers sync listed tracks). A submission is a package plus a finishing replay; a signed-in author submits; a moderator reviews an author's first track; the market is a private beta for invited authors until the terms have had legal review and the production signing key exists.

## Phases

Durations are rough, for a small team, and not commitments. Phases 1 and 2 can overlap with 3.0 Stage 3 and 4 work.

### Phase 0: Inventory and decisions (about 3 weeks)

- Record decisions 1 to 6 as ADRs.
- **Feature inventory of HoverCad** from its source: every tool, dialog, attribute and shortcut, with what to do about each (keep, replace, drop and why). Include the `.TR` structures (nodes, walls, polygons, features, items) as a written spec with the two sample files as test data.
- **Content catalogue**: a tool that dumps every element type, texture, and room attribute the game registers (from `ObjFac1` and the resource library) as a machine-readable list. This becomes the parity test for rule 1.
- Confirm the shared pieces build as WebAssembly: the compiler is done (it is run as its own module instance, not linked in-process); the validator, the linter and the track loader remain.
- Spike for decision 4.

**Exit gate:** ADRs accepted; the catalogue dump and its test run in CI; the compiler, validator and linter link into a test program on Linux, Windows x64 and WebAssembly and give the same answers as the command-line tools on the whole corpus.

### Phase 1: Editor core, no user interface (about 6 weeks)

`HoverNetTrackEditorCore`, a library with no video, audio or platform code (the same discipline as `HoverNetSim`, enforced by the same kind of link check).

- The track model: rooms (polygons), walls with textures, floors and ceilings, connections, features, starts, elements, description and background.
- Readers and writers: `.track.txt` (read and write, byte-stable where possible), the `.TR` importer, and (if decision 4 allows) the `.trk` reader.
- An undo/redo command stack with every edit as a command, so tests and the UI use the same path.
- Live validation: the shared validator, linter and the compiler's own errors mapped to a place in the model.
- Corpus tests (rule 2) and the catalogue coverage test (rule 1).

**Exit gate:** the corpus round trip passes on Linux, Windows x64 and WebAssembly with every failure explained; `Tidal Causeway` and the bundled tracks rebuild to the committed `.trk` files; undo/redo property tests pass.

### Phase 2: Editor with 2D editing, all three platforms (about 8 weeks)

The first thing a person can use. The SDL2 + ImGui editor program, built for Windows x64, Linux and WebAssembly from one source.

- Plan view: pan, zoom, grid and snap, select, move, scale, rotate; draw and split rooms and walls; connect rooms; set heights and textures; place starts, gates, pickups, jumps and other elements from the catalogue; attribute panel; the description and background.
- **New track:** File > New starts from a template (the loop from the editor core's `MakeRingTrack` first; figure-of-eight, out-and-back, and a blank canvas next) or from nothing, with the eight starts, the finish line and both checkpoints already in place so a new track is a race from the first save; changing the template's size, width and room count is a dialog.
- File: open, save, import `.TR`, export `.trk`; recent files; the browser's picker, download and IndexedDB autosave; keyboard shortcuts and a gamepad-free UI usable with a mouse and keyboard.
- Problems panel: validation messages that jump to the element.
- Settings shared with the game where they overlap (UI scale, high contrast, reduced motion, language when it exists).
- Editor parity matrix v1 published, every row marked verified or not verified per platform.
- Scripted UI tests: native with the dummy video driver (as the game's menu tests do), browser in headless Chrome (as `scripts/test-web-client.sh` does).

**Exit gate:** a creator can start a new track from a template, or from nothing, and build a `Tidal Causeway`-class track on each platform and compile it; the matrix has no unexplained gaps for Phase 2 features.

### Phase 3: Preview and playtest (about 5 weeks)

- 3D preview with the game's renderer, free camera, and a switch to any room's view.
- **One-click playtest:** a local race on the track as it is now, starting from the selected start position, weapons on or off, then back to the editor with the validator's and the race's findings. Online playtest with a private race code once the 3.0 server flow allows it.
- **The playtest records a replay.** When the player finishes a lap in a playtest, the editor keeps the finishing replay: it is the proof of playability that Publish sends (ADR 0004 decision 1).
- Preview-versus-game render test (rule 3); the editor's frame time is measured and recorded like the game's.

**Exit gate:** the same track playtested from the editor and from the game's local race gives identical simulation hashes (the determinism tools already compare these) on all three platforms.

### Phase 4: Authoring quality (about 6 weeks)

- Checks beyond today's validator, shared with CI so tracks cannot pass in the editor and fail in CI: topology, collision pinch points, checkpoint order, spawn safety, missing assets, blind traps, jump and pad placement rules from [track-authoring.md](track-authoring.md), a performance budget per room.
- **Free-play tracks (no finish line) as a first-class mode**, shown and tested as such.
- Reusable pieces (straight, curve, jump, pad, chicane) and copy/paste between tracks; scale tool; a texture and element browser with previews; measuring (length in km, as the description shows).
- Accessibility pass on the editor itself (keyboard-only use, scaling, contrast).

**Exit gate:** the checks run in both editor and CI with one implementation; a usability session with at least three people who have not used HoverCad produces a finished playable track.

### Phase 5: Publish and the Track Market (about 10 weeks; the private beta needs no key or legal review, the public launch does)

The design is [ADR 0004](adr/0004-track-marketplace.md). Three parts, in this order:

**5a. Package and check locally (about 2 weeks).**
- Export a package (`HoverNetTrackPack pack`) from the editor with author and licence required ([ADR 0002](adr/0002-content-licence-policy.md); CC-BY-4.0 offered first), `verify --catalogue`, the strict validator and the strict lint run for the author with the messages in plain words, and the finishing replay from the playtest attached.
- A publish checklist the author sees before sending: what passed, what is missing (no finishing lap yet, no author name, a lint warning).

**5b. The submission service and the live catalogue (about 4 weeks).**
- A service beside `hovernet_account.py` (no containers): sign-in required, allow-list for the private beta, rate and size limits, the same checks re-run on the server, a review queue (first track per author), listing, the live catalogue and revocations, per-track reports in the moderator console, unlist and takedown, an audit log.
- Official race servers sync listed tracks and drop revoked ones.
- Signed catalogue once the owner has made the production key; clients pin the public key. Until then it is served over TLS, hash-checked, and called unsigned.

**5c. The Track Market in the game and the editor (about 4 weeks).**
- Published tracks in the shared client's one Community tracks list (desktop and browser from one code base): browse, search (by name or author), author and licence shown, download and play or host, report; attribution in the track selector and on the results screen. Open any listed track in the editor to view and playtest, and to edit when ADR 0003's reader allows it (licence permitting: CC-BY-SA tracks stay share-alike).
- Publish, update (a new version) and unlist from the editor, with the state shown (checking, waiting for review, listed, refused and why).
- Authoring documentation and an example track under a clear licence (rewrite [track-authoring.md](track-authoring.md) around the editor).

**Exit gate:** a creator goes from an empty editor to a track that another player, on another platform and another network, finds in the Track Market, downloads, races, and credits to its author, without touching the source tree; a reported track can be unlisted and a revocation reaches clients and servers; the checks on the server are the same code as the ones in the editor. **Public launch** (any signed-in author) additionally needs the legal review of the terms, the final privacy policy and the signed catalogue.

### Phase 6: Parity hardening and retiring HoverCad (about 4 weeks)

- The editor parity matrix has every row verified on Windows x64, Linux and the browser, or an owner-accepted limit; supported browsers (Chrome, Edge, Firefox, and Safari if it is to be supported) are actually run, not assumed.
- Installers and packages include the editor (Windows installer, the Linux `.deb`, the web deploy), with the release script and `docs/releasing.md` updated.
- HoverCad is removed from the build and the installers, its registry-key check goes with it, and `HoverCad/` is kept in the repository as history with a README pointing here. `docs/track-format.md` is retitled to describe the format, not the tool. `.TR` files remain importable.

**Exit gate:** the editor release criteria below hold, and the owner signs off.

## Keeping parity afterwards

- **A standing checklist for any change that touches track content or rules** (a new element or texture, a validator rule, a rules-version bump, a compiler change): update the editor catalogue, the editor tests and the matrix in the same change. Proposed addition to [CLAUDE.md](../CLAUDE.md) (the owner's decision, not made here): *"A change to track content, the track format, the validator or the simulation rules must update the HoverNet track editor and its parity matrix in the same change."*
- The CI checks that make this automatic: the catalogue coverage test (rule 1), the corpus round trip (rule 2), the preview-versus-game frame test (rule 3), the rules-version check (rule 4), and the same scripted editor tests on Windows x64, Linux and in headless Chrome (rule 5).
- Editor parity rows are reviewed at every release, as the web matrix is.

## Editor release criteria

All of these, on all three platforms:

- Every element, texture and feature the game registers can be placed and edited (rule 1 test green, no exceptions without a written reason).
- All bundled tracks and at least 95% of the community library open, round trip and compile identically to the command-line tools; every failure is listed with its reason.
- A new track made in the editor passes the strict validator, packs, verifies, and races locally and (when available) online with weapons on and off.
- A playtest from the editor and a normal local race of the same track give identical simulation hashes.
- Undo and redo never corrupt a project (property tests), and a closed or crashed browser tab loses at most the last few seconds of work.
- The editor parity matrix has no unexplained gaps.
- A creator can make a new track from a template or from nothing, publish it, and another player can get it from the Track Market on every platform; reported or revoked tracks stop being offered.
- The editor is in the signed installers and packages and in the web deploy; HoverCad is no longer shipped.
- Provenance and licences recorded: every file derived from HoverCad has its notice, every new dependency is in `THIRD_PARTY_NOTICES.md`.

## Risks

| Risk | What reduces it |
| --- | --- |
| The compiler, validator or loader cannot be linked into a WebAssembly build without large changes | Phase 0 checks it first; fall back to compiling on the server for the browser only if the owner accepts the loss of offline editing, and record it as a parity limit |
| `.TR` and `.trk` readers lose information, so old tracks change when re-saved | Corpus round trip with explained failures; never overwrite an original; read-only fallback (decision 4) |
| ImGui is awkward for a precise CAD-style tool (text entry, dense property panels, high-DPI) | A prototype of the plan view in Phase 0; budget for custom widgets; native file dialogs behind one interface |
| The editor falls behind the game over time | The rule 4 CI checks above; the matrix reviewed each release; the proposed CLAUDE.md rule |
| Browser file access and storage differ between browsers | The save/open interface has a download and picker fallback; Firefox and Safari run for real in Phase 6 |
| The market needs server, moderation and signing work that is not started, and two things only the owner can do (the signing key, the legal review of the terms) | Phase 5 is split so the private beta (invited authors, unsigned but hash-checked catalogue) can run first; the public launch is gated on the key and the review (ADR 0004) |
| Unlawful or abusive content in a track's name or description, or a rights claim on a track | First-track review, report button, immediate unlisting while a report is examined, revocation list, the 14-day takedown of ADR 0002; no uploaded pictures or code |
| A published track cannot actually be finished | A finishing replay is part of every submission and is re-verified on the server |
| Licence questions about HoverCad-derived code | Decision 3 first; provenance recorded per file; the notice format in `docs/licence-compliance.md` |

## Not in this roadmap

A native level format (3.0 Stage 4 decides it; the editor core is built so one can be added), a new renderer (3.0 Stage 3), the signed catalogue and moderation, and phone-sized editing.
