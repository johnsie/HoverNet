# Changelog

All notable changes to HoverNet. Versions are the `vX.Y.Z` git tags the release
pipelines build from. See [Releasing](docs/releasing.md) for how a release is made
and how to roll one back.

## Unreleased

## 2.3.14 - 2026-10-18

### Added
- **Replays and records show the players' names.** Track records and the server replay list now say who set a time ("Anna 2:34.00"), and the question of which craft to follow names the winner. The server records the display name each player had when the race started in the results table beside the replay (the replay file itself still holds no names). Names are what players typed: nothing proves a name belongs to the person using it until accounts exist. Races saved before this show "player 2" as before.
- Records and replay lists are trimmed to fit one network frame; with many lap counts the old list could have outgrown it.

### Changed
- The server's replay and records lists carry names, so **update the game together with the server** (a 2.3.13 game cannot read the lists from a 2.3.14 server).

## 2.3.13 - 2026-10-18

### Fixed
- **A race that everybody had left kept running on the server, so restarting the server could wait ten minutes.** If the players of a started race closed their tabs, the race ran on to its time limit with nobody in it, and a restart (which lets races under way finish first) waited for it. A started race with no player and no spectator for two minutes (long enough for a dropped connection to resume) is now removed. Tested in the lobby test.

## 2.3.12 - 2026-10-18

### Fixed
- **Upgrading the server from the pipeline did not restart it.** The packaged server's install script waited for the restart (which drains races under way, up to 11 minutes) and the installation hung, leaving the package half installed and the OLD server running; on the live host that meant no replays were saved after the replay feature shipped. The package now queues the restart and finishes, and a new pipeline job, `verify:server`, waits for the new process and fails the pipeline if the old build is still running, if the package is not fully installed, or if the server runs without `--replay-dir`.

## 2.3.11 - 2026-10-18

### Added
- **Restart race.** In a local (offline) race, the pause menu and the results screen now have **RESTART RACE**: the same track, laps and weapons again, from the grid. (Not offered in online races or replays.)

### Fixed
- **A race on a HoverNet server said "Local" on the HUD.** Races from the HoverNet servers lobby, including one you host alone with **Start Race**, were always online on the server (and recorded as server replays), but the HUD wrote "Local". It now says "Online" with your ping, and "Replay" while watching a replay.
- **The motor sound crackled like a vinyl record (seen in the browser), and engine sounds kept playing after leaving a replay.** The game's software mixer converted each sound with SDL's audio stream, which pads the end of a sound with silence, so every pass of the motor loop had a dropout at the loop point; it stepped through samples without interpolating them; it changed volume and pitch in one jump per frame (a click every frame); and it cut voices off instead of fading them. The mixer now converts each sound itself without padding, loops seamlessly, interpolates, glides volume and pitch over about 10 ms, fades voices in and out, and limits loud mixes softly instead of clipping them. Engines also fade out by themselves when the game stops asking for them (a menu, the end of a race or a replay). The browser's audio buffer is larger so a slow frame does not leave a gap. The old Windows-only OpenAL path now loops engine sounds too (it restarted them from the top each frame). Tested offline (`HoverNetSoftMixer`: no clicks at loop points, volume or pitch changes, starts and stops); not heard on a speaker, so tell me if the browser still crackles.

## 2.3.10 - 2026-10-18

### Added
- **You can tell whose craft you are watching in a replay.** Before a replay with more than one player starts, **Watch a Replay** asks which craft to follow, with the likely one marked and preselected: your own craft for a race you drove (**(you)**), the winner for a server replay (**(winner, 2:34.00)**). Track records still follow the player who set the record. The replay then says "Following player 2 of 3 (the winner)" when it starts and whenever you switch craft with `[` and `]`, and the timeline label shows "Player 2". Replays hold player slots, not names, so "player 2" is the grid slot.

### Changed
- The server's replay list carries the winner's slot, so **update the game together with the server**: a 2.3.9 game cannot read the list from a 2.3.10 server (it stays on "Loading the list...").

## 2.3.9 - 2026-10-18

### Fixed
- **Track records were never saved on a server installed from the package.** The server only saves a race's replay (and with it the records and the shared replays) when it is given a replay folder, and the packaged configuration had that line switched off. The packaged service now always passes `--replay-dir /var/lib/hovernet/replays`, the packaged configuration turns it on (and sets `replay-keep 200`), so servers upgraded from an older install start saving too. Only races on the server count for the records: a race against the clock on your own device counts toward your personal bests (the Track records tab now shows yours beside the server's, and says so when a track has no records).
- **Several menus had no way back to the main menu, and leaving the online lobby could quit the game.** The pause menu and the results screen now have a **MAIN MENU** button, and leaving the online lobby (or failing to reach the server) from a race, a replay or the results screen goes to the main menu instead of closing HoverNet.
- **A replay ended abruptly on a frozen picture.** It now ends with a screen that says the replay is finished, lists who finished and when, and offers **Watch again**, **Watch another replay**, **Main menu** and **Quit** (Esc stays on the last frame; Left or Home rewinds). Starting a race or going to the lobby from a replay now leaves the replay cleanly.

