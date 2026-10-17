# Web client parity matrix

The web client at https://hovernet.outiva.com must match the shared desktop client (Windows x64 / Linux). This
matrix tracks the player experience in [2.0-product-contract.md](2.0-product-contract.md) against it. "Verified"
says how; anything without evidence is listed as **not verified** rather than assumed. Browser-only additions
(touch controls, phone layouts, Game Size, VR) are additive and listed at the end.

Last updated 2026-10-16 (v2.0.18). Evidence comes from `scripts/test-web-client.sh` (headless Chrome, phone-size
touch viewport, run against a local build and the live site), headless screenshots at phone, iPad and desktop
sizes, and `scripts/test-determinism.sh` (native vs WebAssembly).

| # | Flow (desktop client behaviour) | Web status | Evidence / gap |
| --- | --- | --- | --- |
| 1 | First run: driver briefing, then main menu | Verified | Smoke test clicks through all four briefing pages to the main menu |
| 2 | Choose a valid player name (Save blocked until set) | Partly | Settings screen shows "Enter a display name" and a disabled Save; typing via the page's text bridge on a phone is **not verified** in a browser |
| 3 | Settings: display, audio, UI scale, accessibility, server address | Partly | Screen renders and scrolls at phone, iPad and desktop sizes; changing and saving values, window mode/fullscreen and audio are **not verified** |
| 4 | Remap keyboard / controller | Not verified | Controls screen renders; capturing a new binding and controller input not exercised in a browser |
| 5 | Local race setup: track, laps, weapons | Verified | Setup screen reached and Start Race starts a race (smoke test); track list and community-track download in the browser **not verified** |
| 6 | Pick a hovercraft during the countdown | Not verified | Needs left/right during the countdown; on touch the steering stick should do it |
| 7 | Race: steering, accelerate, brake, jump, fire, weapon, camera views | Partly | Race runs and responds to pause; keyboard play in a browser is **not verified**; touch pad rendering seen in screenshots; VR controls not tried on a headset |
| 8 | Race physics, collisions, weapons, pickups | Verified | Scripted two-craft races with missiles replay with identical hashes on native (Release, Debug, sanitizer) and WebAssembly for all seven bundled tracks ([determinism.md](determinism.md)); a native-vs-browser difference in wall bounces was found and fixed |
| 9 | Pause menu: resume, new local race, online, settings, controls, quit | Verified | Smoke test: Escape pauses, Resume returns, New Local Race opens the setup screen |
| 10 | Results screen after finishing | Partly | The smoke test opens the results screen with a test hook and clicks KEEP DRIVING (phone size, headless Chrome); a real finish in a browser has not been run |
| 11 | Online lobby: browse, host, join, chat | Not verified | Gateway upgrade and foreign-origin refusal are checked after deploy (`check-web-deploy.sh`); an actual lobby session is **not verified** |
| 12 | Online race, including mixed browser and native players | Not verified | Same protocol and relay; no mixed race has been run |
| 13 | Errors recover to a menu with a message | Not verified | Server-down and lost-connection paths not exercised in a browser |
| 14 | Track and asset loading, caching | Partly | First-load download and IndexedDB cache work in headless runs; slow-network and storage-full cases not tested |
| 15 | Supported browsers | Not verified | Only Chrome (headless) has been run; Firefox, Edge and Safari/iOS not tested |

## HoverNet 3.0 servers in the browser

The browser build has the same HoverNet Servers screen (main menu, results and pause screens) and plays on protocol 3.0 servers over a
WebSocket. **Verified** (headless Chrome, a real `HoverNetAuthServer` on the loopback interface with the page's origin allowed, a plain
`ws://` connection; `scripts/web-smoke/web-servers-screen.mjs`): connect through the screen, host a one-player time trial and follow the
whole race exactly (every tick's state hash and every snapshot restore); host a two-player race that a native bot joins, follow it exactly,
and receive the bot's chat message. **Not verified:** `wss://` through the production web server, spectating in a browser, typing chat in
a browser, phones and tablets, Firefox/Safari. The browser takes a `wss://` address (its
default is `wss://<this site>/auth/`, the server's default path) and has no server-key field: encryption is TLS at the web server.

## Browser-only additions (not in the desktop client)

- Touch controls: steering stick (drag up to jump), GO, BRAKE, FIRE, JUMP, WEAPON, MENU; shown only in a race.
- Phone layouts for briefing, main menu, local setup, lobby, settings and controls; rotate prompt on phones only.
- Widescreen race frame on phones; Settings > Game Size on desktop.
- Meta Quest VR mode (stereo race view, controllers, chase/cockpit toggle); inert unless entered from a headset browser.

None of these change game rules; the race simulation is the same shared code.

## Next steps to close the gaps

1. Extend the smoke test to finish a race and check the results screen (rows 10, 6).
2. Add a browser test against a local RaceServer through the gateway for the lobby and a two-browser race (rows 11 to 13).
3. Run the smoke test in Firefox and WebKit; run a manual pass on a real iPhone, iPad and Quest (rows 2, 3, 7, 15).
