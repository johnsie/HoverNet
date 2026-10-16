# Determinism experiment

First results of the Stage 0 determinism work in [roadmap-3.0.md](roadmap-3.0.md): does the same game run produce
the same state on every platform and build?

## The tool

`HoverNetDeterminismRun <track> <frames> [--trace|--state]` loads a bundled track, puts one hovercraft on the grid and
puts a second craft on the next grid slot and runs a fixed script of inputs for both (steer, jump, fire, brake,
change weapon) for N frames of 16 ms, so missiles, pickups and craft-to-craft contact are exercised. It uses
`MR_GameSession::SimulateStep`, which advances the simulation by an exact duration instead of reading the wall clock
(the game itself still steps by elapsed real time through `Simulate()`, which now calls the same code). After every
frame the state of both craft (time, position, room, orientation, speeds, fuel, race time) and the position and
orientation of every free element in the level (missiles, mines, power-ups) is hashed; `--trace` prints
the per-frame hashes and `--state` the raw values, so the first frame where two runs differ is a plain `diff`.
`--tables` prints the trigonometry tables.

`scripts/test-determinism.sh` runs all seven bundled tracks for 3000 frames and compares the final hash chains with
[determinism-golden.txt](determinism-golden.txt). It runs as the `HoverNetDeterminism` ctest, and the web CI job runs
it on the WebAssembly build under Node.

The runner, the replay tool and the netcode prototype register the simulation's own element classes
(`MR_RegisterSimElements`) and need no ObjFac1 plug-in, resource file or SDL. Their output is identical to the
plug-in-based runs: the golden hashes, the physics suite and all 1,012 community tracks (checked by comparing the two
builds track by track).

## Golden physics suite

`scripts/test-physics.sh` (ctest `HoverNetPhysicsGolden`, and the web CI job under Node) runs one craft alone through
nine isolated scenarios (`--scenario=`: idle, accelerate, steer-left, steer-right, slalom, jump, brake, missile,
weapons) for 1,500 frames on ClassicH and The River and compares the hash chains with
[physics-golden.txt](physics-golden.txt). A handling change shows up in the scenario that owns it. The hashes are the
2.0 reference; update them (`--update`) only for an intended, documented change. Native Debug and the ASan+UBSan build
agree on all 18.

**Rules version 2 (16 October 2026).** The golden hashes were regenerated once, when the missile was restored to the
original game's speed (46.662, double) instead of the truncated 46 the portable plug-in had been using
([legacy-element-differences.md](legacy-element-differences.md)). Hashes quoted in older notes below are from rules
version 1; all current tables and files are version 2.

## Results

| Comparison | Result |
| --- | --- |
| Same binary, run twice | identical on all 7 tracks |
| Native Release (-O2) vs Debug (-O0) | identical on all 7 tracks |
| Native x86-64 (64-bit) vs WebAssembly (32-bit, different libm and compiler) | identical on all 7 tracks, **after the fix below** |
| Linux sanitizer build (ASan + UBSan) | clean on all 7 tracks; one signed overflow in the wall-distance maths (`ShapeCollisions.cpp`) was made explicitly wrapping, results unchanged |
| All 1,012 community tracks, 1,500 frames each, plain Debug build vs ASan + UBSan build | identical hashes on every track and no sanitizer errors, **after the two fixes below** (`scripts/sweep-community-determinism.sh`) |
| Windows x64 and Win32 (MSVC 14.44, Release, built with CMake on the Windows runner) | identical on all 7 golden tracks and all 18 physics scenarios (16 October 2026) |

### Divergence found and fixed

Native and WebAssembly first differed at frame 110 of ClassicH, the first wall hit. The trigonometry tables and
`atan2` results were bit-identical, but the wall's bounce angle was not: `RAD_2_MR_ANGLE` cast a *negative* double
directly to `unsigned int`, which is undefined behaviour. x86-64 happened to wrap it (-2.97 became 4094); WebAssembly
saturates it to 0. Every wall bounce whose angle came out slightly negative therefore bounced in the wrong direction
in the browser. The macro now truncates to a signed `int` first, which keeps the native results exactly (the golden
hashes did not change) and makes WebAssembly match. This was very probably behind the browser bugs reported as
"crashing into walls" and "sometimes stuck instead of bouncing".

### Community-track sweep: uninitialised start slots and wall-contact overflow

The first sweep found two problems that the seven bundled tracks do not reach:

- **Uninitialised start slots.** Tracks with a single start position (3 of 1,012) never set the second slot's room,
  position or orientation in `MR_Level`; anything reading slot 1 (the second craft here, split screen in the game)
  got stack garbage, so the same run gave different results in different builds. The slots are now initialised
  (room -1, orientation 0), and the runner only adds a second craft when the track defines a second slot.
- **Signed overflow in the wall-contact maths** (`ShapeCollisions.cpp`, three expressions, 12 tracks). Large arenas
  overflow 32-bit products there. The code relied on wrapping, which is undefined behaviour; the wrap is now explicit
  (unsigned multiply and add), giving the same two's-complement values. No hash changed on any of the 1,012 tracks or
  the seven golden ones.

## What this does not prove

- Two local craft and one input script per craft (1,500 frames on community tracks, 3000 on bundled ones); remote (replicated) players and network effects are not covered.
- The clock-driven `Simulate()` path (what the game uses) is not deterministic by nature; this run replaces it with
  fixed steps, which is the Stage 1 direction.
- The Windows comparison was run by hand on the runner, not yet in CI, and only for the bundled tracks and the physics suite (not the community sweep or Debug builds); the rasterizer is not part of the hash.
