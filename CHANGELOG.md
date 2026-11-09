# Changelog

All notable changes to HoverNet. Versions are the `vX.Y.Z` git tags the release
pipelines build from. See [Releasing](docs/releasing.md) for how a release is made
and how to roll one back.

## Unreleased

## 2.3.43 - 2026-11-09

### Fixed
- **The website looped on "Starting HoverNet..." (2.3.42).** The new RmlUi menus were the default and never reported their frames to the page, so after 25 seconds the page decided the browser was stuck, switched to "compatibility mode" and reloaded, over and over. The older (ImGui) menus are the default again, `?menu=rml` on the page (or `HOVERNET_RML_MENU=1`) tries the new ones, and the new screens now count their frames like the others. The browser test for the new menu now fails if the page does not see a frame within 40 seconds (it passed before only because the page gives up waiting after 60 seconds, which is how this was missed). Checked on a fresh WebAssembly build in headless Chrome; not yet on the live site.
- **The Windows client failed to link** (`VideoServices.dll`: unresolved `GpuFinisher` methods). The legacy Visual Studio projects were missing sources the CMake build has (`GpuFinisher.cpp` in VideoServices; `ColourGrade.cpp` and `TextureEnhance.cpp` in ObjFacTools; `CraftLook.cpp` and `CraftDesign.cpp` in ObjFac1), two `MR_2DViewPort` methods were not exported, and `std::min` and `std::max` clashed with the Windows macros in `GpuFinisher.cpp` and `CraftLook.cpp`. Reproduced and fixed on the Windows runner: `VideoServices`, `ObjFacTools`, `ObjFac1` and `Game2` (Win32, Release) now build and link. The full GitHub Actions run has not been seen.

### Changed
- **The Linux package and the Windows installer now include the licence texts of the libraries built into the game**: Dear ImGui (MIT), RmlUi (MIT) and FreeType (the FreeType Licence), in `licenses/` beside the notices file, because those licences ask for their notices to accompany binaries. The Windows package check expects them. Not run: building either package here (the Debian script and installer script were edited, not executed).

### Changed
- **The RmlUi screens follow the UI Scale setting** (75% to 150%), and the main menu's panel is capped at 42% of the window like the older menu's, so a larger scale does not cover the title. Seen at 150% in a 1024x768 screenshot.

### Added
- **Two more RmlUi screens (still opt-in): Local race setup and Controls.** Local race setup has a filterable track chooser for the whole library (a search box and the first 60 matches), the track map, the briefing, laps with your personal bests, weapons, and the download of missing tracks over the screen with progress, Cancel and error messages. Controls has Keyboard and Controller tabs, remapping by pressing the key or button (Escape cancels, a key another control has swaps the two), the controller axes, deadzone and reset. Both keep every behaviour of the older screens and fall back to them if they cannot load. Checked by scripted runs through the real input path (`HoverNetRmlMenu`: the Weapons toggle; choosing Jump and pressing J reports scancode 13) and by screenshots. Not checked: choosing a track from the list, a real download, a real controller, the browser and Windows builds of these two screens.
- **The new music tracks are level-matched.** Measured with ffmpeg (EBU R128): Hard Left -14.0 LUFS, Asphalt -12.5, Fever Stadium -10.8, its Climax -12.0, Hyper Ultra-Racing -8.8. Each track now plays at a gain that brings the race tracks to Asphalt's level (Fever Stadium -1.7 dB, Hyper Ultra-Racing -3.7 dB), including when the music ducks for the pause menu or a hit and when the music slider moves. The files are untouched.

### Changed
- **The RmlUi screens were polished toward the older menus** (still opt-in): every screen is drawn at twice the size and shrunk onto the window, which smooths text and edges; the main menu has the older menu's own animated background (perspective grid floor, drifting speed streaks) and places the turning craft as the older menu does, in place of the vertical bars I had put there; Settings and the Settings hub have the older screens' backdrop and title rule; a tall Settings tab no longer collapses into a narrow column (scrollbars were unstyled); the quality and window-size choosers have their arrows on each side. Seen in 1024x768 screenshots only. Rounded corners are still left out, because SDL's software renderer leaves ragged lines on them (not seen on a GPU renderer: not available here), so buttons are square with a soft gradient. Not yet matched: the pause menu and results screen against their older versions, the UI scale setting, and large windows.
- **The new RmlUi menus are opt-in again.** They looked worse than the older menus (text small and soft on a large window, blocky craft picture), so the ImGui menus stay the default until that is fixed.

## 2.3.42 - 2026-11-09

### Fixed
- **Online: the number of laps you choose is remembered.** Creating a race in the online lobby kept your laps, weapons and track in memory only, so the next session went back to 1 lap. They are now saved, and the dialog opens with them. Checked by `HoverNetOnlineScreenClicks`: a new player starts at 1, hosting with 4 laps makes the next session start at 4. (The local race setup already remembered them.)

### Added
- **More music, and it no longer repeats.** Three new tracks, all CC0 from OpenGameArt (no conditions): *Fever Stadium* and *Hyper Ultra-Racing* join *Asphalt* in a race rotation that never plays the same track twice in a row (the last one is remembered between runs), and on the final lap the faster *Fever Stadium Climax* takes over (its author describes it as final-lap music). Menus still have only *Hard Left*. Credits and licences in `NetTarget/Music/CREDITS.md` and `THIRD_PARTY_NOTICES.md`. Checked: eight race starts in separate runs never repeated a track back to back. Not checked: how the new tracks sound (I cannot listen: please do, and say which to drop), that their loudness matches the others, and the switch to the final-lap track in a real multi-lap race.

