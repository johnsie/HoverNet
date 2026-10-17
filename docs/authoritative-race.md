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

## The predicting client

`PredictingClient` (transport-free, `NetTarget/Authority/PredictingClient.*`, built on `RaceWorld`) is the client half of the design in
[netcode-prototype.md](netcode-prototype.md), now using the real snapshots instead of replaying from the start:

- a **confirmed world** advances only with the server's Tick messages (the inputs every player used), so it is exactly the server's
  race, and every Tick's state hash is checked against it (a mismatch means "resynchronise from a snapshot");
- a **predicted world** is what the player sees. Each frame it is restored from a snapshot of the newest confirmed state and run
  forward to the tick on screen with the player's own inputs (known) and every other player's last confirmed input (a guess). A
  wrong guess is repaired by the next frame's rebuild; nothing has to be unwound.

`HoverNetPredictionTest` runs it against a live host with eight players over a delayed path and compares each frame's prediction
with the server's true positions at that tick (error = distance on the ground, in millimetres; a craft is several metres long):

| Track | One way | Own craft, predicted: mean (max) | Other craft, predicted: mean (max) | Drawing the confirmed state: mean |
| --- | --- | --- | --- | --- |
| ClassicH | 60 ms (120 ms round trip) | 0 mm (0) | 3 mm (1.5 m) | 1.8 m to 2.6 m |
| ClassicH | 125 ms (250 ms round trip) | 6 mm (1.0 m) | 16 mm (1.3 m) | 4.4 m to 4.7 m |
| The Alley2 | 125 ms | 6 mm (1.0 m) | 61 mm (11.9 m) | 3.9 m to 6.8 m |
| Tidal Causeway | 125 ms | 14 mm (1.4 m) | 19 mm (4.5 m) | 4.5 m to 5.1 m |
| ClassicH | 200 ms (400 ms round trip) | 24 mm (2.4 m) | 101 mm (5.9 m) | 4.3 m to 6.9 m |

At the Stage 0 targets the prediction is within a few centimetres on average; the maxima are the moments another craft's
unpredicted input changes a collision or a weapon hit. Every tick's hash matched on every run. Cost: one snapshot per confirmed
tick, and one restore plus 5 to 20 ticks of simulation per frame (the whole eight-player 1,500-tick run takes about 0.7 s).

## The game client on an authoritative server

    HoverNet --auth-connect HOST:PORT

(`HoverNetGame2Player` on Linux; native builds only. The server now accepts WebSocket, see operators.md, but the browser client does not use it yet.) The game connects, says Hello with
the name from its settings, waits for the race to fill and start, checks that its copy of the track is the server's (by SHA-256) and
that the game rules match, and then plays: your controls go to the server a few ticks ahead of where the race is, the server's Tick
messages keep an exact copy of the race, and what is drawn is that copy plus a prediction run forward from it (the lead follows the
measured round trip). It uses the game's normal renderer, HUD, craft and camera; the other craft are simulated by the client
exactly as the server simulates them.

- **How the prediction is drawn.** Each frame the newest confirmed snapshot is restored into the game's own rendered session and
  stepped forward with your inputs and the others' last confirmed ones (`RenderedWorld`, `PredictingClient::PredictOn`).
- **Reconnect.** If the connection drops, the game reconnects with the resume token once a second for 15 seconds, asks for a
  snapshot and carries on; the craft kept going with its last input meanwhile.
- **Resynchronisation.** A Tick whose state hash does not match, or one out of order, makes the client ask for a snapshot.
- **Test modes:** `--auth-script` (the scripted input of the test tools instead of the keyboard), `--auth-verify` (also check every
  frame that restoring the confirmed snapshot into the rendered session reproduces the confirmed state exactly, print a summary and
  leave when the Result arrives) and `--auth-drop N` (close the connection after N ticks to exercise the reconnect).

`scripts/test-authclient.sh` (ctest `HoverNetAuthClient`, `HoverNetAuthClientReconnect`) starts a server, seven scripted bots and the
real game client in a headless window: the client verifies every Tick's hash and about 230 restore checks per race with no
failures, on four tracks at eight players, with a forced disconnect and reconnect, on Linux (also under ASan/UBSan).

## Many races: the lobby

`LobbyServer` (`NetTarget/Authority/LobbyServer.*`, transport-free like `RaceHost`) puts many authoritative races behind one door, and
`HoverNetAuthServer --lobby [--tracks "ClassicH,The River"] [--replay-dir DIR]` serves it over TCP.

- A connection says Hello and receives the list of open races (id, track, players joined of players wanted, laps, weapons,
  started). It then **hosts** a race (`HostRace`: a track the operator allows, 2 to 8 players, 1 to 10 laps, weapons on or off) or
  **joins** one by id (`JoinRace`). From then on its frames go to that race's host unchanged and the conversation is exactly the
  single-race one (HelloAck, Start, Tick, Result).
- A returning player sends `Resume` with the token instead of Hello and is routed to the race that issued it.
- Races are independent: each has its own simulation, limits and replay. Two races running side by side end in exactly the state a
  standalone race with the same inputs ends in. Finished races linger 15 seconds (late Result, reconnect) and are removed;
  abandoned races that never started are removed too; the server caps races (16) and connections (256).
