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
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
  // Emscripten creates /home and /home/web_user as default directories.
  // Recreating /home here throws EEXIST and prevents main() from starting.
  if (!FS.analyzePath('/home').exists) FS.mkdir('/home');
  if (!FS.analyzePath('/home/web').exists) FS.mkdir('/home/web');
  FS.mount(IDBFS, {}, '/home/web');
  addRunDependency('hovernet-idbfs');
  hoverNetSync(true, function() {
    Promise.resolve(window.outivaIdentityReady).then(function(profile) {
      window.outivaProfile = profile;
      var account = document.getElementById('outiva-account');
      if (profile) {
        var label = profile.name || profile.preferred_username || profile.email || 'Outiva user';
        if (account) account.textContent = 'Outiva account: ' + label;
        var source = profile.preferred_username || profile.name || (profile.email || '').split('@')[0];
        var callsign = source.replace(/[^A-Za-z0-9 _-]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 32);
        var configDir = hoverNetConfigPath + '/hovernet';
        var username = configDir + '/username';
        if (callsign && !FS.analyzePath(username).exists) {
          if (!FS.analyzePath(configDir).exists) FS.mkdir(configDir);
          FS.writeFile(username, callsign + '\n');
          hoverNetSync(false);
        }
      }
    }).catch(function(error) {
      console.warn('Optional Outiva identity was not available', error);
    }).finally(function() { removeRunDependency('hovernet-idbfs'); });
  });
  window.setInterval(function() { hoverNetSync(false); }, 15000);
  document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'hidden') hoverNetSync(false);
  });
});
