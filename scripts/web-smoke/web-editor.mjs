// The browser track editor (packaging/web/editor-shell.html, NetTarget/TrackEditor) in headless Chrome: it starts and draws, a track opened through the page's file
// route is the track being edited, a real mouse drag moves a real element into another room, undo puts it back, and saving gives a download that is the track.
//   node web-editor.mjs <web-dir>        (the folder holding HoverNetTrackEditor.html/.js/.wasm)
// Exits 0 on success, 1 on a failed check, 77 without Chrome.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const webDir = process.argv[2];
if (!webDir) { console.error('usage: node web-editor.mjs <web-dir>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const sample = fs.readFileSync(new URL('../../NetTarget/TrackSources/Tidal Causeway.track.txt', import.meta.url));
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

const server = http.createServer((req, res) => {
  const pathname = req.url.split('?')[0];
  if (pathname === '/sample.track.txt') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end(sample); return; }
  const file = path.join(webDir, path.normalize(pathname === '/' ? '/HoverNetTrackEditor.html' : pathname).replace(/^(\.\.[/\\])+/, ''));
  if (!file.startsWith(path.resolve(webDir)) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

const port = 9300 + Math.floor(Math.random() * 500);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-editor-'));
const downloads = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-editor-dl-'));
const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
  `--user-data-dir=${profile}`, '--window-size=1280,800', 'about:blank'], { stdio: 'ignore' });
try {
  let targets;
  for (let i = 0; i < 60 && !targets?.length; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); } }
  const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const exceptions = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); }
    if (d.method === 'Runtime.exceptionThrown') exceptions.push(d.params.exceptionDetails.text + ' ' + (d.params.exceptionDetails.exception?.description || ''));
  };
  const send = (method, params = {}) => new Promise((r) => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params })); });
  const evalJs = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true, awaitPromise: true })).result?.value;
  const call = (fn, ...a) => evalJs(`Module.ccall('${fn}','number',[${a.map(() => "'number'").join(',')}],[${a.join(',')}])`);
  await send('Runtime.enable'); await send('Page.enable');
  await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: downloads });
  await send('Page.navigate', { url: base });
  const waitFor = async (fn, ms) => { for (const end = Date.now() + ms; Date.now() < end;) { if (await fn()) return true; await sleep(300); } return false; };

  const started = await waitFor(async () => (await evalJs("(window.Module&&Module._hn_rooms)?Module._hn_rooms():-1")) >= 0, 60000);
  check(started, 'the editor loads and starts');
  check(await call('hn_rooms') === 24 && await call('hn_elements') === 8, 'it starts with a new loop track: 24 rooms, 8 elements');
  const shot = async (name) => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(downloads, name), Buffer.from(r.data, 'base64')); return Buffer.from(r.data, 'base64').length; };
  await sleep(1000);
  check(await shot('start.png') > 25000, 'the plan is drawn (the picture is not blank)');

  // open a track through the same route the file picker uses
  await evalJs(`fetch('/sample.track.txt').then(r=>r.arrayBuffer()).then(b=>{FS.writeFile('/upload.track.txt',new Uint8Array(b));Module.ccall('hn_file_ready',null,['string'],['Tidal Causeway.track.txt']);return 1})`);
  check(await waitFor(async () => (await call('hn_rooms')) === 39, 15000), 'a track opened through the file route becomes the track being edited (39 rooms)');
  check(await call('hn_dirty') === 0 && await call('hn_problems') === 0, 'it opens clean: not changed, and no problems');
  await sleep(600);
  check(await shot('tidal.png') > 25000, 'the opened track is drawn');

  // drag the element that sits in the first room into another room, with the real mouse
  const px0 = await call('hn_element_px', 0), py0 = await call('hn_element_py', 0), room0 = await call('hn_element_section', 0);
  let targetIndex = -1;
  for (let i = 1; i < 9; i++) { if ((await call('hn_element_section', i)) !== room0) { targetIndex = i; break; } }
  const tx = await call('hn_element_px', targetIndex), ty = await call('hn_element_py', targetIndex);
  check(px0 > 0 && py0 > 0 && tx > 0 && ty > 0, `the editor reports where elements are drawn (${px0},${py0}) and (${tx},${ty})`);
  const mouse = (type, x, y, extra = {}) => send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1, ...extra });
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: px0, y: py0 });
  await sleep(300);
  await mouse('mousePressed', px0, py0); await sleep(300);
  for (let i = 1; i <= 10; i++) { await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: px0 + (tx + 30 - px0) * i / 10, y: py0 + (ty + 6 - py0) * i / 10, buttons: 1 }); await sleep(120); }
  await mouse('mouseReleased', tx + 30, ty + 6); await sleep(500);
  const roomAfter = await call('hn_element_section', 0);
  check(roomAfter !== room0 && await call('hn_dirty') === 1, `a real mouse drag moved the element from room ${room0} into room ${roomAfter}, and the track is now marked changed`);

  // undo with the keyboard
  const key = (type, key, code, vk, modifiers = 0) => send('Input.dispatchKeyEvent', { type, key, code, windowsVirtualKeyCode: vk, modifiers });
  await key('rawKeyDown', 'Control', 'ControlLeft', 17, 2); await key('rawKeyDown', 'z', 'KeyZ', 90, 2); await key('keyUp', 'z', 'KeyZ', 90, 2); await key('keyUp', 'Control', 'ControlLeft', 17);
  check(await waitFor(async () => (await call('hn_element_section', 0)) === room0, 5000), 'Ctrl+Z puts the element back in its room');

  // save: the page offers a download that is the track
  await key('rawKeyDown', 'Control', 'ControlLeft', 17, 2); await key('rawKeyDown', 's', 'KeyS', 83, 2); await key('keyUp', 's', 'KeyS', 83, 2); await key('keyUp', 'Control', 'ControlLeft', 17);
  const got = await waitFor(async () => fs.readdirSync(downloads).some((f) => f.endsWith('.track.txt')), 10000);
  check(got, 'Ctrl+S gives a download');
  if (got) {
    const file = fs.readdirSync(downloads).find((f) => f.endsWith('.track.txt'));
    const text = fs.readFileSync(path.join(downloads, file), 'utf8');
    check(file === 'Tidal Causeway.track.txt' && text.startsWith('[Header]') && (text.match(/^\[Room\]/gm) || []).length === 39, 'the download is named like the opened file and holds the 39 rooms');
    check(text === sample.toString(), 'saved straight after undoing, it is the original track exactly');
  }
  check(exceptions.length === 0, 'no uncaught page exceptions' + (exceptions.length ? ': ' + exceptions[0] : ''));
} finally { try { chrome.kill(); } catch (e) {} await sleep(500); server.close(); }
if (failures) { console.log(`${failures} check(s) failed`); process.exit(1); }
console.log('web editor test passed');
