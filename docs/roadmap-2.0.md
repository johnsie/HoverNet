# HoverNet 2.0 Roadmap

HoverNet 2.0 should be the first polished, cross-platform release rather than a full engine rewrite. The existing foundation includes Windows and Linux clients, working multiplayer, automated packaging, four bundled tracks, the Manta hovercraft, and a broad Linux smoke-test suite.

### Client convergence policy

HoverNet 2.0 is the transition release toward one shared desktop client. The
legacy Win32/MFC client remains supported through 2.0 as a compatibility and
release fallback, but receives only essential parity, accessibility, stability,
security, and packaging work. Substantial new client features should be built in
the shared SDL2/ImGui path and made portable across Windows and Linux rather than
implemented twice. Where schedule and risk allow, 2.0 should ship the shared
client on both platforms; otherwise the legacy Windows client remains available
until the shared Windows build passes the same functional and release gates.

Retiring the legacy Windows runtime is deliberately a HoverNet 3.0 gate, not a
reason to delay a stable 2.0 release.

Assuming one or two regular contributors, this is a realistic six-to-eight-month roadmap beginning in October 2026.

## Implementation status

Phase 0 is in progress. Its initial repository-backed deliverables are:

- [2.0 product contract](2.0-product-contract.md)
- [Windows/Linux platform inventory](2.0-platform-inventory.md)
- [Current network protocol baseline](2.0-network-protocol.md)

Protocol 2.0 negotiation is implemented: lobby traffic is gated behind a
versioned handshake, compatible 2.x minors negotiate down, and incompatible
majors receive a bounded rejection. RaceServer identifiers and automatic-element
headers are explicitly encoded, and the 20-byte character bit stream no longer
uses unaligned host-word access. Missile, mine, and power-up state bodies are
now also encoded through a shared `HoverNetWire` fixed-width, alignment-safe
helper (`NetTarget/Util/WireFormat.h`) instead of native-layout structs, with
`SetNetState` bounds-checked against the wire size before reading. This closes
out the remaining native-layout simulation payload conversion from Phase 0.

Phase 1 is in progress. Disabled diagnostic logging and machine-specific paths
have been removed repository-wide (MazeCompiler, Model/Game2 cross-platform
code, and the Windows-only rendering/audio/lobby code, the last of which also
had dead debug `MessageBox` dialogs that would have interrupted a real player
hosting or joining a race). A cross-platform lobby bug is fixed: hosting or
joining a not-yet-started race no longer drops a player from the RaceServer's
lobby user list or lobby-wide chat, which had made a Windows host invisible
(and unreachable by chat) to Linux clients still browsing -- covered by a new
regression test in `RaceServerClientSmoke.cpp`. Resource-loading and config
failure handling has also been cleaned up: a missing/corrupt `ObjFac1.dat` now
logs the path that was tried instead of failing silently, `DllObjectFactory`
no longer returns a dangling pointer when a DLL fails to open, RaceServer's
still-placeholder `--config` loading no longer falsely reports success, and a
mid-race RaceServer disconnect is now announced on-screen instead of silently
degrading to solo play. A repeatable frame-time and memory baseline now also
exists: `HoverNetMemoryBaselineSmoke` runs a 600-frame autoplay session and
fails if steady-state RSS grows meaningfully from the first half of the run
to the second, catching a leak as a trend instead of relying on someone
noticing a multi-hour session gradually slowing down.

A real, previously-shipping bug was also found and fixed along the way: a
Windows player hosting or joining a not-yet-started race was dropped from
the RaceServer's lobby user list and lobby-wide chat immediately, before the
race even started, making them invisible to (and unreachable by chat from)
Linux clients still browsing. Fixing that surfaced that the Windows client's
"waiting room" was a second, modal dialog layered on top of the lobby one,
which froze the lobby's own chat behind it for the whole wait; hosting/joining
now stays in the lobby dialog until the race actually starts.

