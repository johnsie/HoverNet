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
the web artifact automatically after building it; tagged deploys are manual.
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