### Changed
- **The Game settings screen is an RmlUi form** (tabs for Player, Sound, Access, Graphics and Display; toggles, sliders, choices and text fields; Save is disabled until the name, server and addresses are valid; the same live preview of volume, window size and fullscreen, and Cancel restores them), with the same fallback to the older screen. Built on a reusable form engine for other screens. Checked on Linux by a scripted run through a tab, the Mute toggle and the name field, and by a screenshot of the Sound tab. Not checked: the Graphics and Display tabs on screen, the browser and Windows builds of this screen, a controller, the UI scale and high contrast settings (they apply to the older menus).
- **The Settings hub and the quit confirmation are RmlUi screens too** (same component and fallback as the pause menu; the hub shows each row's detail line). The hub is covered by `HoverNetRmlMenu`; the quit dialog was built and not looked at.
### Changed
- **The pause menu is now an RmlUi screen too**, with the same fallback to the old one; Windows is included: the game with RmlUi builds and links with MSVC (x64, run on the Windows runner with CMake and Visual Studio Build Tools) and there the new main menu answered Enter, Down+Enter and a real mouse click on Garage with the right choice. The Windows installer and its package check now include the new menus' files. Not run on Windows: the pause menu, the results screen, the legacy 32-bit build, a real window (the Windows test ran headless).
### Changed
- **The new RmlUi main menu and results screen are now the default** on Linux and in the browser build (they are built in unless `-DHOVERNET_RMLUI=OFF`). `HOVERNET_IMGUI_MENU=1` (or `?menu=classic` on the web page) brings back the old ones, bounded test runs keep the old ones unless `HOVERNET_RML_MENU=1`, and if the new menu's files cannot be loaded the old menu is shown instead (checked by hiding the `ui` folder). The Debian package now includes the new menus' files. Checked on Linux and (headless) on Windows x64.

### Added
- **Sign in with Outiva, or play as a guest, before the main menu (browser build).** Once per visit, a player who is not signed in sees a gate: SIGN IN WITH OUTIVA (goes to the Outiva login in the same tab and comes back to the game; no new tab) or PLAY AS A GUEST. Signed-in players never see it and race under their Outiva callsign; the Garage tells guests that signing in keeps their crafts on their account. Guests race as `name (nonreg)` (cut to fit the 32-byte name limit; unit-tested in `HoverNetOnlineLobby`). The account service now says whether accounts are configured (`X-HoverNet-Accounts: on/off` on `/account/me`), so a page with no accounts shows no gate. Checked in headless Chrome against a stand-in account service (`scripts/web-smoke/web-signin-prompt.mjs`: the gate, the same-tab sign-in, the guest path, a signed-in browser and an accounts-off page). Not checked: against the real Outiva login, the "(nonreg)" tag seen by another player online, a phone. The desktop build has no Outiva login yet, so it shows no gate and tags nobody.

### Fixed
- **RmlUi: transforms now work.** The SDL renderer that came with RmlUi ignored CSS transforms, so the panel slide-in, the craft drifting in, and the button grow-on-hover and shrink-on-press did nothing but fade. Transforms are now applied to the vertices (marked in the vendored file). The menu also has faint light bars drifting across the background (off with Reduced motion). Seen in a screenshot (the focused button is 2.5% larger); the motion itself was not watched. Rotated translucent shapes are drawn wrongly by SDL's software renderer, so none are used; the GPU renderer was not available here to check.

### Added
- **The RmlUi screens make the game's interface sounds**: a tick as the focus or pointer moves to a button, a click when one is chosen, the back sound on Esc. Checked from the log of the sounds triggered (`HoverNetRmlMenu`), not by listening.
- **RmlUi results screen (prototype, with `HOVERNET_RML_MENU=1`)**: the card eases in, its parts appear one after another, the XP counts up and the level bar fills; VICTORY! in gold. Both RmlUi screens are kept light on text. Checked: renders in a screenshot, and a scripted run after a real autoplay race presses its button (`HoverNetRmlMenu`). Not checked: an online race's RETURN TO LOBBY button, the counting and staggering in motion.
- **RmlUi works in the browser build**: the whole game builds as WebAssembly with RmlUi, and in headless Chrome (`?rml=1` on the page) the new menu is drawn with the turning craft (`scripts/web-smoke/web-rml-menu.mjs`). Not tried on a phone or in a real browser.
- **RmlUi main menu: your own craft turns in it**, the saved Garage design drawn by the same preview code as the ImGui menu, easing in with the layout (still with Reduced motion). Seen in a screenshot of the stock craft; a custom design was not tried.

## 2.3.41 - 2026-11-09

### Changed
- **Main menu**: RACE ONLINE is first and the big red button, with a line under it ("Race real players, climb the leaderboards, and show off your craft."); the offline mode is now PRACTICE OFFLINE, a quieter button below it.
- **Results screen** has only what fits: after an online race RETURN TO LOBBY and MAIN MENU, after an offline race only MAIN MENU (Esc / B goes to the main menu). Keep Driving, Restart, New Local Race, Online Multiplayer and Quit were removed from it (Restart race is still in the pause menu). The results XP now counts up.
- **Garage**: leaving with changes that were never saved (Back, Esc, or closing the program) asks "Save and leave / Leave without saving / Keep editing" in a dialog drawn by the game (so it also works in a browser). Clicking a decal puts it on the paint straight away (and the canvas stamps more); decals show their names, three (Star, Flag, Arrow) are open from the start, and a line names the next one to earn.

### Fixed
- **Online: your own and other players' crafts now show.** A race could start before the server's copy of each design arrived, or the designs were lost when the track loaded and the craft renderers were rebuilt. The lobby now keeps every relayed design, waits up to 1.5 s for your own to be echoed before the race starts (then starts anyway, so a server that never echoes cannot trap you in the lobby), and applies the full set after the track has loaded. Test `HoverNetOnlineLobby` checks the lobby keeps both players' designs matched to their seats. Checked end to end with a real two-client race on the local race server: each client's own craft (red and blue, with a yellow trim) is drawn. I could not get a second real client into the same race on the authoritative server here, so another player's craft on screen was not seen; please check it with a friend.
- **GitHub Actions: the Windows build failed** (`MenuInput.h` used a C++17 inline variable; the Windows client builds as C++14). Fixed; confirmed only by the next Windows job.

### Fixed
- **Garage: choosing another decal replaces the last one** instead of drawing on top of it. The decal buttons put one decal in the middle of the paint; pressing a different decal button takes that one off first (one Undo still takes back the decal now shown). Anything drawn or stamped on the canvas since is never wiped: once you have drawn on the paint, a decal button adds a decal as before. Checked with real clicks: Star then Flag leaves the same 23 cells as the Flag alone, and the Flag twice is still one Flag (`HoverNetGarageDecals`).

### Added
- **A new main menu built with RmlUi (prototype, off by default).** The game's interface library, Dear ImGui, has no layout, transitions or animation, so the main menu was rebuilt with RmlUi, which has them (HTML/CSS-style markup, `:hover` and `@keyframes`). Build with `-DHOVERNET_RMLUI=ON` and run with `HOVERNET_RML_MENU=1`; the ImGui menu stays the default. The craft drifts in, the panel slides in, buttons grow on hover and shrink when pressed, RACE ONLINE pulses. Checked on Linux: it builds, renders (looked at in a screenshot) and a scripted run confirms Enter, Up, Down and a real mouse click on each of the six doors (`HoverNetRmlMenu`). Not done or not checked: the turning craft in your colours, phone layouts, UI scale and contrast, the web build, Windows, a controller, every other screen. See `docs/rmlui.md`. RmlUi 5.1 (MIT) and FreeType 2.13.2 (FreeType Licence) are vendored and listed in `THIRD_PARTY_NOTICES.md`.
- **Every screen eases up into place** as it fades in (a 26-pixel rise that settles over a quarter of a second; not with Reduced motion, and off for the screenshot hooks like the fade). Done in `ScreenFadeFrame` for all screens at once. The main menu and Garage run without crashing with it on and the menu, decal and camera tests pass; the motion has not been seen on screen. Only what is drawn moves, so click areas are a few pixels off for that quarter second.
- **Results screen entrance**: the card eases up into place over about a third of a second (not with Reduced motion), and 2nd or 3rd in a field of three or more now reads PODIUM FINISH instead of RACE COMPLETE. Presentation only. Built and the results tests pass; the motion itself and the new heading have not been seen on screen (the screenshot hooks switch the motion off).
- **Tests for the Garage decals and the results screen**: `HoverNetGarageDecals` makes a real pointer click on each decal button (SDL mouse events through the interface, the button found where it is drawn): the three decals open from the start paint the craft, a locked one does nothing, and it works once unlocked. `HoverNetRestartRace` now also checks that an offline race's results screen offers only MAIN MENU. `HoverNetOnlineLobby` checks that a look the server never echoes does not keep the player in the lobby.
- **Test tools that are kind to the machine**: `scripts/ctest-gentle.sh` runs the suite two tests at a time at low priority, and the browser smoke tests no longer leave Chrome profiles and pictures in /tmp (set `HN_KEEP_PICTURES=1` to keep them).
- **The last lap announces itself**: when the final lap starts the banner reads FINAL LAP (with the lap time just driven) and the lap chime is a major third higher than on other laps. Presentation only; built, not listened to or seen on screen by a person.
- **The music steps back for a heavy hit** (to under half for about a second, then returns), and the music volume now glides to a new setting over about a tenth of a second instead of stepping, so the pause menu's ducking no longer clicks. Checked that the audio output still plays normally in a recorded run; the dip itself is not visible in the levels (the motor is louder than the music's change) and has not been listened to by a person.

