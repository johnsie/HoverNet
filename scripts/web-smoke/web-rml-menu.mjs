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
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-rml-pics-'));
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
  await send('Page.navigate', { url: base + '?menu=rml' });
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
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  const png = Buffer.from(shot.data, 'base64');
  fs.writeFileSync(path.join(out, 'rml-menu-web.png'), png);
  check(png.length > 40000, `the page is drawn (the picture is ${png.length} bytes, not blank)`);
  check(!consoleLines.some((l) => /RmlUi|Rml::|could not load .*main-menu/i.test(l) && /error|could not|fail/i.test(l)), 'no RmlUi error in the console');
  check(!consoleLines.some((l) => /^Uncaught|abort\(/.test(l)), 'no abort or uncaught exception');
} finally {
  try { chrome.kill(); } catch (e) {} await sleep(800); server.close();
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
  if (!process.env.HN_KEEP_PICTURES) { try { fs.rmSync(out, { recursive: true, force: true }); } catch (e) {} }
}
if (failures) { console.log(`${failures} check(s) failed`); process.exit(1); }
console.log('web RmlUi menu test passed' + (process.env.HN_KEEP_PICTURES ? ' (picture in ' + out + ')' : ''));
