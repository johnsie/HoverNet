# Simulation state, hashing and numeric rules

Status: **draft**, to be finalised in Stage 1 with the `sim` library boundary from
[ADR 0001](../adr/0001-simulation-core.md). What exists today is marked *implemented*.

## What the state is

The complete, deterministic state of a race is:

1. **The track** (immutable): identified by name and SHA-256 of the `.trk` file.
2. **Simulation time** (`MR_SimulationTime`, milliseconds, advanced by whole ticks). *Implemented.*
3. **Each craft:** position (x, y, z; integer millimetres), room, orientation (integer angle units), speeds (x, y, z;
   `double`), fuel, control state, on-floor and hover-model flags, out-of-control timer, current weapon and ammunition,
   lap and checkpoint progress, race time.
4. **Free elements:** missiles, mines and power-ups: type, room, position, orientation and their own timers, in the
   level's per-room lists *in list order* (order affects results).
5. **Level logic state:** anything on the track that changes during a race (pickup availability and respawn timers).

*Implemented (rules version 3):* the hash covers items 1 (by track identity in replays), 2, 3 and 4 completely:
`MR_HashLevelState` walks the level and each element feeds its own logic state to the hasher
(`MR_Element::HashLogicState`). Item 5 (level logic such as pickup respawn timers) lives in the elements themselves in
this simulation, so it is covered the same way; surfaces carry no per-race state.

## Inputs

The only way into the simulation is one control word per craft per tick (`SetControlState`), plus the tick length.
No wall clock, no random numbers (there are none in the simulation sources), no platform queries.

## Hashing

FNV-1a, 64 bit, over the fields above in a fixed order, with floating-point values hashed by exact bit pattern. A one-bit
difference anywhere is a divergence. The hash function lives in the simulation library (`Model/StateHash.*`); the determinism runner, the replay tool and the
netcode prototype all call it.

## Numeric rules (proposed)

- Positions, angles, times and all geometry are integers or fixed point. Intermediate products that can exceed 32 bits use
  explicit unsigned wrap helpers (`WrapMul` and friends) or 64-bit arithmetic; **signed overflow is never relied on**, and
  UBSan runs in CI.
- Speeds and fuel are `double`. The build MUST NOT enable FMA contraction or fast-math, and targets SSE2/strict IEEE
  (CMake and MSVC `/fp:precise` are checked by the golden hashes on every platform). Converting them to fixed point is the
  preferred direction (ADR open question 1) and is accepted only if the golden hashes either do not change or change in a
  documented way.
- Trigonometry tables are generated at start-up with the platform `libm`; they were compared across platforms and are
  identical today. Stage 1 commits them as constants and checks the generated values against them in CI.
- Narrowing casts of floating-point values to unsigned types are forbidden (a negative double cast to `unsigned` was the
  one real cross-platform divergence found).

## Snapshots (implemented, rules version 4)

`MR_SaveLevelState` / `MR_RestoreLevelState` (`NetTarget/Model/StateSnapshot.*`). A snapshot is a stream of 8-byte
little-endian values, the same for every platform: header (magic `HNSS`, snapshot version 1, rules version, simulation time,
element count), then every free element in list order, then a checksum of everything before it. For each element: its kind,
a key, room, factory and class id, position, orientation, then its logic state.

- **Kinds.** *Permanent* elements come with the track (power-ups, bumper gates, fuel zones) and are restored in place by
  index. *External* elements are owned by the program (the craft); the caller passes the same list when saving and
  restoring. *Transient* elements (missiles, mines) are destroyed and re-created from their class id.
- **One field list.** Each element lists its behaviourally relevant fields once (`MR_Element::VisitLogicState`), and that
  list is what the state hash, the snapshot writer and the snapshot reader all use. A field cannot be hashed without being
  restored, or the reverse.
- **List order** inside each room is restored, because the simulation order depends on it.
- **Refusals.** A damaged, truncated, wrong-version or wrong-rules snapshot, or one for a different track (a permanent
  element that does not exist, a room the level lacks), is refused with a message. After a refusal the level may be partly
  restored and must be discarded.

**Property test** (`HoverNetSnapshotTest`, ctest `HoverNetSnapshot`, [snapshot-golden.txt](../snapshot-golden.txt)): on the
seven bundled tracks at ticks 400 and 1,400, with missiles and mines in flight at the snapshot point, the race is
snapshotted, run on for 2,000 ticks recording the hash each tick, restored in the same session and in a brand new one, and
must reproduce every one of those hashes; saving again after a restore must give identical bytes; and the snapshot bytes
must be identical on Linux, Windows x64 and Win32. Removing a field from the list (a missile's age) makes the test fail at
the tick the missile would have expired.

Not yet: partial snapshots (deltas) for bandwidth, and compression. Snapshot size is 1.2 to 1.6 KB for two craft.
