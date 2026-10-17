# Protocol compatibility policy

Status: proposed 17 October 2026 for the 3.0 line. It applies to [protocol 3.0](protocol-3.0-draft.md) and to the 2.0 relay protocol it replaces.

## Versions

- A protocol version is `major.minor`, carried in Hello and HelloAck. The server accepts a Hello whose major equals its own and whose
  minor is any value; both sides then act on the lower minor.
- **Minor changes are additive only**, and only in these ways, all already used in 3.0:
  1. a new optional **trailing field** on an existing message, which an old receiver never sees because senders include it only when
     asked to (private-race codes work this way: a message without the field is an ordinary public one);
  2. a new **reject reason** (clients show the message text and do not switch on the number; reason 10, "server draining", was added so);
  3. a new **message type or behaviour behind a capability bit** that both sides set in Hello/HelloAck. Unknown message types without
     such a bit remain errors, and unknown capability bits are ignored.
- **Anything else is a major change**: removing or reordering fields, changing a meaning, changing framing, or a change to the simulation
  that alters results (that one also bumps the *rules version* and regenerates the golden files, see [determinism.md](../determinism.md)).
  A major change is a new hello and a new port or path, never a quiet reinterpretation.
- Receivers are strict: a frame that does not decode exactly is an invalid message (it counts toward the connection's limit). There
  is no "ignore the extra bytes" mode, which is why additions must be negotiated or optional-trailing as above.

## Supporting several versions

- A server supports the **current major and the previous one** for at least **two minor releases or six months, whichever is longer**
  after the new major ships. The retirement date is announced in the changelog and in the server's reject message at least one release
  before it happens, and the server logs how many connections still use the old version (the event log and metrics).
- A 3.0 client may fall back to a 2.0 relay for the same period: it tries 3.0 first and uses the relay only when the server answers a
  3.0 Hello with a reject or with bytes that are not a 3.0 frame. The reverse is refused: a 2.0 client reaching a 3.0 server gets the
  connection closed (3.0 framing is not 2.0 framing), so servers publish 2.0 and 3.0 on different ports and the web gateway on different
  paths (`/race/` for 2.0, the WebSocket path of the authoritative server for 3.0).
- Replays are versioned separately ([replay-format.md](replay-format.md)): a replay is playable only by builds with the same rules
  version, and the viewer says so instead of showing a wrong race.

## Emergency shutdown

If a protocol flaw is found, the server can refuse a version with a reject whose message tells the player what to update. This is a
config change plus a drain ([operators.md](../operators.md)), so no race in progress is cut short.

## Status

The negotiation and strictness are implemented and tested (`HoverNetProtocol`, `HoverNetLobby`, the fuzzers). The two-version window,
the 3.0-to-2.0 fallback in the client, and the "old version" counters are policy only: 3.0 has not shipped.
