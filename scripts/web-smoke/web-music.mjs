// Music after start (scripts/pack-web-music.py, packaging/web/shell.html): in a build made with -DHOVERNET_WEB_DEFERRED_MUSIC=ON the first download holds no music,
// the page fetches HoverNetMusic.bin once the game is on screen, writes the files into the game's file system, and the game starts the menu track by itself.
// Also checks the quiet case: with no HoverNetMusic.bin on the site (a 404) the game still runs and nothing breaks.
//   node web-music.mjs <web-dir>      (the folder holding index.html/HoverNet.js/.wasm/.data, from a -DHOVERNET_WEB_DEFERRED_MUSIC=ON build)
// Exits 0 on success, 1 on a failed check, 77 without Chrome. HN_MUSIC_BIN=<file> names the packed music (default: packed here with scripts/pack-web-music.py).
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const webDir = process.argv[2];
if (!webDir) { console.error('usage: node web-music.mjs <web-dir>'); process.exit(2); }
const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const here = path.dirname(new URL(import.meta.url).pathname);
const bin = process.env.HN_MUSIC_BIN ?? path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'hn-music-')), 'HoverNetMusic.bin');
if (!process.env.HN_MUSIC_BIN) spawnSync('python3', [path.join(here, '..', 'pack-web-music.py'), bin], { stdio: 'inherit' });
const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };

async function run(withMusic) {
  const requests = [];
  const server = http.createServer((req, res) => {
    const pathname = req.url.split('?')[0];
    requests.push(pathname);
    let file = path.join(webDir, path.normalize(pathname === '/' ? '/index.html' : pathname).replace(/^(\.\.[/\\])+/, ''));
    if (pathname === '/HoverNetMusic.bin') file = withMusic ? bin : '/nonexistent';
    if (!fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
    fs.createReadStream(file).pipe(res);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}/`;
  const port = 9300 + Math.floor(Math.random() * 500);
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hn-music-prof-'));
  const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
    '--autoplay-policy=no-user-gesture-required', `--user-data-dir=${profile}`, '--window-size=1024,768', 'about:blank'], { stdio: 'ignore' });
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
    const evalJs = async (expression) => (await send('Runtime.evaluate', { expression, returnByValue: true })).result?.value;
    await send('Runtime.enable'); await send('Page.enable');
    await send('Page.navigate', { url: base + '?testmusic' });
    let running = false;
    for (const end = Date.now() + 120000; Date.now() < end && !running;) { running = await evalJs("document.body.classList.contains('running')") === true; if (!running) await sleep(500); }
    check(running, `${withMusic ? 'with' : 'without'} music file: the game starts and draws a frame`);
    check(await evalJs("Module.FS.readdir('/NetTarget/Music').filter(n => n.endsWith('.ogg')).length") === 0 || withMusic, 'the first download holds no music files');
    if (withMusic) {
      let arrived = false;
      for (const end = Date.now() + 60000; Date.now() < end && !arrived;) { arrived = await evalJs("Module.FS.analyzePath('/NetTarget/Music/HardLeft.ogg').exists") === true; if (!arrived) await sleep(500); }
      check(arrived, 'the music files arrive after the game has started');
      check(await evalJs("Module.FS.stat('/NetTarget/Music/HardLeft.ogg').size") === fs.statSync(path.join(here, '..', '..', 'NetTarget', 'Music', 'HardLeft.ogg')).size, 'the menu track is whole (same size as the original)');
      let playing = false;
      for (const end = Date.now() + 15000; Date.now() < end && !playing;) { playing = consoleLines.some((l) => l.includes('MUSICTEST HardLeft.ogg')); if (!playing) await sleep(500); }
      check(playing, 'the game starts the menu track by itself once it has arrived');
    } else {
      await sleep(4000);
      check(requests.includes('/HoverNetMusic.bin'), 'the page asked for the music');
      check(await evalJs("document.body.classList.contains('running') && !document.querySelector('.error')") === true, 'a missing music file leaves the game running and quiet');
      check(!consoleLines.some((l) => l.includes('MUSICTEST')), 'no track starts without the files');
    }
    check(!consoleLines.some((l) => /abort|Aborted|RuntimeError/.test(l)), 'no abort in the console');
  } finally { chrome.kill(); server.close(); }
}
await run(true);
await run(false);
process.exit(failures ? 1 : 0);