Phase 2 has also started early: RaceServer now enforces per-connection rate
limits on both chat (`ClientConnection::AllowChatMessage`) and race creation
(`AllowRaceCreation`), a fixed-window limiter generous enough to clear
`RaceServerClientSmoke.cpp`'s existing 25-message TCP-coalescing burst test
without weakening the limit meaningfully against real flooding. A third,
per-IP connection-attempt rate limit (`MR_ServerSocket::AllowNewConnection`)
caps how fast one non-loopback address can open new connections, exempting
127.0.0.1 outright since every real player connects from their own distinct
public IP and the only traffic that would otherwise throttle is local
testing. All three limits are covered by regression tests confirming the
exact accept/reject counts, each verified to actually fail without its
corresponding server-side check.
The protocol's advertised maximum payload is now enforced per connection as
well: a peer that sends a larger frame is disconnected without affecting other
clients, covered in both compatibility and strict-protocol integration runs.
Incomplete TCP frames also expire five seconds after their first byte, even if
the sender keeps trickling fragments; the stalled peer is removed without
blocking or desynchronizing other clients.
Player names, race/track labels, and lobby chat are now bounded canonical UTF-8
text; malformed sequences, embedded controls/NULs, overlength fields, and
trailing host-request bytes are rejected before storage, logging, or relay.
Started-race chat keeps its legacy byte-index alphabet with a strict size bound.
Message direction and minimum connection state are explicit: clients cannot
submit server-only replies/announcements, and gameplay traffic is ignored until
the sender has joined a race.
Repeated invalid direction, state, player-name, or chat commands are capped per
connection and the abusive peer is disconnected after the ten-message window.
RaceServer configuration is loaded transactionally with bounded values and is
wired into connection limits, TCP options/buffers, player disconnect timeout,
race capacity, cleanup timeout, and log level. The server emits a structured
ten-second health record with uptime, live connections, races, and players.
When a waiting-room host disconnects, ownership now transfers to a remaining
player and the server sends a refreshed join acknowledgement so that player's
client can start the race instead of leaving an orphaned waiting room.
Automated lifecycle coverage now enforces maximum concurrent races, proves
capacity is reclaimed when the sole host disconnects, requires a clean SIGTERM
exit, restarts immediately on the same port, and verifies no race state leaks
across the restart.
The decoder also has a deterministic 500-frame black-box fuzz test with random
message types, payload bytes, and TCP fragmentation. GitHub CI runs protocol,
lifecycle, and fuzz scenarios under AddressSanitizer and UndefinedBehaviorSanitizer.
A repeatable soak driver composes those workloads for the release gate; its
required invocation is `scripts/run-raceserver-soak.sh build/linux 86400`.

## Phase 0 — Define the 2.0 contract

**October 2026 · 1–2 weeks**

- Write a concise product specification covering local play, multiplayer, supported platforms, and compatibility.
- Inventory Windows and Linux differences.
- Document the existing network protocol and identify which RaceServer code paths are active versus unfinished scaffolding.
- Decide the compatibility policy:
  - 2.0 clients must reject incompatible servers cleanly.
  - Existing tracks and resource files remain supported.
  - Server-authoritative physics is deferred unless anti-cheat becomes a requirement.
- Convert the roadmap into tracked issues with owners and acceptance criteria.

**Exit gate:** The intended 2.0 player experience and protocol compatibility rules are unambiguous.

## Phase 1 — Stabilize the existing game

**October–November 2026 · 4–6 weeks**

- Finish and commit the current Windows rendering and input cleanup.
- Remove disabled diagnostic logging and machine-specific paths.
- Run systematic playtesting for:
  - local races;
  - Windows to Windows;
  - Linux to Linux;
  - Windows to Linux;
  - hosting, joining, leaving, reconnecting, and race completion.
- Fix crashes, hangs, focus problems, and resource-loading failures.
- Add clean handling for missing tracks, missing object libraries, server outages, and malformed settings.
- Establish a repeatable frame-time and memory baseline.

A Linux mid-race SIGFPE affecting both local and multiplayer play was traced from
kernel trap records to `RenderAlternateWallSurface`: distant or edge-on geometry
could round one projected wall edge to zero height and use that height as a
divisor. Degenerate edges now use zero ratio variation with 64-bit intermediates,
and `HoverNetViewportSmoke` renders the formerly crashing projection directly.
Post-menu race state is also initialized and no longer read after a menu-only exit.

**Exit gate:** Two-hour local and online sessions complete on both platforms without crashes, hangs, or growing memory use.

## Phase 2 — Harden multiplayer and RaceServer

