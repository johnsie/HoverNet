# Determinism experiment

First results of the Stage 0 determinism work in [roadmap-3.0.md](roadmap-3.0.md): does the same game run produce
the same state on every platform and build?

## The tool

`HoverNetDeterminismRun <track> <frames> [--trace|--state]` loads a bundled track, puts one hovercraft on the grid and
runs a fixed script of inputs (steer, jump, fire, brake) for N frames of 16 ms. It uses
`MR_GameSession::SimulateStep`, which advances the simulation by an exact duration instead of reading the wall clock
(the game itself still steps by elapsed real time through `Simulate()`, which now calls the same code). After every
frame the whole craft state (time, position, room, orientation, speeds, fuel, race time) is hashed; `--trace` prints
the per-frame hashes and `--state` the raw values, so the first frame where two runs differ is a plain `diff`.
`--tables` prints the trigonometry tables.

`scripts/test-determinism.sh` runs all seven bundled tracks for 3000 frames and compares the final hash chains with
[determinism-golden.txt](determinism-golden.txt). It runs as the `HoverNetDeterminism` ctest, and the web CI job runs
it on the WebAssembly build under Node.

## Results

| Comparison | Result |
| --- | --- |
| Same binary, run twice | identical on all 7 tracks |
| Native Release (-O2) vs Debug (-O0) | identical on all 7 tracks |
| Native x86-64 (64-bit) vs WebAssembly (32-bit, different libm and compiler) | identical on all 7 tracks, **after the fix below** |
| Windows x64 and Win32 | not run yet |

### Divergence found and fixed

Native and WebAssembly first differed at frame 110 of ClassicH, the first wall hit. The trigonometry tables and
`atan2` results were bit-identical, but the wall's bounce angle was not: `RAD_2_MR_ANGLE` cast a *negative* double
directly to `unsigned int`, which is undefined behaviour. x86-64 happened to wrap it (-2.97 became 4094); WebAssembly
saturates it to 0. Every wall bounce whose angle came out slightly negative therefore bounced in the wrong direction
in the browser. The macro now truncates to a signed `int` first, which keeps the native results exactly (the golden
hashes did not change) and makes WebAssembly match. This was very probably behind the browser bugs reported as
"crashing into walls" and "sometimes stuck instead of bouncing".

## What this does not prove

- One craft, one input script. Weapons, pickups, several craft and remote players are not covered yet.
- The clock-driven `Simulate()` path (what the game uses) is not deterministic by nature; this run replaces it with
  fixed steps, which is the Stage 1 direction.
- The Windows builds have not been compared (needs the Windows runner), and the rasterizer is not part of the hash.
