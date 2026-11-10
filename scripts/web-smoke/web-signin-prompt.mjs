// The sign-in gate before the main menu on the browser build, in headless Chrome against a stand-in for the page's account service:
//   - a guest who can sign in (/account/me answers 204 with X-HoverNet-Accounts: on): the gate offers SIGN IN WITH OUTIVA and PLAY AS A GUEST before the main menu;
//     SIGN IN opens /account/login (the stand-in records the request); PLAY AS A GUEST leads on to the main menu;
//   - signed in (200 with a callsign): no gate, and the callsign is saved as the player's name;
//   - no accounts here (204 with X-HoverNet-Accounts: off): no gate.
//   node web-signin-prompt.mjs <web-dir>        (the folder holding HoverNet.html/.js/.wasm/.data)
// Exits 0 on success, 1 on a failed check, 77 without Chrome. HN_KEEP_PICTURES=1 keeps the screenshots.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const webDir = process.argv[2];
if (!webDir) { console.error('usage: node web-signin-prompt.mjs <web-dir>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-signin-pics-'));

let mode = 'guest'; const loginHits = [];   // 'guest' | 'signedin' | 'off'
const server = http.createServer((req, res) => {
  const pathname = req.url.split('?')[0];
  if (pathname === '/account/me') { if (mode === 'signedin') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify({ sub: 'x', callsign: 'SkyRacer' })); } else { res.writeHead(204, { 'X-HoverNet-Accounts': mode === 'off' ? 'off' : 'on' }); res.end(); } return; }
  if (pathname === '/account/login') { loginHits.push(Date.now()); res.writeHead(200, { 'Content-Type': 'text/html' }); res.end('<p>sign in</p>'); return; }
  const file = path.join(webDir, path.normalize(pathname === '/' ? '/HoverNet.html' : pathname).replace(/^(\.\.[/\\])+/, ''));
  if (!file.startsWith(path.resolve(webDir)) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

let chromePort = 0;
async function session(label, fn) {
  const port = 9300 + Math.floor(Math.random() * 500); chromePort = port;
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-signin-'));
  const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
    `--user-data-dir=${profile}`, '--window-size=1024,768', 'about:blank'], { stdio: 'ignore' });
  try {
    let targets;
    for (let i = 0; i < 60 && !targets?.length; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); } }
    const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
    await new Promise((r) => (ws.onopen = r));
    let id = 0; const pending = new Map(); const consoleLines = [];
    ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); } if (d.method === 'Runtime.consoleAPICalled') consoleLines.push(d.params.args.map((a) => a.value ?? a.description ?? '').join(' ')); };
    const send = (method, params = {}) => new Promise((r) => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params })); setTimeout(() => { if (pending.has(n)) { pending.delete(n); r({ timeout: true, data: '' }); } }, 30000); });
    const click = async (x, y) => { for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased']) { await send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1 }); await sleep(120); } };
    const shot = async (name) => { const r = await send('Page.captureScreenshot', { format: 'png' }); const buf = Buffer.from(r.data, 'base64'); fs.writeFileSync(path.join(out, `${label}-${name}.png`), buf); return buf.length; };
    await send('Runtime.enable'); await send('Page.enable');
    await send('Page.navigate', { url: base + '?menu=imgui' });   // (the sign-in gate is the older kind of screen either way)
    let running = false;
    for (const end = Date.now() + 90000; Date.now() < end && !running;) { running = (await send('Runtime.evaluate', { expression: "document.body.classList.contains('running')", returnByValue: true })).result?.value === true; if (!running) await sleep(500); }
    check(running, `${label}: the browser build starts`);
    await sleep(2500);
    await fn({ send, click, shot, consoleLines });
  } finally { try { chrome.kill(); } catch (e) {} await sleep(800); try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {} }
}

const signIn = JSON.parse(process.env.HN_SIGN_IN_AT || '[512,230]'), guestAt = JSON.parse(process.env.HN_GUEST_AT || '[512,300]');
try {
  mode = 'guest';
  await session('signin', async ({ click, send, shot }) => {
    check(await shot('gate') > 20000, 'guest: the gate is drawn before the main menu');
    await click(...signIn); await sleep(2000);
    check(loginHits.length === 1, 'guest: SIGN IN WITH OUTIVA goes to the account login (the stand-in saw one request)');
    const where = (await send('Runtime.evaluate', { expression: 'location.pathname', returnByValue: true })).result?.value;
    check(where === '/account/login', `guest: the sign-in happens in this tab (it is at ${where})`);
    const targets = await (await fetch(`http://127.0.0.1:${chromePort}/json`)).json();
    check(targets.filter((t) => t.type === 'page').length === 1, 'guest: no new tab was opened');
  });
  await session('guest', async ({ click, shot }) => {
    const gate = await shot('gate');
    await click(...guestAt); await sleep(2500);
    const menu = await shot('menu');
    check(gate > 20000 && menu > 20000 && gate !== menu, 'guest: PLAY AS A GUEST moves on to the main menu (the picture changed)');
  });
  mode = 'signedin';
  await session('signedin', async ({ shot, consoleLines }) => {
    await sleep(1500);
    await shot('menu');
    check(consoleLines.some((l) => /ACCOUNT signed in as SkyRacer/.test(l)), 'signed in: no gate, and the Outiva callsign is taken as the player\'s name');
  });
  mode = 'off';
  await session('off', async ({ shot }) => {
    await sleep(1500);
    await shot('menu');
    check(loginHits.length === 1, 'no accounts here: no gate and no further sign-in request');
  });
} finally {
  server.close();
  if (!process.env.HN_KEEP_PICTURES) { try { fs.rmSync(out, { recursive: true, force: true }); } catch (e) {} }
}
console.log('pictures in ' + out);
if (failures) { console.log(`${failures} check(s) failed`); process.exit(1); }