**November–December 2026 · 5–7 weeks**

**Implementation status:** Complete. Protocol hardening, validation, rate
limits, configuration, lifecycle behavior, health metrics, fuzzing, sanitizer
CI, and automated scenario coverage are implemented. The phase exit gate --
the recorded 24-hour soak described in
[Phase 2 verification](phase2-verification.md) -- is deliberately **deferred
until Phase 3 is complete**: it's a real-time, day-long verification run with
no code left to write, so it's more useful run once against the fuller 2.0
feature set than burned early against a codebase Phase 3 is still going to
change substantially. `scripts/run-raceserver-soak.sh` is ready to run
(`build/linux 86400 phase2-soak.log`) whenever that gate is actually needed.

- Formalize message framing, size limits, endianness, and protocol-version negotiation.
- Replace silent or indefinite network waits with explicit timeouts and errors.
- Validate player names, race settings, chat messages, and state packets.
- Add connection, chat, and race-creation rate limits.
- Complete configuration loading rather than shipping placeholder XML handling.
- Add graceful shutdown, structured logs, health reporting, and useful server metrics.
- Add automated scenarios for:
  - multiple simultaneous races;
  - disconnect during lobby, countdown, or race;
  - host departure;
  - duplicate names;
  - partial and coalesced TCP messages;
  - malformed and oversized packets;
  - server restart and capacity limits.
- Perform protocol fuzzing and dependency/security scanning.

**Exit gate:** A 24-hour soak test with repeated joins, races, disconnects, and malformed traffic produces no crash, deadlock, or orphaned race.

## Phase 3 — Complete the 2.0 user experience

**January–February 2027 · 6–8 weeks**

**Implementation status:** In progress. "Persist all settings in a documented
per-user location" is done on Linux: the four previously-scattered dotfiles
(`.hovernet_host_prefs`, `.hovernet_local_race_prefs`, `.hovernet_username`,
`.hovernet_server_url`, all dumped directly in `$HOME`) now live under
`$XDG_CONFIG_HOME/hovernet/` (falling back to `~/.config/hovernet/`), following
the XDG Base Directory convention -- the actual "documented per-user location"
this item asks for. A one-time migration copies each old dotfile over the
first time its new path is read, so upgrading players keep their settings.
`HoverNetGame2Player --print-config-paths` prints the resolved paths for
diagnosis. A first piece of "audio and volume settings" is also done: a master
volume slider (0-100%) in the Settings screen, applied live while dragging and
persisted alongside the other settings. A separate persisted mute control
preserves that chosen level while silencing all audio, previews live, and rolls
back with Cancel. `MR_SoundServer::SetMasterVolume`/`GetMasterVolume` scale every
sound's gain in the shared SDL2 mixer
(`HeadlessSoundServer.cpp`, which Windows now builds too -- the legacy OpenAL
`SoundServer.cpp` also got the same API for consistency, but is no longer
compiled on either platform). The Windows Options sheet now exposes the same
persisted master-volume and mute behavior in a dedicated Audio tab, including
live preview and Cancel rollback. Windows settings now write to the current
user's neutral `HKCU\Software\HoverNet` registry namespace instead of
requiring machine-wide access or implying a company owner. A fullscreen toggle
is also done: `SDL2Graphics.cpp` already
calls `SDL_RenderSetLogicalSize` to scale the fixed-resolution framebuffer to
whatever window size results, so `SDL_SetWindowFullscreen` alone (no render-
path changes) toggles it, applied live from the same Settings screen and
persisted the same way as volume.

Fixed a regression the fullscreen toggle exposed: every menu screen (ImGui
ones -- Lobby, Local Race Setup, Settings -- and the raw bitmap-font ones --
pause menu, quit confirmation) lays out and hit-tests against the fixed
1024x768 logical size, but once the window size no longer equals that exactly
(any resize, and especially fullscreen at a different aspect ratio), SDL's
`SDL_RenderSetLogicalSize` letterboxes the rendered output while ImGui's
`io.DisplaySize`/`io.MousePos` and raw `SDL_MOUSEBUTTONDOWN`/`SDL_MOUSEMOTION`
events still report real window pixels -- causing exactly what was reported:
ImGui screens overflowing their visible letterboxed area, and every screen's
clicks landing in the wrong place.

