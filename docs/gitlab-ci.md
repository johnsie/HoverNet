# GitLab CI and Race Server Delivery

The pipeline builds and packages the central `RaceServer` and native Linux game client as Ubuntu `amd64` Debian packages. It also builds the Windows client and creates an Inno Setup installer, and builds a Windows `RaceServer.exe` (zipped with a template `config.xml`) for anyone who wants to run the race server on Windows instead of Linux.

## GitLab runners

Configure protected runners with these tags, or override the CI variables in GitLab:

- `race-server`: C++14 compiler, `dpkg-deb`, Bash, and narrowly scoped deployment privileges. It builds/packages the server and deploys tagged releases.
- `windows`: PowerShell, Visual Studio MSBuild with the **C++ CMake tools for Windows** component, all client dependencies, and Inno Setup (`iscc` on `PATH`).

Mark the `race-server` runner and the production environment as protected. The deploy job is manual and runs only for a protected Git tag.

## Linux quality gates

`build:game:linux:sanitizers` configures a RelWithDebInfo build with `-DHOVERNET_WARNINGS=ON` (`-Wall -Wextra`) and `-DHOVERNET_SANITIZE=ON` (AddressSanitizer and UndefinedBehaviorSanitizer), then runs the whole ctest suite with sanitizer errors fatal. The compiler output is kept as the `build/sanitize/build.log` artifact so warning counts can be tracked. Both options are off by default; use them locally with the same CMake flags.

The community tracks are deliberately not built by CI: they are about 700 MB of
third-party files that are not in the repository. Only their small manifest
(`NetTarget/CommunityTracks.tsv`, which also holds each track's size and SHA-256)
is, and every package that needs it (game `.deb`, RaceServer `.deb`, Windows
installer, Windows RaceServer zip) includes the manifest. The game downloads
tracks on demand from GitHub releases; `scripts/package-community-tracks.sh`
builds the files to upload (see [Community tracks](community-tracks.md)). The
`HoverNetTrackDownloadSmoke` and `HoverNetCommunityTracks` ctest targets cover
the downloader (against a local `file://` host, so they need `curl` and `gzip` on
the Linux runner), the selector, and online races including a joiner downloading
the host's track.

## Server setup

On `192.168.10.181`, tag the registered system-mode runner with `race-server` and configure it to accept protected jobs. Install the root-owned deployment wrapper from `packaging/debian/hovernet-deploy`, then permit the runner account passwordless `sudo` only for that wrapper; do not give it unrestricted passwordless sudo. Open both TCP and UDP port `9600` in the host firewall.

The package creates the unprivileged `hovernet` service account, installs the server under `/usr/lib/hovernet`, preserves `/etc/hovernet/config.xml` across upgrades, and writes logs to `/var/log/hovernet`.

## Publishing

Add the requested GitLab remote without replacing the existing GitHub remote:

```bash
git remote add gitlab https://gitlab.outiva.com/devteam/hovernet.git
git push --set-upstream gitlab feat/gitlab-cross-platform-delivery
```

Merge through a protected default branch and create a protected version tag to expose the manual production deployment job.