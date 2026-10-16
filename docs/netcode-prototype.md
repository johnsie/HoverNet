# Netcode prototype (Stage 0)

Answers the Stage 0 question in [roadmap-3.0.md](roadmap-3.0.md): can the existing deterministic simulation support
prediction and rollback at the latency targets, and what does it cost? See also [ADR 0001](adr/0001-simulation-core.md).

## The tool

`HoverNetNetcodeProto <track> <ticks> <rtt-ms> <loss-percent> [--jitter=ms] [--input-delay=ticks] [--seed=n]`
([NetcodeProto.cpp](../NetTarget/LinuxClient/NetcodeProto.cpp)) runs two players, each with a complete simulation of both
craft, through a modelled network:

- Topology is client to RaceServer to client, so the delay between players is one full client round trip. Each hop is
  lossy; packets get random jitter and so arrive out of order.
- Every packet carries the last 16 inputs, so an isolated loss costs nothing.
- A client applies its own input immediately (optionally `--input-delay` ticks later, sent early), predicts the other
  player as "same as last known input", and when the real input contradicts the prediction it rolls back and
  re-simulates. There is no snapshot facility in the simulation, so a rollback rebuilds the session and replays from
  tick 0. This is deliberately the worst case.
- Success means both clients, once the network drains, reach exactly the state of an ideal run that had every input
  on time (a state hash of both craft). It also prints how far the remote craft was from its true position while the
  race ran, which is what a player would see snap.

`HoverNetNetcodeTarget120` and `HoverNetNetcodeTarget250` in ctest run the two roadmap targets. The runs are
deterministic for a given seed and also pass in the ASan+UBSan build.

## Results (The River, 2,000 ticks, seed 7, Debug build)

| Network | Input delay (ticks of 16 ms) | Rollbacks (client 0) | Deepest rollback | Remote craft error: mean / max |
| --- | --- | --- | --- | --- |
| 120 ms, 2% loss, 10 ms jitter | 0 | 95 | 9 ticks | 1 mm / 152 mm |
| 120 ms, 2% loss, 10 ms jitter | 4 | 95 | 5 | 0 mm / 5 mm |
| 120 ms, 2% loss, 10 ms jitter | 8 | 9 | 1 | 0 mm / 0 mm |
| 120 ms, 2% loss, 10 ms jitter | 16 | 0 | 0 | 0 mm / 0 mm |
| 250 ms, 5% loss, 25 ms jitter | 0 | 88 | 18 | 16 mm / 495 mm |
| 250 ms, 5% loss, 25 ms jitter | 8 | 88 | 10 | 1 mm / 74 mm |
| 250 ms, 5% loss, 25 ms jitter | 16 | 33 | 2 | 0 mm / 0 mm |
| 400 ms, 10% loss, 50 ms jitter | 0 | 80 | 28 | 83 mm / 1,408 mm |

Every row, including the 400 ms one, converged to the ideal run bit for bit. A craft is several metres long, so
errors of a few centimetres to half a metre are small; the 400 ms case is where a correction becomes visible.

## What this answers

1. **The existing simulation supports prediction and rollback.** Determinism is what makes it work: the replay after a
   late input reproduces the ideal state exactly. Nothing about the design needed to change for correctness.
2. **Rollback is cheap even without snapshots.** Re-simulating costs about 7 microseconds per tick in a Debug build
   (including rebuilding the session), so a 9-tick correction at the 120 ms target is far below a frame. The
   from-scratch replay used here grows with race length (about 14 ms for a full 2,000-tick replay, 8 ms per rollback on
   average over this run), which is fine for a prototype and for 3-minute races but should be replaced by periodic
   checkpoints. The ADR's open question about snapshot cost is therefore **not blocking**: the first
   implementation can checkpoint by rebuilding from a saved input log, and a real snapshot is an optimisation.
3. **A modest input delay removes almost all visible correction.** About half of the one-way delay in ticks (4 ticks,
   64 ms, at the 120 ms target) cuts the worst remote-craft error from 152 mm to 5 mm; matching it (8 ticks) removes it.
   That trade (a slightly later response to your own steering against smoother opponents) is a Stage 2 tuning
   decision, probably adaptive to the measured round trip.
4. **The architecture's free-element lists are not a barrier.** State here includes missiles, mines and power-ups, and
   the hash covers them through the craft they hit.

## What this does not answer

- It is a relay model of two peers. The authoritative-server design (the server owns the result and corrects clients)
  is a Stage 2 build; the same rollback machinery runs on the client in that design. Server-side lag compensation for
  weapons (rewinding other craft to the shooter's view) needs real snapshots and is not exercised.
- Eight players: prediction error grows with players and the replay cost with craft count; not measured yet.
- Loss beyond the 16-tick window is modelled as a reliable resend delivered at the end, not as a stall.
- Timings are Debug-build, desktop CPU. Phones and WebAssembly are untested (`HoverNetNetcodeProto` is not built for
  the browser yet).
