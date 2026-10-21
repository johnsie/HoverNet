// The public status for the Outiva games console, end to end: starts the real account service (packaging/web/hovernet_account.py, guest-only) against a
// stand-in loopback monitoring port and checks, over HTTP as the console's page would ask, the JSON contract, the CORS and cache headers, a draining
// server, a dead monitoring port, and that nothing from /metrics leaks through.
//   node outiva-status.mjs
// Exits 0 on success, 1 on a failed check, 77 without Python.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';

const python = ['python3', 'python'].find((p) => spawnSync(p, ['-c', '']).status === 0);
if (!python) { console.log('SKIP: no Python installed'); process.exit(77); }
const script = new URL('../../packaging/web/hovernet_account.py', import.meta.url).pathname;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

let health = 200, metrics = '# TYPE hovernet_players_active gauge\nhovernet_players_active 5\nhovernet_connections_open 9\n';
const monitor = http.createServer((req, res) => {
  const [code, body] = req.url === '/health' ? [health, health === 200 ? 'ok\n' : 'draining\n'] : req.url === '/metrics' ? [200, metrics] : [404, ''];
  res.writeHead(code); res.end(body);
});
await new Promise((r) => monitor.listen(0, '127.0.0.1', r));
const accountPort = 9700 + Math.floor(Math.random() * 200);
const service = spawn(python, ['-I', script], { stdio: 'ignore', env: { PATH: process.env.PATH, HOVERNET_ACCOUNT_PORT: String(accountPort),
  HOVERNET_STATUS_URL: `http://127.0.0.1:${monitor.address().port}`, HOVERNET_OUTIVA_ORIGIN: 'https://outiva.com' } });
const ask = async () => { const r = await fetch(`http://127.0.0.1:${accountPort}/outiva-status.json`, { headers: { Origin: 'https://outiva.com' } }); return { r, text: await r.text() }; };
try {
  let first;
  for (let i = 0; i < 40 && !first; i++) { try { first = await ask(); } catch (e) { await sleep(250); } }
  if (!first) { check(false, 'the account service started'); }
  else {
    check(first.r.status === 200 && JSON.stringify(JSON.parse(first.text)) === '{"status":"available","players":5}', 'a healthy server: {"status":"available","players":5}');
    check(first.r.headers.get('access-control-allow-origin') === 'https://outiva.com', 'CORS allows only https://outiva.com');
    check(first.r.headers.get('cache-control') === 'public, max-age=15', 'Cache-Control: public, max-age=15');
    check((first.r.headers.get('content-type') || '').startsWith('application/json'), 'served as JSON');
    check(!/hovernet_|connections/.test(first.text), 'no raw metrics are passed on');
    health = 503; await sleep(5200);
    check(JSON.parse((await ask()).text).status === 'degraded', 'a draining server is degraded');
    health = 200; monitor.close(); monitor.closeAllConnections(); await sleep(5200);
    check(JSON.stringify(JSON.parse((await ask()).text)) === '{"status":"degraded","players":0}', 'an unreachable monitoring port is degraded with 0 players');
  }
} finally { service.kill(); monitor.close(); }
process.exit(failures ? 1 : 0);
