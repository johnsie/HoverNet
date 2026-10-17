# Network protocol 3.0 (draft)

Status: **draft for review**; the framing, the message set and the host's rules are now implemented and tested without sockets
(`NetTarget/Authority/Protocol3.*`, `RaceHost.*`, [authoritative-race.md](../authoritative-race.md)); encryption and the real transports
are not. Stage 2 implements the rest and hardens it. It builds on the implemented
[2.0 baseline](../2.0-network-protocol.md) (explicit little-endian encoding, hello negotiation, frame limits, rate limits)
and on what the [netcode prototype](../netcode-prototype.md) showed the simulation needs. Where this draft and the code differ, the code and its tests are authoritative; the differences are noted in the message list below.

## Principles

1. **Every byte is specified.** Explicit-width, little-endian fields only; no native structs, no padding, no pointers.
2. **The server is the authority and the only path.** Clients talk to RaceServer; there is no client-to-client traffic
   (players are on different networks and countries). Browser clients use the same messages over `wss://` through the
   gateway, so the protocol must not depend on UDP-only features.
3. **Inputs, not state, are what clients send.** The server runs the simulation ([ADR 0001](../adr/0001-simulation-core.md)),
   so it needs each player's control word per tick and nothing a client could forge about the outcome.
4. **Version negotiation first, and the server never trusts a length.** A new major version is a new hello; unknown messages
   are errors, not ignored.
5. **Security is part of the protocol:** authenticated encryption, replay protection, and bounded work per connection.

## Transport and framing

- **Transport:** TLS 1.3 over TCP (port 9600 for native clients; `wss://hovernet.outiva.com/race/` for browsers, where TLS
  is terminated at the web server and the gateway forwards frames unchanged). Native sessions that do not use TLS
  termination use the in-protocol secure session below.
