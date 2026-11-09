# RmlUi: the new interface technology (prototype)

The game's screens are drawn with Dear ImGui, an immediate-mode toolkit made for tools. It has no layout engine, no transitions and no animation, so
polish costs workarounds (the screen slide in `ScreenFadeFrame` moves vertices after the fact). The owner is open to better technology, so the main menu
has been rebuilt with **RmlUi** to find out what a real interface library gives.

## What it is

[RmlUi](https://github.com/mikke89/RmlUi) draws interfaces written in HTML-like markup (`.rml`) and CSS-like style sheets (`.rcss`): flex-style layout,
`transition`, `@keyframes` animation, `:hover`, `:focus` and `:active` states, fonts through FreeType. The files are in
`NetTarget/LinuxClient/assets/ui/` (`main-menu.rml`, `main-menu.rcss`); the C++ side is `NetTarget/LinuxClient/RmlMenu.{h,cpp}` and knows nothing of the game
except what it is handed (level, version) and the door chosen.

Version 5.1 is used because it is the last line that builds as C++14 (the Windows client does). It is vendored in `NetTarget/ThirdParty/rmlui` with FreeType
2.13.2 (`NetTarget/ThirdParty/freetype`); licences and checksums are in `THIRD_PARTY_NOTICES.md`. Three changes were made to the vendored code, each marked
"HoverNet, 2026-11-09": the SDL backend decodes images with stb_image (no SDL_image dependency), and the font engine loads glyphs unhinted (hinting dropped
the bottom row of small capitals in the variable fonts). RmlUi 5.1's `robin_hood.h` needs `<cstdint>` first on current compilers; the build adds it with a
compile option instead of editing the file.

## Using it

* Build: `cmake -DHOVERNET_RMLUI=ON ...` (default OFF; the GitHub and GitLab builds do not use it yet).
* Run: `HOVERNET_RML_MENU=1 HoverNetGame2Player --play` shows the new main menu; anything else shows the ImGui one, which stays the default.
* Test: ctest `HoverNetRmlMenu` (`scripts/test-rml-menu.sh`): Enter, Up and Down move and choose, and a real mouse click on each of the six doors is reported.
  The screenshot hook `HOVERNET_CAPTURE_MENU=file.bmp` works as for the ImGui menu.

## What the prototype does and does not do

Done: the layout (brand and level bar top left, the craft, the panel with RACE ONLINE first and its pitch), the craft drifting in, the panel sliding in from the
right, the button hover and press feel, the RACE ONLINE pulse, keyboard, mouse and controller focus, Esc and B, closing the window with the quit confirmation.

Not done, and not checked: the turning craft in the player's own colours (it shows the stock picture), phone and touch layouts, high-contrast and UI-scale
settings, the Track Market refresh, the web build (RmlUi builds with Emscripten and FreeType, which the web toolchain provides, but this was not tried),
Windows, the controller (the code maps D-pad and A and B, but no pad was tried), and any other screen. Rounded corners on buttons drew a ragged top edge in
the software renderer, so the buttons are square for now (the panel's rounded corners are fine). Small text is softer than ImGui's because of the unhinted
glyphs.

## If it goes on

Move one screen at a time, keeping the ImGui version until the new one is proven on desktop and web; keep ImGui for the track editor and tools, which suit it.
Decide the Windows and web builds before the second screen, because that is where a library choice becomes expensive.
