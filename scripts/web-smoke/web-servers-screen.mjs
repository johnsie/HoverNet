// Headless-browser test of the web client on a HoverNet 3.0 server (protocol 3.0 over a WebSocket): a real HoverNetAuthServer is started
// with the page's origin allowed, the browser connects through the HoverNet Servers screen, hosts a one-player time trial, and the browser
// client must follow the whole race exactly (every tick's state hash, every snapshot restore).
//
//   node web-servers-screen.mjs <web-dir> <out-dir> <HoverNetAuthServer>
// (the original header of the smoke test follows)
//
//
//   node web-smoke.mjs <web-dir | https://site> <out-dir>
//
// With a URL it tests that live site instead of serving a directory. Serves <web-dir> (index.html, HoverNet.js/.wasm/.data) with the headers the build needs, drives it in
// headless Chrome as a phone in landscape (844x390, touch), and checks the flow a player takes:
// load -> driver briefing -> main menu -> local race -> pause (race controls hidden) -> resume -> results screen -> keep driving.
// Screenshots go to <out-dir>. Exits 0 on success, 1 on a failed check, 77 if Chrome is not installed.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const [webDir, outDir, serverBin] = process.argv.slice(2);
if (!webDir || !outDir || !serverBin) { console.error('usage: node web-servers-screen.mjs <web-dir> <out-dir> <HoverNetAuthServer>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser']
  .find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
fs.mkdirSync(outDir, { recursive: true });

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const server = http.createServer((req, res) => {
  const name = req.url.split('?')[0] === '/' ? '/index.html' : req.url.split('?')[0];
  const file = path.join(webDir, path.normalize(name).replace(/^(\.\.[/\\])+/, ''));
  if (!file.startsWith(path.resolve(webDir)) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, {
    'Content-Type': types[path.extname(file)] || 'application/octet-stream',
    'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
  });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const webPort = server.address().port;
const url = `http://127.0.0.1:${webPort}/`;

// The game server: a lobby that lets this page's origin in over WebSocket.
const game = spawn(serverBin, ['--lobby', '--port', '0', '--ws-origin', `http://127.0.0.1:${webPort}`, '--limit-ticks', '500', '--start-delay-ms', '500'], { stdio: ['ignore', 'pipe', 'pipe'] });
let gameOut = '';
game.stdout.on('data', (d) => (gameOut += d)); game.stderr.on('data', (d) => (gameOut += d));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let gamePort = 0;
for (let i = 0; i < 100 && !gamePort; i++) { const m = /listening on [0-9.]*:(\d+),/.exec(gameOut); if (m) gamePort = +m[1]; else await sleep(100); }
if (!gamePort) { console.log('FAIL: the game server did not start'); console.log(gameOut); process.exit(1); }

const port = 9400 + Math.floor(Math.random() * 500);
const profile = fs.mkdtempSync('/tmp/hn-servers-');
const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle',
  '--use-angle=swiftshader', '--enable-unsafe-swiftshader', `--user-data-dir=${profile}`, '--window-size=837,715', 'about:blank'],
  { stdio: 'ignore' });
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };
async function finish() {
  try { chrome.kill(); } catch (e) {}
  try { game.kill(); } catch (e) {}
  server.close();
  await sleep(1000);
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
  console.log(failures ? `${failures} check(s) failed` : 'web servers screen test passed');
  process.exit(failures ? 1 : 0);
}

let targets;
for (let i = 0; i < 60 && !targets?.length; i++) {
  try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); }
}
if (!targets?.length) { check(false, 'Chrome started'); await finish(); }
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let nextId = 0; const pending = new Map(); const exceptions = []; const consoleLines = [];
ws.onmessage = (m) => {
  const d = JSON.parse(m.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); }
  else if (d.method === 'Runtime.exceptionThrown') exceptions.push(d.params.exceptionDetails.exception?.description || d.params.exceptionDetails.text);
  else if (d.method === 'Runtime.consoleAPICalled') consoleLines.push(d.params.args.map((a) => a.value ?? a.description ?? '').join(' '));
};
const send = (method, params = {}) => new Promise((r) => { const id = ++nextId; pending.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const evalJs = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
const frames = () => evalJs("(window.Module&&Module.ccall)?Module.ccall('hovernet_frames','number',[],[]):-1");
const inRace = () => evalJs("(window.Module&&Module.ccall)?Module.ccall('hovernet_in_race','number',[],[]):-1");
const setenv = (n, v) => evalJs(`Module.ccall('hovernet_test_setenv','void',['string','string'],[${JSON.stringify(n)},${JSON.stringify(v)}])`);
const shot = async (name) => fs.writeFileSync(path.join(outDir, `${name}.png`), Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).data, 'base64'));
async function click(x, y) {
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y }); await sleep(150);
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 }); await sleep(100);
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
}
async function waitFor(fn, ms) { for (let t = 0; t < ms; t += 500) { if (await fn()) return true; await sleep(500); } return false; }

await send('Runtime.enable'); await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 837, height: 715, deviceScaleFactor: 1, mobile: false });
await send('Page.navigate', { url });

check(await waitFor(async () => (await frames()) >= 5, 120000), 'the game draws its first frames');
await sleep(1500);
// The test switches (the same ones the native tests use) are set while the driver briefing is on screen; the main menu then opens the
// HoverNet Servers screen by itself, which connects to the server below and hosts a time trial.
await setenv('HOVERNET_TEST_MENU', 'authserver'); await setenv('HOVERNET_TEST_MENU_FORCE', '1');
await setenv('HOVERNET_TEST_AUTH_SERVER', `ws://127.0.0.1:${gamePort}/auth/`);
await setenv('HOVERNET_TEST_AUTH_VERIFY', '1'); await setenv('HOVERNET_TEST_AUTH_SCRIPT', '1');
// Driver briefing at 837x715: Next, then Next x3 (the last is Finish).
await click(135, 636); await sleep(900);
for (let i = 0; i < 3; i++) { await click(277, 636); await sleep(900); }
check(await waitFor(async () => (await inRace()) === 1, 60000), 'the browser connected to the server over WebSocket and a race started');
await sleep(2000); await shot('servers-1-race');
check(await waitFor(async () => consoleLines.some((l) => /auth client: [1-9][0-9]* ticks verified, 0 tick failures, [0-9]+ restore checks, 0 restore failures/.test(l)), 90000),
      'the browser followed the whole race exactly (every tick verified)');
check(exceptions.length === 0, `no uncaught page exceptions${exceptions.length ? ': ' + exceptions[0] : ''}`);
if (failures) { await shot('servers-2-end'); console.log(consoleLines.slice(-12).join('\n')); console.log('server output:\n' + gameOut.split('\n').slice(-12).join('\n')); }
await finish();