The first attempt at this fix corrected `io.MousePos` directly after
`ImGui_ImplSDL2_NewFrame()`, which turned out not to work: ImGui's SDL2
backend queues mouse-position updates via `io.AddMousePosEvent()`, and
`ImGui::NewFrame()` drains that queue and overwrites `io.MousePos` from it,
silently clobbering any correction applied between the two calls -- so the
Lobby and Settings screens stayed unclickable in fullscreen even after that
change shipped (v0.1.73). The working fix rewrites each `SDL_Event`'s own
x/y in place, via a new `RewriteMouseEventToLogical` helper called
immediately after `SDL_PollEvent` and before the event reaches
`ImGui_ImplSDL2_ProcessEvent` (for the 3 ImGui screens) or the raw
`SDL_MOUSEBUTTONDOWN`/`SDL_MOUSEMOTION` handling (`ConfirmQuit`,
`RunPauseMenu`).

That second attempt (v0.1.74) fixed the raw bitmap-font screens but not the
ImGui ones: clicking still needed an offset move toward the letterbox bars.
Root cause: `ImGui_ImplSDL2_NewFrame()` has its own "global mouse state"
fallback (`ImGui_ImplSDL2_UpdateMouseData`) that re-queries the real,
uncorrected window-pixel mouse position via `SDL_GetGlobalMouseState` every
single frame the mouse isn't held down, and queues it *after* whatever
`RewriteMouseEventToLogical` already fixed on the `SDL_Event` -- so it always
won for plain hovering (most of the time between clicks), even though the
click frame itself (mouse button down, fallback skipped) was correct. The
mismatch between where hovering said the cursor was and where a click
actually landed is exactly "move left and up to click." Fix: for the 3 ImGui
screens, stop rewriting the `SDL_Event` (that only ever fixes one of the two
competing sources) and instead correct `io.MousePos` directly in a new
`CorrectImGuiMousePos`, called right after `ImGui::NewFrame()` -- the one
point in the frame after all queued position updates (real or fallback) have
already been drained into `io.MousePos`, so nothing overwrites the
correction again before widgets hit-test against it.
`RewriteMouseEventToLogical` still handles `ConfirmQuit`/`RunPauseMenu`,
which don't go through ImGui and aren't subject to this fallback.
`io.DisplaySize` is unaffected by either issue and is still corrected
directly before `ImGui::NewFrame()` via `SyncImGuiToLogicalSize`, since
`NewFrame()` needs it immediately to size the main viewport. This could only
be diagnosed by reasoning through the ImGui/SDL event pipeline, not by
interactive testing (unavailable in this environment) -- please re-verify by
toggling fullscreen and confirming the Lobby, Settings, and pause menu are
all fully visible and clickable, with the cursor landing where it visually
appears, at both window sizes.

Even that (v0.1.75) still didn't line up. Both prior attempts computed the
window-to-logical conversion by hand, from `SDL_GetWindowSize` and the same
scale/offset formula `SDL_RenderSetLogicalSize` is documented to use
internally -- correct in principle, but window size isn't necessarily what
`SDL_RenderSetLogicalSize`'s viewport math is actually based on. It uses the
renderer's real output size (`SDL_GetRendererOutputSize`), which differs from
window size whenever the display applies HiDPI or fractional scaling, and
reverse-deriving that from window size alone can't account for it. Rather
than keep guessing at SDL's internal math, `WindowToLogicalPoint` now calls
`SDL_RenderWindowToLogical` (SDL 2.0.18+) directly -- the one API SDL ships
specifically to reverse a `SDL_RenderSetLogicalSize` mapping, using whatever
mapping it actually set up rather than a reimplementation of it. This could
only be diagnosed by reading the SDL2 source and API docs, not by
interactive testing (unavailable in this environment) -- please re-verify by
toggling fullscreen and confirming the Lobby, Settings, and pause menu are
all fully visible and clickable, with the cursor landing where it visually
appears, at both window sizes.

