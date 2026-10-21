# Outiva console integration plan

## Status (2026-10-21)

**Done and tested locally on Linux** (`scripts/test-web-account.py`, `scripts/test-observability.sh`; nothing deployed, nothing pushed):

- `GET /outiva-status.json` in `packaging/web/hovernet_account.py` (works without OIDC): `available` / `degraded`, 15 s public cache, CORS for `https://outiva.com` only, loopback-only status URL (`HOVERNET_STATUS_URL`, default `http://127.0.0.1:9612`), 2 s timeout, 5 s internal cache.
- New `hovernet_players_active` gauge in the server's `/metrics` (drivers in unfinished races; excludes spectators and lobby visitors).
- Adapter tests: healthy, draining, malformed/negative/missing/huge gauge, timeout, refused connection, non-loopback URLs refused, headers.
- Observability script checks the gauge; `docs/operators.md` and `CHANGELOG.md` updated; Apache proxy lines added to `packaging/web/hovernet.outiva.com.conf`.

**Still to do:**

1. ~~Web smoke suite~~ Done: `scripts/web-smoke/outiva-status.mjs` (called from `scripts/test-web-client.sh`) runs the real service against a stand-in monitoring port and checks the JSON contract, CORS and cache headers, draining and a dead port. Passed locally with node on Linux; not yet run in CI.
2. Windows: the server change is untested there (rule 6 needs the Windows runner checks before a release).
3. Deploy: build and install the new server (adds the gauge), set `status-port 9612` in `/etc/hovernet/server.conf`, install the updated `hovernet_account.py` and Apache site, enable the `/outiva-status.json` proxy, restart the account service. Until the server is updated the gauge is missing and the adapter reports `degraded`.
4. Check the live endpoint: `curl -i https://hovernet.outiva.com/outiva-status.json` (status, players, headers), and that the monitoring port is still not reachable from outside.
5. Catalogue: the sibling Outiva worktree now has a real 1280x720 gameplay WebP (125 KB) and already declares keyboard, touch, gamepad,
   single-player, online and optional Outiva-account support. It is deliberately still missing `statusUrl`: add that only after the deployed endpoint
   passes the console repository's exact-origin CORS check.
6. Release only when asked, from `main`, per `docs/releasing.md`.

HoverNet already has loopback `/health` and Prometheus `/metrics`. The integration should expose a deliberately smaller JSON view through the public web tier; the monitoring port must remain loopback-only.

## 1. Public adapter

- Add `GET /outiva-status.json` to the web/account service or reverse proxy application layer.
- Read the loopback status endpoint with a short timeout. Convert a healthy server to `available`, a draining server to `degraded`, and an unreachable authority to `degraded` if local play/web assets are still usable.
- Parse only the aggregate `hovernet_connections_open` or a more precise active-player gauge. If connections include spectators or control peers, add an explicit `hovernet_players_active` gauge before publishing the count.
- Return `{"status":"available","players":N}` with `Cache-Control: public, max-age=15` and `Access-Control-Allow-Origin: https://outiva.com`.
- Never proxy raw Prometheus output, replay results, player names or device identifiers to the console.

## 2. Tests and deployment

- Add adapter tests for healthy, draining, malformed metrics, timeout and loopback refusal.
- Extend `scripts/test-observability.sh` and the web smoke suite with the public JSON contract and CORS assertion.
- Update `docs/operators.md`; retain the current loopback bind and document that only the adapter is public.

## 3. Catalogue work

- ~~Export a 16:9 gameplay image as WebP rather than using the square cutout; target 100–180 KB.~~ Done in the sibling Outiva worktree:
  1280x720, 125 KB, captured from the real game renderer.
- ~~Confirm keyboard, touch and gamepad; single-player and online; optional Outiva account.~~ Already declared in `web/games.json`.
- Set `statusUrl` to `https://hovernet.outiva.com/outiva-status.json` after the deployed adapter passes its smoke check.

## Acceptance

- The status endpoint remains useful during graceful drain and never makes the private monitoring port public.
- Player count semantics are documented and exclude non-player connections where possible.
- Local play still launches when the online authority is degraded.
