# HoverNet web client roadmap

## Goal and parity target

Deliver a playable browser client at **https://hovernet.outiva.com**, served by Apache on `192.168.10.181`. Match the supported **shared Windows x64 client** (`HoverNet.exe`) in its user-visible menus, settings, controls, tracks, race rules, HUD, and gameplay behavior. The current 64-bit client is the shared SDL2/ImGui client, also built on Linux; it is the reference implementation, not the legacy Win32/MFC client.

“Exactly the same” is the acceptance target for behavior and presentation. Browser constraints still require browser-shaped inputs, storage, audio policy, and networking. These adaptations must preserve the same actions and outcomes; they must not silently remove game features. Publish a parity matrix and get the product owner to accept any unavoidable browser-specific differences before calling the web client complete.

## Current baseline and main gaps

- `HoverNetGame2Player` builds the shared SDL2/ImGui client used for the x64 Windows package. `NetTarget/LinuxClient/Track3DViewer.cpp` contains much of the shared client and game loop.
- The web build statically links the same `ObjFac1` factory and packages the shared resource library, tracks, manifest, and menu assets. Browser startup and persistence use the shared SDL2/ImGui client with an IndexedDB-backed settings directory.
- The browser client frames the existing RaceServer protocol over WebSocket. A bounded gateway relays those messages to the existing TCP service, and Apache proxies the WebSocket endpoint.
- Browser persistence, window sizing, controller access, audio start policy, fullscreen, and keyboard focus still need screen-by-screen comparison with the Windows x64 experience.
- GitLab CI now builds and deploys the web artifact from the protected default branch; tag deployments remain manual.

## Implementation status

The repository now has an Emscripten build path, browser shell and IndexedDB
settings mount, static object-factory registration, browser WebSocket framing,
browser Fetch-based community-track downloads, a bounded TCP/WebSocket gateway,
an Apache virtual-host template, and a validating web artifact deploy wrapper.
Emscripten 3.1.74 builds the shared client locally. The server's Apache vhost
and origin TLS certificate are installed, and the WebSocket gateway is active.
The default-branch CI deploy is configured to publish the first release.

Screen-by-screen parity, browser online-race interoperability, and clean-profile
browser behavior remain open acceptance items. The first live release must be
checked in supported browsers before the client can be called fully equivalent
to the Windows x64 version.

## Delivery phases

### 1. Freeze parity contract and browser support

1. Record the x64 client reference build and capture every screen and flow: onboarding, main menu, local race setup, race, pause, results, settings, controls, online lobby, hosting/joining, chat, and track download.
2. Turn the x64 product contract and `docs/linux-client.md` into a per-feature parity matrix with screenshots, input steps, and expected results.
3. Choose supported desktop browsers and minimum versions. Define desktop viewport, keyboard, mouse, and controller expectations; mobile support is a separate decision.
4. Agree that the first browser release requires network access for online play and specify behavior when audio, storage, or controller permission is unavailable.

**Exit:** the web acceptance matrix is approved and covers every x64 core flow.

### 2. Prove the WebAssembly port

1. Add an Emscripten toolchain/build target for the shared client. Start with SDL2's Emscripten port and the existing ImGui renderer/input path to retain layout, rendering, and game loop behavior.
2. Make platform services explicit behind small adapters: startup, files, settings, clocks, audio, input, networking, and browser lifecycle.
3. Replace dynamic `ObjFac1` loading in the web target with a statically linked factory. Package the same `ObjFac1.dat`, official tracks, fonts, and other runtime assets in the build output; confirm their licenses and URL paths.
4. Implement asynchronous browser file loading and settings persistence using IndexedDB or local storage. Do not block the browser main thread on file, network, or audio operations.
5. Check keyboard focus and prevent game controls from scrolling the page only while the game canvas is focused. Map pointer lock/fullscreen requests to explicit user gestures. Start/resume audio from a user gesture.
6. Produce an initial local-play build and compare it screen by screen and race behavior by behavior with the x64 reference.

**Exit:** the browser build starts reliably, renders the same menus, and completes a local race with matching track, physics, HUD, sound, pause, and results behavior. Any known differences are listed in the parity matrix.

### 3. Add browser online play

1. Define a transport interface around `RaceServerClient` so the game client does not depend directly on POSIX/WinSock TCP calls.
2. Implement a browser WebSocket transport that preserves the current protocol framing, protocol-2 negotiation, limits, timeouts, and message ordering.
3. Provide a gateway on `192.168.10.181` between WebSocket connections and the existing TCP RaceServer on port 9600. Keep protocol validation and connection/rate limits at the boundary. Decide whether it is a small supervised service or a WebSocket endpoint added to RaceServer; document the chosen owner and lifecycle.
4. Route `wss://hovernet.outiva.com/...` through Apache to that gateway. Keep the existing native clients on their current TCP endpoint.
5. Exercise lobby presence, chat, host/join, start, gameplay relay, disconnect, reconnect/error paths, and browser-to-native mixed races. Validate that WebSocket-origin checks do not block supported clients and that unauthenticated web clients cannot abuse the gateway with oversized or excessive frames.