## 2.3.8 - 2026-10-18

### Fixed
- **The "A newer version of HoverNet is available" bar no longer comes back after you tap it.** In 2.3.7 the page compared its build id with a pattern that the packaged (minified) page does not match, so it always thought it was out of date. It now reads the id with an HTML parser, and the test runs the real packaged page, not only a hand-written one.

## 2.3.7 - 2026-10-18

### Fixed
- **A phone can no longer keep running an outdated web client.** Each web build now carries its own build id. When the page opens it asks the server for its current copy (never from a cache) and, if it is out of date, reloads once through a fresh address; the game data is fetched under the same id. If the page is brought back later (a restored tab, the app resumed from the home screen) and is out of date, a blue bar offers to update instead of reloading by itself, so a race is never interrupted. Tested in headless Chrome with a server that plays an old cached copy. Not tried on a real iPhone.
- **The Windows client was missing the race time, lap line and countdown on the HUD.** The texts came from a Windows string table the new client does not have; they now use the same texts as Linux and the browser.
- **Linux and browser frames now match Windows exactly.** Their stand-in for the Windows `MulDiv` truncated instead of rounding, so about 5% of each 3D picture was a pixel off from Windows. The simulation is untouched (same rules version, same golden physics and replays); only the picture changes, by at most one pixel at texture edges.

### Added
- **Everyone's replays and track records.** The server now keeps the newest official replays of its races (200 by default) and shares them with every player: **Watch a Replay, Server replays** lists them and plays any, including races you were not in, and **Track records** shows the fastest races and fastest laps for each track and lap count, with a button to watch the run that set each one. They are anonymous ("player 2"), checked when downloaded, and verifiable by anyone with the replay tool. Server owners: `--replay-keep N`. See [docs/replays.md](docs/replays.md).
- **Watch a Replay.** Races on a HoverNet server that you take part in, or watch from the start, are now saved on your device when they end (the newest 20), and **Watch a Replay** in the main menu and the pause menu lists them, plays the one you pick, and deletes old ones. The saved file is exactly the server's official replay of the race, and a race in which you missed any ticks is not saved. Nothing is sent anywhere. See [docs/replays.md](docs/replays.md).
- **Ghost of your best run.** When you set a new best finish on a track, the game keeps the path you drove and draws it beside your craft in your next race on that track and lap count, so you race your own best time (**Settings, Show my best run as a ghost**; on by default). It is only drawn: it cannot touch the race or the results, and it never leaves your device. See [docs/ghosts.md](docs/ghosts.md).
- **Replay timeline.** Watching a replay now shows a bar along the bottom with the time, the speed and a mark at every lap crossing and finish (yours brighter). Click the bar to jump there, or press **,** and **.** to jump to just before the previous or next lap. The marks appear as the replay plays through them. **C** steps the chase camera through closer and farther distances (replays and spectating).
- **Race view setting (Settings, Race view).** Choose the shape of the picture while racing: Classic (4:3, as before and still the default), Widescreen (16:9), or Match my window (follows your window or screen shape, between 4:3 and 2.4:1). The renderer draws a wide picture just as fast (about 6 ms for 1920x1080 on a desktop CPU), so a wide window or monitor no longer has to be pillarboxed. Takes effect the next time you start HoverNet. Not shown on phones and tablets, which always use a picture shaped like their screen.

## 2.3.6 - 2026-10-18

### Fixed
- **Builds for ARM (and for newer x86 CPUs) can no longer drift from other platforms.** The compiler may fuse a multiply and an add into one instruction that rounds differently; with that on, the determinism, snapshot and physics tests fail on this machine. Fusing is now switched off for every target (`-ffp-contract=off`, `/fp:precise`). The results on x86-64 are unchanged (same golden files, same rules version).

### Added
- **Personal bests.** The game remembers your fastest finish on each track (per number of laps) and your fastest lap, on your own machine only (nothing is sent anywhere). The results screen says "NEW PERSONAL BEST!" with the time you beat, or shows your best for the track. The browser keeps them with your other settings. The local race setup screen shows your best for the chosen track and number of laps. Delete the `personal_bests` file in the settings folder to forget them.

## 2.3.5 - 2026-10-17

