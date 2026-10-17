# GitLab CI: web client and Linux race server

GitLab builds, tests and **deploys** two things, and nothing else:

| What | Jobs | Runs on |
| --- | --- | --- |
| Web client (https://hovernet.outiva.com) | `build:game:web` (also replays the determinism golden hashes on the WebAssembly build), `test:web:smoke`, `deploy:web`, `check:web:deployed` | default branch (build, smoke test and deploy on `main`), tags (all of it) |
| Linux race server | `build:raceserver:linux` (with the server's protocol, lifecycle and fuzz tests), `package:raceserver:deb`, `deploy:raceserver` | every pipeline builds; tags package and deploy |

Everything public lives in GitHub Actions instead: the Windows and Linux clients, the Windows and Linux servers,
all installers and packages, the sanitizer runs and the GitHub releases (see [github-actions.md](github-actions.md)).
GitLab no longer builds Windows, no longer builds the Linux client, and no longer publishes releases, so the
Windows runner is not used any more and can be retired.

## GitLab runners

Configure protected runners with these tags, or override the CI variables in GitLab:

- `race-server`: C++14 compiler, `dpkg-deb`, Bash, and narrowly scoped deployment privileges. It builds/packages the server and deploys tagged releases.
- `race-server` also runs the browser build. If `EMSDK` is unset, the web job installs the pinned SDK under the runner user's cache and reuses it on later builds.

Mark the `race-server` runner and production environments as protected. The RaceServer deploy runs automatically on protected version tags. The web deploy runs automatically from the protected default branch and for tags.

The browser client is built by `build:game:web` and packaged as a standalone
static artifact. `deploy:web` runs from the default branch after the web build
and has its own `production-web` resource group. Tagged web deployments run
automatically too. On `192.168.10.181`, install
`packaging/web/hovernet-web-deploy` as the root-owned
`/usr/local/sbin/hovernet-web-deploy` wrapper and permit the `gitlab-runner`
account to invoke only that wrapper with passwordless sudo. The wrapper
validates/extracts the artifact and atomically switches the Apache release
symlink. It does not enable Apache sites or install the WebSocket gateway;
those server components must be configured before enabling the deploy job.

## Quality gates

The full test suite and the nightly ASan/UBSan run are in GitHub Actions. GitLab runs the race server's protocol, lifecycle
and fuzz tests on every build, and the web build must replay the determinism golden hashes (`scripts/test-determinism.sh`).

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

## Release engineering checks

`HoverNetReproduciblePackages` builds each Debian package twice and fails if they
differ; `HoverNetRuntimeDependencies` checks the shipped Linux binaries' shared
libraries against `packaging/debian/runtime-dependencies.tsv` and the packages'
`Depends`. Release jobs run `scripts/make-release-checksums.sh` to publish a
`SHA256SUMS`. See [Releasing](releasing.md).

## Server setup

On `192.168.10.181`, tag the registered system-mode runner with `race-server` and configure it to accept protected jobs. Install the root-owned deployment wrapper from `packaging/debian/hovernet-deploy`, then permit the runner account passwordless `sudo` only for that wrapper; do not give it unrestricted passwordless sudo. Open both TCP and UDP port `9600` in the host firewall.

The package creates the unprivileged `hovernet` service account, installs the server under `/usr/lib/hovernet`, preserves `/etc/hovernet/config.xml` across upgrades, and writes logs to `/var/log/hovernet`.

## Publishing

Add the requested GitLab remote without replacing the existing GitHub remote:

```bash
git remote add gitlab https://gitlab.outiva.com/devteam/hovernet.git
git push --set-upstream gitlab feat/gitlab-cross-platform-delivery
```

Merge through a protected default branch and create a protected version tag to run the production deployment jobs automatically.

### Pipeline speed

Build jobs use `needs: []` so Linux, Windows and web builds start immediately and
in parallel instead of waiting for earlier stages. The web job stays in the first
(`web`) stage so it is queued ahead of the native builds when runner slots are scarce. `build:game:web` keeps its CMake
tree in `~/.cache/hovernet-web-build` on the runner (guarded by `flock`), so
pipelines rebuild only changed sources; delete that directory to force a clean
build. Web builds are skipped for changes that touch no web inputs (docs only).

### Nightly sanitizers

`build:game:linux:sanitizers` (ASan + UBSan build and full `ctest`) is slow, so it runs
only from a nightly pipeline schedule, not on pushes, merge requests or tags. Every other
job skips scheduled pipelines (`.no_schedule`). Create the schedule once on the GitLab
server with `sudo bash packaging/web/gitlab-nightly-schedule.sh` (02:00 Europe/London).

## Web smoke test

`test:web:smoke` (stage `package`) unpacks the web artifact and runs `scripts/test-web-client.sh`, which serves it
and drives it in headless Chrome as a phone: first frames, briefing, main menu, local race, pause (race controls
must hide), resume, and the setup screen opened from the pause menu. Screenshots are kept as job artifacts. It needs
Node and Chrome/Chromium on the web runner; without them the job exits 77, which is allowed, and the deploy never
waits for it.

## The HoverNet 3.0 server

`build:authserver:linux` builds the server and runs its lobby and protocol tests; `package:server:deb` makes the `hovernet-server` Debian package;
`deploy:server` (a tag's pipeline only, automatic, after the package job) installs it on the race-server host through `/usr/local/sbin/hovernet-server-deploy`. Install
that wrapper and its one sudo rule with `packaging/server/install-deploy-helper.sh` (see [operators.md](operators.md#deploying-from-gitlab-the-race-server-host)).
None of these jobs has run on GitLab yet.

## When a pipeline runs

`workflow:` in `.gitlab-ci.yml` creates a pipeline only for a merge request, the `main` branch and a tag. A push to another branch (for example
`gitlab-mobile`) starts nothing on GitLab: that is by design, not a failure. GitHub Actions runs on every branch.
