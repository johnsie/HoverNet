// The optional Outiva sign-in in the real built web client (packaging/web/web-pre.js): the page asks /account/me and applies the account.
//   node web-account.mjs <web-dir>
// Serves the build with a stand-in account service and checks, in headless Chrome with a fresh profile per case: a signed-in account is shown and
// its name applied; a saved account name wins; a guest is unchanged; a hung service never holds the game back; a changed name is saved on
// pagehide. Exits 0 on success, 1 on a failed check, 77 without Chrome.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const webDir = process.argv[2];
if (!webDir) { console.error('usage: node web-account.mjs <web-dir>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']
  .find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const vhost = fs.readFileSync(new URL('../../packaging/web/hovernet.outiva.com.conf', import.meta.url), 'utf8');
const csp = (/Header always set Content-Security-Policy "([^"]+)"/.exec(vhost) || [])[1];
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

let mode = 'guest';      // guest | signed-in | saved | hang
const puts = [];
const server = http.createServer((req, res) => {
  const pathname = req.url.split('?')[0];
  if (pathname === '/account/me') {
    if (mode === 'hang') return;   // never answers
    if (mode === 'guest') { res.writeHead(204); res.end(); return; }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ sub: 's1', name: 'Pat Racer', preferred_username: 'pat', email: 'pat@example.com', callsign: mode === 'saved' ? 'Pat R' : '' }));
    return;
  }
  if (pathname === '/account/callsign' && req.method === 'PUT') {
    let body = ''; req.on('data', (c) => { body += c; }); req.on('end', () => { puts.push(body); res.writeHead(204); res.end(); });
    return;
  }
  const name = pathname === '/' ? '/index.html' : pathname;
  const file = path.join(webDir, path.normalize(name).replace(/^(\.\.[/\\])+/, ''));
  if (!file.startsWith(path.resolve(webDir)) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream',
    'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Content-Security-Policy': csp });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

async function session(caseMode, body) {
  mode = caseMode;
  const port = 9400 + Math.floor(Math.random() * 500);
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-account-'));
  const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle', '--use-angle=swiftshader',
    '--enable-unsafe-swiftshader', `--user-data-dir=${profile}`, '--window-size=844,390', 'about:blank'], { stdio: 'ignore' });
  try {
    let targets;
    for (let i = 0; i < 60 && !targets?.length; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); } }
    const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
    await new Promise((r) => (ws.onopen = r));
    let id = 0; const pending = new Map();
    ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); } };
    const send = (method, params = {}) => new Promise((r) => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params })); });
    const evalJs = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
    await send('Runtime.enable'); await send('Page.enable');
    await send('Page.navigate', { url: base });
    const started = Date.now();
    const waitFor = async (fn, ms) => { for (const end = Date.now() + ms; Date.now() < end;) { if (await fn()) return true; await sleep(400); } return false; };
    const frames = () => evalJs("(window.Module&&Module.ccall)?Module.ccall('hovernet_frames','number',[],[]):-1");
    const running = await waitFor(async () => (await frames()) >= 1, 120000);
    await body({ evalJs, waitFor, running, startedMs: Date.now() - started });
  } finally { try { chrome.kill(); } catch (e) {} await sleep(800); try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {} }
}
const username = "(function(){try{return FS.readFile('/home/web/.config/hovernet/username',{encoding:'utf8'}).trim()}catch(e){return null}})()";
const label = "document.getElementById('outiva-account').textContent";

await session('signed-in', async ({ evalJs, running }) => {
  check(running, 'signed in: the game starts');
  check(await evalJs(label) === 'Outiva account: Pat Racer', 'signed in: the page shows the Outiva account');
  check(await evalJs(username) === 'pat', 'signed in, no saved name: the display name starts from the Outiva username');
  // the player renames themselves in the game; leaving the page remembers it for the account
  await evalJs("FS.writeFile('/home/web/.config/hovernet/username','Speedy\\n'); window.dispatchEvent(new Event('pagehide')); 0");
  await sleep(1500);
  check(puts.length === 1 && JSON.parse(puts[0]).callsign === 'Speedy', `a changed name is saved for the account (${puts.join(' ')})`);
});
await session('saved', async ({ evalJs, running }) => {
  check(running, 'saved name: the game starts');
  check(await evalJs(username) === 'Pat R', 'a name saved with the account is applied');
});
await session('guest', async ({ evalJs, running }) => {
  check(running, 'guest: the game starts');
  check((await evalJs(label)).startsWith('Guest play'), 'guest: the page says guest play');
  check(await evalJs(username) === null, 'guest: no name is invented');
});
await session('hang', async ({ evalJs, running, startedMs }) => {
  check(running, `a hung account service does not stop the game (started after ${Math.round(startedMs / 1000)} s)`);
  check((await evalJs(label)).startsWith('Guest play'), 'a hung account service: guest play');
});
server.close();
console.log(failures ? `${failures} check(s) failed` : 'web account test passed');
process.exit(failures ? 1 : 0);
