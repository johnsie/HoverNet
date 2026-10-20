# Browser client and server setup

## Build the browser client

Install the Emscripten SDK and activate the version declared by
`WEB_EMSDK_VERSION` in `.gitlab-ci.yml`. This checkout has Emscripten 3.1.74
installed in `.tools/emsdk` (ignored by Git). In a new shell, run:

```sh
source .tools/emsdk/emsdk_env.sh
emcmake cmake -S . -B build/web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/web --target HoverNetGame2Player --parallel 2
```

The output is `build/web/HoverNet.html` with `HoverNet.js`, `HoverNet.wasm`, and
`HoverNet.data`. The data file contains the game resource library, bundled
tracks, community manifest, and menu artwork. The HTML shell stores the shared
client's XDG settings in IndexedDB.

The browser client opens a binary WebSocket at
`wss://hovernet.outiva.com/race/` by default. For local development, override
`HOVERNET_WEB_SOCKET_URL` at CMake configure time, for example with
`ws://localhost:8080/race/`.

## Mobile and touch devices

On touch devices (`pointer: coarse`) `shell.html` adds a fullscreen button, a
controls toggle, a portrait "rotate your device" hint, and an on-screen race pad
(steer, GO, BRAKE, FIRE, JUMP, WEAPON, MENU). Held buttons are passed to the
game through the exported `hovernet_set_touch_mask` function and combined with
keyboard and gamepad input, so remapped keys do not matter; MENU taps Escape for
the pause menu. Menus work through SDL's touch-to-mouse emulation. The canvas
fills the page (SDL follows its CSS size); the in-race 4:3 frame is letterboxed.
The page blocks pinch-zoom and pull-to-refresh and uses `100dvh` so mobile
toolbars do not clip the canvas. The pad's visibility is remembered.

The start-up screen shows a progress bar driven by Emscripten's download status
(`Downloading game data — x of y MB`, then `Preparing…`), and a Try again button
if loading fails.

## Install the WebSocket gateway

The gateway relays bounded binary RaceServer frames between a browser WebSocket
and the existing local TCP listener on port 9600. It accepts the production
site's HTTPS origin, limits concurrent browser sessions to 64, and listens on
loopback port 9601 so Apache is the only public WebSocket endpoint.

Install the Python `websockets` dependency from `requirements.txt` into the
system Python (or install the matching distro package), then install the
gateway and service file as root:

```sh
sudo install -o root -g root -m 0644 packaging/web/websocket_gateway.py /usr/lib/hovernet/websocket_gateway.py
sudo install -o root -g root -m 0644 packaging/web/hovernet-websocket-gateway.service /etc/systemd/system/hovernet-websocket-gateway.service
sudo systemctl daemon-reload
sudo systemctl enable --now hovernet-websocket-gateway.service
```

The service runs as the existing `hovernet` system account and does not expose
its listener outside loopback. The RaceServer remains on its existing port and
native clients continue to connect directly.

## Apache and TLS

The virtual-host template is `hovernet.outiva.com.conf`. It serves the current
immutable web release, supplies the WebAssembly MIME type and browser security
headers, redirects HTTP to HTTPS, and proxies `/race/` to the local gateway.
Cross-origin isolation headers are required for the existing background track
download worker in the Emscripten build; third-party assets must therefore be
served with compatible CORS/CORP headers or from this same origin.
Enable Apache's `ssl`, `headers`, `expires`, `proxy`, `proxy_http`, and
`proxy_wstunnel` modules. Install and enable the virtual host after DNS resolves
to the HAProxy frontend and an origin certificate exists at:

- `/etc/ssl/certs/hovernet-origin.crt`
- `/etc/ssl/private/hovernet-origin.key`

Validate the global Apache configuration before reloading it. A failure in any
enabled site's configuration prevents a safe reload of all sites.

## GitLab deployment

The `race-server` runner downloads the pinned Emscripten SDK into its user cache
when `$EMSDK` is not configured. The protected default-branch pipeline deploys
the web artifact automatically after building it; tagged releases deploy automatically too.
The job invokes `sudo /usr/local/sbin/hovernet-web-deploy` with a versioned
archive.
Install that script root-owned and grant the GitLab runner passwordless sudo
only for this wrapper. The wrapper accepts artifacts only from the runner build
tree, extracts only the four expected regular files, publishes to
`/var/www/hovernet.outiva.com/releases/<version>`, and atomically switches the
`current` symlink. Apache should have read-only access to the releases.

The wrapper deliberately does not alter Apache sites, certificates, system
packages, or services. Configure the gateway and virtual host separately
before enabling production deployment.

## Repair the browser multiplayer gateway

On Python 3.10 or later, Debian's `websockets 9.1` package passes a removed
`loop` argument to `asyncio.Lock`, so browser WebSocket handshakes fail. To
install the compatibility fix and restart the gateway on `.181`, run:

```sh
sudo bash packaging/web/fix-websocket-gateway.sh
```

## Configure the host

The public TLS certificate terminates on the HAProxy server. Apache still
needs a certificate for the HTTPS backend connection. Copy
`fix-lifehub-backend-tls.sh` to `.181` and run it with sudo; it creates a
self-signed certificate with DNS and backend IP subject names, validates
Apache, and reloads Apache with rollback on failure:

```sh
scp packaging/web/fix-lifehub-backend-tls.sh john@192.168.10.181:/tmp/
ssh -t john@192.168.10.181 'sudo bash /tmp/fix-lifehub-backend-tls.sh'
```

Configure HAProxy to trust the origin certificate if its backend uses
certificate verification. After public DNS for `hovernet.outiva.com` resolves
to HAProxy, run `sudo bash /tmp/hovernet-web-setup/setup-host.sh` on `.181`.
That script creates a self-signed HoverNet origin certificate, installs the
WebSocket gateway and scoped GitLab deploy sudo rule, and configures the
Apache vhost. It does not request a public certificate because HAProxy owns
the public TLS certificate.

## Mobile behaviour

- **Text fields**: menu text boxes are drawn by ImGui inside the canvas, so phones
  never raise a keyboard for them. `shell.html` keeps a hidden real `<input>`;
  tapping a field (the game reports `hovernet_wants_text`) focuses it, and typed
  text/Backspace/Enter go back to the game as SDL events (`hovernet_text_input`,
  `hovernet_text_key`). The page resizes to the visual viewport while the
  keyboard is open so the field stays visible.
- **Screen space**: race controls and the controls/fullscreen buttons appear only
  during a race (`hovernet_in_race`); the portrait hint is a slim banner rather
  than a blocking overlay; the canvas respects the notch/safe-area insets; phones
  default to an 85% UI scale until the player picks one in Settings.

## VR (Meta Quest browser)

In a headset browser that supports WebXR (Meta Quest 2/3 browser; `?vr=1` forces the button for
testing) an **ENTER VR** button appears once the game is running. Desktop and phone browsers never
show it and never run any of the VR code.

- **View:** a large virtual screen in front of you. In a race the game renders the 3D view twice per
  frame (`MR_Observer::SetEyeOffset`: the chase camera shifted 140 mm sideways per eye, 70 mm in the
  cockpit view) and the page draws one image per eye (`window.hnVR` in `shell.html`), so you get real
  stereo depth. The Quest browser uses a 640x480 frame to keep two renders per frame affordable.
- **Race controls:** left or right stick steers, trigger GO, grip BRAKE, right A FIRE, right B JUMP,
  right stick click WEAPON, left X switches chase/cockpit view, left Y opens the pause menu.
- **Menus:** point the right controller at the screen and pull the trigger to click. Typing needs the
  keyboard, so enter your name before putting the headset on.
- `HOVERNET_TEST_VR=<dir>` (native) writes each eye's frame as a PPM, for checking the stereo camera
  without a headset.

## Never serving an older build

A CI job for an old commit (a retry, a late runner) must not replace a newer release: the old page would then be the live one, and an old
page has no update check. The CI job and `deploy-local-build.sh` stamp the page with the commit time (`<meta name="hn-order">`, from
`git log -1 --format=%ct`); `hovernet-web-deploy` reads it from the archive and from the live `current/index.html` and refuses a build that is
older than the live one, or that has no stamp when the live one does (equal is allowed, so a tag of the live commit deploys). A rollback
is a new commit that reverts. `scripts/test-web-deploy-order.sh` tests the rule. The wrapper is installed by hand and is not updated by a
deploy: reinstall it (`sudo install -o root -g root -m 0755 packaging/web/hovernet-web-deploy /usr/local/sbin/`) after changing it.
If a stale release is live anyway, `scripts/web-set-current.sh <release>` on the web host points `current` back.

## Outdated cached pages (phones)

`shell.html` carries a build id (`<meta name="hn-build">`, the placeholder `__HN_BUILD__` filled in by the CI web job, and by
`deploy-local-build.sh`, with the tag or commit). The `hn-fresh` script asks the server for the page's current copy with `cache: 'no-store'`;
a different id makes the page reload once through `?v=<id>`, and the wasm and data files are requested with the same `?v=`. A page that
comes back later (restored tab, resumed home-screen app) only shows an update bar. A build whose placeholder was not filled in does
nothing, so local builds are unaffected. `scripts/web-smoke/web-stale-page.mjs` tests the script against a server that plays an old copy
(run by `scripts/test-web-client.sh`). The server side is unchanged: HTML, js, wasm and data are already sent with `no-cache`.
