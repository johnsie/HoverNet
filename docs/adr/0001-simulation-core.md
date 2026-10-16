# ADR 0001: Evolve the existing simulation behind a clean boundary

- Status: **Proposed** (16 October 2026), for confirmation at the Stage 0 review
- Deciders: project owner
- Roadmap: [roadmap-3.0.md](../roadmap-3.0.md), Stage 0 "Architecture decision", feeding Stage 1

## Question

Does 3.0 build its deterministic, server-authoritative simulation by **evolving the existing C++ code** behind a
clean boundary, or by **writing a new simulation core**? The renderer is decided separately (see below).

## Evidence gathered in Stage 0

| Question | Finding | Source |
| --- | --- | --- |
| Is the simulation already separable from video, audio and UI? | Largely. It builds and runs headless as static libraries (`HoverNetModelCore`, `HoverNetMainCharacter`, `HoverNetResourceCore`), about 5,400 lines in `Model/` and `MainCharacter/`. The remaining entanglement is a handful of virtual `Render(MR_3DViewPort*, ...)` methods on model classes, not shared state. | `CMakeLists.txt`, `Model/MazeElement.h`, `MainCharacter/MainCharacter.h` |
| Does it run on a fixed step without the wall clock? | Yes. `MR_GameSession::SimulateStep(ms)` advances by an exact duration; the game's `Simulate()` now calls the same code. No `rand()` or `time()` in the simulation sources. | [determinism.md](../determinism.md) |
| Is it deterministic across platforms? | Yes on everything tried: Linux Debug/Release, ASan+UBSan, WebAssembly (32-bit, different libm), Windows x64 and Win32 (MSVC). Identical hashes for 7 bundled tracks x 3,000 frames and 18 isolated physics scenarios; 1,012 community tracks identical between plain and sanitizer builds. | [determinism.md](../determinism.md), [physics-golden.txt](../physics-golden.txt) |
| What breaks determinism? | Real but small: two undefined-behaviour bugs on the community corpus plus one cross-platform bug (negative double cast to `unsigned`), all found by the harness and fixed in a few lines each. | determinism.md |
| Is it free of undefined behaviour? | Yes under UBSan for the whole suite and all 1,012 tracks, with no sanitizer exemptions (the rasterizer's was removed). | roadmap-2.0.md |
| Floating point in the simulation? | Present: about 32 `double`/`float` uses in `MainCharacter.cpp` (speeds, fuel) and 2 in `PhysicalCollision.cpp`; positions and angles are fixed-point integers. Results match across x86-64, x86 (SSE2) and WebAssembly so far. | source audit |
| Is there a usable server-side simulation to build on? | No. `GameSimulation` and related server code are unfinished scaffolding and unused; the RaceServer is a relay. | [2.0-platform-inventory.md](../2.0-platform-inventory.md) |

## Decision

**Evolve the existing simulation.** Keep `Model` and `MainCharacter` as the seed of a `sim` library with a hard
boundary, and make the new server, replay viewer, tests and client all link that one library. Do not start a new core.

Reasons, in order of weight:

1. **The expensive uncertainty is gone.** The case for rewriting was that determinism was unknown and perhaps
   unattainable. It has been measured on every target and holds; the defects found were few and local.
2. **Fidelity is a product goal.** "Fast handling, compact tracks, weapons" is what players love. A rewrite has to
   reproduce a 1990s physics feel, and the 1,012 community tracks and 2.0 golden hashes exist to check the existing
   code, not a re-implementation of it. A new core would have to re-earn all of that.
3. **It is small.** About 5,400 lines is cheap to refactor and fully test; it is not a legacy monolith.
4. **The test net is already built.** Golden hashes, per-scenario physics, the corpus sweep, sanitizers and
   WebAssembly run in CI. Refactoring behind that net is low-risk and every step is verifiable.

## What "evolve" means (the work Stage 1 inherits)

1. **Cut the render dependency.** *Started (16 October 2026): the hovercraft's rendering and sound moved out of `MainCharacter.cpp` into `MainCharacterView.cpp`; the simulation now only reports sound events (`QueueSound`). The new `HoverNetSim` library (model, collisions, hovercraft physics, and a headless presentation stub) links with no video, audio or resource code, and the determinism, netcode and replay tools link only that. Golden hashes did not change. The simulation headers and sources also include nothing from video, audio or resources, checked by `scripts/check-sim-boundary.sh` (ctest `HoverNetSimBoundary`). Builds verified on Linux, Windows x64 (CMake) and the legacy Win32 project. Remaining: the `Render` virtuals on the level's element classes (`MazeElement.h`, forward-declared only, so not blocking), and the plug-in free elements in ObjFac1 (missiles, mines and power-ups still live in a plug-in that links the video and resource libraries).*  Move `Render(...)`/`RenderWallSurface`/`RenderHorizontalSurface` out of the model
   classes into a separate view layer keyed by element type; `sim` must not include `3DViewport.h`, audio or UI
   headers. Enforce with a link-time check (the `sim` library links without video or sound).
2. **Replace the dynamically loaded object factory** (`ObjFac1`, `DllObjectFactory`) inside `sim` with a static
   registry of element types. The plug-in model is the main blocker for the server, replays and WebAssembly (the
   browser build already compiles the factory in). Keep reading old track archives through it until Stage 4.
3. **Make state explicit and snapshot-able.** Define one serialisable simulation state (positions, speeds, fuel,
   weapons, timers, RNG-free) with a version number, and hash it in one function that the tests and replays share
   (today the hash lives in the test tool).
4. **Fix the numeric rules in writing.** Positions and angles stay integer/fixed-point. Decide for the `double`
   speeds and fuel: convert to fixed-point (preferred, removes the compiler-flags risk entirely) or keep `double`
   with a pinned rule set (no FMA contraction, SSE2/strict IEEE, no `-ffast-math`, enforced in CMake and checked
   by the golden hashes on every platform). The golden suite makes either change measurable: a conversion is
   accepted only if the hashes change in a documented, explainable way or not at all.
5. **Inputs as the only way in.** Control state per tick, as a plain struct (the determinism tool already works this
   way through `SetControlState`), so the server, a replay, and a client all feed the same stream.
6. **Keep `SimulateStep` as the one entry point**, remove the wall-clock `Simulate()` path from the client in favour
   of a fixed-step accumulator with render interpolation.

## Alternatives considered

- **New simulation core (clean-room, fixed-point throughout).** Cleanest end state and the best-defined numeric
  contract. Rejected for now: months of work to match behaviour that already works, a larger parity-risk surface, and
  no evidence that the current code cannot reach the same determinism. It stays available as a **fallback** if a
  gate below fails.
- **Keep it as it is, add the server beside it.** Rejected: leaves rendering and the plug-in loader inside the
  authoritative path, which blocks a headless server and replays.

## Review gates (what would reverse this decision)

Revisit and consider the rewrite if any of these happen during Stage 1:

- Removing the render and factory coupling takes materially longer than the Stage 1 budget (say, more than a third of
  it) or changes more than a few golden hashes for reasons that cannot be explained.
- A platform or compiler combination in the support matrix cannot be made to reproduce the golden hashes without a
  per-platform workaround.
- Server-side lag compensation or rollback needs state snapshot/restore that the existing structure (linked lists of
  heap-allocated free elements, pointers between elements) cannot provide cheaply. The netcode prototype below is
  the experiment that answers this.

## Renderer (decided separately)

Not decided here. The renderer is a 1990s software rasterizer driven by 4096-scaled coordinates. Because the
simulation no longer depends on it after step 1, the renderer can be replaced (or kept as the classic preset) on its
own schedule in Stage 3. The classic preset must match 2.0 captures within an agreed tolerance; that is a Stage 3 gate,
not a Stage 1 one.

## Open questions

1. Fixed-point `double` speeds, or pinned IEEE rules? Needs a quick prototype on the golden suite.
2. Snapshot cost of the free-element lists: the [netcode prototype](../netcode-prototype.md) shows rollback by replay costs about 7 microseconds per tick, so snapshots are an optimisation, not a prerequisite. Still needed for server-side weapon lag compensation.
3. Does the corpus sweep become a CI job (it takes minutes) or a nightly? Proposal: nightly plus a 50-track sample per
   push.