Still not fixed after that (v0.1.76): the report came back "I still cant
click anything and its stuck in fullscreen mode" -- and instrumenting the
actual click path (a new headless regression test, `HoverNetImGuiLogicalMouseSmoke`,
built specifically because four straight shipped guesses without one wasn't
working) showed why: `ImGui::NewFrame()` doesn't just copy the queued mouse
position into `io.MousePos`, it also runs `UpdateHoveredWindowAndCaptureFlags()`
*using that position* to decide which window is hovered, and that happens
*inside* `NewFrame()` -- so v0.1.75/76's fix (correcting `io.MousePos` *after*
`NewFrame()` returns) was already too late for hover detection on every
frame, even though the field itself looked correct afterwards. Moving the
correction to push a freshly-computed logical position via `io.AddMousePosEvent()`
between `ImGui_ImplSDL2_NewFrame()` and `ImGui::NewFrame()` (so it's queued
after the SDL backend's own competing "global mouse state" fallback, and
consumed by `NewFrame()` before hover detection runs) fixed the synthetic
test -- but by that point, four attempts at reverse-engineering the letterbox
mapping in a row justified stepping back from "fix the coordinate math" to
"stop needing coordinate math at all."

**The design that actually shipped**, after all of the above: don't correct
ImGui's mouse coordinates for the letterbox at all. Instead, disable the
renderer's logical size (`SDL_RenderSetLogicalSize(renderer, 0, 0)`) for the
duration of each ImGui screen's frame, so ImGui draws 1:1 against the real
window -- `io.DisplaySize` (from `SDL_GetWindowSize`) and every mouse
coordinate SDL reports (real events and the backend's own fallback alike)
are already in the same space with zero scaling involved, so there is
nothing left to get wrong regardless of window size, aspect ratio, or frame
timing. Logical size is restored to the fixed `kWidth`/`kHeight` immediately
after presenting each ImGui frame, before gameplay renders again. Pause and
quit have since joined the shared ImGui flow too, so the old raw-menu panel,
hit-test, and letterbox coordinate helpers have been removed rather than left
as a second UI path. `HoverNetImGuiLogicalMouseSmoke` pins the fix down for real:
it forces
a 16:9 window against the 4:3 legacy resolution (the exact mismatch every
prior attempt broke on), warps the mouse to a real window position, and
checks both that ImGui reports the button there as hovered *and* -- via
`SDL_RenderReadPixels` -- that the button is actually drawn at that pixel,
catching both the click-misalignment and the original "screen overflowed,
can't see any buttons" symptom in one test. Verified against both known-bad
states before finalizing (hover-only check passes even with the wrong
DisplaySize-forcing approach, since ImGui's hover math never touches the
renderer's logical size; the pixel-readback check fails, as expected, with
logical size left enabled) -- the strongest confidence this bug has had
behind it yet, though still not the same as a human clicking it. Please
re-verify by toggling fullscreen and confirming the Lobby, Settings
(including actually unchecking Fullscreen), and pause menu are all fully
visible and clickable, with the cursor landing exactly where it visually
appears, at both window sizes.

The in-game control-reference portion of onboarding is now implemented on
Linux. A consistent ImGui Controls screen lists the currently active driving,
weapon, camera, and HUD bindings and is reachable from the main menu, the
in-race pause menu, and the lobby's Escape menu. It safely reuses an existing
ImGui context when opened from the lobby and owns one when opened from a legacy
menu; `HoverNetControlsReferenceSmoke` exercises the standalone render and
teardown path headlessly.

Linux display settings now also include a persisted window resolution selector
for 1024x768 through 2560x1440, with live preview and complete Cancel rollback.
The window is resizable in windowed mode, the selected size is stored under the
same XDG configuration directory, and startup restores it before entering play.
Gameplay continues rendering through the fixed logical framebuffer, so changing
the window dimensions does not alter simulation or legacy rendering assumptions.

Linux UI scaling is now persisted alongside those settings and adjustable from
75% to 150%. The shared ImGui theme scales fonts, spacing, controls, scrollbars,
and other interaction targets consistently across Lobby, Local Setup, Settings,
and Controls; preview changes are rolled back by Cancel.

Linux now supports standard SDL game controllers throughout the core player
flow. The left stick and triggers drive the craft, face buttons cover jumping,
weapons, and selection, Start opens the pause menu, and D-pad/A/B navigation is
available in the raw main and pause menus. ImGui screens have keyboard and
gamepad navigation enabled, disconnected controllers are released safely, and
newly connected controllers are discovered during play. The in-game Controls
screen and Linux documentation show the active controller mapping.

Core Linux gameplay keyboard controls are now remappable from that same Controls
screen. Selecting a binding captures the next key, Escape cancels capture, a
single action restores the defaults, and every accepted change is persisted in
the per-user XDG configuration directory and applied immediately. Invalid or
truncated binding files fall back atomically to the full default layout.

Core controller gameplay buttons are now remappable and persisted alongside the
keyboard layout: Jump, Fire, and Select Weapon can be reassigned from the
Controls screen, with validated atomic loading and a shared reset-to-defaults
action. Start remains reserved for pause. Advanced analog customization is now
implemented too: steering, accelerate, and brake can each map to any standard
SDL stick/trigger axis, direction inversion is independent per action, and the
shared deadzone is adjustable. The extended settings load atomically while
remaining backward-compatible with the earlier three-button file format.

The shared Windows/Linux race HUD now supplements its existing graphical meters
and weapon sprites with a readable status line: explicit speed and fuel
percentages plus missile charge/readiness, mine count, or power-up count. Live
position is included as an exact rank when known, or as an honest best/worst
range for racers tied on the same completed lap (the relay protocol does not
carry continuous checkpoint distance). Race timing is now explicitly labelled,
the current-lap clock is visible from the first lap rather than appearing only
after one full circuit, and the final circuit gets a prominent `FINAL LAP`
banner. This keeps the original compact artwork while removing the need to
infer critical state from unlabeled bars and animation frames or displaying
false within-lap precision. Fuel state also gains explicit `LOW FUEL` and
`FUEL EMPTY` warnings at the same thresholds as the colour-changing meter, so
its critical state does not depend on colour perception.

Online races now add a real RaceServer TCP round-trip measurement to that status
line, refreshed every two seconds, with explicit measuring and connection-lost
states; local races say Local. Measurements at or above 200 ms retain their exact
value and add a `HIGH LATENCY` warning so connection quality is understandable
without interpreting a raw number. The lag probe remains compatible with legacy peer
relay behavior, while the server also echoes the opaque token to its sender.

Damage and pickup feedback is now explicit as well. The HUD detects the transition
into HoverNet's loss-of-control damage state and shows a prominent impact callout;
fuel replenishment, mine collection, and power-up collection receive short,
counted callouts derived from character state changes rather than being buried in
the chat log or relying only on legacy sounds and sprites. A persisted Large HUD
text accessibility option also avoids the bitmap renderer's automatic shrinking:
dense race status is split across short native-size lines independently of menu
UI scaling. The same mode now keeps received chat and the active chat prompt at
native size in compact and split-screen viewports instead of shrinking the text.
A separate persisted Reduced motion option freezes the main menu's
speed streak animation without removing its racing backdrop or navigation cues.
Windows Options now exposes and persists the same Large HUD text mode, applies
it to every local, split-screen, network, and online race observer, previews it
for an active race, and rolls it back on Cancel. Its Audio & HUD page also has a
combined defaults action that previews full volume, unmuted audio, and normal HUD
text without persisting until the property sheet is accepted. The Windows display
page likewise offers a reversible reset to neutral gamma, contrast, and brightness,
with live preview and the existing Cancel rollback. Persisted High
contrast menus replace subtle dark shades with blacker surfaces,
white body text, brighter secondary text and borders, darker action buttons, and
stronger focus/selection states across the shared ImGui theme. Settings also
has a single Restore defaults action covering identity, server, audio, display,
and accessibility values; it previews those defaults live while retaining full
Cancel rollback and only persists them after Save.

Guided onboarding is now implemented on Linux as a four-step first-run
walkthrough covering race selection, driving and weapons, the race HUD, and
configuration/accessibility. It explains live preview plus Save/Cancel semantics
so a new player can safely adjust the game without external documentation. It is
skipped for automated bounded runs, records completion only after Finish, and
remains reachable through How to Play on the main menu. Completion is versioned,
so players who finished the earlier three-step walkthrough see this expanded
version once; finishing records the new version. The first and final pages have
separate bounded smoke coverage.

The Linux main menu is now consolidated into ImGui without looking like a
settings dialog: an animated speed-line and perspective-grid race backdrop,
a bespoke round-skirt hovercraft hero with visible pilot and oversized turbines,
stronger title treatment, prominent race-mode actions, and a separate utility
group give it a game-front-end hierarchy. It retains UI scaling,
resized/fullscreen mouse accuracy, keyboard/controller navigation, version
display, and bounded automation's Local Play default.

The in-race and lobby Escape menu is now consolidated into that same ImGui
visual language as well. It borrows the lobby's live ImGui context when nested
there and owns a temporary context when opened during a race, preserving
keyboard/controller navigation and all resume, race-switching, settings,
controls, and quit behavior. `HoverNetPauseMenuSmoke` gives the standalone
render/lifecycle path deterministic headless coverage.
The quit confirmation now follows the same context-sharing pattern and visual
language, defaults keyboard/controller focus to the safe Keep Playing action,
and retains Escape/B cancellation plus explicit confirmation for closing.
`HoverNetQuitConfirmationSmoke` covers its standalone lifecycle path.

Finishing a race now opens a dedicated ImGui results flow instead of merely
printing “Press ESC to exit the race.” It presents placement, finish time, and
best lap, then offers explicit routes to keep driving, configure another local
race, enter online multiplayer, or quit. Keyboard/controller navigation and
quit confirmation remain consistent with the other menus, and
`HoverNetPostRaceSmoke` covers its standalone render/lifecycle path. Local race
setup and the online host dialog now describe each bundled course's character,
difficulty, and suggested race length instead of asking new players to choose
from unexplained internal track names. Local setup also loads the selected
track's embedded minimap into a cached, aspect-correct preview beside the race
options, with a stacked fallback for narrow windows and an explicit placeholder
for tracks without map artwork. The lobby's Host Race flow now uses the same
map-forward selector and course guidance rather than a smaller text-only dialog;
browsing an existing race also shows a compact preview of its selected course.

The primary Linux player flows are now consolidated in ImGui; the remaining
Phase 3 work is HUD/accessibility completion, exit-gate verification, and
essential Windows/Linux parity while convergence moves toward the shared client.
An unavailable RaceServer now produces an in-game cross-platform error with the
failed address plus Retry, Open Settings, and Back actions instead of silently
returning after writing only to the terminal. Saving a corrected server address
retries it immediately without leaving the online flow. Protocol-version rejection displays
the server's reason and recommends a compatible release rather than misreporting
it as a generic outage; bounded automation remains noninteractive. If a connected
RaceServer disappears while the lobby is open, stale race and roster controls are
replaced by explicit Reconnect and Back actions. A successful reconnect returns to
the browser and requests fresh race and player lists rather than retaining an
invalid waiting-room state. The lifecycle smoke test now keeps that client alive
through a graceful server shutdown, verifies that polling detects the closed
socket, and reconnects the same object after restart to confirm clean state.

For the remaining Phase 3 work, "Windows/Linux parity" means converging on the
shared SDL2/ImGui implementation. Avoid adding substantial new functionality to
the legacy Win32/MFC screens once the essential 2.0 settings and flows are safe.
Each shared screen or service should be kept platform-neutral and exercised on
Windows as soon as the shared Windows build is available.
The first portability pass now lets the SDL renderer create its own native Windows
window (while preserving legacy HWND embedding), stores shared-client settings
under `%APPDATA%/HoverNet`, treats Linux `/proc` memory reporting as an optional
diagnostic on Windows, and removes the shared RaceServer client dependency on
the legacy MFC precompiled header. Its socket ownership is now pointer-width safe
for native x64 Windows instead of truncating Winsock handles to 32-bit integers.
GitHub Actions and GitLab CI now also configure the portable CMake graph with
the native x64 MSVC toolchain and build the shared `HoverNetGame2Player`
target as the player-facing `HoverNet.exe`, plus its object-factory plug-in.
GitLab retains the executable, plug-in, and x64 SDL2
runtime together as a testable pipeline artifact. Both pipelines now run that
x64 executable with SDL's headless video/audio drivers through startup and
bounded Local Play, Settings, Controls, pause, results, and onboarding flows.
This is not yet a replacement for the legacy installer: interactive Windows
playtesting and packaging remain required before changing the player-facing
executable. Automated online-lobby coverage now starts the portable x64
RaceServer locally, negotiates protocol 2.0, and renders the real bounded lobby;
it does not depend on or modify the production server.

- Consolidate the main menu, settings, pause menu, lobby, local setup, and post-race flow into one consistent ImGui design.
- Add remappable controls and controller support.
- Add display mode, resolution, scaling, fullscreen, audio, and volume settings.
- Persist all settings in a documented per-user location.
- Improve the HUD:
  - position and lap progress;
  - race timer and results;
  - weapon and ammunition state;
  - connection quality;
  - clearer damage and pickup feedback.
- Add an onboarding and control reference reachable in-game.
- Improve keyboard navigation, text scale, colour contrast, and window resizing.
- Ensure Windows and Linux expose the same core menus and options.
- Establish a supported Windows build of the shared SDL2/ImGui client, or record
  the remaining blockers and retain the legacy client as the explicit 2.0
  fallback; do not silently maintain two long-term feature implementations.

**Exit gate:** A new player can install, configure, start a local race, and join an online race without external documentation.

## Phase 4 — Content and tools

**February–March 2027 · 4–6 weeks**

- Validate every bundled track for starts, checkpoints, finish detection, collision, and multiplayer consistency.
- Add at least two release-quality tracks.
- Balance hovercraft, missiles, mines, pickups, and weapon-enabled versus weapon-disabled races.
- Finish the track compiler's highest-impact TODOs.
- Create a command-line track validator suitable for CI.
- Publish a minimal track-authoring guide and example project.
- Decide whether HoverCad is supported, experimental, or explicitly legacy.

**Exit gate:** All bundled tracks pass automated validation and a complete multiplayer playthrough.

## Phase 5 — Release engineering and beta

**March–April 2027 · 4–6 weeks**

- Remove generated binaries and build artefacts from source control; enforce this through `.gitignore` and CI.
- Add compiler warnings and sanitizers on Linux.
- Add Windows smoke tests beyond compilation.
- Exercise the shared SDL2/ImGui client on Windows in CI, including its menus,
  settings, input, local-race startup, and online lobby path. Keep legacy-client
  jobs only while they are required for the 2.0 compatibility fallback.
- Make x64 the supported Windows client, RaceServer, compiler, and packaging
  target. Audit pointer/integer casts, structure-size assumptions, third-party
  libraries, installers, plug-ins, and saved/network formats before retiring
  Win32 builds; keep wire and content compatibility independent of pointer size.
- Run x64 Windows-to-x86-64 Linux multiplayer and clean-install tests in CI,
  with a temporary Win32 compatibility job until the x64 release candidate is
  proven.
- Produce reproducible versioned packages from a clean checkout.
- Add checksums, dependency and licence inventory, changelog, migration notes, and rollback instructions.
- Run:
  - an internal alpha;
  - a two-week public beta;
  - a release-candidate freeze;
  - a production-server migration rehearsal.
- Verify clean installation, upgrade, uninstall, and retained server configuration.

**Exit gate:** The release candidate passes CI, cross-platform playtesting, packaging checks, and the production deployment rehearsal.

## HoverNet 2.0 release criteria

Ship 2.0 only when:

- Windows and Linux can host and join each other reliably.
- Supported Windows binaries are native x64 and do not depend on 32-bit DLLs.
- All supported race flows have automated coverage or a documented manual test.
- There are no known crash, hang, data-loss, or remotely exploitable defects.
- The server survives the 24-hour soak test.
- Clean installations contain everything needed to play.
- Settings and error messages do not require source-tree knowledge.
- Every bundled track completes correctly in local and online play.
- Release and rollback procedures have both been exercised.

## Explicitly deferred to 2.1 or later

To keep 2.0 achievable, defer:

- fully server-authoritative physics;
- accounts, rankings, and persistent progression;
- matchmaking;
- replay and spectator infrastructure;
- a complete rendering-engine rewrite;
- mobile and console ports;
- a completely rewritten visual track editor.

The most important architectural decision is avoiding a half-finished authoritative simulation in 2.0. The repository contains unfinished server simulation, lap detection, UDP, and dispatcher code, while the working multiplayer path is primarily relay-based. Hardening and documenting the proven path is substantially safer; an authoritative protocol can then be designed deliberately for a later release.