## 2.3.40 - 2026-11-09

### Added
- **Sounds for sliders and tabs**: a soft tick as a slider moves (at most every 45 ms) and a click when another Settings tab is chosen. Not listened to by a person.

- **Garage patterns now replace the paint** instead of drawing over what was there (clicking several piled them up); Undo brings the old paint back. Decals still go on top.
- **The button hover tick no longer repeats** while the pointer rests on a button, including when keyboard or controller focus is on a different button (the likely cause: the two alternated every frame). Checked with a scripted run (focus on one button, pointer held on another, 200 frames): 2 ticks played.
- **Web: the music no longer goes crazy when the page is not in the foreground.** The browser throttles a hidden page's timers and the audio is mixed on the page's own thread, so it starved and stuttered; audio now pauses while the page is hidden and carries on from the same place when it is shown again. The web build compiles with the change; I could not test a real hidden tab here, so please check it in your browser. A window that is visible but not focused is not paused.

## 2.3.39 - 2026-11-09

### Added
- **Garage unlocks, cosmetic only** (nothing changes how a craft drives; the five hulls stay open to everyone). Patterns open by level (3 at the start, all 14 by level 9); four extra colour sets (Metallic, Neon, Pastel, Earth) open at levels 3, 5, 7 and 9; the shape sliders reach +/-10% at the start and widen to +/-25% by level 9; nine decals, stamped on the paint with a click, are each earned by an achievement (first finish, podium, win, 10 races, 50 races, personal best, online race, saving a craft, the tutorial). Locked items stay visible, greyed out, and say how to open them. Level titles (Rookie to Legend) show beside the level on the main menu and the results screen, and a level-up lists what it opened. Nothing is enforced on a server and nothing new is stored in a design. Test `HoverNetGarageUnlocks`; checked by screenshots of the Garage at level 1 and with everything open; the decal stamp was checked with a test hook that uses the same stamp code, not with a real click, and the level-up line on the results screen was not seen on screen. Not done: engine glow and matte/gloss finishes (they need a new design format, server relay, account-service support and renderer work), titles sent to other players.
- **Live HUD numbers**: the speed eases between values instead of jumping, and the position chip grows and flashes gold when you gain a place and red when you lose one. Checked by a screenshot of the HUD (the speed reads normally); the place-change flash was not seen, because a one-car race has no place changes to trigger it.

## 2.3.38 - 2026-11-09

### Added
- **Garage patterns**: 14 ready-made patterns for the paint (racing stripe, twin stripes, hoops, checker, chevrons, diamonds, dots, zigzag, diagonals, flames, rings, pinstripes, bars, fade dots) in the selected paint colour, on top of what is painted; Mirror keeps them symmetrical and Undo takes them back. They are ordinary paint, so nothing new is stored or sent and other players see them as before. Test `HoverNetCraftPatterns`; checked by screenshots of the paint canvas and the preview craft (the button grid itself was not looked at on screen).
- **A low air rumble** under the craft that follows its speed (`HOVERNET_NO_SPEED_SOUNDS=1` turns it off). Checked by recording the audio output, not by listening.

### Fixed
- **The motor and wind sounds were silent in the Linux client** (the web client was fine). The object library carries its own copy of the sound server on Linux, so the craft's motor and friction sounds were registered with a mixer nothing played. The game now exports its sound server so the library uses the game's. Checked by recording the audio output of a bounded run: it now has sound from the first frame where it was silent before; not listened to by a person.
- **The button hover tick could repeat** while the pointer stayed on a button (when another button shared its identity, or the hover flickered): it now plays once on arrival and at most every 150 ms. Fixed from reading the code; I did not reproduce it with a real mouse.
- **The Windows build** failed in `3DViewportScene.cpp` (`Polygon` clashed with the GDI function); renamed. The v2.3.36 and v2.3.37 Windows jobs failed because of it. Not confirmed on Windows until its job runs.

## 2.3.37 - 2026-11-09

### Added
- **The main menu shows your own craft**, turning slowly, with the design from your active Garage slot (it stands still with reduced motion). Checked by a screenshot of the menu on Linux; a changed design shows after the next visit to the menu.
- **Screens fade in** from black (a quarter of a second) when you move between menu screens. Checked by sampling the screen brightness of the main menu as it opens; the other screens use the same call but were not each looked at. `HOVERNET_NO_FADE=1` turns it off.
- **A lap banner** when you complete a lap: the lap, its time, and "BEST LAP" in gold when it beats your earlier laps in that race. Checked by a screenshot with a test hook (`HOVERNET_TEST_LAPBANNER`); not yet seen after a real lap, because the autoplay bot gets stuck on a wall before finishing one.

## 2.3.36 - 2026-11-09