**Exit:** browser-to-browser and browser-to-x64/Linux races complete using the same RaceServer race behavior, and transport failures return the user to a working menu with a clear error.

### 4. Browser parity and hardening

1. Complete keyboard remapping, controller mapping, settings preview/save/cancel, onboarding, accessibility options, and all x64 menu flows.
2. Verify all bundled tracks and representative community track downloads. Serve any downloadable content with correct CORS and content types, or implement the existing download source through browser fetch APIs.
3. Test current Chrome, Firefox, and Edge desktop releases on clean profiles, common viewport sizes, keyboard-only navigation, controller present/absent, slow network, lost connection, storage disabled/full, and audio denied until user gesture.
4. Measure load time, memory, steady-state frame rate, race latency, and asset transfer size. Set budgets against the x64 experience and tune only where needed to meet them without changing game behavior.
5. Run accessibility and input-focus checks; ensure the canvas can be exited and the browser remains navigable.

**Exit:** every core parity-matrix row passes, no high-severity browser defect remains, and performance/load budgets are met on the supported browser set.

### 5. CI, Apache, and production delivery

1. Add a Linux CI job that installs/pins Emscripten, configures the web target, builds the assets, and publishes a versioned static artifact. Add automated smoke checks for required output files, asset URLs, MIME types, and a headless browser startup/local-race scenario where runner capacity permits.
2. Keep merge-request builds non-deploying. Deploy the protected default branch after its web build, and keep tag deployments manual. Serialize releases with a `production-web` resource group and retain the previous release for rollback.
3. Add a narrowly scoped root-owned deployment wrapper, analogous to `packaging/debian/hovernet-deploy`, for the web artifact. It should validate the artifact and version, unpack to a new immutable release directory, then atomically switch the Apache document root symlink. Do not grant the runner general passwordless sudo.
4. Configure Apache name-based TLS virtual hosting for `hovernet.outiva.com`, with document root pointing at the active release. Public TLS terminates at HAProxy; Apache uses a self-signed origin certificate for the HTTPS backend. Confirm DNS and routing to HAProxy before release.
5. Enable required Apache modules (`ssl`, `headers`, `proxy`, `proxy_http`, `proxy_wstunnel`, and compression as appropriate). Serve `.wasm` as `application/wasm`, JavaScript with a JavaScript MIME type, and apply immutable caching to versioned assets. Keep the HTML entry point revalidated so releases take effect promptly. Proxy the WebSocket path to the gateway with TLS and suitable idle/read timeouts.
6. Set a restrictive Content-Security-Policy and other browser security headers after inventorying the actual asset, worker, and network needs. Enable cross-origin isolation headers only if the chosen Emscripten build requires shared memory/threads; those headers can affect embedded resources.
7. Add post-deploy checks for HTTPS certificate/redirect, landing page, WASM MIME response, asset fetches, browser startup, WebSocket upgrade, online lobby, and rollback to the previous release.

**Exit:** a protected tag can deploy a tested immutable build to Apache, checks pass from outside the server, and rollback is documented and demonstrated.

## Server and network checklist

- Verify DNS for `hovernet.outiva.com` points to `192.168.10.181` (or its public NAT address), and that inbound TCP 80/443 is routed to Apache. The address is private, so public DNS alone will not make the site reachable from the internet without routing/NAT or a VPN.
- Identify the existing Apache sites, certificate management, document-root conventions, firewall, and runner account before preparing the deploy wrapper.
- Keep the current RaceServer and native TCP port 9600 deployment independent from web static-file releases. Add only the gateway listener/proxy port needed for WebSockets, preferably bound to localhost behind Apache.
- Give Apache read-only access to immutable release files. Give the runner deployment privilege only through a validating wrapper, following the existing deploy pattern.
- Define log rotation, health checks, disk cleanup for old releases, and an emergency rollback command.

## Recommended implementation order

Complete phases 1 and 2 before committing to the online gateway design. The local-play port determines whether the SDL2/ImGui and factory reuse strategy is viable. In parallel after the port proof, design the WebSocket gateway and Apache route. Treat phase 3 as a separate milestone because browser networking requires a new service boundary. Do not announce full web parity until all phase 4 and production phase 5 exit criteria pass.
