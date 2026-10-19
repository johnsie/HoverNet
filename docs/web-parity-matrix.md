# Web client parity matrix

The web client at https://hovernet.outiva.com must match the shared desktop client (Windows x64 / Linux). This
matrix tracks the player experience in [2.0-product-contract.md](2.0-product-contract.md) against it. "Verified"
says how; anything without evidence is listed as **not verified** rather than assumed. Browser-only additions
(touch controls, phone layouts, Game Size, VR) are additive and listed at the end.

Last updated 2026-10-17 (rows 11 and 12 for HoverNet 3.0 servers, from the tests described below, run against v2.3.5 plus later local changes). Evidence comes from `scripts/test-web-client.sh` (headless Chrome, phone-size
touch viewport, run against a local build and the live site), headless screenshots at phone, iPad and desktop
sizes, and `scripts/test-determinism.sh` (native vs WebAssembly).

| # | Flow (desktop client behaviour) | Web status | Evidence / gap |
| --- | --- | --- | --- |
| 1 | First run: driver briefing, then main menu | Verified | Smoke test clicks through all four briefing pages to the main menu |
| 2 | Choose a valid player name (Save blocked until set) | Partly | Settings screen shows "Enter a display name" and a disabled Save; typing via the page's text bridge on a phone is **not verified** in a browser |
| 3 | Settings: display, audio, UI scale, accessibility, server address | Partly | Screen renders and scrolls at phone, iPad and desktop sizes; changing and saving values, window mode/fullscreen and audio are **not verified** |
| 4 | Remap keyboard / controller | Not verified | Controls screen renders; capturing a new binding and controller input not exercised in a browser |
| 5 | Local race setup: track, laps, weapons | Verified | Setup screen reached and Start Race starts a race (smoke test); track list in the browser **not verified**. Community-track download: the browser cannot fetch GitHub's release downloads (no CORS), so the web build downloads from the web host's own `/community-tracks/` (script and Apache snippet in `packaging/web/`); the sync script was tested natively, **the browser download itself has not been run** |
| 6 | Pick a hovercraft during the countdown | Not verified | Single player and, since rules 7, online races simulate the countdown, so the pick is part of the race. Native: tested against a real server (`HoverNetCraftPick`, `HoverNetAuthority`). The browser build has no online play yet and the wasm build was not run for this |
| 7 | Race: steering, accelerate, brake, jump, fire, weapon, camera views | Partly | Race runs and responds to pause; keyboard play in a browser is **not verified**; touch pad rendering seen in screenshots; VR controls not tried on a headset |
| 8 | Race physics, collisions, weapons, pickups | Verified | Scripted two-craft races with missiles replay with identical hashes on native (Release, Debug, sanitizer) and WebAssembly for all seven bundled tracks ([determinism.md](determinism.md)); a native-vs-browser difference in wall bounces was found and fixed |
| 9 | Pause menu: resume, new local race, online, settings, controls, quit | Verified | Smoke test: Escape pauses, Resume returns, New Local Race opens the setup screen |
| 10 | Results screen after finishing | Partly | The smoke test opens the results screen with a test hook and clicks KEEP DRIVING (phone size, headless Chrome); a real finish in a browser has not been run. The personal-best lines ("new personal best", "your best finish here") are shared code; checked on Linux in a headless screenshot, not yet in a browser |
| 11 | Online lobby: browse, host, join, chat | Verified on HoverNet 3.0 servers | `scripts/web-smoke/web-servers-screen.mjs` (headless Chrome, real `HoverNetAuthServer`, through a TLS proxy): connect, host, join, lobby and race chat (typed with real key presses), watch, and watch-before-start (the browser asks to watch a race that has not started, stays out, and is let in when it starts). Not verified: the old 2.x lobby, the production web server |
| 12 | Online race, including mixed browser and native players | Verified on HoverNet 3.0 servers | The same test runs a two-player race between the browser and a native scripted player, and the browser follows every tick's state hash exactly. Not verified: a real internet connection, a native desktop client (not a scripted bot) against a browser |
| 13 | Errors recover to a menu with a message | Not verified | Server-down and lost-connection paths not exercised in a browser |
| 14 | Track and asset loading, caching | Partly | First-load download and IndexedDB cache work in headless runs; slow-network and storage-full cases not tested |
| 15 | Supported browsers | Not verified | Only Chrome (headless) has been run; Firefox, Edge and Safari/iOS not tested |
| 16 | Ghost of the best run (Settings toggle, drawn beside the craft) | Partly | Shared code; checked on Linux in a headless client test (save on a new best, shown next race, hidden by the setting) and the WebAssembly build compiles; **not run in a browser** (the browser smoke test does not finish a race) |
| 17 | Watch a Replay (saved races of a 3.0 server, play, delete) | Partly | Shared code; the native client is tested end to end (the saved file equals the server's replay, the menu plays it to the verified hash); the browser build compiles and its main and pause menus fit at phone size in the smoke test; the browser's saving and playing a replay is **not run** |
| 18 | Server replays and Leaderboards (everyone's races and times per craft, download and watch) | Partly | Shared code over the lobby's WebSocket-capable connection; the native client is tested against a real server (list, records, a checked download, played to the verified hash); the browser build compiles; **not run in a browser** |
| 19 | Recent winners and Monthly league tabs (device id for the league's cheat checks) | Partly | Shared code; native tested against a real server (winners list, league, seeded history, cheat rules); the browser keeps its device id in the same settings store; the wasm build is **not yet built or run in a browser** for this |
| 20 | Records broken tab on Leaderboards | Partly | Shared code (Leaderboards screen, lobby client); native tested against a real server (rules, lobby message, the tab); the wasm build is **not yet built or run in a browser** for this |
| 21 | Online races on community tracks (host, join, watch; download first) | Partly | Shared code; native tested against a real server (`HoverNetOnlineCommunityTrack`: real clicks, download and a server hosting it, and a server without the folder refusing; `HoverNetLobby`: the server's rules); the browser build downloads from the web host's `/community-tracks/` but was **not run in a browser** for this |

## HoverNet 3.0 servers in the browser

The browser build has the same online lobby (ONLINE MULTIPLAYER, with the classic lobby as the fallback) and plays on protocol 3.0 servers over a
WebSocket. **Verified** (headless Chrome, a real `HoverNetAuthServer` on the loopback interface with the page's origin allowed, reached over
`wss://` through a small TLS-terminating proxy in the test (a self-signed certificate, the same job Apache or nginx does in production); `scripts/web-smoke/web-servers-screen.mjs`): connect through the screen, host a one-player time trial and follow the
whole race exactly (every tick's state hash and every snapshot restore); host a two-player race that a native bot joins, follow it exactly,
receive the bot's chat message, type a chat message with real key presses and see it come back, and watch a race two bots are running as a
spectator (also followed exactly). **Not verified:** `wss://` through the real production web server (Apache) and its certificate, phones and tablets (including the soft
keyboard), Firefox/Safari. The browser takes a `wss://` address (its
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