### Added
- **The picture passes run on the graphics card** (first stage of the GPU renderer): de-banding with dither, edge smoothing, sharpening, bloom, the highlight roll-off, the dark edge beside the HUD and the vignette are shaders now (OpenGL 3.3 or ES 3.0 through SDL, its own hidden context), with the HUD's pixels untouched. Each pass matches the processor's within 0.6 levels (test `HoverNetGpuFinish`, skipped where no OpenGL context can be made); 7 ms against 12 ms at 640x480 on an Intel Iris Xe. Without a card, in the browser or with `HOVERNET_GPU=0` (or Settings > Graphics > "Use my graphics card") the processor passes run as before.
- **The card shows the picture and draws the scene.** The depth-based passes (haze and ambient light, contact shading, lights, craft rim, reflections, halos, edge smoothing) are shaders too; the window's renderer is OpenGL when there is a card and the finished picture is written straight into the texture it shows (no read-back); and the walls, floors and craft are drawn by the card instead of the software rasteriser, from the same camera (tests `HoverNetGpuScene`, `HoverNetGpuFinish`). `HOVERNET_GPU_SCENE=0` keeps the software rasteriser, `HOVERNET_GPU=0` or the Graphics setting turns the card off, `HOVERNET_RENDERER=software` keeps the old window renderer. Particles and weapon effects, the browser and the Windows build are not on the card yet (the Windows path was not run).

## 2.3.35 - 2026-11-09

### Added
- **Music.** *Hard Left* (Dan Knoflicek, used under its CC-BY 3.0 option) in the menus and results, *Asphalt* (Sygil, GPL 3.0) in races: streamed Ogg Vorbis, crossfaded, softer under the pause menu, with its own volume in Settings > Sound and a "Now playing" credit. Credits, licences and what they ask of you: `NetTarget/Music/CREDITS.md`.
- **Sounds made in code**: hover, click and back on every button; countdown beeps, GO, a chime each lap, a finish fanfare and a new-best sting.
- **A start sequence**: 3, 2, 1, GO! on screen, with controller rumble; edge flashes for impacts, fuel and pickups; rumble on impacts.
- **Progression**: XP, levels and nine achievements, shown on the results screen and as a level bar on the main menu. Saving a craft and finishing the tutorial earn achievements.
- **A loading screen** with the track's name and a tip.
- Roadmap: "Making it feel like a modern, big-budget game" (`docs/roadmap-3.0.md`), including the recommendation for a GPU renderer.

## 2.3.34 - 2026-11-09

### Changed
- **Settings has tabs** (Player, Sound, Access, Graphics, Display) with Save and Cancel under the panel, so it never scrolls.
- **The main menu has a Tutorial button** next to Leaderboards and Garage; it is highlighted until you have done the tutorial.
- **The chat line you type, and the replay's "who am I watching" label, are drawn in the menu typefaces**, not the bitmap font (the in-race words were already). The only bitmap-font text left in the game is the player number on the minimap icons.
- Windows: the packages and the CI staging now carry `ObjFac1.textures` (the AI-enhanced textures), and the legacy project lists the new sources.

### Fixed
- Overlapping chat lines at the start of a replay; a replay no longer shows the driver's start-of-race hints ("pick your craft", "Esc for the pause menu").

## 2.3.33 - 2026-11-09

