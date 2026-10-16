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

## Snapshots (Stage 1)

A versioned snapshot is a tagged, explicit-width, little-endian serialisation of items 2 to 5, with a leading
`snapshot_version`, the `rules` version and a CRC. Requirements: restoring a snapshot then simulating N ticks gives the
same hash as simulating those N ticks from the original state (property test over the corpus); a snapshot taken on one
platform restores on every other. Snapshots are needed for mid-race joins, spectators, replay seeking and server-side lag
compensation; rollback prediction does not need them ([netcode-prototype.md](../netcode-prototype.md)).
