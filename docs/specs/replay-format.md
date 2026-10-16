# Replay format v1

Status: **implemented and tested** (reference implementation: [ReplayTool.cpp](../../NetTarget/LinuxClient/ReplayTool.cpp), ctest
`HoverNetReplayFormat`). Part of the Stage 0 format specifications in [roadmap-3.0.md](../roadmap-3.0.md).

A replay is the information needed to run a race again on the deterministic simulation: which track, which rules, and
every player's input on every tick. It does not store positions. It stores periodic state hashes so a viewer, the server or
CI can tell **where** a replay stops reproducing, and a CRC32 so a damaged file is rejected before it is used.

All integers are little-endian and unsigned. There is no padding and no host-native layout anywhere in the file.

| Field | Size | Meaning |
| --- | ---: | --- |
| magic | 4 | ASCII `HNRP` |
| version | 2 | `1`. A reader rejects any other value |
| flags | 2 | Race settings: low byte = laps (1 to 255), bit 8 = weapons enabled; `0` (older files) means 5 laps with weapons; any other bit is refused |
| rules | 4 | Simulation rules version the race was recorded under (`1` for the first portable simulation, `2` once the original missile speed was restored, `3` once the hash covered the full simulation state, `4` since the snapshot field list, `5` since each craft's hover id is its grid slot (shots and hits are attributed to it)). Bumped when a deliberate change alters the golden hashes ([determinism.md](../determinism.md)) |
| tick_ms | 4 | Tick length in milliseconds. `16` in v1 |
| track name | 1 + n | Length byte, then the name as the game knows it (bytes, no terminator) |
| track sha256 | 1 + 64 | Length byte (`64`), then lower-case hex SHA-256 of the `.trk` file (the manifest hash) |
| players | 1 | `1..8`; player `i` is start slot `i` and its craft's hover id is `i` |
| ticks | 4 | Number of ticks, at most 450,000 (two hours) |
| checkpoint_interval | 4 | `250` in v1 |
| inputs | `ticks * players * 2` | Tick-major: for each tick, each player's control bits as a `u16` |
| checkpoint_count | 4 | Must equal `ticks / checkpoint_interval` (integer division) |
| checkpoints | `8 * checkpoint_count` | State hash after tick `k * interval` for `k = 1..count` |
| final_hash | 8 | State hash after the last tick |
| crc32 | 4 | CRC-32 (IEEE, as in zlib) of every preceding byte |

**Input bits** are the simulation's control word: `1` motor, `2` right, `4` left, `8` strafe right, `16` strafe left,
`32` brake/reverse direction, `64` jump, `128` fire, `2048` select weapon (`MR_MainCharacter`). Unknown bits must be
preserved by writers and are ignored by the simulation.

**State hash** is `MR_HashLevelState` (`NetTarget/Model/StateHash.cpp`): FNV-1a (64-bit) over the simulation time, then
every free element of the level in list order (room, factory and class id, position, orientation, and the element's own
logic state through `MR_Element::HashLogicState`), then the element count. Hovercraft contribute their whole behavioural
state (speeds and fuel by exact bit pattern, control and weapon state, countdowns, mine and power-up stocks, lap and
checkpoint progress, the last-hit list, race time); missiles, mines, bumper gates, power-ups and the physical test element
contribute theirs. The hash definition is part of the format: changing it requires a new `rules` value (current: `5`). See
[simulation-state.md](simulation-state.md).

## Reader rules

A conforming reader MUST, before simulating: check the CRC; check magic and version; check every count against the
remaining bytes before allocating; reject trailing bytes; refuse a `rules` value it was not built for; and refuse to
verify if the local track file's SHA-256 differs from the recorded one. It MUST NOT read past the end of the buffer on any
input. The ctest corrupts, truncates and re-labels files to check this.

## Verification

`HoverNetReplay verify file.hnr` rebuilds the session, feeds the inputs, compares the hash at each checkpoint and at the
end, and reports the 250-tick window where it first diverges. A replay recorded on Linux verifies on Windows x64
(MSVC) with the same final hash (checked 16 October 2026).

## Not in v1 (deliberately)

- **Compression.** Raw inputs are about 4 bytes per tick for two players (12 KB for 48 seconds); run-length coding is a
  v2 feature once real files show it matters.
- **Player names, results, chat, spectator cameras and a thumbnail.** Results are derivable by replay; metadata belongs in a
  TLV block added under a new version or flag bit.
- **Mid-race joins, seeking and embedded snapshots.** The versioned snapshot now exists ([simulation-state.md](simulation-state.md)); a v2 replay can carry snapshots at intervals so a viewer can seek without replaying from the start.
- **Signatures.** Server-issued official replays will add a signature block in Stage 5 (accounts).