### Fixed
- **Your craft design now shows in races on desktop** (the craft is drawn by the object library, which kept its own copy of the designs, and it only showed in true colour: the design is now sent to the library and baked into the craft's 8-bit pixels too).
- Chat lines in the HUD were shown in the bitmap font's code and in the wrong place; the HUD words now sit on the picture, not the window.
- **Finishing a race (or the tutorial) now says so in big letters: "Press ENTER for your results"**, and that Esc is the pause menu. It stays up until the results screen has been opened.
- The tutorial's lesson card is at the top right, under the weapon meter.

## 2.3.32 - 2026-11-09

### Changed
- **A simpler main menu with no scrollbars.** Play Offline, Race Online, Leaderboards, Garage, and a Settings button. Everything else (game settings, controls, watch a replay, tutorial) is in a new Settings hub. The buttons are sized from the room the panel has, so the menu fits any window from 640x480 up without scrolling.
- **The HUD's words are drawn in the menu typefaces** (Oxanium and Inter) on soft panels instead of the pink bitmap font: the race clock and lap at the top, speed, fuel and weapon chips under them, the lap time at the bottom, callouts and chat. The bars, meters, weapon indicator and map are unchanged. `HOVERNET_CLASSIC_HUD_TEXT=1` brings back the bitmap font (the render golden frames use it, so they are unchanged). The tutorial's lesson card is drawn the same way.
- **The Garage** has labelled sliders (right-click resets, Ctrl+click types a value), ready-made colour swatches with a full picker, tooltips, and a preview that stays in view. A test checks that every control visibly changes the preview craft on every hull.

### Added
- **A real tutorial**: Settings > Tutorial is a practice race with a coach. Eight lessons (accelerate, steer, brake, jump, fire, read the HUD, finish a lap); each waits until you have done it, names your own keys or controller buttons, and shows a progress bar over the race. New test `HoverNetTutorial`.

### Removed
- The four-page **How to Play** walkthrough and its first-run screen (and `tutorial-race.bmp`).

## 2.3.31 - 2026-11-09

### Added
- **The Garage: design your own craft.** Shape (length, width, height, nose), body and trim colours and a 32x32 painted livery, with a live preview, eight saved designs and the one used in races. A look only: the hull you pick still decides speed, weight and handling, and designs never enter the race, replays or golden files. Other players see your design in online races (relayed by the race server, never client to client; capability bit 32, messages 36 and 37). Signed-in Outiva accounts can keep the designs on the account (`/account/craft`). New tests: `HoverNetCraftDesign`, `HoverNetCraftLook`, `HoverNetCraftDesignNet`, and the craft checks in `scripts/test-web-account.py`. Details and what was not checked: `docs/craft-designer.md`.

### Changed
- Soft craft shadows, light from the craft and haze, engine/trail/spark effects, and richer colour and vignette are now off by default (still in Settings > Graphics; a saved file from before is reset once).

### Fixed
- The web client stopped loading with "window is not defined" in the page's worker threads (`web-pre.js` touched `window` at the top level, which Emscripten also runs in pthread workers).

## 2.3.30 - 2026-11-09

### Fixed
- The WebAssembly determinism, physics and render checks no longer overflow the default 64 KB stack (they crashed in 2.3.29, failing its GitLab web job). No change to the game itself.

## 2.3.29 - 2026-11-09

### Changed
- **Camera restored.** At the owner's request the chase camera follows the craft exactly as it did before 2026-11-08 (smoothed once per drawn picture). `docs/camera.md`, `scripts/test-camera-rule.sh` updated with it.

### Changed
- **A less cartoony picture.** The colour grade no longer boosts saturation (colours are now a little under the palette's, where they were 12% over), its contrast curve is much gentler, and the cool and warm toning is half as strong; textures get a trace of surface variation (about +-3% brightness, on flat areas only, never on edges, the same every time and tiling seamlessly) so flat colour fields look like surfaces rather than paint. The game's own art is stylised and stays so.

### Added
- **AI-enhanced textures.** `NetTarget/ObjFac1.textures` (14.2 MB, 66 textures at 4x, true colour) is loaded beside `ObjFac1.dat`; `HOVERNET_NO_AI_TEXTURES=1` turns it off. New test `HoverNetTexturePack`; method and costs in `docs/texture-pipeline.md`. Linux only checked; the web build does not ship it.
- **Better textures (Settings > Graphics > Smooth textures).** Every texture gets a true-colour copy: the palette's dither and banding smoothed away without blurring edges, smaller levels made by averaging in linear light (no shimmer in the distance), and every level at twice the resolution with an edge-directed upscale, so walls, floors and craft are crisp close up instead of blocky or blurred. Built on first use; switches off with the adaptive quality. See `docs/graphics-engine.md` ("Textures") for what it can and cannot do.

### Fixed
- **The chase camera follows the craft the same way at any frame rate.** It smoothed its position once per drawn frame, so a slower frame rate made it trail the craft by more (and a faster one by less); it now smooths by race time, exactly as before at one picture per 16 ms tick. A new check (`HoverNetRenderGolden <track> <scene> camerarate`) draws the same race at two frame rates and compares.
- **Less shudder from the drawing itself.** The true-colour textures are now all built on a background thread the moment the palette is set (building a large one on first sight took about 36 ms, a visible hitch, and all 80 take about 290 ms); the adaptive quality now also watches how long whole frames take (not just the passes) and lowers the quality below about 47 frames a second, waiting longer each time before it raises it again; the glowing trail behind the craft and the exhaust and dust streams are removed (a ribbon that swings as the craft turns reads as an elastic band).
- **The shadow is under the craft on the real floor.** It had been placed by a slowly-chasing average of the craft's height, so on a jump or a slope it lagged and looked as if it ran ahead of the craft; it now uses the floor height from the level (within 12 cm of the craft's base when it is driving, on the floor when it is in the air).
- **The craft's shadow stays on the floor while the craft bobs above it, and the engine glow no longer flickers.** The shadow and the floor reflection were glued to the craft's own height, which made its bobbing look much stronger; the glow's random flicker read as vibration. The shadow is also softer (five fine rings instead of three) and only a hint cool in tone, where it had looked like a blue puddle.
- **The look no longer touches the camera, and the picture no longer follows the speed.** 2.3.28 widened the field of view with speed, closed the vignette in with speed, adapted the exposure to the view and drew speed lines; together they made the craft seem to pull away from the camera, especially in turns, and gave the race a film-camera feel. All four are removed; the camera is exactly the game's own again (a check now proves the look leaves the 8-bit scene unchanged). If you installed 2.3.28, this is the fix.

## 2.3.28 - 2026-11-08

### Known limits of this release
- **Released before its checks had finished, at the owner's instruction** (the project rule that a release waits for the full local suite, the Windows runner checks and the GitHub Actions run for the commit was waived for this release). What had been run on this code: the unit and scene checks for the new graphics, the 28 golden frames and the scene checks natively and in WebAssembly under Node, the new rasteriser tests, the sanitizers on the graphics unit test, and a headless run of the real client; the full local suite last passed (150 of 150) on an earlier state of this work, and its run on this commit had not finished. **Not run:** Windows (x64 or Win32), a real browser or phone, a headset, the sanitizers on the whole client.
- **The Modern looks are heavy on one CPU thread** (details and measurements in `docs/render-golden.md`); the adaptive quality steps them down, and on most machines at 720p and above it will settle on the light look. The default look is now Modern on a desktop and Modern light on phones, tablets and in the browser; the Classic look is gone.

### Changed
- **The pause menu, the results screen and the end-of-replay screen have one red button.** The first choice (Resume, Keep driving, Watch again) is red and the others are quiet outlined buttons, so a player sees at once what carries on. Display only. Checked in a headless screenshot of the results screen.
- **Thirty more graphics improvements** (see `docs/render-golden.md`): sky-tinted ambient light, mist, colours fading with distance and a horizon glow; walls that block light; a headlight cone; light that follows surfaces (diffuse and glints) and blends as light does at high quality; a rim light, sky sheen and glints on craft; a reflection of the craft in the floor; glows on missiles, mines, power-ups and other craft; exhaust, dust, missile smoke and power-up sparkle; anti-aliased edges that leave textures alone; a dark edge beside HUD text; sharper textures; a soft highlight roll-off; a split-toned grade and a **Brightness** setting; a finer dither; cool-toned soft shadows; and **adaptive quality** that lowers itself when the passes are too slow and comes back when there is room. Settings > Graphics has the new options under *More graphics options*. The medium and high looks are heavy on one CPU thread (see the costs in `docs/render-golden.md`); on most machines at 720p and above the adaptive quality will settle on the light look.
- **The rasteriser can draw true colour with filtered textures, and on several threads.** Walls, floors and objects seen close up are no longer blocky (*Smooth textures*, in Modern and high); its working state is per thread so two frames can be drawn at once (checked by a new test: 160 frames on four threads, all identical to the single-thread answer). The 8-bit picture and the 28 golden frames are unchanged.
- **The graphics pipeline is now a list of passes** (filter, lighting, bloom, effects, vignette) over the renderer's depth, normals and lights, with a way to add a pass, per-pass timing in `HoverNetRenderGolden`, and a write-up in `docs/graphics-engine.md`. No pixel changed in the restructure (checked on 24 hashes).

### Fixed
- **`HoverNetTrack3DViewer` builds again.** Its `RunClient` set the playtest path (`--playtest`) in a build that does not have it, so that target failed to compile (the game client was not affected).

### Added
- **A new look for the race view (Settings > Graphics), and the Classic look is gone.** The original picture is no longer offered (it limited what the graphics could be improved to do; a saved "classic" becomes the default for your device). *Modern* (the default on a desktop) adds soft craft shadows, richer colour and a vignette, smoother colour ramps (de-banding with dither), an engine glow and a light trail behind the craft, impact sparks and speed lines, a slightly wider view at speed, and **lighting**: coloured light pools from the craft and a headlight on the floor and walls, a light under other craft and your ghost, on missiles, mines (pulsing) and power-ups, an orange flash on impact, depth haze, a little contact shading where a surface has something just in front of it, and light that follows the surface (a wall turned towards a light is lit more than one turned away, with a glint on the floor). There is no speed blur, and the HUD is never blurred or smoothed. *Modern (high quality)* also smooths jagged edges and adds a glow around bright lights, and needs a fast processor; *Modern (light)* leaves out the smoothing and de-banding for slow devices. Each part can be switched off, and **Reduced motion** turns off every moving effect (speed lines, sparks, the impact flash, the wider view). *Modern light* is the default on phones, tablets and in the browser. It is presentation only: the race and the replays are unchanged, the rasteriser's own output is unchanged (the 28 golden frames still match, on native and WebAssembly; they now serve as its regression test), and the headset gets the plain picture. New checks: `HoverNetModernGraphics`, `HoverNetModernGraphicsScalarMatches` and `HoverNetModernGraphicsScenes`; costs and what was and was not run are in `docs/render-golden.md` ("The modern look"); the plan, with the parts not yet done (a GPU renderer, bloom and smoothing that are cheap enough for phones, weapon effects), is in `docs/roadmap-3.0.md` ("Graphics improvements"). Not yet run on Windows or looked at in a browser.
- **The track editor opens HoverCad projects (`.TR`).** File > Open (and `HoverNetTrackSource import FILE.TR OUT.track.txt`) converts a HoverCad project into a track source using HoverCad's own export rules, lists anything it could not carry over, and offers it for saving under a new name. Checked on Linux with the three HoverCad projects in `NetTarget/mazes`: all import and compile, and ClassicH plays exactly like the official track (same lap time and stage ticks with the autopilot driver). Steeplechase and The Alley2 could not be compared that way. Derived from HoverCad, so it carries the modification notice.
- **The track editor as a Debian package, `hovernet-editor`** (it needs the `hovernet-game` package, which holds the game, the object plug-in and the fonts). It installs the editor, the compiler and validator it runs, the track background pictures the compiler reads, the bundled track sources as examples, a `hovernet-editor` launcher and a menu entry. Checked by unpacking the package into a folder and running it from there the way an installed copy runs: it finds its fonts, compiles and strictly checks an example with its background picture, playtests it in the installed game, and the launcher takes file names relative to where you are (`scripts/test-editor-package.sh`). The CI package step now builds it; **never installed with `dpkg -i`, and there is no Windows installer for the editor yet.**
- **Playtest from the editor.** *Track > Playtest in the game* compiles the track being edited with the real compiler and opens the game straight onto it (`HoverNetGame2Player --playtest FILE.trk`, listed as "Playtest", no menus, then the usual pause and results screens). A track with a fault the editor can see (for example a surface the game does not have) is refused first, with the reason; advice such as "no checkpoints yet, so it plays as free play" does not stop you driving it. Desktop only for now (the browser editor says so). The game and the editor must sit side by side (the editor looks for the game next to itself). Tested on Linux, headless: the bundled tracks and tracks made in the editor.
- **Editor autosave in the browser.** The browser editor keeps a copy of the track being edited in the browser a moment after every change and offers it back after a reload ("Restore my work" or "Start fresh"); saving to a file clears it. Tested in headless Chrome with real clicks and reloads (36 checks).

## 2.3.27 - 2026-11-08

### Changed
- **A cleaner look for the menus.** The solid red header slab (with its title stuck in a corner) on Local Race Setup, Settings, Controls, Watch a Replay and Leaderboards is replaced by a title in the display font with a thin accent line under it, like the lobby. Each screen now has one red primary button; Cancel, Back, Reset and Download all are quiet outlined buttons, and buttons have a little depth. Settings groups have headings, and its clipped hint text wraps. The track map is a clean slate-blue plan with a light edge in a rounded frame instead of a flat lavender blob. Display only: no change to the simulation, replays or golden files. Checked on Linux with headless screenshots and the existing screen tests; the browser build compiles the same code and the browser smoke test passed, but every screen was not looked at in a browser or on Windows. The main menu, lobby, results and the in-race HUD are not restyled yet.

### Fixed
- **The track compiler gives the same bytes on every platform.** Two sorts in `LevelBuilder.cpp` left surfaces with equal levels in whatever order the C library chose, so the same track source compiled to different files on glibc and musl (287 bytes of 578,799 differed in the WebAssembly build). It is now a stable sort; every bundled track still compiles to exactly its committed `.trk` (checked), so no track and no replay changes.

### Added
- **For operators: the Track Market service and its tools** (not part of the installers or the game): a submission service with a live catalogue (`packaging/web/hovernet_market.py`, invited authors only, a moderator reviews an author's first track, reports hide unlawful content at once, revocations), a sync that puts listed tracks into the race server's community folder and removes revoked ones, and one-command installers for the market and the browser editor (`packaging/web/install-market.sh`, `packaging/web/deploy-editor.sh`, `scripts/owner/deploy-all.sh`). The game deletes a revoked market track from the player's own folder. See `docs/adr/0004-track-marketplace.md`, `docs/specs/track-market.md` and `docs/owner-actions.md`. Not installed by this release; the market starts closed.
- **The editor can build a road from scratch.** An *Extend the road* tool adds a room beyond any open edge, with a live preview, a chosen length and a left or right turn; rooms whose edges meet are joined automatically, so a loop closes itself; rooms can be deleted (they renumber, and a room that still holds starts or elements is refused with the reason); and *New* offers a start room to build the road from. The editor now also says when a track has no finish line, no checkpoints or fewer than eight starts. A loop built room by room from a single start room compiles, passes the strict validator, is finished by the game's driver and is admissible to the catalogue. Tested headless on Linux; the browser build compiles the same code but a real-mouse test of this tool was not run.
- **A track editor (first version).** `HoverNetTrackEditor` makes and edits tracks: start a new loop (rooms, radius and road width are yours to choose) that is already a race, or open a `.track.txt`; see the plan, drag starts and elements and room corners (rooms that share a corner stay joined), place pickups, fuel, speed pads, starts and the gates from the game's own list, change heights and surfaces, undo and redo, and see a plain-language list of what is wrong. Track > Compile and check runs the real compiler and strict validator. It is the same program on Linux and in the browser (a track opens through the file picker and saves as a download); the Windows build was not run. **It is not in the installers or packages in this release**: the browser editor is put online separately (`packaging/web/deploy-editor.sh`), and the desktop editor program is only built from source for now. Publishing and the in-browser compile are not in it yet. Tested headless (`HoverNetTrackSource`, `HoverNetTrackEditor`) and in headless Chrome (`scripts/web-smoke/web-editor.mjs`); nothing is deployed (`packaging/web/deploy-editor.sh`). See `docs/roadmap-track-editor.md`.
- **The game lists and downloads Track Market tracks** (desktop and browser, one code base). Tracks published to the market are community tracks: they appear in the one "Community tracks" list with the inherited library, sorted with it and found by the same search (which also matches the author), each credited with its author and licence (what CC-BY asks for), and download from the market's own address with the same size and SHA-256 checks as community tracks. The catalogue is fetched in the background when the main menu opens (never in a test run unless pointed at a test server; `HOVERNET_NO_MARKET=1` turns it off); an answer that is not a catalogue never replaces the last good copy, and a track without an author or licence is not listed. Display and download only: no change to the simulation, replays or golden files. Tested natively (`HoverNetTrackMarketClient`, 16 checks, and the existing catalogue, download and community tests); **the browser build compiles with it (not run in a browser) and no real market server has been tried**. The market service, its server sync and installer (`packaging/web/`) are committed but **not installed anywhere**: see `docs/owner-actions.md`.

## 2.3.26 - 2026-11-07

### Fixed
- **The nightly sanitizer run (ASan and UBSan, every test) is green again.** It had failed every night since at least 25 October. Real defects: the client leaked its observer, menu font and ghost craft on some ways out (online runs especially), `HoverNetDriver` leaked its factory registrations, and Ed25519 shifted negative values left (undefined behaviour; the signatures are unchanged and the RFC 8032 vectors still pass). The rest were test set-up: AddressSanitizer's free-memory quarantine read as growth in the load test, and the crash-report test crashed on purpose under sanitizers that handle the crash first. No change to the game's behaviour or the simulation. Checked with the full test suite on Linux, normal and sanitizer builds.

### Added
- **Gap to your ghost on the HUD.** While your best-run ghost is shown, the status line adds `Ghost +1.2s` (behind it) or `Ghost -0.4s` (ahead). Display only: it is not part of the simulation and changes no replay or golden file. Tested by `HoverNetGhostRun` (the gap, a track that crosses itself, off the line, another room) and `scripts/test-ghost.sh` (the real client shows a gap while racing its saved ghost). Checked on Linux only; the browser build shares the code but was not run. See [docs/ghosts.md](docs/ghosts.md).
- **Outiva console status.** The web site now answers `GET /outiva-status.json` with `{"status":"available","players":N}` (cached 15 seconds, CORS for outiva.com only), served by the account service (`packaging/web/hovernet_account.py`, works without OIDC configured). It reads the race server's loopback `/health` and `/metrics`; a draining, unreachable or unreadable server is `degraded`, and nothing else (raw metrics, names, results, device ids) is passed on. The count is the new `hovernet_players_active` gauge (drivers in races that have not finished) plus the new `hovernet_lobby_users` gauge (people in the lobby), not spectators or moderators. The monitoring port stays loopback-only and the adapter refuses any other address. **Operators:** add the `/outiva-status.json` proxy lines from `packaging/web/hovernet.outiva.com.conf`, set `status-port 9612` on the server, and restart the account service; see `docs/outiva-console-integration.md` and `docs/operators.md`. Tested by `scripts/test-web-account.py` and `scripts/test-observability.sh`.

### Changed
- **New hovercraft picture on the main menu**: a large render of the red craft (jet engine with flame, chrome and red cockpit) now fills the left of the menu (`NetTarget/LinuxClient/assets/menu-banner.bmp`, transparent background; the earlier pilot picture `menu-hovercraft.bmp` stays as the fallback). The third line under the title now reads "FAST, FUN, FREE!". Display only: no change to the simulation, replays or golden files. Checked by rendering the native client headlessly at desktop and phone-portrait sizes; the browser, Windows and phone-landscape renders were not checked. The origin and licence of the pictures are not recorded yet (see `THIRD_PARTY_NOTICES.md`).
- **The web account service keeps your Outiva callsign and a callsign chosen in the game apart.** The player name now follows the Outiva account callsign, and a name picked in the game is remembered separately instead of replacing it (`packaging/web/hovernet_account.py`; web host only, no change to the desktop client or the simulation).
- **Outiva sign-in for the web client now works the way NetPanzer's does.** The first version (browser-side OpenID Connect with the token handed around in the page) is replaced by a small account service on the web host (`packaging/web/hovernet_account.py`): it is the confidential OIDC client, keeps the secret, and gives the browser an HttpOnly cookie. The page asks `/account/me` who is signed in, so it works the same at hovernet.outiva.com and framed inside outiva.com, with no token in a URL, `window.name` or local storage. Signing in from the Outiva console is a silent round trip through `/account/login` that comes back to the game. Your display name follows your account: it is remembered per account and applied in any browser; an account with no saved name starts from the Outiva username without replacing a name the browser already has. Guests are unchanged, and the service is optional (not installed, or Keycloak down, means guest play). The race servers do not check the name, so it is a convenience, not proof of identity. **Operators:** `sudo bash packaging/web/configure-account-oidc.sh` on the host (turns the existing Keycloak client `hovernet-web` into a confidential client with a secret, installs the service), enable the `/account/` proxy lines in the Apache site, and reinstall `packaging/web/hovernet-web-deploy` (the deployed bundle no longer carries `outiva-auth.js`). Tested by `scripts/test-web-account.py`.

### Fixed
- **Replays show the real names of the racers, not "Player 1".** The names were only used in the craft question and the "Following" message; the timeline label while watching and the finishing list at the end of a replay still said "Player N". Both now use the racer's name. The server sends an opt-in final slot/name roster before a race starts (and when a spectator joins), without changing start behaviour. Races you take part in or watch from the beginning now keep that roster beside the replay in Watch a Replay > My races (`<file>.hnr.names`; the replay file itself is unchanged), so those replays are named too. Replays saved before this change have no names and still say "Player N". Covered by `HoverNetReplayLibrary`, `HoverNetClientReplay` and the protocol/replay screen suites.
- **Gamepad menus behave the same on every screen.** B (back) only worked on about half of the menu screens; on Settings, Controls, the lobby, the online-servers screen and others it did nothing, so you had to reach for the keyboard. B now does what Escape does on every menu screen, on the desktop and web clients. While you are typing in a field or a drop-down is open, B closes that first, and while Controls waits for a button to bind, B can be bound. Covered by `HoverNetMenuInputSmoke` (`NetTarget/LinuxClient/MenuInput.h`).
- **Screens with blocks of options, the laps setting and the How to Play briefing are now workable with a gamepad.** The D-pad used to stay trapped inside the first block of a screen (you had to press A to enter a block and B to leave it); it now moves through every option on the screen in order. A slider (laps, volume, UI scale, axis deadzone) changes with left/right as soon as it is highlighted, instead of needing A first. In the briefing the focus starts on NEXT, so A pages through it, B goes back a page (and leaves from the first page), and LB / RB turn the pages.

## 2.3.25 - 2026-10-20

### Fixed
- **A replay said "Following player 1 of 1" instead of who was racing.** It now names the racer (for records, winners, records broken and server replays), including when switching craft with `[` and `]`. Replay files still do not store names; they come from the server lists.
- **The web site could end up serving an old client** (a late or re-run CI job for an old commit replaced the newer release; the old page had no self-update check, so browsers stayed on it). The web builds now carry their commit time and the deploy wrapper refuses to put a build older than the live one in front of it. **Operators:** install the new `packaging/web/hovernet-web-deploy` on the web host (the next deploy does not update it by itself).

## 2.3.24 - 2026-10-19

### Fixed
- **Records broken listed a one-lap race twice** ("Fastest lap" and "Fastest race, 1 lap" with the same time): in a race of one lap the finish is the lap, so the result is now listed once, as the lap. The 1-lap race record is still on All records. The server makes the change: updating it is enough.

## 2.3.23 - 2026-10-19

### Fixed
- **Records broken now lists the first time on a board too** ("set the first record"). Your Bi-Turbo time on The Alley2 did not appear because it was the first Bi-Turbo time there: nobody could pick another craft online before 2.3.20, so almost every board of a craft other than the Basic is empty, and a first time is a record. A time that beats the previous holder still says who and by how much. Update the game and the server together: an older game cannot read the new list (it says so).

### Added
- **All records and Records broken include the community tracks.** All records has the same searchable track picker as the race screens (the official tracks first, then the community library) and shows a community track's boards per craft; Watch on a time on a community track downloads the track first if you do not have it.

## 2.3.22 - 2026-10-19

### Added
- **Community tracks work in online races, on every client (Linux, Windows and the browser).** Host any community track in the lobby (the button says **Download & Create** if you do not have it yet), and join or watch races on one (**Download & Join**); a private race downloads its track when it is about to start. A server hosts a community track when its owner has put it in the server's community folder (`--community-dir`; the packaged service reads `/var/lib/hovernet/community-tracks`, filled by `scripts/sync-server-community-tracks.sh`, about 700 MB, every file checked against the manifest). Servers without the folder keep hosting the official tracks only, and say so when a community track is asked for. See [community-tracks.md](docs/community-tracks.md). **Operators:** run the sync script on the server to offer them; nothing changes until you do.

## 2.3.21 - 2026-10-19

### Fixed
- **The web client's community-track download did nothing.** A browser cannot fetch GitHub's release downloads (they redirect to another host and send no CORS header, so the browser blocks the request), so the web client now downloads the tracks from the web host itself (`/community-tracks/`). **Operators of a web host** need to run `scripts/sync-web-community-tracks.sh` (about 151 MB) and enable the Apache snippet `packaging/web/community-tracks.conf` before the download works (`scripts/deploy-web-community-tracks.sh` does both); see [community-tracks.md](docs/community-tracks.md). The native game still downloads from GitHub.

## 2.3.20 - 2026-10-18

### Fixed
- **You can pick your craft in the countdown of an online race again** (Left and Right on a keyboard, the steering stick on a phone). The server's race used to start at time zero with no countdown at all, so the simulation never saw the keys pressed before the start and every online race was driven in the Basic craft. The countdown is now part of the race: **6 seconds** (375 ticks, as in single player), simulated by the server and by the game, and recorded in the replay, so the craft you pick is the craft the server runs, shows on the leaderboards and replays. The wait from the lobby to the start is about a second longer than before (1 s for the game to load the race, then the 6 s countdown) but you can now use it.
- **Update the game and the server together.** This changes the game's rules version (6 to 7): a 2.3.19 game cannot join a 2.3.20 server and says so. Replays saved before (rules 6) can still be watched and verified. One thing is unchanged: Left pressed on the Basic craft keeps it Basic (Right four times reaches the Manta), as the simulation has always counted.

## 2.3.19 - 2026-10-18

### Added
- **Leaderboards: Records broken tab**, now the first tab; the former *Records* tab is called **All records**. Lists the newest results that beat a record (a craft's best lap, or best race time of a given lap count, on a track), with the time, the holder they beat and a Watch button. The first result on a board sets a record and is not listed; a tie does not break one. It is worked out from the replays the server keeps, like the Records tab. Update the game and the server together: an older server does not know the tab (the game says so).

## 2.3.18 - 2026-10-18

### Added
- **Leaderboards: Recent winners and Monthly league tabs.** Recent winners lists races won with two or more players (with Watch the race). The league gives 3 points to the winner of a race that counts (nothing for second and third, so two-player races are not the best way to farm points), resets on the 1st of each month (UTC), and shows the winner of every past month and of every finished year. Races count only when the players are on different devices with different names (a random per-installation device id; **IP addresses are not used**), and a pair of devices scores at most 3 duels a day. Racing yourself from two tabs or clients gives nothing and is not listed. A device id cannot prove identity (accounts will); see [leaderboards.md](docs/leaderboards.md).
- Update the game and the server **together**: a game before this version cannot score, and an older server does not know the new tabs (the game says so). New server option `--league-file`; new results-table column `valid`.

### Changed
- The web client no longer shows **QUIT HOVERNET** (pause menu, race results, replay end screen): a browser tab has nothing to quit to. The native clients keep it.

## 2.3.17 - 2026-10-19

### Added
- The end-of-replay screen has a **LEADERBOARDS** button (Watch again, Watch another replay, Leaderboards, Main menu, Quit); watching a record from there plays it and ends on the same screen.

## 2.3.16 - 2026-10-19

### Added
- **Leaderboards.** A new main-menu screen with the community's best times: pick a track and see, for each craft (Basic, Hi-Tech, Bi-Turbo, Eon, Manta), the best five fastest laps or fastest races (by number of laps), each with the player's name, the time and the date, and a WATCH button that plays the run. Records are per craft, so a record in one craft never replaces one in another. It replaces the Track records tab of Watch a Replay. The server writes the craft (and the name) of each player into the results table at the end of a race; races saved before this have no craft and are not on a board. See [docs/leaderboards.md](docs/leaderboards.md).
- **The server replay list shows everyone in the race, in the order they finished**, with their name, craft and time ("did not finish" last), and the question of which craft to follow names each player and marks the winner.
- **Dates and times are written the UK way** everywhere in these screens ("18 Oct 2026, 21:11", in the device's own time zone), in My races, Server replays and the Leaderboards.

### Changed
- The records lists changed shape (per craft, best five), so **update the game together with the server**: a 2.3.15 game cannot read the records from a 2.3.16 server.

## 2.3.15 - 2026-10-19

### Fixed
- **The server package could not be upgraded from the pipeline since 2.3.9, so the live server never got the replay saving.** 2.3.9 changed the packaged server configuration file; dpkg then asks what to do about a configuration file the operator has edited, and with no terminal it fails, leaving the new package unpacked but not configured and the old server running (the pipeline log: "end of file on stdin at conffile prompt"). The packaged configuration is again byte for byte what it always was (the replay folder is passed by the service file), and a test (`scripts/test-conffile-upgrade.sh`, also run in the pipeline's validate stage) upgrades over an edited configuration with no terminal and fails if a question appears. It reproduces the failure with the changed file.
- The Server replays and Track records tabs say so when the server answers in a form this version of the game cannot read, instead of loading forever.

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
