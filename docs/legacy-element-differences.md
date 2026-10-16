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

## Decision needed

Which missile is the 3.0 missile?

- **Restore the legacy missile** (46.662, `double` speeds), because it is the original game design and what the legacy client
  still plays. This changes the missile hashes in the golden suite once (bump the rules version) and makes a missile
  about 1.4% faster than the 2.0 shared client's. Cleanest for fidelity and for the Stage 3 retirement of the legacy
  client, since it ends the duplicate.
- **Keep the 46 missile** as the 3.0 reference and record it as a design change from the original. No hash changes now,
  but the server-authoritative simulation then differs from the game as originally built.

Recommendation: restore the legacy values in Stage 1, in the same change that moves the elements behind the simulation
boundary (below), since both change the golden hashes and should be reviewed once. Until decided, nothing changes.

## What this means for the architecture

[ADR 0001](adr/0001-simulation-core.md) step 2 (replace the dynamically loaded factory with a static registry) is also the
chance to end the duplication: one implementation of each element's *simulation* (shared by the server, replay, tests and
client) and a separate presentation class per element, the way the hovercraft was just split. The portable plug-in's
`Headless*` classes are the better base, because they already compile everywhere; the missile maths is the one thing to
bring back from the legacy code.
