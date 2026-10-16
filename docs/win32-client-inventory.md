# Win32 client inventory and retirement plan

Stage 0 deliverable of [roadmap-3.0.md](roadmap-3.0.md). It lists every feature that only the legacy Win32/MFC client
(`NetTarget/Game2`, about 18,600 lines plus `Game2.rc`) has, compared with the shared SDL2/ImGui client
(`NetTarget/LinuxClient/Track3DViewer.cpp` and friends, which also runs on Windows x64 and in browsers), and assigns
each one a disposition so retirement is a dated decision rather than a drift.

Dispositions: **Covered** (the shared client already does it), **Migrate** (build it in the shared client before
retirement), **Replace** (the need is real but the feature is redesigned), **Retire** (drop it, with the reason).

How this was made: the Win32 menu and dialog resources (`Game2.rc`), its command handlers (`GameApp.cpp`), and its
source files were read against the shared client's windows, settings and command line. Items marked *to confirm*
rest on a read of the code and have not been exercised on a running legacy client.

## Menu commands (`FIREBALL_MAIN_MENU`)

| Win32 feature | Shared client | Disposition |
| --- | --- | --- |
| Game > Internet (F2): public room list, join | Online lobby: browse, host, join, chat through RaceServer | **Covered** |
| Game > Practice (Ctrl+N): local race | Local Race screen (track, laps, weapons) | **Covered** |
| Game > Split Screen: two players on one machine, upper/lower view (`NewSplitSession`, `eUpperSplit/eLowerSplit`) | None | **Migrate**, decision needed: either a real two-viewport local mode (needs the renderer to draw two views, Stage 3) or retire in favour of online play. Proposed: migrate in Stage 3 with the new renderer, since the simulation already supports two local craft. |
| Game > Network > Connect to TCP server / Create a TCP server (direct host and join by address) | Hosting and joining go through RaceServer only | **Retire** for gameplay. Direct client-to-client hosting is excluded by the no-peer-to-peer rule (players are on different networks; all traffic routes through RaceServer). A LAN user runs RaceServer locally; document that. |
| Game > Internet Meeting Room (public/private room dialogs, `InternetRoom.cpp`, 3,789 lines) | Lobby covers browse/host/join/chat | **Replace**: the legacy dialog talks to the long-gone HoverRace meeting-room web service; the lobby is its successor. Room passwords/private rooms: *to confirm* whether RaceServer needs them (Stage 2 accounts decision). |
| Game > Registration info, registration key, login/password dialogs, licence nag, demo-licence and beta-notice bitmaps (`License.cpp`, `IDD_REGISTER`, `IDD_LOGIN*`, `IDD_REGKEY_PASSWD`, `IDD_LEGAL_NOT`) | None | **Retire.** The game is free and open; the registration back end no longer exists. |
| Game > Exit | Quit confirmation screen | **Covered** |
| View > Following camera (F3) / Cockpit (F4) | Camera views including cockpit | **Covered** |
| View > Larger / Smaller (+/-): viewport size | UI scale and fullscreen/window settings; no in-race viewport resize | **Replace**: resolution scaling plus UI scale cover the need; revisit with the new renderer (Stage 3). |
| View > Players list (F5) | Players list in race | **Covered** |
| View > More messages (F6) | Message history (partial) | **Covered**, *to confirm* the scroll depth matches |
| View > Refresh colors (F12), `AssignPalette`, display intensity page (`IDD_DISPLAY_INTENSITY`) | None needed | **Retire.** Both exist because of 8-bit palette video modes; the shared client renders true colour. Brightness is partly replaced by the high-contrast option. |
| Setting > fixed resolutions 320x200 to 640x480 (F8/F9 combinations), Window (Alt+Enter) | Resolution, fullscreen and window settings | **Covered** (modern resolutions) |
| Setting > Properties: property sheet with display, audio, controls pages | Settings, Controls screens; remapping, controller axes, dead zone, inversion | **Covered** |
| Help > Contents (`Help.doc`, `NetTarget/Help`) | How to Play screen | **Migrate** (small): fold anything in the old help not in How to Play into the Stage 5 manual. |
| Help > GrokkSoft WWW site, The Beta Zone | None | **Retire.** Defunct sites. Link the project site from About instead. |
| Help > About | About text in the client | **Covered** |

## Other Win32-only behaviour

| Win32 feature | Disposition |
| --- | --- |
| Ladder: match report and score submission to the old ladder server (`MatchReport.cpp`, `IDD_LADDER_*`, the `[LADDER_ANSWER_1]` HTTP protocol) | **Replace.** The ladder service is gone. Rankings come back as accounts and ratings with server-verified results in Stage 5. The result data (`MatchReport` fields) is useful input to that design. |
| Sponsor and banner screens (`Sponsor.cpp`, `Banner.cpp`, `sponsor.dat`) | **Retire.** Advertising for a service that no longer exists. |
| Intro video (`Intro.avi`) and the intro/about bitmaps | **Retire** the video (needs a Windows codec path). Keep the artwork for About and the splash if wanted. |
| Windows registry settings (`HKCU\Software\HoverNet`) | **Covered**: the shared client keeps settings in a per-user config file. Migration of existing registry values on first run of the new client: *to decide* (small importer, or start fresh). |
| Win32 UDP/peer-to-peer code in `NetInterface.cpp` (2,791 lines) and `NetworkSession.cpp` | **Retire.** Superseded by the RaceServer relay; the Stage 2 protocol replaces it. |
| Observer class (`Observer.cpp`): the in-race view, HUD, messages, split mode | **Replace.** The shared client has its own HUD; the class is the Stage 1 seam between simulation and rendering and is dissolved by the renderer work. |
| Mixed 16/32-bit GDI/DirectDraw video paths, `BadMode` dialogs | **Retire.** Not applicable to SDL2. |
| Crash handling with `__try/__except` around session start | **Covered** by the shared client's error screens; crash reporting for Windows x64 is tracked under Windows crash debugging. |
| Installer shortcuts and file associations (Inno Setup) | **Covered** by the x64 installer; the legacy build is a separate package. |

## Summary of work this creates

1. **Split screen**: one decision (migrate in Stage 3, or retire). Everything else that is Migrate is documentation.
2. **Registry importer**: decide.
3. **Private rooms / passwords**: decide in Stage 2 with accounts.
4. All **Retire** rows are removals with no replacement; none changes gameplay.

## Retirement timeline (proposal, matches the Stage 3 plan)

| When | Step |
| --- | --- |
| Now | Legacy client stays in the 2.x packages, unchanged apart from fixes. This inventory is the baseline. |
| Stage 3, midpoint | Deprecation notice in the legacy client's About and on the download page; decision recorded on split screen. |
| Stage 3 exit | Web and desktop parity matrices accepted, split screen resolved; the Win32 client is removed from 3.0 packages. The final 2.x release remains downloadable as the legacy build. |