- **Frame:** `u16 length` (little-endian, excluding the length field itself, maximum negotiated, default 1,200 bytes),
  `u8 type`, then the payload. (2.0's 3-byte packed prefix with 6-bit type and 8-bit datagram number goes away.)
- Receivers buffer partial frames and handle coalesced frames. An incomplete frame lives five seconds from its first byte.
- A frame longer than the negotiated maximum, or any parse error, closes only that connection.

## Session establishment

1. **Hello** (client, first frame, clear text): magic `HN3\0`, `u16 major = 3`, `u16 minor`, `u32 capabilities`,
   `u16 max_frame`. A server that supports only 2 can still answer a v3 hello with a 2.x reject carrying the supported range,
   so a client can fall back or tell the player to update.
2. **HelloAck** (server): accepted major.minor, negotiated capabilities and `max_frame`, a 16-byte server nonce, and the
   server's tick length (`16 ms`) and rules version (`replay-format.md: rules`). Or **Reject** with a machine-readable
   reason (`unsupported_version`, `server_full`, `banned`, `rate_limited`, `server_draining`) and a short UTF-8 message.
3. **Secure session** (native without TLS termination): ephemeral X25519 key exchange, server identity signed with a long-term
   Ed25519 key pinned in the client (and checked against the downloaded manifest), then AEAD (ChaCha20-Poly1305) with a
   per-direction 64-bit message counter as nonce. Counters never repeat, so replayed frames are rejected. Browser sessions
   rely on `wss://`; the first encrypted frame still carries the counter so replays are rejected the same way.
4. **Auth** (Stage 5): anonymous guest names in 3.0 early stages; account tokens later, as an additional frame type.

No lobby, race or gameplay frame is processed before step 2 succeeds.

## Message groups

| Group | Types | Notes |
| --- | --- | --- |
| Lobby | set name, user list, presence, chat, game list | As 2.0; text is bounded canonical UTF-8, single line, no control bytes |
| Race lifecycle | host (track, laps, weapons, max players), join by ID, join result, ready, start | Server assigns the start slot and the shared **race seed/epoch**; the start message carries the start tick and the track SHA-256 every client must hold |
| Input | `Input{ tick, repeat of last N inputs }` client to server | One `u16` control word per tick, redundantly carrying the last 16 ticks so loss does not stall (prototype default); rate limited to one frame per tick |
| Authority | `Tick{ server_tick, per-player inputs for ticks up to server_tick, state hash }` server to clients | Clients confirm their prediction; a hash mismatch triggers a resync |
| Resync | request/receive a versioned snapshot ([simulation-state.md](simulation-state.md)) | Used on join mid-race, a hash mismatch or reconnect |
| Events | lap, finish, pickup, hit, weapon fired, results | Server-generated and authoritative; clients may display them early but must reconcile |
| Spectate / replay | subscribe to a race; fetch the finished replay ([replay-format.md](replay-format.md)) | Stage 5 |
| Keep-alive / time | ping with client tick and server tick, measured round trip | Drives the adaptive input delay |

### What the code does today (message list as implemented)

| Type | Name | Direction | Payload |
| ---: | --- | --- | --- |
| 1 | Hello | client to server | magic `HN3\0`, major, minor, capabilities, max frame, player name (1 to 32 bytes of UTF-8) |
| 2 | HelloAck | server to client | version, capabilities, negotiated max frame, your player slot, players, tick length, rules version, track name and SHA-256, laps, weapons, resume token; then, only if the client set capability bit 0 (`kCapRaceId`) in Hello, a `u32` race number (the lobby's number for the race, 0 outside a lobby), so a host can tell friends which private race to join |
| 3 | Reject | server to client | reason (version, full, bad message, rate limited, not in race), short message; the connection is then closed |
| 4 | Start | server to client | countdown in ms before tick 0 |
| 5 | Resume | client to server | the resume token from the HelloAck (replaces Hello when rejoining a running race; answered with a HelloAck for the same slot) |
| 10 | Input | client to server | first tick, 1 to 32 control words (the newest are for the future, the older repeat earlier ones for loss) |
| 11 | Tick | server to client | tick number, state hash after it, the control word each player used (so a client or spectator can re-simulate the race exactly) |
| 12, 13 | SnapshotRequest, SnapshotChunk | client to server, server to client | request id; chunks of up to 900 bytes with index, count and the tick |
| 14, 15 | Ping, Pong | both | client tick and a token; the server adds its tick (drives the adaptive input delay) |
| 24 | Spectate | client to server | the same as Hello; asks to watch the race instead of driving (answered with a HelloAck, a keyframe snapshot and the delayed Tick stream) |
| 20 to 23, 25 | ListRaces, RaceList, HostRace, JoinRace, SpectateRace | lobby | list open races; host a race (track, players 1 to 8, laps, weapons; one player is a time trial); join or watch one by id. HostRace, JoinRace and SpectateRace may end with a code (6 to 16 characters): a race hosted with one is private, unlisted, and joined or watched only with it (a wrong code is answered as "no such race"; 30 wrong guesses lock the race) |
| 16 | Result | server to client | ticks, final hash, per player: rank, finished, laps, finish and best lap times |

The encrypted session is implemented, with a simpler key scheme than this draft's first sketch (a pinned server key and X25519, no signatures): see [secure-session.md](secure-session.md). Not in the code yet: the lobby and race
list messages (still the 2.0 relay's), account tokens, and a separate Events group (lap, pickup and hit events are derived
from the replayed simulation; the Result is the only authoritative announcement so far). The Resync of the draft is
SnapshotRequest and SnapshotChunk.

## Timing and prediction

- Tick 16 ms. Clients simulate locally with their own input applied immediately and the others' predicted as "same as last
  known" (the prototype's rule), rolling back by re-simulation when the authoritative inputs differ.
- **Input delay** is chosen per client from the measured round trip, about half the one-way delay in ticks (4 ticks at a
  120 ms round trip), capped so steering never feels late (the prototype shows this removes nearly all visible correction).
- The server accepts an input for tick *t* only within a window around its own tick; earlier is late (applied as a repeat
  of the previous input, and the client is told), later than the window is rejected.

## Limits and abuse handling (carried from 2.0, tightened)

- Per connection: frame size cap, a token bucket per message class (input, chat, lobby commands), and a hard cap on
  unauthenticated connections per address.
- More than ten invalid messages in ten seconds closes the connection (2.0 behaviour kept). A client whose reported hash
  disagrees with the server's more than a configured number of times is dropped from the race, not the server.
- Server-to-client message types received from a client are errors. Gameplay messages before joining a race are errors.
- Logs record reason and connection identity and never untrusted text as a format string.

## Compatibility

- 2.x clients keep using 2.0 against a 2.x server; a 3.0 server can run a 2.0 front end for the migration period (Stage 5 rolls
  back the same way). Replays and tracks are versioned independently of the protocol.
- Unknown capability bits are ignored; unknown message types are errors.

## Open questions

1. Key management: pinned server key in the client vs. WebPKI only for browsers and the gateway. Proposal: pinned key for
   native, WebPKI for the gateway.
2. One frame type per tick for every player's inputs versus delta-coded inputs (a measurable bandwidth question at 8
   players: 8 players x 2 bytes x 62.5 ticks/s is about 1 KB/s each way before headers).
3. Does UDP return as an optional fast path for native clients? Not in 3.0: browser parity and simplicity outweigh the gain.
4. Reconnect after a dropped connection: resume token versus rejoin-by-snapshot.

## Fuzzing and tests Stage 2 must add

Decoder fuzzing for every frame type and the handshake, fragmented and coalesced frames, a malformed-nested-length corpus,
replay-attack tests (duplicate and reordered counters), downgrade attempts (v3 hello answered with a v2 accept), slow-loris
frames, and the load, soak and abuse runs through the WebSocket gateway.


**Lobby chat (capability bit 1, `kCapLobbyChat`).** A client that sets it in Hello gets `LobbyUsers` (type 34: `u16` total, `u8` count, up to 30 names; sent whenever who is in the lobby changes) and may send Chat / receive ChatMessage while it is still in the lobby (before it hosts, joins or watches a race); the server rate limits lobby chat (a burst, then one a second) and applies mutes. A client that does not set the bit never sees the new message type.
