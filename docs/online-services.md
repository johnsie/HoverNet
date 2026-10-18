# Online services basics (Stage 6a)

What the authoritative server provides for safe public play today, how moderators work, and the privacy and retention rules it
follows. Nothing here needs an account: players are identified by the display name they choose for the session. Accounts, sign-in,
ratings, matchmaking and sanctions with appeals are Stage 6b and later.

## What players can do

- **Chat** in a race (`Chat`): one line of up to 120 bytes of valid UTF-8 (no control characters), at most a burst of three and then
  one a second. Everyone in the race sees it, spectators included (spectators cannot chat).
- **Block** a name (`Block`): the server stops sending that name's messages to you; nobody else is affected. Up to 50 names.
- **Report** a name (`Report`, with a reason of up to 120 bytes): at most three a minute, never yourself. The moderators receive the
  report together with the last 15 chat lines.
- **Host, join and watch** public races through the lobby ([authoritative-race.md](authoritative-race.md)); a guest path is simply
  choosing a name. Private lobbies, parties, invitations and region choice need accounts or service discovery and are not built.

## What moderators can do

Moderators use the loopback-only console of `HoverNetAuthServer --moderation-dir DIR --admin-port PORT` through
`HoverNetModTool`. `DIR/moderators.txt` lists `name secret` per line. A session starts with a random challenge and the moderator
proves who they are with **HMAC-SHA256(secret, challenge + ":" + name)**: the secret never crosses the connection, a proof cannot be
reused in another session, and three failed attempts close the session.

| Command | Effect |
| --- | --- |
| `REPORTS`, `REPORT <id>` | list open reports; show one with its chat context |
| `RESOLVE <id> <note>` | close a report with a note |
| `MUTE <name> <minutes>`, `UNMUTE <name>`, `MUTES` | a muted player's chat is not delivered (they are told); at most 30 days |
| `KICK <name>` | removes that player's connection from their race |
| `BAN <address>`, `UNBAN <address>` | the game port refuses that address |
| `AUDIT-CHECK` | verifies the audit log's hash chain |

**Every action is authenticated and audited.** `audit.log` is append-only; each line has a sequence number, the time, who did it,
what, the details, the hash of the previous line and its own hash, so an edited, removed, inserted or reordered line is detected
(`HoverNetModTool audit-verify audit.log`). Logins, failed logins (with the address), report views, resolutions, mutes, bans, kicks
and logouts are all recorded, and so is every player report.

The console listens on 127.0.0.1 only; reach it from another machine through an SSH tunnel. Keep `moderators.txt` readable by the
server user only.

## Privacy and retention

| Data | Kept | For how long |
| --- | --- | --- |
| Display name | in memory while connected; copied into a report or the audit log only if that player is reported or sanctioned | session; reports 30 days after resolution; audit lines 180 days |
| IP address | in memory for connection limits; written to the audit log only for a moderator's failed login or a ban | connection; audit 180 days |
| Chat | not stored; the last 40 lines are held in memory so a report can include its context | 24 hours (purged hourly), then gone; kept longer only inside a report |
| Reports | reporter, target, reason, chat context | resolved reports 30 days |
| Audit log | moderation and security events | 180 days, purged hourly (the purge is itself logged) |
| Replays | track, settings, per-slot controls and hashes: **no names, addresses or chat** in the replay file itself | the newest `--replay-keep` (default 200) are kept and **offered to every player**; older ones are removed as new ones are saved |
| League log (`league.tsv`, 2.3.18) | for each race that counted: the time, the players' **device ids** (a random number each installation makes; **no IP address**) and, per scoring player, the display name and points | kept so that months and years can be shown (the winners of past months and years); an operator can delete the file or lines at any time, which resets or removes that history |
| Results tables (beside each replay) | rank, slot, laps, times and, since 2.3.14, **the display name each player typed** (so leaderboards and replay lists can show who raced). A display name is not verified: anyone can type any name until accounts exist | removed together with the replay it belongs to (the newest `--replay-keep`); an operator can delete a table, or a name from it, at any time |
| Telemetry | counters only (connections, bytes, ticks, invalid messages); no content | in memory; printed per race |

**Deletion.** A player can ask an operator to delete a report that names them or an audit entry that records their name; because the
audit log is a hash chain, the operator removes a line by purging up to it, which is recorded, rather than editing it. Replays hold
no personal data, so they need no deletion for privacy reasons. There is no account to delete today; the draft policy for the optional accounts planned for 3.0
(device-key sign-in, email only) is [account-privacy-policy-draft.md](account-privacy-policy-draft.md), awaiting legal review.

**Children.** The server collects no age, email or real name. Operators who run public servers are responsible for their own rules and
for complying with the law where they operate.

## Tests

`HoverNetModerationTest` (ctest `HoverNetModeration`): HMAC-SHA256 against the RFC 4231 test vectors; audit log tamper detection
(edit, remove, insert, reorder), persistence across restarts and purge; the console (wrong proof, unknown moderator, proof reuse
across sessions, lockout after three failures, every command); chat delivery, block, text rules, rate limit, reports with context
and their rate limit, mute and kick inside a race. `scripts/test-moderation-tcp.sh` (ctest `HoverNetModerationTcp`) does it over
real sockets with bots, a moderator and the audit file.

## Not built yet

Sign-in and accounts, privacy controls beyond blocking, parties and invitations, private lobbies and region selection, persistent
mutes across a restart, a web page for moderators, and 6b (sanctions with appeals, ratings, matchmaking, tournaments).
