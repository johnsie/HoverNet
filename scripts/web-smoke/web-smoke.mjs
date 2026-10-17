// Headless-browser smoke test for the web client (docs/web-client-roadmap.md, phase 5).
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

const [webDir, outDir] = process.argv.slice(2);
if (!webDir || !outDir) { console.error('usage: node web-smoke.mjs <web-dir> <out-dir>'); process.exit(2); }
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
const isUrl = /^https?:\/\//.test(webDir);
if (!isUrl) await new Promise((r) => server.listen(0, '127.0.0.1', r));
const url = isUrl ? webDir : `http://127.0.0.1:${server.address().port}/`;

const port = 9400 + Math.floor(Math.random() * 500);
const profile = fs.mkdtempSync('/tmp/hn-smoke-');
const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle',
  '--use-angle=swiftshader', '--enable-unsafe-swiftshader', `--user-data-dir=${profile}`, '--window-size=844,390', 'about:blank'],
  { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };
async function finish() {
  try { chrome.kill(); } catch (e) {}
  server.close();
  await sleep(1000);
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
  console.log(failures ? `${failures} check(s) failed` : 'web smoke test passed');
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
let nextId = 0; const pending = new Map(); const exceptions = [];
ws.onmessage = (m) => {
  const d = JSON.parse(m.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); }
  else if (d.method === 'Runtime.exceptionThrown') exceptions.push(d.params.exceptionDetails.exception?.description || d.params.exceptionDetails.text);
};
const send = (method, params = {}) => new Promise((r) => { const id = ++nextId; pending.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const evalJs = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
const frames = () => evalJs("(window.Module&&Module.ccall)?Module.ccall('hovernet_frames','number',[],[]):-1");
const inRace = () => evalJs("(window.Module&&Module.ccall)?Module.ccall('hovernet_in_race','number',[],[]):-1");
const shot = async (name) => fs.writeFileSync(path.join(outDir, `${name}.png`), Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).data, 'base64'));
async function click(x, y) {
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y }); await sleep(150);
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 }); await sleep(100);
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
}
async function key(k, vk) {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code: k, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk }); await sleep(80);
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code: k, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk });
}
async function waitFor(fn, ms) { for (let t = 0; t < ms; t += 500) { if (await fn()) return true; await sleep(500); } return false; }

await send('Runtime.enable'); await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 844, height: 390, deviceScaleFactor: 1, mobile: true });
await send('Emulation.setTouchEmulationEnabled', { enabled: true });
await send('Page.navigate', { url });

check(await waitFor(async () => (await frames()) >= 5, 120000), 'the game draws its first frames');
await sleep(1500); await shot('1-briefing');
// Driver briefing: Next, then Next x3 (the last is Finish). Positions are for 844x390 landscape.
await click(95, 331); await sleep(900);
for (let i = 0; i < 3; i++) { await click(201, 331); await sleep(900); }
await sleep(1500); await shot('2-main-menu');
await click(641, 88); await sleep(1500); await shot('3-local-setup');
await click(129, 339);
check(await waitFor(async () => (await inRace()) === 1, 30000), 'Start Race begins a race (race controls enabled)');
await sleep(3000); await shot('4-race');
await key('Escape', 27);
check(await waitFor(async () => (await inRace()) === 0, 10000), 'Escape pauses and hides the race controls');
await sleep(1500); await shot('5-pause');
await click(597, 121);
check(await waitFor(async () => (await inRace()) === 1, 10000), 'Resume Race returns to the race');
await sleep(1500); await shot('6-resumed');
// The results screen (shown without driving a whole race: the screen and its buttons are what is under test): its controls are
// hidden, KEEP DRIVING returns to the race.
await evalJs("Module.ccall('hovernet_test_show_results','void',[],[])");
check(await waitFor(async () => (await inRace()) === 0, 10000), 'the results screen opens (race controls hidden)');
await sleep(1500); await shot('6b-results');
await click(416, 195);
check(await waitFor(async () => (await inRace()) === 1, 10000), 'KEEP DRIVING on the results screen returns to the race');
await sleep(1000);
await key('Escape', 27); await sleep(2500);
await click(597, 193); // New Local Race
await sleep(2500); await shot('7-new-local-race');
check((await inRace()) === 0, 'the setup screen opened from the pause menu has no race controls');
check(exceptions.length === 0, `no uncaught page exceptions${exceptions.length ? ': ' + exceptions[0] : ''}`);
await finish();
