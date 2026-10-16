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

## The race host and protocol 3.0 on the wire

`Protocol3.*` is the framing and message set of the [protocol draft](specs/protocol-3.0-draft.md) as code, and `RaceHost.*`
hosts one authoritative race behind it. Neither has sockets or a clock: a transport (TCP, the WebSocket gateway, or the
in-memory pipes of the tests) hands bytes in and takes bytes out, and the caller supplies the time.

- **Frames:** `u16 length | u8 type | payload`, little-endian, at most 1,200 bytes (a client may negotiate a smaller maximum).
  Partial and coalesced frames work; a zero or oversize length is a framing error that closes the connection.
- **Messages:** Hello / HelloAck / Reject, Start, Input (up to 32 control words, redundant by design), Tick (the inputs every
  player used at a tick, plus the state hash after it), SnapshotRequest / SnapshotChunk, Ping / Pong, Result. Decoders are
  strict (exact length, counts checked before allocation, UTF-8 text rules: no control bytes, overlong forms, surrogates or
  truncated sequences).
- **Host rules:** a protocol major other than 3 is rejected with a message; nothing but Hello is accepted first; one player slot
  per connection and a full server or a started race refuses newcomers; inputs are token-bucket limited (125 frames per second,
  burst 16); ten invalid messages in ten seconds close the connection; snapshot requests are limited to six a minute; a
  connection that does not say Hello within five seconds is closed; a race can have a time limit and then broadcasts its Result
  (ranks, laps, times, final hash) once.
- **Clients need nothing but the stream.** In `HoverNetProtocolTest` eight clients and a spectator rebuild the whole race from
  the Tick messages alone and match the server's state hash at every tick, over a path with 60 ms each way; a snapshot fetched
  through the protocol (about 3.2 KB in four chunks for eight players) restores into a fresh copy of the race.

Tests (`HoverNetProtocolTest`, ctest `HoverNetProtocol`): framing and every message round trip, every truncation and a trailing
byte refused, the text rules, the eight-player race above, the result broadcast, and the abuse cases: wrong version, input
before Hello, oversize and zero-length frames, malformed Hello, bad text, a flood of invalid messages (and the same number spread
over time, which is fine), a full server, a late joiner, an input flood that closes only the flooder, the Hello timeout, the
snapshot limit, and 3,000 connections of random bytes. Passes on all seven bundled tracks at eight players on Linux (also under
ASan/UBSan) and Windows x64.

## A dedicated server over TCP

`HoverNetAuthServer` wraps `RaceHost` in a one-thread, non-blocking TCP server and `HoverNetAuthBot` is a scripted player
(`NetTarget/LinuxClient/AuthServer.cpp`, `AuthBot.cpp`, `AuthSocket.h`: POSIX sockets and Winsock). It hosts one race at a time and starts
a new one when the race ends (`--once` for tests). Options: `--track`, `--players`, `--laps`, `--port` (0 picks a free one),
`--limit-ticks`, `--start-delay-ms`, `--max-connections`, `--no-weapons`.

- **It listens on 127.0.0.1 unless `--bind` says otherwise.** Exposing it is a deliberate act: until the encrypted session
  exists it belongs behind TLS termination or on a trusted network. It also caps connections overall (64) and per address (16),
  drops a client that stops reading (1 MB of unsent output), and closes anything that fails the host's rules.
- **`scripts/test-authserver.sh`** (ctest `HoverNetAuthServerTcp`) starts a server and eight bots on the loopback interface; each
  bot sends its input 12 ticks ahead every 16 ms, rebuilds the race from the Tick stream and checks the state hash of every tick,
  and must receive the Result. It passes on Linux (also under ASan/UBSan) and on Windows x64 over Winsock. Running it with a
  large tick count (for example `... 8 200000`, about an hour) is the soak test of the Stage 2 exit gate; it is not in CI.

## Not built yet

Many races at once and the lobby around `RaceHost`, the WebSocket gateway, authenticated encryption and
server identity, account/guest identity, reconnect (snapshot plus the Tick log since), observability (health, metrics,
tracing, crash reports), compression and bandwidth budgets, and the soak, load and abuse suites of the Stage 2 exit gate.
