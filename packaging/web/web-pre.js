// Emscripten preRun hook: hydrate the per-browser config directory before
// main() reads settings. Files are copied back to IndexedDB periodically and
// when the page becomes hidden.
var hoverNetConfigPath = '/home/web/.config';
var hoverNetSync = function(populate, done) {
  FS.syncfs(populate, function(error) {
    if (error) console.error('HoverNet browser storage sync failed', error);
    if (done) done();
  });
};
// Optional Outiva account (see hovernet_account.py): the server keeps the sign-in in an HttpOnly cookie; the page only asks who is signed in.
// A guest, a missing or slow service, or any error all mean "guest play" and never hold the game back (3 second limit).
var hoverNetAccountCallsign = '';   // the display name last agreed with the account, "" for a guest
function hoverNetValidCallsign(value) { return /^[A-Za-z0-9 _-]{1,20}$/.test(value); }
function hoverNetUsernameFile() { return hoverNetConfigPath + '/hovernet/username'; }
function hoverNetReadUsername() {
  try { return FS.readFile(hoverNetUsernameFile(), { encoding: 'utf8' }).trim(); } catch (e) { return ''; }
}
function hoverNetAccount() {
  var controller = typeof AbortController === 'function' ? new AbortController() : null;
  var timer = controller ? window.setTimeout(function() { controller.abort(); }, 3000) : 0;
  return fetch('/account/me', { credentials: 'same-origin', cache: 'no-store', signal: controller ? controller.signal : undefined })
    .then(function(response) { return response.status === 200 ? response.json() : null; })
    .then(function(profile) {
      if (!profile || !profile.sub) return;
      window.outivaProfile = profile;
      var label = profile.name || profile.preferred_username || profile.email || 'Outiva user';
      var note = document.getElementById('outiva-account');
      if (note) note.textContent = 'Outiva account: ' + label;
      // The name saved with the account follows the player to every browser. With none saved yet, the account's name is only a
      // starting point: it never replaces a name this browser already has.
      var derived = (profile.preferred_username || profile.name || (profile.email || '').split('@')[0] || '')
        .replace(/[^A-Za-z0-9 _-]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 20);
      var existing = hoverNetReadUsername();
      var chosen = hoverNetValidCallsign(profile.callsign || '') ? profile.callsign : (existing || (hoverNetValidCallsign(derived) ? derived : ''));
      if (chosen && chosen !== existing) {
        FS.mkdirTree(hoverNetConfigPath + '/hovernet');   // a fresh browser has no .config yet
        FS.writeFile(hoverNetUsernameFile(), chosen + '\n');
        hoverNetSync(false);
      }
      hoverNetAccountCallsign = profile.callsign || '';
      window.hoverNetAccountSignedIn = true;
    })
    .catch(function(error) { console.warn('Optional Outiva account was not available', error); })
    .then(function() { if (timer) window.clearTimeout(timer); });
}
// When a signed-in player changes their name in the game, remember it for the account.
function hoverNetSaveAccountCallsign() {
  if (!window.hoverNetAccountSignedIn) return;
  var name = hoverNetReadUsername();
  if (!hoverNetValidCallsign(name) || name === hoverNetAccountCallsign) return;
  hoverNetAccountCallsign = name;
  fetch('/account/callsign', { method: 'PUT', credentials: 'same-origin', keepalive: true,
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ callsign: name }) })
    .catch(function(error) { console.warn('Could not save the account display name', error); });
}
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
  // Emscripten creates /home and /home/web_user as default directories.
  // Recreating /home here throws EEXIST and prevents main() from starting.
  if (!FS.analyzePath('/home').exists) FS.mkdir('/home');
  if (!FS.analyzePath('/home/web').exists) FS.mkdir('/home/web');
  FS.mount(IDBFS, {}, '/home/web');
  addRunDependency('hovernet-idbfs');
  hoverNetSync(true, function() {
    hoverNetAccount().then(function() { removeRunDependency('hovernet-idbfs'); });
  });
  window.setInterval(function() { hoverNetSync(false); }, 15000);
  document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'hidden') { hoverNetSync(false); hoverNetSaveAccountCallsign(); }
  });
  window.addEventListener('pagehide', hoverNetSaveAccountCallsign);
});
