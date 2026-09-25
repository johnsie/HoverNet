# GitLab CI and Race Server Delivery

The pipeline builds and packages the central `RaceServer` and native Linux game client as Ubuntu `amd64` Debian packages. It also builds the Windows client and creates an Inno Setup installer.

## GitLab runners

Configure protected runners with these tags, or override the CI variables in GitLab:

- `race-server`: C++14 compiler, `dpkg-deb`, Bash, and narrowly scoped deployment privileges. It builds/packages the server and deploys tagged releases.
- `windows`: PowerShell, Visual Studio MSBuild, all client dependencies, and Inno Setup (`iscc` on `PATH`).

Mark the `race-server` runner and the production environment as protected. The deploy job is manual and runs only for a protected Git tag.

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