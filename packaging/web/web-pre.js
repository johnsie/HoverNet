// Emscripten preRun hook: hydrate the per-browser config directory before
// main() reads settings. Files are copied back to IndexedDB periodically and
// when the page becomes hidden.
var hoverNetConfigPath = '/home/web/.config';
var hoverNetSync = function(populate, done) {
  FS.syncfs(populate, function(error) {
    if (error) console.error('HoverNet browser storage sync failed', error);
    if (done) done(error);
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
window.hoverNetCraftStatus = '';
window.hoverNetCraftRevision = 0;
window.hoverNetCraftBusy = false;
function hoverNetCraftDesignValid(bytes) {
  var fixed = 4 + 1 + 4 + 3 + 1 + 3 + 1 + 16 * 3 + 32 * 32 / 2 + 1;
  if (!bytes || bytes.length < fixed + 4 || bytes.length > 640 ||
      bytes[0] !== 72 || bytes[1] !== 67 || bytes[2] !== 68 || bytes[3] !== 49 || bytes[4] >= 5) return false;
  var hash = 2166136261;
  for (var i = 0; i < bytes.length - 4; ++i) hash = Math.imul(hash ^ bytes[i], 16777619) >>> 0;
  var end = bytes.length - 4;
  var stored = (bytes[end] | (bytes[end + 1] << 8) | (bytes[end + 2] << 16) | (bytes[end + 3] << 24)) >>> 0;
  if (hash !== stored) return false;
  function signed(value) { return value > 127 ? value - 256 : value; }
  for (var shape = 5; shape < 8; ++shape) if (Math.abs(signed(bytes[shape])) > 25) return false;
  if (Math.abs(signed(bytes[8])) > 30) return false;
  var nameAt = fixed - 1;
  var nameLength = bytes[nameAt];
  if (nameLength > 24 || nameAt + 1 + nameLength + 4 !== bytes.length) return false;
  for (var n = nameAt + 1; n < nameAt + 1 + nameLength; ++n) if (bytes[n] < 32 || bytes[n] > 126) return false;
  return true;
}
function hoverNetCraftAccountFetch(path, options) {
  var controller = typeof AbortController === 'function' ? new AbortController() : null;
  var timer = controller ? window.setTimeout(function() { controller.abort(); }, 5000) : 0;
  options = options || {};
  options.credentials = 'same-origin';
  options.cache = 'no-store';
  if (controller) options.signal = controller.signal;
  return fetch(path, options).then(function(response) {
    if (!response.ok) throw new Error('Account service returned HTTP ' + response.status);
    return response;
  }).then(function(response) { return response; }).finally(function() { if (timer) window.clearTimeout(timer); });
}
function hoverNetCraftSlotsFromBrowser() {
  var directory = hoverNetConfigPath + '/craft';
  var designs = [];
  for (var i = 0; i < 8; ++i) {
    var path = directory + '/design' + i + '.hcd';
    if (!FS.analyzePath(path).exists) { designs.push(null); continue; }
    var bytes = FS.readFile(path);
    if (!hoverNetCraftDesignValid(bytes)) throw new Error('A local craft design is damaged.');
    var binary = '';
    for (var j = 0; j < bytes.length; ++j) binary += String.fromCharCode(bytes[j]);
    designs.push(btoa(binary));
  }
  var active = -1;
  try { active = parseInt(FS.readFile(directory + '/active', { encoding: 'utf8' }), 10); } catch (e) {}
  if (!Number.isInteger(active) || active < 0 || active >= 8 || !designs[active]) active = -1;
  return { designs: designs, active: active };
}
window.hoverNetSaveCraftAccount = function() {
  if (!window.hoverNetAccountSignedIn) { window.hoverNetCraftStatus = 'Sign in to your Outiva account before saving.'; return; }
  if (window.hoverNetCraftBusy) return;
  window.hoverNetCraftStatus = 'Saving craft designs to your Outiva account...';
  var document;
  try { document = hoverNetCraftSlotsFromBrowser(); }
  catch (error) { window.hoverNetCraftStatus = 'Could not save: ' + error.message; return; }
  window.hoverNetCraftBusy = true;
  hoverNetCraftAccountFetch('/account/craft', {
    method: 'PUT', keepalive: true, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(document)
  }).then(function() {
    window.hoverNetCraftStatus = 'Saved craft designs to your Outiva account.';
  }).catch(function(error) {
    console.warn('Could not save craft designs to the account', error);
    window.hoverNetCraftStatus = 'Could not save craft designs to the account.';
  }).finally(function() { window.hoverNetCraftBusy = false; });
};
window.hoverNetLoadCraftAccount = function() {
  if (!window.hoverNetAccountSignedIn) { window.hoverNetCraftStatus = 'Sign in to your Outiva account before restoring.'; return; }
  if (window.hoverNetCraftBusy) return;
  window.hoverNetCraftStatus = 'Loading craft designs from your Outiva account...';
  window.hoverNetCraftBusy = true;
  hoverNetCraftAccountFetch('/account/craft').then(function(response) { return response.json(); }).then(function(document) {
    if (!document || !Array.isArray(document.designs) || document.designs.length > 8) throw new Error('Invalid craft-design list.');
    var designs = [];
    for (var i = 0; i < 8; ++i) {
      var encoded = document.designs[i];
      if (encoded == null) { designs.push(null); continue; }
      if (typeof encoded !== 'string') throw new Error('Invalid craft-design entry.');
      var binary = atob(encoded), bytes = new Uint8Array(binary.length);
      for (var j = 0; j < binary.length; ++j) bytes[j] = binary.charCodeAt(j);
      if (!hoverNetCraftDesignValid(bytes)) throw new Error('The account has a damaged craft design.');
      designs.push(bytes);
    }
    var active = document.active;
    if (!Number.isInteger(active) || active < 0 || active >= 8 || !designs[active]) active = -1;
    var directory = hoverNetConfigPath + '/craft';
    FS.mkdirTree(directory);
    for (var slot = 0; slot < 8; ++slot) {
      var path = directory + '/design' + slot + '.hcd';
      if (designs[slot]) FS.writeFile(path, designs[slot]);
      else if (FS.analyzePath(path).exists) FS.unlink(path);
    }
    FS.writeFile(directory + '/active', active + '\n');
    return new Promise(function(resolve) {
      hoverNetSync(false, function(error) {
        ++window.hoverNetCraftRevision;
        if (error) window.hoverNetCraftStatus = 'Restored account designs, but browser storage could not be saved.';
        else window.hoverNetCraftStatus = "Restored account designs, replacing this browser's local slots.";
        resolve();
      });
    });
  }).catch(function(error) {
    console.warn('Could not restore craft designs from the account', error);
    window.hoverNetCraftStatus = 'Could not restore craft designs from the account.';
  }).finally(function() { window.hoverNetCraftBusy = false; });
};
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
