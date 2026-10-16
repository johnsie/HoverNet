# The authoritative race (Stage 2 core)

`NetTarget/Authority/AuthoritativeRace.*` is the server's side of a race in the [protocol 3.0 draft](specs/protocol-3.0-draft.md):
the part that decides what happens. It has no sockets and no clock, so the dedicated server, the tests, the spectator and the
replay tools all use the same code. It sits on the simulation library only (`HoverNetSim`); it needs no renderer, plug-in,
resource file or SDL.

## What the server owns

- The simulation: positions, collisions, weapons, pickups, laps, finishes and the result are computed by the server's own copy
  of the race. A client can send exactly one thing, a control word for a tick (`SubmitInput(player, tick, controls)`); it
  cannot report where it is, what it hit or when it finished.
- Time: the race advances only through `AdvanceTick()`, in exact 16 ms ticks, from tick 0 for everybody at once. (The
  countdown is the clients' presentation phase before tick 0.)
- The record: `ReplayBytes()` is a verifiable replay of the race as it was simulated (the inputs actually used, a state hash
  every 250 ticks, and the race settings), and `Snapshot()` is a state snapshot for spectators and reconnects.

## Input policy

| Situation | Result |
| --- | --- |
| Input for a tick not yet simulated, within 64 ticks ahead | `Accepted` (or `Replaced` if that tick already had one) |
| Input for a tick the server has already simulated | `Late`: refused; the player's previous input was used |
| Input more than 64 ticks ahead | `TooFarAhead`: refused, so a client cannot queue a long script |
| Unknown or negative player | `BadPlayer` |
| After the race ends | `Finished` |
| Control bits the simulation does not define | Masked off before use |
| No input for a tick | The player's previous input repeats |

Late inputs are not re-simulated: the client that predicts its own craft (prediction and rollback, see
[netcode-prototype.md](netcode-prototype.md)) sends its inputs ahead of time by about half the one-way delay in ticks and
reconciles with the server's tick messages. Server-side re-simulation (lag compensation for weapons) is possible because
snapshots exist ([simulation-state.md](specs/simulation-state.md)) and is not built.

## Tests (`HoverNetAuthorityTest`, ctest `HoverNetAuthority*`)

Run on all seven bundled tracks at **eight players**, on Linux (plain and under ASan/UBSan) and Windows x64:

1. The race the server runs equals the race its own replay reproduces, checkpoint by checkpoint.
2. Every row of the input policy above, including masked bits, and a race containing those edge cases still reproduces.
3. A missing input repeats the previous one.
4. Eight clients across a modelled network with latency, jitter and loss (120 ms with 2% loss and 250 ms with 5% loss) drive
   the server: its race still equals its replay. Over a 250 ms path, sending inputs 1 tick ahead leaves thousands of them
   late, and sending 14 ticks ahead leaves none; with enough lead the race is identical to the zero-delay race (the same hash
   on Linux and Windows).
5. A spectator that joins from a server snapshot at tick 800 matches the server's state at once and follows it tick for tick.

## Not built yet

Sockets and the protocol 3.0 framing, authenticated encryption, account/guest identity, reconnect (snapshot plus the input
log since), the server loop with observability, and the soak, load and abuse suites of the Stage 2 exit gate.
