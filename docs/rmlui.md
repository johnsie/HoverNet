# RmlUi: the new interface technology (experimental, opt-in)

The game's screens are drawn with Dear ImGui, an immediate-mode toolkit made for tools. It has no layout engine, no transitions and no animation, so polish
costs workarounds (the screen slide in `ScreenFadeFrame` moves vertices after the fact). The owner is open to better technology, so screens are being rebuilt
with **RmlUi** to find out what a real interface library gives. **It is not the default**: the older ImGui screens are what players see, because the first RmlUi
versions looked worse (small, soft text; a blocky picture; a background that was not the old one). It is switched on to compare and to finish.

## What it is

[RmlUi](https://github.com/mikke89/RmlUi) draws interfaces written in HTML-like markup (`.rml`) and CSS-like style sheets (`.rcss`): flex layout, `transition`,
`@keyframes` animation, `:hover`, `:focus` and `:active` states, fonts through FreeType. The files are in `NetTarget/LinuxClient/assets/ui/`
(`main-menu`, `results`, `choice`, `form`); the C++ side is `NetTarget/LinuxClient/RmlMenu.{h,cpp}`, which knows nothing of the game except what it is handed and
what the player chose. The game's own screens call it from `Track3DViewer.cpp` (search `UseRmlUi`) and keep their old code as the fallback.

Version 5.1 is used because it is the last line that builds as C++14 (the Windows client does). It is vendored in `NetTarget/ThirdParty/rmlui` with FreeType 2.13.2
(`NetTarget/ThirdParty/freetype`); licences and checksums are in `THIRD_PARTY_NOTICES.md`. Changes to the vendored code are marked "HoverNet, 2026-11-09": the SDL
backend decodes images with stb_image (no SDL_image) and applies 2D transforms (the original ignored them, so slides and scales did nothing), and the font engine loads
glyphs unhinted. RmlUi 5.1's `robin_hood.h` needs `<cstdint>` first on current compilers; the build adds it as a compile option instead of editing the file.

## Using it

* Build: RmlUi is built in by default (`-DHOVERNET_RMLUI=OFF` leaves it out). Linux, the browser build and Windows x64 all compile it.
* Show it: `HOVERNET_RML_MENU=1 HoverNetGame2Player ...`, or `?menu=rml` on the web page. `HOVERNET_RML_MENU=0` or nothing shows the ImGui screens. If a new screen cannot load
  its files it falls back to the ImGui one (checked by hiding the `ui` folder). Bounded test runs (`--frames N`) keep the ImGui screens unless `HOVERNET_RML_MENU=1`.
* Test: ctest `HoverNetRmlMenu` (`scripts/test-rml-menu.sh`) drives the real event path: keys and real mouse clicks on every main-menu door, interface sounds, the results
  screen after a real race, the pause menu in a race, the Settings hub and form, the local race setup and Controls (a remap). Scripted input is `HOVERNET_TEST_RML`:
  `down,enter` is a list of steps for every screen; `a;b;c` gives each screen, in the order they open, its own list. Steps: `up` `down` `enter` `esc`, `click:ID`, `tab:N`,
  `text:ID=value`, `press:KEY`. Screenshots: `HOVERNET_CAPTURE_MENU=file.bmp` (main menu) and `HOVERNET_CAPTURE_RML=file.bmp` (the others; in a scripted run at the 62nd frame).
* Browser: `scripts/web-smoke/web-rml-menu.mjs <web-dir>` opens the page with `?menu=rml` in headless Chrome and fails if the page does not see a drawn frame within 40 seconds.
  (The page reloads in "compatibility mode" after 25 seconds without one: every screen must call the game's frame hook, `HoverNetRml::SetFrameHook`. Missing that looped the
  live site in 2.3.42, and the first version of this test passed anyway because the page gives up waiting after 60 seconds.)

## What is built

| Screen | State |
| --- | --- |
| Main menu | Built, with the older menu's animated background (grid floor, drifting speed streaks), the turning craft placed as before, the level bar. |
| Results | Built, with the older screen's red wash and stripes; the XP counts up and the parts appear in turn. |
| Pause menu, Settings hub, quit confirmation | Built (one "choice menu" component). |
| Game settings | Built (the form engine: tabs, toggles, sliders, choosers, text fields; same live previews, validation and Save/Cancel). |
| Local race setup | Built (filterable track chooser, track map, briefing, laps and records, download with progress). |
| Controls | Built (Keyboard and Controller tabs; remapping by pressing the key or button, with swap on conflict). |
| Not built | Online lobby and its host/join dialogs, Garage, Leaderboards, Replays, the browser sign-in gate, loading screen, tutorial overlay, ImGui-only tools (the track editor stays on ImGui). |

## How it is drawn

Every screen is drawn at twice the size into an off-screen picture and shrunk onto the window with smooth filtering (`Supersample` in `RmlMenu.cpp`; `HOVERNET_RML_SS=1`
turns it off). Without it the text and edges looked rough, because SDL's renderer does not smooth, while Dear ImGui builds smooth edges into its own shapes.

## Known problems and what was not checked

* Rounded corners: SDL's **software** renderer leaves ragged dark lines along the top of rounded boxes, so buttons and cards are square. This was not seen on a GPU renderer
  because none was available where this was built; rounded corners may be fine there and could be restored.
* The screens follow the UI Scale setting but do not scale with the window size; text is the size it was designed at (1024x768) times that. High contrast is not applied.
* Compared against the older screens by screenshots at 1024x768 only. Run on Windows x64 headless (the Settings form, the local race setup and a Controls remap, 2026-11-09) and
  the main menu in headless Chrome. Not checked: large windows, phones and touch, a real controller, a real download, choosing a track from the list in the local setup, the browser
  build of the form screens, a real (not headless) window on Windows.
* The craft is a small preview picture (560x420) stretched, so it looks blocky next to a vector UI.

## If it goes on

Keep the ImGui screen until the new one is shown to be better on desktop and web; keep ImGui for the track editor and tools. Compare each screen with its older version in
the same window size before making any of them the default.
