# Running a HoverNet server

For people who host races for others. The authoritative server (`HoverNetAuthServer`) is a **preview**: it is tested with scripted
players and the game client, but it does not encrypt traffic yet. Read the security notes first.

## Quick start

    HoverNetAuthServer --lobby --port 9610 --replay-dir replays

Players connect with `HoverNet --auth-connect HOST:PORT` and host, join or watch races through the lobby. Without `--lobby` the server hosts
one fixed race (`--track`, `--players`, `--laps`). Everything can be put in a configuration file (`--config FILE`, see
`packaging/server/hovernet-server.conf`, which documents every option); command line options override the file.

## Security notes (read before exposing it)

- It **listens on 127.0.0.1** unless you pass `--bind`. To serve players over the internet, give it an identity (`--identity-file`) and
  `--require-encryption`: players then connect over the encrypted, authenticated session of [specs/secure-session.md](specs/secure-session.md),
  pinning the key the server prints at start-up (`server key ...`; give it to your players, who pass `--server-key`). Without encryption,
  anyone who can read the traffic can read chat and resume tokens, so keep a plain server on a trusted network. The session code has been
  tested against the RFC vectors but not independently audited.
- The moderators' console (`--admin-port`), the monitoring port (`--status-port`) and the crash reports never leave the machine. Reach them
  with an SSH tunnel. `moderators.txt` holds secrets: keep it readable only by the server's user.
- The server runs no player code, only reads the track files it is told about, and writes only to the replay, moderation and crash
  directories. A hardened systemd unit is in `packaging/server/hovernet-server.service`.
- Limits you can rely on: connections (default 64 per server, 16 per address), a per-connection input rate, ten invalid messages in ten
  seconds ends a connection, a race count cap in lobby mode, and one memory-bounded history per race.

## Options

