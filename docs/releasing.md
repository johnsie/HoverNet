# Releasing HoverNet

How a release is made, verified, upgraded to, and rolled back.

## Making a release

1. Make sure `main` is green on GitLab and GitHub Actions (both build and test).
2. Update [CHANGELOG.md](../CHANGELOG.md): move *Unreleased* under the new version
   and date.
3. Tag `vX.Y.Z` (an annotated tag on a commit that is already on `main`) and push
   it to **both** remotes (`gitlab` and `origin`). A tag push builds and packages:
   - Linux `hovernet-game` and `hovernet-raceserver` `.deb` packages;
   - the Windows x64 installer (`HoverNet-X.Y.Z-windows-x64-setup.exe`), the legacy
     Win32 installer, and the Windows RaceServer zip;
   - a `SHA256SUMS` file over all of them.
4. The `publish:github-release` (GitLab) and `release` (GitHub) jobs create the
   public GitHub release only after **every** package job succeeds. If a job
   fails, no release is published; fix it and use a **new** tag (do not move or
   reuse a tag that has been pushed).
5. The race server is **never** deployed by a release. `deploy:raceserver` is a
   manual GitLab job on a protected tag.
6. Community track downloads are hosted separately (see
   [Community tracks](community-tracks.md)); a release does not change them.

The `community-tracks-N` tags only host track files and never trigger builds.

## Reproducible packages

Building the same commit twice gives byte-identical binaries and Debian
packages. File times inside a package are pinned to `SOURCE_DATE_EPOCH` (set it in
CI to pin it, otherwise the commit time is used). `HoverNetReproduciblePackages`
checks this on every test run; to compare two full builds yourself, build into two
different directories and `cmp` the outputs.

## Verifying a download

Every release carries a `SHA256SUMS` file. Download it next to the files you want
and run `sha256sum -c --ignore-missing SHA256SUMS` (Linux) or
`Get-FileHash <file> -Algorithm SHA256` (Windows) and compare. Checksums protect
against corruption and mirroring mistakes; they are not a signature, so get
`SHA256SUMS` from the same GitHub release page.

## Upgrading and migration notes

- **Settings and saved choices** live outside the install folder
  (`~/.config/hovernet` on Linux, `%APPDATA%\HoverNet` on Windows), so upgrading,
  uninstalling and reinstalling keep them. Older per-user dotfiles
  (`~/.hovernet_*`) are copied there the first time a newer build runs; the old
  files are left in place.
- **Remembered race choices** (`host_prefs`, `local_race_prefs`) are stored by
  track name from 0.3.1. Older numeric files are still read. An older build reading
  a newer file ignores it and uses its defaults.
- **Community tracks** downloaded by the game live in the per-user
  `CommunityTracks` folder and are safe to delete (they are downloaded again if
  needed).
- **Server configuration** `/etc/hovernet/config.xml` and
  `/etc/hovernet/CommunityTracks.tsv` are Debian conffiles: an upgrade keeps your
  edits and asks if the packaged version changed.
- **Compatibility.** A 0.3.x client works with an older server for the seven
  official tracks only: an older server refuses to host a community track. An older
  client works with a newer server. Upgrade the server first.

## Rolling back

- **Linux client:** `sudo apt install --allow-downgrades ./hovernet-game_<older>.deb`.
- **Linux server:** `sudo dpkg -i hovernet-raceserver_<older>.deb` then
  `sudo systemctl restart hovernet-raceserver`; configuration is preserved. The
  previous package is attached to its own GitHub release and is also a GitLab
  artifact of that tag's pipeline (re-run its manual `deploy:raceserver` job to
  redeploy it through the normal, audited path).
- **Windows:** run the older release's installer over the current one, or uninstall
  first; settings are kept either way.
- **Community track downloads** do not need rolling back: files are immutable and
  named by hash. To withdraw one, delete its asset from the `community-tracks-N`
  release and remove its line from `NetTarget/CommunityTracks.tsv`.
- If a bad release was published, mark it as a pre-release or delete it on GitHub,
  and cut a fixed version under a new tag. Do not move or reuse a published tag.

## Still manual

The internal alpha, the two-week public beta, the release-candidate freeze, the
production-server migration rehearsal, and clean install/upgrade/uninstall
rehearsals on real machines are human steps; record their results in the release
notes.
