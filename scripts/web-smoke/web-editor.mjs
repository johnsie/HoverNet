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
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

// a stand-in Track Market: what the editor asks it and what the editor sends it
let marketAnswer = { status: 201, body: { id: 1, status: 'waiting for review', name: 'Test Publish', version: 1 } };
let infoAnswer = { declaration: 'draft-0', declarationText: 'I made this track, or I have the right to publish it.', licences: ['CC0-1.0', 'CC-BY-4.0', 'CC-BY-SA-4.0'], you: { signedIn: true, invited: true } };
const received = [];
const server = http.createServer((req, res) => {
  const pathname = req.url.split('?')[0];
  if (pathname === '/market/info') { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(infoAnswer)); return; }
  if (pathname === '/market/submit-source' && req.method === 'POST') {
    const chunks = []; req.on('data', (c) => chunks.push(c));
    req.on('end', () => { received.push({ origin: req.headers.origin, type: req.headers['content-type'], body: Buffer.concat(chunks).toString('utf8') }); res.writeHead(marketAnswer.status, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(marketAnswer.body)); });
    return;
  }
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
  // the editor asks before a page with unsaved work is left (a reload too): the test, like a person, says "leave"
  ws.addEventListener('message', (m) => { const d = JSON.parse(m.data); if (d.method === 'Page.javascriptDialogOpening') send('Page.handleJavaScriptDialog', { accept: true }); });
  await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: downloads });
  await send('Page.navigate', { url: base + '?menu=imgui' });   // the editor test clicks the older menu's buttons
  const waitFor = async (fn, ms) => { for (const end = Date.now() + ms; Date.now() < end;) { if (await fn()) return true; await sleep(300); } return false; };

  const started = await waitFor(async () => (await evalJs("(window.Module&&Module._hn_rooms)?Module._hn_rooms():-1")) >= 0, 60000);
  check(started, 'the editor loads and starts');
  check(await call('hn_rooms') === 24 && await call('hn_elements') === 8, 'it starts with a new loop track: 24 rooms, 8 elements');
  const shot = async (name) => { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(path.join(downloads, name), Buffer.from(r.data, 'base64')); return Buffer.from(r.data, 'base64').length; };
  await sleep(1000);
  check(await shot('start.png') > 25000, 'the plan is drawn (the picture is not blank)');

  // build a road with the real mouse: a start room, then the Extend tool clicked on its open end
  const mouseDown = (type, x, y) => send('Input.dispatchMouseEvent', { type, x, y, button: 'left', buttons: type === 'mouseReleased' ? 0 : 1, clickCount: 1 });
  await evalJs("Module.ccall('hn_test_new_start_room',null,[],[])");
  await sleep(500);
  await evalJs("Module.ccall('hn_test_tool_extend',null,[],[])");
  await sleep(500);
  const ex = await call('hn_open_end_px'), ey = await call('hn_open_end_py');
  check(await call('hn_all_rooms') === 1 && ex > 0 && ey > 0, `a start room is one room, with its open end drawn at (${ex},${ey})`);
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: ex, y: ey }); await sleep(300);
  await mouseDown('mousePressed', ex, ey); await mouseDown('mouseReleased', ex, ey); await sleep(600);
  check(await waitFor(async () => (await call('hn_all_rooms')) === 2, 5000), 'a real click on the open end of the road with the Extend tool adds the next room');
  await sleep(500);   // the view follows the road when a new room runs off the edge
  const ex2 = await call('hn_open_end_px'), ey2 = await call('hn_open_end_py');
  check(ex2 > 0 && ex2 < 935 && ey2 > 30 && ey2 < 640, `the new room's open end is in view (${ex2},${ey2}): the editor followed the road`);
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: ex2, y: ey2 }); await sleep(300);
  await mouseDown('mousePressed', ex2, ey2); await mouseDown('mouseReleased', ex2, ey2); await sleep(600);
  check(await waitFor(async () => (await call('hn_all_rooms')) === 3, 5000), 'and another beyond that (three rooms, each joined to the last)');
  const keyEv = (type, key, code, vk, modifiers = 0) => send('Input.dispatchKeyEvent', { type, key, code, windowsVirtualKeyCode: vk, modifiers });
  await keyEv('rawKeyDown', 'Control', 'ControlLeft', 17, 2); await keyEv('rawKeyDown', 'z', 'KeyZ', 90, 2); await keyEv('keyUp', 'z', 'KeyZ', 90, 2); await keyEv('keyUp', 'Control', 'ControlLeft', 17);
  check(await waitFor(async () => (await call('hn_all_rooms')) === 2, 5000), 'Ctrl+Z takes the last room off again');

  // autosave: the editor keeps a copy in the browser, and offers it back after a reload
  await sleep(1500);
  const kept = await evalJs("localStorage.getItem('hn_autosave')");
  check(typeof kept === 'string' && (kept.match(/^\[Room\]/gm) || []).length === 2, 'a moment after a change the editor keeps a copy in the browser (the two rooms)');
  await send('Page.reload');
  check(await waitFor(async () => (await evalJs("(window.Module&&Module._hn_restore_visible)?Module._hn_restore_visible():-1")) === 1, 60000), 'after a reload the editor offers the unsaved work back');
  await sleep(800);
  const rx = await call('hn_restore_btn_x'), ry = await call('hn_restore_btn_y');
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: rx, y: ry }); await sleep(300);
  await mouseDown('mousePressed', rx, ry); await mouseDown('mouseReleased', rx, ry); await sleep(800);
  check(await call('hn_all_rooms') === 2 && await call('hn_restore_visible') === 0, 'clicking Restore my work brings back the two-room road and closes the question');
  // and the other answer: after another reload, Start fresh forgets the copy
  await send('Page.reload');
  check(await waitFor(async () => (await evalJs("(window.Module&&Module._hn_restore_visible)?Module._hn_restore_visible():-1")) === 1, 60000), 'it asks again after another reload (nothing was saved to a file)');
  await sleep(800);
  const fx = await call('hn_fresh_btn_x'), fy = await call('hn_fresh_btn_y');
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: fx, y: fy }); await sleep(300);
  await mouseDown('mousePressed', fx, fy); await mouseDown('mouseReleased', fx, fy); await sleep(800);
  check(await call('hn_all_rooms') === 24 && await evalJs("localStorage.getItem('hn_autosave')") === null, 'Start fresh keeps the new loop and forgets the copy');
  await evalJs('window.__beforeReload = 1');
  await send('Page.reload');
  // wait for the NEW page: the old one is still answering until the reload takes over
  check(await waitFor(async () => (await evalJs("(!window.__beforeReload && window.Module && Module._hn_rooms) ? Module._hn_rooms() : -1")) >= 0, 60000), 'the editor starts again after the next reload');
  await sleep(3000);   // long enough for a copy to be offered if there were one
  const askedAgain = await call('hn_restore_visible'), leftover = await evalJs("localStorage.getItem('hn_autosave')");
  check(askedAgain === 0 && leftover === null, `and it does not ask again (there is no copy left): asked=${askedAgain}, copy=${leftover === null ? 'none' : (leftover || '').length + ' characters, ' + ((leftover || '').match(/^\\[Room\\]/gm) || []).length + ' rooms'}`);

  // Escape goes back to the Select tool, ready for the drag test below
  await keyEv('rawKeyDown', 'Escape', 'Escape', 27); await keyEv('keyUp', 'Escape', 'Escape', 27);
  await sleep(300);

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
  // publishing: the same request the Publish button sends (the form itself is checked by looking at it below)
  const publish = (name, author, licence, declaration, description) => evalJs(`Module.ccall('hn_publish_test',null,['string','string','string','string','string'],[${JSON.stringify(name)},${JSON.stringify(author)},${JSON.stringify(licence)},${JSON.stringify(declaration)},${JSON.stringify(description)}])`);
  await publish('Test Publish', 'Pat Racer', 'CC-BY-4.0', 'draft-0', 'a test');
  check(await waitFor(async () => (await call('hn_publish_working')) === 0 && (await call('hn_publish_http')) === 201, 10000), 'Publish sends the track and the market answers 201');
  const req = received[received.length - 1];
  const field = (n) => { const m = new RegExp(`name="${n}"\\r\\n\\r\\n([^\\r]*)`).exec(req.body); return m ? m[1] : null; };
  check(req.origin === base.replace(/\/$/, '') && /^multipart\/form-data/.test(req.type), 'the request is a same-site form post (the market refuses anything else)');
  check(field('name') === 'Test Publish' && field('author') === 'Pat Racer' && field('licence') === 'CC-BY-4.0' && field('declaration') === 'draft-0' && field('description') === 'a test', 'it carries the name, author, licence, declaration and description exactly');
  check(req.body.includes('[Header]') && (req.body.match(/^\[Room\]/gm) || []).length === 39 && req.body.includes(sample.toString().split('\n')[0]), 'and the track source itself: the 39 rooms of the track being edited');
  marketAnswer = { status: 422, body: { id: 2, status: 'refused', reasons: ['no way of driving a lap round this track was found'] } };
  await publish('Another One', 'Pat Racer', 'CC0-1.0', 'draft-0', '');
  check(await waitFor(async () => (await call('hn_publish_working')) === 0 && (await call('hn_publish_http')) === 422, 10000), 'a turned-down track is reported (422) and the editor is free to try again');
  marketAnswer = { status: 403, body: { error: 'the Track Market is in private beta' } };
  await publish('Third', 'Pat', 'CC0-1.0', 'draft-0', '');
  check(await waitFor(async () => (await call('hn_publish_working')) === 0 && (await call('hn_publish_http')) === 403, 10000), 'a refusal because the author is not invited is reported (403)');
  check(await evalJs("localStorage.getItem('hn_author')") === 'Pat', 'the author name used is remembered in the browser for next time');

  // the Publish dialog, opened with the real mouse through the Track menu
  marketAnswer = { status: 201, body: { id: 3, status: 'waiting for review' } };
  const tx0 = await call('hn_menu_track_x'), ty0 = await call('hn_menu_track_y');
  check(tx0 > 0 && ty0 > 0, `the editor reports where its Track menu is drawn (${tx0},${ty0})`);
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: tx0, y: ty0 }); await sleep(300);
  await mouse('mousePressed', tx0, ty0); await mouse('mouseReleased', tx0, ty0); await sleep(600);
  const px1 = await call('hn_menu_publish_x'), py1 = await call('hn_menu_publish_y');
  check(px1 > 0 && py1 > 0, `the Publish item is in the open menu (${px1},${py1})`);
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: px1, y: py1 }); await sleep(300);
  await mouse('mousePressed', px1, py1); await mouse('mouseReleased', px1, py1); await sleep(1500);
  const dlg = await shot('publish-dialog.png');
  check(await call('hn_publish_visible') === 1, 'clicking Track > Publish to the community opens the Publish dialog');
  check(dlg > 25000, 'and it is drawn');
  check(exceptions.length === 0, 'no uncaught page exceptions' + (exceptions.length ? ': ' + exceptions[0] : ''));
} finally {
  try { chrome.kill(); } catch (e) {} await sleep(800); server.close();
  // /tmp is RAM on many systems: never leave the Chrome profile behind, and keep the pictures only when asked (HN_KEEP_PICTURES=1).
  try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
  if (!process.env.HN_KEEP_PICTURES) { try { fs.rmSync(downloads, { recursive: true, force: true }); } catch (e) {} }
}
if (failures) { console.log(`${failures} check(s) failed`); process.exit(1); }
console.log('web editor test passed' + (process.env.HN_KEEP_PICTURES ? ' (pictures in ' + downloads + ')' : ''));
