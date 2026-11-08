# ADR 0003: One SDL2 + ImGui track editor, text source as the project, parity enforced by tests

- Status: **Accepted** (8 November 2026) by the project owner's delegation ("make decisions for me based on legality, playability and usability"); open to revision
- Deciders: project owner (delegated)
- Roadmap: [roadmap-track-editor.md](../roadmap-track-editor.md), Phase 0 decisions 1, 2 and 3

## Question

HoverCad (Windows only, MFC, binary `.TR` files) is to be replaced by an editor that runs natively on Windows and Linux and in the browser and
always stays at parity with the game client. How is it built so that the three platforms and the game cannot drift apart?

## Evidence gathered so far (8 November 2026)

| Question | Finding | How checked |
| --- | --- | --- |
| Does the compiler accept editor-only data inside the text source? | Yes: an `[Editor]` section placed at the start or at the end of `Tidal Causeway.track.txt` is ignored and the compiled `.trk` is byte-identical to the one compiled without it. | Ran `HoverNetMazeCompiler` three ways on Linux and compared with `cmp` |
| Can a tool list what the game can create? | **No.** The factory is a `switch` on a class id, repeated in the legacy plug-in (`NetTarget/ObjFac1/main.cpp`), the shared plug-in (`LinuxPlugin.cpp`) and the simulation (`SimElements.cpp`). | Read the three files |
| Do those registries agree? | They did not: id 12 (the ball) exists in the legacy factory only. | `HoverNetTrackElementCatalogue` (added with this ADR) failed on it |
| Is the compiler's idea of a valid free element the factory's? | Yes: the compiler creates each `Free_Element` through the factory and requires it to be a free element, each surface likewise (`LevelBuilder.cpp`). | Read `LevelBuilder.cpp` |
| Is the compiler a library? | No, and it should not be made one: its success path ends with `exit()` on purpose, to avoid a destructor crash in the legacy code. Rewriting that is risk for no gain. | Read `main.cpp` |
| Does the compiler build and run as WebAssembly? | Yes, with a 16 MB stack (the default 64 KB overflows) and a 64 MB initial memory. Under Node it compiles all three bundled track sources. | Built with Emscripten 3.1.74 and run (`HoverNetMazeCompiler.js`) |
| Does it give the same bytes as native? | **Not at first.** Two `qsort` calls in `LevelBuilder.cpp` sorted surfaces by level only, so equal levels came out in the C library's order (glibc and musl differ): 287 bytes of a 578,799-byte track differed. With a stable sort the native output still equals the committed `.trk` for all three sources and the WebAssembly output equals native. | `scripts/test-compiler-parity.sh`, ctest `HoverNetCompilerParity` (native half); WebAssembly half run by hand |
| Does the shared client already build for all three platforms? | Yes (SDL2 + Dear ImGui; Windows x64, Linux, Emscripten). | Existing CI and [web-parity-matrix.md](../web-parity-matrix.md) |

## Decision (proposed)

1. **One editor program in SDL2 + Dear ImGui**, the stack of the game client, built for Windows x64, Linux and WebAssembly from one source, on top of
   a UI-free core library (`HoverNetTrackEditorCore`). No second native UI toolkit and no separate web UI. File dialogs, clipboard and storage sit
   behind one small interface with a native, a browser and a test implementation.
2. **The text source (`.track.txt`) is the project file.** Editor-only data (view, grid, layers, notes) goes in an `[Editor]` section that the compiler
   ignores (verified above on Linux; the Windows and WebAssembly compiler builds are to be checked in Phase 0). No new project format until the 3.0
   native level format is decided; the core's model stays format-agnostic.
3. **Parity is enforced by tests, not by convention.** The editor takes its element and texture list from
   `NetTarget/TrackEditor/TrackElementCatalogue.h`, and the ctest `HoverNetTrackElementCatalogue` fails when that table and the object factory
   differ in either direction (see the roadmap's five parity rules for the other four checks).
4. **Compile in a separate process or module instance** (see Consequences); the compiler is never reimplemented or linked in-process.
5. **Clean implementation.** The editor is written new from the documented formats and the game's code; HoverCad is a behaviour reference and the
   source for a `.TR` reader only. Anything derived from HoverCad carries the notice required by `docs/licence-compliance.md`, and each new file's
   provenance is recorded.

## Consequences

- One UI to test, so the editor parity matrix has one implementation to verify on three platforms.
- A less native look than a Qt or Win32 interface, and ImGui is a poor fit for dense text editing: budget custom widgets and a Phase 0 prototype.
- Every change that adds a track element, texture or compiler rule must touch the catalogue, which makes the editor change part of that change.
- **The editor compiles by running the real compiler in its own process or instance, not in-process:** the native editors run `HoverNetMazeCompiler`;
  the browser editor loads the WebAssembly build as a fresh module instance for each compile (the compiler ends with `exit()`). No second compiler.
- The one change to HoverRace-derived compiler code so far (the stable sort in `LevelBuilder.cpp`) carries the modification notice and leaves every
  committed `.trk` byte-identical, so no rules-version bump or golden regeneration was needed.

## Alternatives considered

- **Qt (or Win32) for native, a separate web UI:** nicer native feel, but three code bases that drift; rejected for rule 5 of the roadmap.
- **Keep HoverCad and fix it:** Windows only, no web, registry-key licence check, binary format; cannot meet the goals.
- **A new binary project format now:** adds a format to maintain before the 3.0 level format is decided; text already diffs, compiles and is the documented workflow.