### Fixed
- **Online: smoother motion (desktop and web).** The simulation moves in whole 16 ms ticks, but frames do not fall on tick boundaries, so the craft's on-screen speed swung from frame to frame (single player does not have this, its clock is continuous). The craft are now drawn between the current tick and the next. Measured on a scripted race: the speed change from one frame to the next fell from 14% to 4% on a clean link, and from 16-19% to 7-8% with 60 +- 40 ms of network jitter. The simulation and the rules version are unchanged. `scripts/test-smooth-motion.sh` now checks this too.

## 2.3.4 - 2026-10-17

### Changed
- **Watch Race works before the race has started.** Choose a race that is still waiting for players and press Watch Race: the lobby waits (the button turns into "Waiting for the race to start", click it to cancel) and lets you in as a spectator the moment the host starts the race. If the race is cancelled first you are told. Nothing changes on the server.

## 2.3.3 - 2026-10-17

### Fixed
- **Online: the sound no longer stutters.** The client rebuilds its prediction every frame and replays the last few ticks; the sounds those replayed ticks queued (jump, fire, bump, finish) were played again each time. Only a tick newer than any already heard can now make a sound.
- **Online: you can chat again after the results screen.** Opening the results screen turned text input off and it was never turned back on in the race, so typing did nothing.
- **Online: steadier picture.** The picture's clock may now run at most 12% faster or slower than real time while it catches up with the server (it was an unbounded fraction of the gap).


## 2.3.2 - 2026-10-17

### Fixed
- **Online: the craft no longer jumps around.** The picture of your craft was tied to the moment the server's ticks arrived, so any clumping of ticks on the network (common over the internet and through web proxies) made it stall and then lurch (measured: 0.44x to 1.88x of real time, up to 3.4x, with 60 +- 40 ms of jitter). The picture now runs on its own smooth clock and is only eased toward the server's (0.89x to 1.41x, the same as a perfect connection). A spectator's view is smoothed the same way.
- **Online: the craft no longer gets stuck at the end of the race.** When everyone has finished the server stops simulating, and the client kept putting the craft back where the server's last state had it. After the final result the client now drives the craft on from your own keys, as in single player (the other craft stay where they finished).

## 2.3.1 - 2026-10-17

### Changed
- A race on the HoverNet server always has seats for 8; the number-of-players slider is gone from the host dialog (the host starts the race with whoever is there, so it is redundant). A time trial is simply starting alone.

### Fixed
- The host can press **Start Race** even when nobody else has joined (to practise); the button was disabled until a second player arrived. Others can still join until the host starts.

## 2.3.0 - 2026-10-17

### Changed
- **The host starts the race.** A race on the HoverNet server no longer starts when the last seat fills: the host sees who has joined in the waiting room (player list, race chat) and presses **Start Race**; the race starts with whoever is there, even with seats empty. If the host leaves first, the others go back to the lobby. A one-player race (time trial) still starts at once. Protocol: capability bit 2, messages `StartRace` and `Roster` (additive; races hosted by older clients and the bots keep starting when full).

## 2.2.0 - 2026-10-17

### Changed
- **One way into online play.** The separate HOVERNET SERVERS button is gone: **ONLINE MULTIPLAYER** connects to the HoverNet server by itself (nothing to type but, once, your name) and opens the classic lobby if the HoverNet server is not available. A different server (an address and, optionally, a key) is chosen in **Settings**, under *HoverNet server (online multiplayer)*, not in the lobby. The lobby screen was rebuilt in the classic lobby's layout (game list, details with the track map, actions, host dialog) instead of a plain form.

### Added
- The race list refreshes by itself (every two seconds); there is no Refresh button, and your selection stays on the race you chose when the list changes.
- **Lobby chat and a list of who is in the lobby** on the HoverNet server and in both clients (protocol: capability bit 1 and message 34, additive; older clients are unaffected). Messages are rate limited and mutes apply.
- The client connects to a server by **name** (it only accepted a numeric IPv4 address), gives up after four seconds instead of hanging, and a missing server is reported at once.

## 2.1.1 - 2026-10-17

