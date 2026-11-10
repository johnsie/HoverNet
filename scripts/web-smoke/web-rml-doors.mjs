// The RmlUi screens in the browser build (docs/rmlui.md), in headless Chrome: the default page opens, the Leaderboards door and the online lobby door are clicked and each screen must
// draw, with no RmlUi error or abort in the console. (A first version of the menu drew into a picture larger than the canvas and filled only a quarter of the window in the browser; this
// catches a screen that does not draw, and HN_KEEP_PICTURES=1 keeps the pictures to look at.)  node web-rml-doors.mjs <web-dir>
// The RmlUi main menu prototype (docs/rmlui.md) running in the browser build, in headless Chrome: the page is opened (with ?menu=rml), the game starts, the new menu is
// drawn (no RmlUi error in the console, the picture is not blank) and the picture is saved when HN_KEEP_PICTURES=1.
//   node web-rml-menu.mjs <web-dir>        (the folder holding HoverNet.html/.js/.wasm/.data, built with -DHOVERNET_RMLUI=ON)
// Exits 0 on success, 1 on a failed check, 77 without Chrome.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const webDir = process.argv[2];
if (!webDir) { console.error('usage: node web-rml-menu.mjs <web-dir>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

const server = http.createServer((req, res) => {
  const pathname = req.url.split('?')[0];
  const file = path.join(webDir, path.normalize(pathname === '/' ? '/HoverNet.html' : pathname).replace(/^(\.\.[/\\])+/, ''));
  if (!file.startsWith(path.resolve(webDir)) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;
const port = 9300 + Math.floor(Math.random() * 500);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-rml-'));
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-rml-doors-'));
const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
  `--user-data-dir=${profile}`, '--window-size=1024,768', 'about:blank'], { stdio: 'ignore' });
try {
  let targets;
  for (let i = 0; i < 60 && !targets?.length; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); } }
  const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const consoleLines = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); }
    if (d.method === 'Runtime.consoleAPICalled') consoleLines.push(d.params.args.map((a) => a.value ?? a.description ?? '').join(' '));
  };
  const send = (method, params = {}) => new Promise((r) => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params })); });
  await send('Runtime.enable'); await send('Page.enable');
  const query = process.env.HN_WEB_QUERY ?? '?menu=rml';   // HN_WEB_QUERY= (empty) opens the default page, which shows the older menus
  await send('Page.navigate', { url: base + query });
  const started = async () => (await send('Runtime.evaluate', { expression: "typeof Module !== 'undefined' && !!Module.calledRun", returnByValue: true })).result?.value === true;
  let up = false;
  for (const end = Date.now() + 90000; Date.now() < end && !up;) { up = await started(); if (!up) await sleep(500); }
  check(up, 'the browser build starts with the new menus');
  // The page hides its loading screen (body.running) once the game has drawn a frame; then the menu's opening animations need a moment.
  let running = false; const waitStart = Date.now();
  for (const end = Date.now() + 90000; Date.now() < end && !running;) { running = (await send('Runtime.evaluate', { expression: "document.body.classList.contains('running')", returnByValue: true })).result?.value === true; if (!running) await sleep(500); }
  check(running, 'the loading screen goes away: the game has drawn a frame');
  check(Date.now() - waitStart < 40000, `the page saw a drawn frame promptly (${Math.round((Date.now() - waitStart) / 1000)} s; the page gives up and reloads in compatibility mode after 25 s with none)`);
  await sleep(3000);
  const click = async (x, y) => { for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased']) { await send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mousePressed' ? 1 : 0, clickCount: 1 }); await sleep(60); } };
  const key = async (k, code, vk) => { for (const type of ['keyDown', 'keyUp']) { await send('Input.dispatchKeyEvent', { type, key: k, code, windowsVirtualKeyCode: vk }); await sleep(80); } };
  const shotTo = async (name) => { const s = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(out, name), Buffer.from(s.data, 'base64')); return Buffer.from(s.data, 'base64').length; };
  // Doors by keyboard (the layout depends on the window size): the menu opens on RACE ONLINE; two Downs reach Leaderboards. Escape comes back to the menu (and from the menu it would open Practice Offline).
  for (const [name, downs] of [['leaderboards', 2], ['online', 0]]) {
    for (let n = 0; n < downs; n++) await key('ArrowDown', 'ArrowDown', 40);
    await key('Enter', 'Enter', 13);
    await sleep(6000);
    const bytes = await shotTo(`door-${name}.png`);
    check(bytes > 15000, `${name}: the screen is drawn (${bytes} bytes)`);
    const title = (await send('Runtime.evaluate', { expression: "document.title", returnByValue: true })).result?.value;
    await key('Escape', 'Escape', 27);
    await sleep(2500);
  }
  check(!consoleLines.some((l) => /RmlUi|Rml::|could not load/i.test(l) && /error|could not|fail/i.test(l)), 'no RmlUi error in the console');
  check(!consoleLines.some((l) => /^Uncaught|abort\(/.test(l)), 'no abort or uncaught exception');
  fs.writeFileSync(path.join(out, 'console.txt'), consoleLines.join('\n'));
} finally {
  try { chrome.kill(); } catch (e) {} await sleep(800); server.close();
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
  if (!process.env.HN_KEEP_PICTURES) { try { fs.rmSync(out, { recursive: true, force: true }); } catch (e) {} }
}
if (failures) { console.log(`${failures} check(s) failed`); process.exit(1); }
console.log('doors passed');