| Option | Meaning |
| --- | --- |
| `--lobby` | many races, hosted and joined through the lobby |
| `--tracks "A,B"` | tracks players may host (default: the seven official ones) |
| `--track`, `--players`, `--laps`, `--no-weapons` | the fixed race when not in lobby mode |
| `--port`, `--bind`, `--max-connections` | network (port 0 picks a free port) |
| `--identity-file FILE`, `--require-encryption` | the server's secret key (created if missing) and refusing plain connections |
| `--limit-ticks N` | end each race after N ticks (16 ms each); free-play tracks default to 10 minutes |
| `--start-delay-ms` | countdown before tick 0 |
| `--spectator-delay-ms` | how far behind the drivers spectators see the race |
| `--replay-dir DIR` | save a replay of every race |
| `--moderation-dir DIR`, `--admin-port P` | audit log, moderators and the console ([online-services.md](online-services.md)) |
| `--status-port P`, `--crash-dir DIR` | `/health`, `/metrics`; crash reports |
| `--ws-origin ORIGINS`, `--ws-path P` | accept browsers over WebSocket from these comma-separated origins, at this path (default `/auth/`) |
| `--event-log FILE` | append one JSON object per line: `server_start`, `race_finished` (track, ticks, final hash, each player's rank, laps and times), `drain_start`, `drained`; no names or addresses |
| `--drain-timeout S` | longest a drain waits for races under way (default 600 seconds) |
| `--once` | serve one race and exit (tests) |
| `--config FILE` | read options from a file |

## Running it as a service

- Debian/Ubuntu package: `packaging/debian/build-authserver-deb.sh VERSION BUILD_DIR` builds `hovernet-server` (the server, `HoverNetModTool`,
  `HoverNetReplay`, the seven official tracks, `/etc/hovernet/server.conf`, the systemd unit, the licence as `copyright`). Installing creates
  the `hovernet` user and `/var/lib/hovernet`, enables the service and, on an upgrade only, restarts it through a drain; a fresh install is
  not started until you have reviewed the configuration. The release pipelines do not build it yet.

- systemd: copy the binary to `/usr/bin`, the configuration to `/etc/hovernet/server.conf`, the tracks to `/usr/share/hovernet/NetTarget/Tracks`
  and `packaging/server/hovernet-server.service` to `/etc/systemd/system/`; create a `hovernet` user and `/var/lib/hovernet`.
- HoverNet does not use Docker or any other container tooling, for building, testing or deployment; run the server as a service
  as above or from a terminal.


## Browsers (WebSocket)

With `--ws-origin https://your.site` the same port also accepts a WebSocket upgrade, and a browser's binary messages carry exactly the same
protocol 3.0 frames as a TCP connection (the server has one session layer, not a second protocol). The upgrade is refused unless the
`Origin` header is one of the listed origins and the path matches. The server speaks plain `ws://`: put a web server in front that
terminates TLS and proxies `wss://your.site/auth/` to the loopback port (Apache `ProxyPass` with `mod_proxy_wstunnel`, or nginx with
`proxy_set_header Upgrade`/`Connection`), and keep the server bound to loopback. Because every browser then arrives from the proxy's
address, the per-address connection cap is not applied to loopback in this mode and address bans do not reach individual browsers
(mutes and kicks by name still do); `--max-connections` still bounds the total. Tested with a scripted client over a real socket
(`HoverNetWebSocketTcp`) and a unit test with the RFC 6455 vectors and random bytes (`HoverNetWebSocket`); the game's browser
client plays on it (its address field takes `wss://your.site/auth/`; its default is the page's own site and that path), checked in headless Chrome
against a local server over `wss://` through a small test proxy that terminates TLS (`scripts/web-smoke/web-servers-screen.mjs`); Apache or nginx in
front has not been tried here. `HoverNetAbuse` throws 120 hostile connections at a live server (garbage, oversized and
truncated frames, half-open and giant upgrade requests, bad WebSocket frames) and checks it stays up, healthy and serving, including under
ASan/UBSan.

## Restarting without ending anyone's race (rolling deployment)

Send the server `SIGTERM` (`systemctl stop` or `reload`-style restarts do this). It **drains**: races under way run to their end and their
replays are saved, hosting or joining a race is refused with "the server is restarting" (reject reason 10), `/health` answers `503 draining`
so a load balancer or DNS health check stops sending players, and the process exits 0 when no race is running (after a short pause so the
last results reach the players) or after `--drain-timeout`. A second signal exits at once. Races that have not started are dropped.
Set `TimeoutStopSec` in the systemd unit above the drain timeout. To roll a pool: start the new build on a spare port, point the balancer
at it, `SIGTERM` the old one, and remove it from the pool when it exits. Tested on Linux (`HoverNetDrain`); not yet on Windows.

## Watching a server

`curl http://127.0.0.1:STATUSPORT/metrics` gives Prometheus text: uptime, races running and hosted, connections, bytes, ticks simulated,
inputs accepted and late, invalid messages, snapshots served. Alert on `hovernet_invalid_messages_total` growing fast, on
`hovernet_inputs_late_total` growing relative to inputs accepted (players' networks are too slow for their lead), and on the process
disappearing (crash reports are in the crash directory). A server costs about 0.2 to 0.4 CPU-seconds per race-minute for eight players
in a debug build ([authoritative-race.md](authoritative-race.md)); size for tens of races per core.

## Upgrades and rollback

1. Start the new version on a second port and watch `/metrics`.
2. Stop sending new races to the old one (remove it from your server list); running races finish, then stop it. A running race is never
   moved between versions.
3. Replays carry a **rules version**: a replay made by one rules version can only be verified by a build with the same rules version, and
   the game refuses others. After an upgrade that changes the rules version (see [determinism.md](determinism.md)), keep the old
   build around to verify old replays. Roll back by starting the previous build; nothing on disk needs migrating (audit logs and replays
   are append-only files).
4. Players on a different game version are told so when they connect (the rules and track checks fail with a message).

## Not provided yet

Accounts, a server list for players to browse, regional pools, an updater, signed packages.