### Fixed
- **Web client: opening HOVERNET SERVERS froze the game.** The screen asked the browser for its own address with `emscripten_run_script_string`, which uses `eval()`; the site's Content-Security-Policy forbids `eval()`, so the page threw an error and stopped drawing. It now uses a function compiled into the page. The address field's default was also wrong (`ws://ws:///auth/`, from one shared buffer being read twice) and is now `wss://<this site>/auth/`.
- The browser tests now run under the production Content-Security-Policy (read from the repository's copy of the web server configuration) and include a real click on the main menu's HOVERNET SERVERS button; the 2.1.0 tests entered the screen through a test switch and so could not see this.

### Added
- Automatic deploy of the 3.0 server from a tag's GitLab pipeline (`deploy:server`, through a root-owned wrapper), `scripts/release.sh` (a guarded release from your own machine) and `scripts/setup-server-host.sh` (install and configure the 3.0 server on the host). The first tag after the host helper is installed will install the `hovernet-server` package there (enabled, not started).

## 2.1.0 - 2026-10-17

This release adds the HoverNet 3.0 server-side groundwork (authoritative server, lobby, private races, time trials, WebSocket transport, replay tools, signing tool). The desktop game and the browser game both have a HoverNet Servers screen for it; the deployed 2.0 race server is unchanged.

### Added
- `scripts/soak-authserver.sh`: a real-time soak of the authoritative server (rounds of eight-player races with scripted bots over TCP, memory and health sampled). One 60-minute run: 69 races, no failure, no memory growth; the multi-day gate is still open.
- **The browser plays on HoverNet 3.0 servers.** The web client has the HoverNet Servers screen and speaks protocol 3.0 over a WebSocket (a `wss://` address; the server side exists since `--ws-origin`). Checked in headless Chrome against a real server on the loopback interface: connect, host a time trial, a two-player race with a native bot (followed exactly), a chat message arriving, and watching a race as a spectator. The connection is `wss://` through a test TLS proxy; not yet checked: Apache/nginx in front, phones.
- **HoverNet Servers screen** (desktop client, main menu): connect to a 3.0 server by address (with the server's key for encrypted ones), see its open races, host a race (public, private with a code, or a one-player time trial), join, join a friend's private race by number and code, or watch a running race. Refusals are explained in plain words. The browser build does not have it: no browser transport for these servers exists yet. Tested against a real server (`HoverNetOnlineLobby`) and through the real client end to end (`HoverNetOnlineMenu`: screen, time trial, every tick verified); how it looks was checked in headless screenshots only, and no one has clicked through it yet.
- `HoverNetReplay results race.hnr` prints each player's rank, laps, finish time and best lap from a replay (the basis for leaderboards and time trials).
- `HoverNetDriver`, a search tool that drives a craft around a track and writes the replay, so leaderboards and events are tested with real finishers (`HoverNetLeaderboard`, Linux).
- `HoverNetReplay leaderboard a.hnr b.hnr ...` re-simulates replays and ranks finishers and best laps per track; damaged or non-reproducing replays are skipped, never ranked. `HoverNetAuthServer` also writes a `.results.tsv` table beside each official replay.
- `HoverNetTrackPack verify FILE --catalogue` enforces the catalogue admission rule: a named author and a CC0-1.0, CC-BY-4.0 or CC-BY-SA-4.0 licence.
- `HoverNetAuthServer` drains on SIGTERM/SIGINT for rolling restarts: races under way finish and are saved, new ones are refused (reject reason 10), `/health` returns 503, then it exits 0 (`--drain-timeout`). See docs/operators.md.
- **Private races on the authoritative server (protocol level).** A race hosted with a code (6 to 16 characters) is not listed and can be joined or watched only by id and code; a wrong code looks like a race that does not exist, and 30 wrong guesses lock the race. The bot takes `--code` and `--race-id`. The desktop game's HoverNet Servers screen offers it.
- **Browser transport for the authoritative server.** `HoverNetAuthServer --ws-origin https://site` accepts WebSocket upgrades on its port (origin allow-list, masked frames, bounded sizes, ping/pong, close codes) and feeds the same protocol 3.0 session layer as TCP. Behind a TLS-terminating web server this is the `wss://` path; no browser client uses it yet. See docs/operators.md.
- `HoverNetTrackPack lint FILE [--strict]` explains why a track is not a race (missing checkpoints), duplicate finish lines and short grids; the corpus report counts tracks with warnings.
- `HoverNetAuthServer --event-log FILE` writes server and race events as JSON lines for operators' tooling (no names or addresses).
- `HoverNetSign` (keygen, sign, verify) signs files with Ed25519; the implementation matches the RFC 8032 vectors and an independent reference. Nothing is signed yet: the project key must be generated by the owner. See docs/signing.md.
- Lap and finish events: `AuthoritativeRace::Events`, `HoverNetReplay events`, and a `laps` list in the server's event log (the basis for replay timeline markers).
- Time trials on the authoritative server: a lobby race may have one player. It starts at once, is saved as an official replay, and its result feeds `HoverNetReplay leaderboard`.
- A `hovernet-server` Debian package for the authoritative server (reproducible, dependency-checked; built by the GitHub Actions Linux job and attached to the release, and built by the GitLab jobs `build:authserver:linux` and `package:server:deb`). A tag's GitLab pipeline then installs it on the race-server host automatically (`deploy:server`, through a root-owned wrapper set up once with `packaging/server/install-deploy-helper.sh`); a fresh install is enabled but not started, an upgrade drains a running server first. These jobs have not run on GitLab yet.
- All Debian packages now ship the HoverRace licence text as `/usr/share/doc/<package>/copyright`, as the licence requires of binary distributions. The Windows x64 installer is set to install `LICENSE.txt` and `THIRD_PARTY_NOTICES.md` too.

### Changed
- The host of a private race is shown the race's number (and told to give it to friends with the code): the server sends it in the join acknowledgement, only to clients that ask for it (capability bit 0), so a client built before it is unaffected. Without this a private race could not be shared.
- Watching a replay or a race as a spectator: `[` and `]` move the camera and the HUD between the craft (a spectator can also use Left and Right; in a replay those still seek).
- Crossing the finish line no longer opens the results screen over the race. The player is told, in the race, that they can press Enter (the controller's Back button, or MENU on a touch screen) for their results and race options, and may keep driving as long as they like; the finish time is kept from the moment of the finish. Chat keeps working after the finish, and now also in races on HoverNet 3.0 servers (type, Enter to send; messages from the others appear on screen). The HoverNet Servers button is also on the results screen.

## 2.0.21 - 2026-10-17

### Added
- **Replay playback.** `HoverNet --replay race.hnr [--replay-player N]` plays a replay file back with the normal renderer, exactly as
  the race happened (P pauses, 1 to 5 set the speed). Replays are small, verifiable files; see [docs/replays.md](docs/replays.md).
  The game refuses a replay recorded under different rules or on a different version of the track instead of showing a wrong race.
- **Authoritative server (preview, for testers and server operators).** `HoverNetAuthServer` runs a race in which the server alone
  decides every position, collision, lap and result and players send only their controls; it writes an official replay of each race
  (`--replay-dir`). `HoverNetAuthBot` is a scripted player for it. It listens on the loopback address unless told otherwise and does
  not encrypt yet, so do not expose it to the internet. This is the first part of HoverNet 3.0's networking; the game's online play
  still uses the existing race server. See [docs/authoritative-race.md](docs/authoritative-race.md).
- **Playing on the authoritative server (preview).** `HoverNet --auth-connect HOST:PORT` joins a race on `HoverNetAuthServer` with the normal
  renderer and controls: the server decides the race, the game shows an exact copy of it plus a prediction of the next few ticks, and
  reconnects by itself if the connection drops. Command line only for now; see [docs/authoritative-race.md](docs/authoritative-race.md).
- **Lobby, spectating and moderation on the authoritative server (preview).** `HoverNetAuthServer --lobby` hosts many races: players host and join
  through the lobby, and anyone can watch a running race a few seconds behind the drivers (`HoverNet --auth-connect HOST:PORT --spectate`). Races support chat,
  blocking and reports; moderators use a loopback console with challenge-response login, and every moderation action goes into a tamper-evident audit log.
  See [docs/online-services.md](docs/online-services.md).
- **Encrypted, authenticated connections to the authoritative server.** `HoverNetAuthServer --identity-file KEY --require-encryption` serves players over a
  secure session (ChaCha20-Poly1305 with an X25519 handshake); players pin the server's key with `--server-key`. The cryptography is written out from the RFCs and
  checked against their test vectors, and has not yet had an independent review. See [docs/specs/secure-session.md](docs/specs/secure-session.md).
- **Track packages.** `HoverNetTrackPack` wraps a classic track in a versioned, verifiable package with author, licence and hashes without changing the track,
  and imported all 1,012 community tracks. See [docs/specs/track-package.md](docs/specs/track-package.md).
- Replays can be rewound and fast-forwarded in the game (Left, Right, Home).
- `--fixed-step` runs the game's simulation in whole 16 ms ticks whatever the frame rate (off by default).

### Fixed
- The checksum code that verifies a downloaded track read the file through a 64 KB buffer on the stack, which overflows WebAssembly's
  default 64 KB stack (it crashed the replay and server tools when they were built for WebAssembly). It now uses the heap, which removes a
  possible failure when the browser game verifies a downloaded community track.
- Missiles fly at the original game's speed again. The shared client had been rounding the missile speed down from
  46.66 to 46 units per millisecond, so its missiles were about 1.4% slower than the original game's and ended a few
  metres short; they also hit with a slightly different impulse. Missiles now use the original maths exactly, which
  also makes them agree with the legacy Windows client. Replays recorded before this change are marked as an older
  simulation version and are not verified against it.

## 2.0.20 - 2026-10-16

### Added
- Every race now tells the player how to open the pause menu as it begins: "Press Esc for the pause menu" on
  desktop (and in the browser), "Tap MENU for the pause menu" on touch devices, plus "Left/Right: pick your craft"
  (your own steering keys; "Drag stick: pick your craft" on touch) during the countdown. It shows for the first part of
  every race, including races started again from the menu.

### Fixed
- Tracks with a single start position left the unused start slots uninitialised, so a second craft or split screen
  read garbage there; the slots are now initialised.
- Wall contact on very large community arenas relied on signed integer overflow (undefined behaviour); the wrap is now
  explicit, with identical results. All 1,012 community tracks now replay the same in plain and sanitizer builds with no
  sanitizer errors (`scripts/sweep-community-determinism.sh`).
- CI: pushes to `main` now run the GitLab pipeline (the project's default branch is not `main`); the web smoke
  test job passes when the runner has no Chrome; the post-deploy web check runs against the local Apache.
- Windows build: no C++17 if-initialisers in the client (GitHub's Windows build now compiles).

## 2.0.19 - 2026-10-16

### Changed
- CI split: GitLab now only builds, tests and deploys the web client and the Linux race server. GitHub Actions
  builds the Windows and Linux clients and servers, runs the full tests (nightly under ASan/UBSan), makes every
  package and installer, and publishes the GitHub release. A 64-bit Windows race server zip is now packaged too.

## 2.0.18 - 2026-10-16

### Changed
- The determinism run now races two craft with weapons and hashes every free element (missiles, mines,
  power-ups); native Release, Debug, sanitizer and WebAssembly builds replay all seven bundled tracks
  identically.

### Fixed
- A signed-integer overflow in the wall-distance maths (found on Tidal Causeway under UBSan) now wraps
  explicitly; behaviour is unchanged.

## 2.0.17 - 2026-10-16

### Fixed
- Web client: wall bounces went the wrong way. A table-angle conversion cast a negative number to an
  unsigned one, which gives a different (wrong) result in WebAssembly than on the desktop builds, so any
  wall hit whose angle came out slightly negative bounced off at the wrong angle (and could leave the craft
  stuck against the wall). Found by the new cross-platform determinism run.

### Added
- `HoverNetDeterminismRun` and `scripts/test-determinism.sh`: scripted fixed-step races on all seven bundled
  tracks hashed frame by frame, checked against golden values on Linux (ctest) and on the WebAssembly build
  (web CI job). `MR_GameSession::SimulateStep` steps the simulation by an exact duration. See docs/determinism.md.
- `scripts/check-web-deploy.sh` and the `check:web:deployed` job: post-deploy checks of headers, file types and
  the WebSocket gateway on the live site.

## 2.0.16 - 2026-10-16

### Added
- `scripts/test-web-client.sh` and the `test:web:smoke` CI job: a headless-browser test of the web
  client (load, briefing, menu, local race, pause and resume) with screenshots kept as artifacts.

## 2.0.15 - 2026-10-16

### Changed
- Web client: tablets (iPad and similar) keep their desktop menu layouts and are no longer asked
  to rotate in portrait; only phones are.
- Web client on phones: Settings and Controls have a slim header, and Settings keeps Save and
  Cancel pinned under the scrolling options.
- Web client on phones: the wide race frame is nudged so the whole hovercraft stays in view.

### Fixed
- Web client: the on-screen race controls no longer float over the screens opened from the pause
  menu (New Local Race, Settings, Controls) or the results screen.
- README describes the browser, phone, tablet and VR play.

## 2.0.14 - 2026-10-16

### Added
- Web client: VR mode for the Meta Quest browser. An ENTER VR button (shown only in a headset
  browser) puts the game on a big virtual screen with real stereo 3D in races -- the game renders
  one view per eye -- and a controller to drive, fire, jump and switch between the chase and
  cockpit views; menus are pointed at and clicked with the controller. Desktop and phone
  browsers are unaffected.

### Fixed
- A hovercraft could stop dead against a wall or platform instead of bouncing (most often at
  high speed): the bounce is triggered by a contact ring slightly wider than the space the craft
  may move in, so it could rest just outside it. A craft whose moves keep being refused is now
  bounced back.

## 2.0.13 - 2026-10-16

### Fixed
- Web client: after a browser stall (hidden tab, long pause, slow phone frame) the race no longer
  replays the whole gap at once, which threw hovercraft into walls and platforms and let
  missiles skip past targets.

## 2.0.12 - 2026-10-15

### Changed
- Web client on phones: dragging the steering knob up jumps (the JUMP button still works).

## 2.0.11 - 2026-10-15

### Changed
- Web client on phones: the two steering arrows are replaced by a steering slider -- drag the
  knob left or right to steer. The other race controls are unchanged.

### Fixed
- Web client on phones: the pause menu's top buttons (Resume, New Local Race, Online
  Multiplayer) could not be tapped because the race controls floated over them; the race
  controls are now hidden while paused.

## 2.0.10 - 2026-10-15

### Added
- Web client (desktop): a "Game Size" setting sizes the gameplay area inside the browser
  window (Fit, or 800x600 up to 1920x1080) without resizing the browser. Remembered per browser.

### Changed
- Web client (phones): the race frame matches the screen's shape (960 wide, about 2.2:1 in
  landscape) instead of a letterboxed 4:3 frame.

### Fixed
- Wide framebuffers were stretched horizontally because the 3D projection assumed a 4:3 screen;
  pixels are now square at every resolution (4:3 output is unchanged).
- Web client on phones: less flicker -- frames are paced to the display and the canvas is no
  longer resized when iOS toolbars slide in and out.

## 2.0.9 - 2026-10-15

### Changed
- Web client on phones: holding the phone in portrait now shows a full-screen "Rotate your
  phone" prompt on every screen (including loading and the menus), with a
  "Continue in portrait" option for the rest of the session.

## 2.0.8 - 2026-10-15

### Changed
- Phone layouts for the web client: the main menu (portrait stack and compact landscape),
  the Driver Briefing (auto-sized cards, touch-control page on touch devices), the Internet
  Meeting Room (tabs for games/race, chat and users) and Local Race Setup (slim header,
  reachable buttons).

### Fixed
- The main menu footer showed the version as "vv2.0.7".

## 2.0.7 - 2026-10-15

### Fixed
- Web client on phones no longer gets stuck on the loading screen: startup used `eval`, which
  the site's Content-Security-Policy blocks, so the game stopped before drawing its first frame.
- Windows x64 installer packaging in GitHub Actions now stages every client asset
  (tutorial image, fonts and licences) and the package validator checks for them.

## 2.0.6 - 2026-10-15

### Fixed
- Web client on phones: real startup errors are no longer overwritten by "Still loading…",
  stray page errors no longer break the loader, the last startup log lines are shown while
  loading, and a browser that draws no frame within 25 seconds falls back to the software
  renderer automatically.

## 2.0.5 - 2026-10-15

### Fixed
- Web client: sliders and clicks now follow the pointer at any window size (SDL no longer
  rescales mouse events into 1024x768 while a frame sleeps).

### Changed
- The online lobby screen is titled "Internet Meeting Room".

## 2.0.4 - 2026-10-15

### Fixed
- Web client: menus, sliders and text boxes are no longer laggy. The browser build now
  renders with WebGL instead of the CPU software renderer (falling back to software if
  WebGL is unavailable), and frames are paced to a 16 ms budget.

## 2.0.3 - 2026-10-15

### Fixed
- Web client: the desktop GUI no longer appears stretched, and phones no longer show a
  blank screen after the download (the loader stays up until the first frame is drawn,
  and startup errors are shown on the page).
- The sanitizer job runs only from a nightly schedule.

## 2.0.2 - 2026-10-15

### Changed
- Every CI deploy is automatic: version tags deploy both the web client and the RaceServer.
- The web build is queued ahead of native builds, and the runner allows three concurrent jobs.

## 2.0.1 - 2026-10-15

### Fixed
- Web client on phones: menu text boxes (username, chat, server) can now be typed
  into on iOS; the race controls appear only during a race; the page fits above the
  on-screen keyboard and inside the notch/safe area; phones default to a tighter UI scale.

### Changed
- CI builds run in parallel, the web build is incremental, and docs-only changes skip it.

## 2.0.0 - 2026-10-02

HoverNet 2.0, the first stable release of the 2.0 line (it follows the public beta).
See the [release notes](docs/release-notes.md).

### Changed
- The redesigned lobby: no header bar; the Users list now includes you and everyone
  else in the lobby; the game details show the map, track, laps, weapons, players,
  style and status; the waiting room shows all eight players without scrolling; the
  chat keeps 200 lines, scrolls, and stays on the newest message; only the main
  action is a red button.
- A build made from files that differ from its commit now shows `+local` after the
  version (for example `v2.0.0+local`).
- Stable tags publish with `docs/release-notes.md`, which invites feedback through
  GitHub issues.

### Known
- The 24-hour race-server soak test is still running for this build.

## 2.0.0-beta.2 - 2026-10-01

Replaces beta.1, whose `.deb` file names GitHub rewrote (`~` became `.`), so
`SHA256SUMS` could not verify them.

### Fixed
- Debian package file names use the plain tag version (`hovernet-game_2.0.0-beta.2_amd64.deb`)
  while the package's own version stays `2.0.0~beta.2`, so checksums verify and a
  beta still sorts before the final release.

## 2.0.0-beta.1 - 2026-10-01 (superseded by beta.2)

First public beta of HoverNet 2.0, published as a GitHub *pre-release*. Everything
in 0.4.x, now treated as the 2.0 feature set; see the
[beta notes](docs/beta-release-notes.md) and [test plan](docs/beta-test-plan.md).

### Changed
- Pre-release tags (`vX.Y.Z-beta.N`) are published as GitHub pre-releases, and the
  Debian packages use `X.Y.Z~beta.N`, so a beta always sorts before the final
  release on upgrade.
- Decision: the 32-bit Windows client stays in 2.0 as the compatibility fallback.

## 0.4.1 - 2026-10-01

### Fixed
- Windows x64 readiness audit: an uninitialised loop condition in collision force
  maths, a missing virtual destructor on bitmap resources, type-punned pointer and
  enum access, bad `printf`/`sscanf` arguments, and a `memset` of a class object.
  Track data loads identically (all 1,009 community tracks and the 7 bundled ones).
- File archives now reject, at compile time, any field type whose size differs
  between Win32, Windows x64 and Linux, so shared file formats cannot silently
  diverge.

### Added
- Windows client acceptance in both CI pipelines: a local race on every bundled
  track and the community-track downloader (verified on a real Windows x64 runner).
- `scripts/test-cross-platform-race.sh`: Windows x64 <-> Linux races through a Linux
  race server, both directions (release rehearsal).
- [x64 audit](docs/x64-audit.md): method, MSVC `/W4` results (no pointer-truncation
  warnings) and what is not covered.

## 0.4.0 - 2026-10-01

### Added
- Community tracks download on demand: pick one in any track selector and press
  *Download & Start Race*, *Download & Create* or *Download & Join*, or run
  `hovernet --download-community-tracks --all`. Downloads are verified against a
  SHA-256 in the manifest before they are installed.
- `SHA256SUMS` is published with every release.
- Debian packages are reproducible: building the same commit twice gives
  byte-identical packages (`SOURCE_DATE_EPOCH` is honoured).
- Third-party notices and a checked runtime-dependency inventory
  ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

### Fixed
- The `hovernet-raceserver` package now depends on `libstdc++6` and `libgcc-s1`
  (the server could fail to start on a minimal system without them).
- A missing track file on the download server is reported as "not available yet"
  instead of a connection problem.

## 0.3.1 - 2026-10-01

### Added
- A library of about 1,000 classic community tracks, played unmodified and
  listed below the official tracks in every selector, with a search box. Free-play
  tracks (no finish line) show elapsed time instead of laps.
- A lenient `HoverNetTrackValidator --community` level, a manifest generator and
  pack/install scripts.
- The race server accepts community tracks listed in `CommunityTracks.tsv`.
- Linux `-DHOVERNET_WARNINGS=ON` / `-DHOVERNET_SANITIZE=ON` build options and a CI
  job that runs the whole test suite under AddressSanitizer and
  UndefinedBehaviorSanitizer.

### Changed
- Remembered race choices are stored by track name instead of list position
  (older files are still read).
- A level's negative start angle is normalised when it loads.

### Fixed
- Out-of-bounds trig-table reads, a `delete`/`delete[]` mismatch in MazeCompiler,
  32-bit overflows in the wall renderer and in collision and map maths, a divide by
  zero on zero-height walls, bad room links, and leaks on the client's quit path.
- Several community-track loading faults: missing classic object types, a vertex
  clamp that misaligned section data, and an unbounded audible-room count.

## 0.3.0 - 2026-10-01
Tag only; its release run failed while packaging the Windows x64 build (the
GitHub workflow did not stage the new track manifest). Superseded by 0.3.1.

## 0.2.2
Local tag only (never pushed): the GitLab Windows runners locate CMake when it is
not on `PATH`.

## 0.2.1 - 2026-10-01
- Content and tools (roadmap phase 4): seven bundled tracks validated, the
  command-line track compiler and validator, a track-authoring guide, HoverCad
  declared legacy.

## 0.2.0 - 2026-09-29
- Complete 2.0 user experience (roadmap phase 3): one consistent menu, settings,
  pause, lobby and post-race flow; remappable controls and controller support;
  display, audio and persisted settings; an improved HUD; onboarding; accessibility
  options; the shared SDL2/ImGui client on Windows and Linux.
- Installed-Windows acceptance tests for the client and its online lobby.