- Refused with a reason: a track the operator does not allow (including names that try to leave the track directory), bad player or
  lap counts, an unknown, full or already running race, an old protocol version, a request before Hello, a flood of lobby requests, a
  connection that never says Hello, and too many races.

Tests: `HoverNetLobbyTest` (in memory, ctest `HoverNetLobby`, also as WebAssembly) and `scripts/test-lobby-tcp.sh` (ctest
`HoverNetLobbyTcp`): nine scripted players host and join three races on three tracks over real TCP; every player verifies every tick
and the server saves three replays. Linux and Windows x64.

## Spectating

A spectator watches a running race **a fixed delay behind the drivers** (10 s by default, `--spectator-delay-ms`), which protects
competitive integrity. It sends `Spectate` (a Hello that asks to watch; through the lobby, `SpectateRace` with the race id) and gets
the race details, then the newest **keyframe snapshot** not newer than "now minus the delay" (the server keeps one every 250 ticks and
every Tick it has sent), then the Tick stream from the keyframe on; a spectator holds nothing newer than the delay allows, and the
delay keeps running after the race ends so the last Ticks and the Result still arrive. Spectators cannot drive (their inputs count as
invalid), cannot join before the race has started, are capped (64), and can ask for a fresh keyframe if they fall out of step.
Drivers are unaffected.

- Tests: `TestSpectator` in `HoverNetProtocolTest` (joins at tick 700 with a 160-tick delay, starts from keyframe 500, follows to the
  end with every hash matching, receives the Result), `scripts/test-spectator-tcp.sh` (a spectator bot joins a running race over TCP,
  directly and through the lobby) and the game client itself (`HoverNet --auth-connect HOST:PORT --spectate [--spectate-player N]`,
  ctest `HoverNetAuthClientSpectate`): the client draws the confirmed state with the normal renderer from the chosen craft's view.

## Bad networks

`HoverNetNetProxy` sits between players and the server and adds delay, jitter, a bandwidth limit, periodic freezes (a loss burst:
nothing moves, then everything arrives at once) and disconnects. Because the paths are TCP, a bad network shows up as lateness, bursts
and stalls, which is what loss and reordering look like to the game. `scripts/test-network-conditions.sh` (ctest
`HoverNetNetworkConditions`) races scripted players through 60 ms each way with jitter (eight players), 150 ms each way with 40 ms of
jitter, a 500 ms freeze every three seconds, and a 48 kbit/s pipe, and requires every player to follow every tick exactly. Then it puts
the real game client behind a proxy that hangs up on it every 14 KB (about every six seconds): the client reconnects with its token,
resynchronises from a snapshot and still verifies every tick. (The UDP-style conditions, true loss and reordering of datagrams, are
exercised by the in-memory prediction model in [netcode-prototype.md](netcode-prototype.md).)

This harness found a real defect: a freeze followed by the burst of queued inputs looked like a flood and got players disconnected. The
input limit now allows a second's burst, drops excess frames (inputs repeat each other, so nothing is lost) and only ends a connection
after a long unbroken run of them.

## Running it: observability, load and soak

- **Status port.** `--status-port P` opens a loopback-only HTTP port: `GET /health` answers `ok`, `GET /metrics` answers Prometheus text
  (uptime, races running and hosted, connections, bytes, ticks simulated, inputs accepted and late, invalid messages, snapshots served).
  Scrape it from a monitoring agent on the same machine or through a tunnel.
- **Crash reports.** `--crash-dir DIR` installs a handler that writes `crash-<time>.txt` (version, uptime, signal or exception code and,
  on Linux, a stack trace) when the server dies. Nothing is sent anywhere. `--crash-test` crashes on purpose for the test.
- **Load and soak** (`HoverNetLoadTest races players race-minutes rounds`, ctest `HoverNetLoad`): races of scripted players hosted through
  the lobby on a virtual clock, faster than real time, round after round. It checks nobody is refused or sent invalid messages, finished
  races are removed, sampled replays re-simulate to their recorded final hash, and memory does not grow. Measured on the development
  machine in a debug build: 0.2 to 0.4 s of CPU per race-minute for 8 players, i.e. one core could host 150 to 280 eight-player races in
  real time; 9 rounds of 8 races x 8 players left memory flat at 13.8 MB after the first round; 16 races x 8 players x 3 rounds ran
  clean. Passes on Linux and Windows x64. This does not replace the multi-day real-time soak of the Stage 2 exit gate (run
  `scripts/test-authserver.sh ... 8 <large tick count>` or the load test with many rounds for that).
- **Real-time soak** (`scripts/soak-authserver.sh BUILD MINUTES LOG`): one lobby server and round after round of eight-player ClassicH races
  (3,000 ticks, 50 seconds each) played by scripted bots over real TCP, checking every round that each bot saw its race through with no
  divergence, and sampling the server's resident memory, connections, races running and replay files. **Run on 17 October 2026 (Linux,
  x86-64, 8 cores, build of commit `c29d286`, under `nice` while other work and tests ran on the same machine): 60 minutes, 69 rounds
  (69 races, 552 player connections), 0 bad rounds, server still healthy at the end. Resident memory was 5.6 MB after the first round and
  2.6 MB after the last (between 2.6 and 5.8 MB throughout, no upward trend); no connections left open; one finished race listed at a time;
  69 replay files written.** That is one hour with scripted players on one track, not the days the exit gate asks for, and it does not cover
  browsers, many simultaneous lobbies or real networks; the multi-day run remains open.

