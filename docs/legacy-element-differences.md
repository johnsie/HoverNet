# Legacy ObjFac1 versus the portable plug-in: recorded differences

The elements that take part in the simulation (missiles, mines, power-ups, bumper gates, fuel and speed zones, finish
line) exist twice:

- **Legacy plug-in** (`NetTarget/ObjFac1/main.cpp`, `Missile.cpp`, `Mine.cpp`, `PowerUp.cpp`, `BumperGate.cpp`, ...): a
  Windows DLL loaded by the legacy Win32 client.
- **Portable plug-in** (`NetTarget/ObjFac1/LinuxPlugin.cpp`, the `Headless*` classes): the same elements re-implemented
  for the shared SDL2 client, which ships as `ObjFac1.dll` / `ObjFac1.so` on Windows x64, Linux and in the browser. All
  golden hashes in [determinism.md](determinism.md) and [physics-golden.txt](physics-golden.txt) use this one.

Stage 1 of [roadmap-3.0.md](roadmap-3.0.md) requires golden tests against 2.0 behaviour with every difference fixed or
recorded. This is the result of reading both implementations side by side (16 October 2026). It has not been confirmed by
running the legacy plug-in, which does not build outside Windows.

## Result: one real difference, in the missile

| Element | Same behaviour? |
| --- | --- |
| Mine | Yes: same size (400 radius, 140 high), same fall at 0.6 per ms, same landing and effect rules |
| Power-up | Yes: same size, rotation by the elapsed time, same effect |
| Bumper gate | Yes: same size interpolation, same 1,500 / 9,000 / 13,000 ms timeline |
| Fuel, speed zones, finish line | Compiled from the same source files in both |
| Surfaces (classic 1 to 5, track set 50 to 73) | Yes: the legacy classes all derive from `MR_BitmapSurface`, whose only simulation behaviour is one shared infinite-weight, zero-speed collision effect; `MR_SimSurface` is the same |
| **Missile** | **No** (below) |

### Missile differences

1. **Speed.** The legacy missile moves at `21 * 2222 / 1000` computed in `double`: **46.662** units per millisecond, and
   steers with `double` x and y speeds. The portable missile computes `const MR_Int32 speed = 21 * 2222 / 1000`, which
   truncates to **46**. A portable missile is therefore about **1.4% slower** and, over its 7.5 s life, ends roughly
   5 m short of where a legacy missile ends.
2. **Speed given to a craft it hits.** Legacy: the double speed times 256. Portable: the same expression in integer
   arithmetic (`21 * 2222 * cos / (1000 * 4096) * 256`), so the impact impulse differs slightly.
3. **Movement per 5 ms slice.** Legacy multiplies the duration by a double speed and converts; portable multiplies
   integers and divides, so positions differ by rounding on every slice.
4. **End of life.** Legacy: the speed formula `(1200 - lived - 7500) * speed / 1200` is never positive, so after 7,500 ms
   the missile stops; at exactly 7,500 ms it still moves one more slice. Portable moves only while `lived < 7500`.
   One-slice difference, only at that boundary.
5. **Animation frames** differ in how the sequence and frame are chosen near the end (no effect on the simulation).

Items 1 to 3 change where a missile is and what it does to a craft. In a race between a legacy client and a shared client
the two simulate their own copy of each missile, so the copies drift apart by up to a few metres until the owner's next
state update corrects them (missile positions are relayed). Races between shared clients, including the browser, agree with
each other exactly, which is what the golden hashes verify.

## Decision and resolution

Decided 16 October 2026: **restore the legacy missile**, because it is the original game design and it played well. The
portable plug-in's missile now uses the legacy maths exactly (a `double` speed of 21 x 2222 / 1000 = 46.662, double x and
y speeds, the same 5 ms slices and halving search, the double speed times 256 as the impact impulse).

Effect on the golden suite: only the scenarios that involve a missile changed (`missile` and `weapons` on both tracks)
and all seven full-race hashes; idle, accelerate, steer, slalom, jump and brake did not move, as expected. The replay
rules version went from 1 to 2. The result was then re-verified: plain and ASan+UBSan builds agree on all 1,012 community
tracks with no sanitizer findings, and Windows x64 and Win32 reproduce the new golden hashes and verify a replay recorded
on Linux. The WebAssembly comparison runs in the web CI job.

## What this means for the architecture

Done on 16 October 2026 ([ADR 0001](adr/0001-simulation-core.md) step 2): each element's simulation is now one class in
`NetTarget/ObjFac1/SimElements.cpp`, built into the `HoverNetSim` library, and the portable plug-in's classes derive from
it and only add presentation (the way the hovercraft was split). A bumper gate's collision shape follows its animation, so
its frame count (10) is simulation data, pinned by `kBumperGateFrames` and checked against the resource file by
`HoverNetResourceSmoke`. The only remaining duplicate is the legacy Win32 plug-in, which is retired with the legacy client.
