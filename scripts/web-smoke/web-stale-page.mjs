// Headless-browser test of the web client's "outdated cached page" guard (the hn-fresh script in packaging/web/shell.html).
//
//   node web-stale-page.mjs [<web-dir>]
//
// With a web directory it also runs the real packaged index.html (minified by the build) through the same upgrade, with its placeholder filled in.
//
// The script is cut out of the real shell and put in a tiny page. A test server plays a phone that is holding an old copy: the first
// page it serves has build id OLD, every later answer has NEW. The page must (1) load OLD, notice NEW when it asks the server, (2) reload once
// through ?v=NEW and end up on NEW, (3) not reload again, and (4) when the server keeps saying NEW but the page it was handed still says OLD
// even after a reload, show the update bar instead of looping. A page whose build id is still the placeholder must do nothing.
// Exits 0 on success, 1 on a failed check, 77 if Chrome is not installed.
import { spawn, spawnSync } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';

const chromeBin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find((b) => spawnSync('sh', ['-c', `command -v ${b}`]).status === 0);
if (!chromeBin) { console.log('SKIP: no Chrome/Chromium installed'); process.exit(77); }
const shell = fs.readFileSync(new URL('../../packaging/web/shell.html', import.meta.url), 'utf8');
const guard = (/<script id="hn-fresh">[\s\S]*?<\/script>/.exec(shell) || [])[0];
if (!guard) { console.error('cannot find the hn-fresh script in packaging/web/shell.html'); process.exit(2); }
// The packaged page is minified (the emscripten HTML minifier reorders the attributes and drops the quotes): serve that form too.
let minified = false;
const page = (id) => `<!doctype html><html><head>${minified ? `<meta content=${id} name=hn-build>` : `<meta name="hn-build" content="${id}">`}${guard}</head><body><p id="id">${id}</p></body></html>`;

const realFile = process.argv[2] ? `${process.argv[2]}/index.html` : '';
const realHtml = realFile && fs.existsSync(realFile) ? fs.readFileSync(realFile, 'utf8') : '';
let useReal = false;
let mode = 'upgrade';   // upgrade: the first page is OLD, later ones NEW. stuck: every navigation is OLD, fetches say NEW. dev: placeholder
let served = 0;
const log = [];
const server = http.createServer((req, res) => {
  const navigation = req.headers['sec-fetch-mode'] === 'navigate';
  served++;
  let id;
  if (mode === 'dev') id = '__HN_BUILD__';
  else if (mode === 'stuck') id = navigation ? 'OLD' : 'NEW';
  else id = (navigation && log.filter((l) => l.nav).length === 0) ? 'OLD' : 'NEW';
  log.push({ url: req.url, nav: navigation, id });
  res.writeHead(200, { 'Content-Type': 'text/html', 'Cache-Control': 'no-store' });
  res.end(useReal ? realHtml.split('__HN_BUILD__').join(id) : page(id));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const port = 9400 + Math.floor(Math.random() * 500);
const profile = fs.mkdtempSync('/tmp/hn-stale-');
const chrome = spawn(chromeBin, ['--headless=new', '--no-sandbox', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'PASS' : 'FAIL'}: ${what}`); if (!ok) failures++; };
let targets;
for (let i = 0; i < 60 && !targets?.length; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch (e) { await sleep(250); } }
if (!targets?.length) { console.log('FAIL: Chrome started'); chrome.kill(); server.close(); process.exit(1); }
const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let nextId = 0; const pending = new Map();
ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d.result || d.error); pending.delete(d.id); } };
const send = (method, params = {}) => new Promise((r) => { const id = ++nextId; pending.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const evalJs = async (e) => (await send('Runtime.evaluate', { expression: e, returnByValue: true })).result?.value;
await send('Runtime.enable'); await send('Page.enable');

// 1. an old copy is replaced
await send('Page.navigate', { url: `${base}/` });
await sleep(2500);
check(await evalJs("document.getElementById('id').textContent") === 'NEW', 'a page that is out of date reloads itself and ends on the current build');
check(/[?&]v=NEW/.test(await evalJs('location.search')), 'the reload went through a fresh address (?v=NEW)');
const navs = log.filter((l) => l.nav).length;
await sleep(2500);
check(log.filter((l) => l.nav).length === navs, 'it does not keep reloading');
check(await evalJs("!!document.getElementById('hn-update')") === false, 'no update bar when the page is current');

// 2. the server keeps handing out the old page: no loop, an update bar instead
mode = 'stuck'; log.length = 0;
await send('Runtime.evaluate', { expression: "try{sessionStorage.clear()}catch(e){}" });
await send('Page.navigate', { url: `${base}/stuck` });
await sleep(3500);
const stuckNavs = log.filter((l) => l.nav).length;
check(stuckNavs <= 2, `a page that cannot be refreshed does not loop (${stuckNavs} loads)`);
check(await evalJs("!!document.getElementById('hn-update')") === true, 'it shows the update bar instead');

// 2b. the same upgrade with the packaged page's minified tag: the id must still be read correctly (the first version of this guard compared against
// the pattern inside its own script and showed the update bar forever)
minified = true; mode = 'upgrade'; log.length = 0;
await send('Runtime.evaluate', { expression: "try{sessionStorage.clear()}catch(e){}" });
await send('Page.navigate', { url: `${base}/minified` });
await sleep(3000);
check(await evalJs("document.getElementById('id').textContent") === 'NEW', 'a minified page that is out of date reloads and ends on the current build');
check(await evalJs("!!document.getElementById('hn-update')") === false, 'and shows no update bar once it is current');
await sleep(2000);
check(log.filter((l) => l.nav).length <= 2, 'and does not keep reloading');
minified = false;

// 2c. the real packaged page
if (realHtml) {
  useReal = true; mode = 'upgrade'; log.length = 0;
  await send('Runtime.evaluate', { expression: "try{sessionStorage.clear()}catch(e){}" });
  await send('Page.navigate', { url: `${base}/real` });
  await sleep(4000);
  check(await evalJs('window.HN_BUILD') === 'NEW', 'the real packaged page that is out of date reloads and ends on the current build');
  check(await evalJs("!!document.getElementById('hn-update')") === false, 'and shows no update bar');
  useReal = false;
}

// 3. a development build (placeholder id) is left alone
mode = 'dev'; log.length = 0;
await send('Page.navigate', { url: `${base}/dev` });
await sleep(2000);
check(log.filter((l) => l.url !== '/favicon.ico').length === 1, 'a build without an id does not ask the server anything');

chrome.kill(); server.close(); await sleep(800);
try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); } catch (e) {}
console.log(failures ? `${failures} check(s) failed` : 'stale page guard test passed');
process.exit(failures ? 1 : 0);