## Reconnect, tampering, bandwidth and metrics

- **Reconnect.** The HelloAck carries a random resume token for the player's slot. A player who drops keeps the slot (the craft
  carries on with its last input); a new connection sends `Resume(token)` instead of Hello, gets the slot back, asks for a
  snapshot and follows the Tick stream from the snapshot's tick, matching the server's state hash on every tick
  (`TestReconnect`). A wrong token, or a resume before the race starts, closes the connection; the latest connection holding the
  token replaces an earlier one. Tokens come from a seeded generator (`HostConfig.tokenSeed`); a production server must seed it
  from the operating system, and until the session is encrypted the token is only as private as the connection.
- **Tampering.** Player 1 of a two-player race sends everything it should not: a forged Tick, Result, HelloAck and Start, an
  input 1,000 ticks ahead, inputs for ticks already simulated, an unknown type, and undefined control bits. All of it is
  counted as invalid and ignored, and the official state hash, results and replay (byte for byte) are exactly those of the honest
  run with the same legitimate inputs (`TestTampering`). A client can change the outcome only through its own control words for
  future ticks, which is the game.
- **Bandwidth budget** (eight players, measured in the test): the Tick stream costs 2.0 KB/s per player down, a player's inputs
  2.8 KB/s up (each message repeats the last 16 inputs, so loss never stalls); the test fails above 3 KB/s and 4 KB/s. A snapshot
  for eight players is about 3.2 KB. Delta coding of the Tick stream and trimming the input redundancy are the obvious next
  savings; neither is needed to meet the budget.
- **Metrics.** `RaceHost::GetMetrics()` counts connections, bytes and frames each way, invalid messages, rejected connections,
  ticks, inputs accepted, late and too far ahead (only a message's newest input is judged, since older ones repeat on purpose),
  snapshots served and Tick bytes; `HoverNetAuthServer` prints them when a race ends.

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

## How smooth the picture is (measured 2026-10-17)

The simulation runs in whole 16 ms ticks and frames do not fall on tick boundaries, so a client that shows whole ticks makes the craft's
on-screen speed change from frame to frame. The client draws the craft between the current tick and the next (it replays the prediction to
both and blends by how far the clock is into the tick), which single player, with its continuous clock, never needed. Measured with
`HOVERNET_TEST_JITTER=1` (the real client, a scripted race, a headless window at about 30 frames a second; the number is how much the
craft's on-screen speed, distance per millisecond, changes from one frame to the next, as a percentage of its average):

| Case | Whole ticks | Between ticks |
| --- | --- | --- |
| Your craft alone on a clean link | 14.3% | 4.0% |
| Your craft alone, 60 +- 40 ms jitter | 15.7-18.6% | 7.3-7.9% |
| Your craft with a second player, clean link | not measured | 7.2% |
| Your craft with a second player, 60 +- 40 ms jitter | not measured | 9.4% |
| The other player's craft, clean link | not measured | 6.5% |
| The other player's craft, 60 +- 40 ms jitter | not measured | 13.3% |

`scripts/test-smooth-motion.sh` fails above 12% for your own craft. **Tried and not kept:** the other player's craft is predicted from
their last known input, so a new input corrects it in one step (up to about 1.6 m on one frame in these runs). A filter that spread the
correction over a few frames (low-passed per-frame movement plus a slow pull to the prediction) was measured three times each way at
60 +- 40 ms of jitter: the other craft's speed change averaged 11.1% with it and 10.5% without, run-to-run noise being about 2 points, so it
bought nothing measurable and was removed rather than risk added lag. A better approach (a decaying visual offset from a second
prediction with the previous state) is untried. The numbers are from one Linux machine with no display, not from a browser or a real
internet connection; the remote-craft figures in the table vary by a few points between runs.

## Not built yet

- Browser checks still open: Apache or nginx in front (a local test proxy over wss:// with several players, spectating and typed chat is checked in headless Chrome) and phones. (The browser client itself exists: it speaks protocol 3.0 over a WebSocket and has the servers screen.)
- Accounts or any identity beyond a session name (a device-key design was proposed; the owner has not decided, see the roadmap).
- Distributed tracing (counters, a status port, an event log and crash reports exist), delta coding and compression of the Tick stream.
- The multi-day soak of the Stage 2 exit gate: `scripts/soak-authserver.sh` runs a real-time soak of any length (results below or in the
  release notes say how long it was actually run); regional pools; and the 2.0 fallback path in the client.

Built since this page was first written, and described elsewhere: the lobby and private races, time trials, encrypted sessions with a pinned
server key, the WebSocket transport, a graceful drain for rolling restarts, the desktop screen for choosing a server, and chat on these servers.
