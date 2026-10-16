# GitHub Actions: clients, servers, packages and releases

GitHub Actions (`.github/workflows/build.yml`) builds, tests, packages and **publishes** everything public. GitLab only
builds and deploys the web client and the Linux race server (see [gitlab-ci.md](gitlab-ci.md)).

## What it builds

| Job | Runs on | Produces |
| --- | --- | --- |
| `linux` | push, pull request, tag | Linux client and race server, the whole ctest suite (physics, determinism, tracks, protocol), `hovernet-game_*.deb`, `hovernet-raceserver_*.deb` |
| `windows` | push, pull request, tag | shared 64-bit client with installed-package acceptance tests, `HoverNet-*-windows-x64-setup.exe`, the legacy `HoverNet-*-win32-setup.exe`, `hovernet-raceserver_*_win32.zip` and `hovernet-raceserver_*_win64.zip` |
| `raceserver-sanitizers` | push, pull request, tag | the race server's protocol, lifecycle and fuzz tests under ASan and UBSan |
| `sanitizers-full` | nightly (03:17 UTC) and manual | the whole test suite under ASan and UBSan |
| `dependency-review` | pull request | dependency security review |
| `release` | `v*` tags | `SHA256SUMS` and the public GitHub release |

Workflow artifacts (installers, `.deb` files, server zips) are kept for 30 days on every run.

## Releases

Pushing a tag that starts with `v` runs the builds and, only if **every** job passed, creates the public release with
all packages and `SHA256SUMS`. A hyphenated tag (`v2.1.0-beta.1`) is published as a pre-release with the beta notes;
a stable tag starts with `docs/release-notes.md`. Push the same tag to GitLab to deploy the web client and the server.
The repository setting **Settings > Actions > General > Workflow permissions** must allow workflows to create releases.

## Deployment

GitHub does not deploy. Production deployment stays in GitLab so the protected runner, environment approvals and the
`/usr/local/sbin/hovernet-deploy` and `hovernet-web-deploy` privilege boundaries are unchanged.
