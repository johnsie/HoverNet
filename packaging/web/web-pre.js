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
  hoverNetSync(true, function() { removeRunDependency('hovernet-idbfs'); });
  window.setInterval(function() { hoverNetSync(false); }, 15000);
  document.addEventListener('visibilitychange', function() {
    if (document.visibilityState === 'hidden') hoverNetSync(false);
  });
});
