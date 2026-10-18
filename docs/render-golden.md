# Render golden frames (the classic preset's reference)

Stage 3 of the [3.0 roadmap](roadmap-3.0.md) replaces the 1990s software renderer behind the simulation boundary and promises a **classic
preset that matches the 2.0 output, verified by automated frame captures**. This is that verification, built first so that any change to
the renderer, now or later, shows up as a changed hash instead of as a surprise on screen.

## What is checked

`HoverNetRenderGolden <track> <scene>` plays a scripted two-craft race (the same inputs as the determinism and replay tools) on one of the
seven bundled tracks, stops at a fixed tick, and draws the **whole race view the way the game does**: the 3D scene, both craft, the
track map and the HUD, through `MR_Observer::RenderNormalDisplay` into the 8-bit framebuffer. It prints two hashes: of the framebuffer bytes (the palette indices the rasteriser writes) and of the picture after the palette the game builds is applied (`rgb=`; the palette uses gamma and intensity maths in floating point, so it is hashed on its own).
The simulation is identical on every platform and its time is frozen, so the picture is too.

| Scene | Tick | Size | View |
| --- | --- | --- | --- |
| `grid640` | 0 | 640x480 | chase view on the starting grid |
| `drive640` | 600 | 640x480 | chase view, mid-race |
| `wide1280` | 600 | 1280x720 | the same, at a widescreen size |
| `cockpit640` | 600 | 640x480 | cockpit view |

`scripts/test-render-golden.sh <command>` runs all 28 (7 tracks x 4 scenes) and compares with `docs/render-golden.txt`
(`--update` rewrites it). It runs in ctest (`HoverNetRenderGolden`) and in the GitLab web job against the WebAssembly build.
`HOVERNET_RENDER_DUMP=<directory> HoverNetRenderGolden <track> <scene>` also writes the frame as a PGM (the palette indices as grey levels)
so a changed hash can be looked at; `HOVERNET_RENDER_TRACE=1` prints stage markers.

## Results (2026-10-18)

- Native (GCC, Release, Linux x86-64): 28 frames, identical on repeated runs.
- WebAssembly under Node (Emscripten): the same 28 pairs of hashes. The software rasteriser, the HUD and the sprites draw the same pixels in
  the browser's build as in the desktop build.
- **Windows x64 (MSVC 14.44, Release, built with CMake on the Windows runner from the same commit, run by hand): the same 28 pairs of
  hashes.** This only held after two fixes that the first Windows run found (before them every frame differed):
  1. The Linux and web builds' stand-in for the Win32 `MulDiv` truncated; the real one rounds to the nearest integer. The renderer's
     projection uses it, so Linux and web drew some pixels one off from Windows (about 5% of the picture). The stand-in now rounds like Windows
     (`NetTarget/Platform/MfcCompat.h`), so all three platforms agree, and the goldens were regenerated (the simulation does not use `MulDiv`;
     the physics and replay goldens are unchanged, rules version unchanged).
  2. The Windows build of the client took the HUD texts (the race time and lap line, the countdown, the lap times, the rank tables) from a Win32
     string table that the CMake-built `HoverNet.exe` does not have, so on Windows those lines were **blank**. It now uses the same literal texts
     as Linux and the browser; only the legacy `Game2.exe` still reads its resources. Seen in a headless capture of the real Windows client
     (the countdown "Start in 02.57 sec. for 1 laps" was missing before, present after).
- The GitHub Actions Windows job now builds the tool and runs `scripts/test-render-golden.sh` with the others (the script passes under Git Bash on
  the Windows runner; the Actions run itself has not been seen yet).
- Not yet run: Win32 (32-bit), and a
  comparison with a capture of the original 2.0 build (the committed hashes are of the current renderer, so they guard against change; they
  are not themselves proof of equality with 2.0 until a 2.0 capture is compared, which needs the 2.0 binary).

## Frame time (measured 2026-10-17, one desktop x86-64 CPU, one thread)

`HoverNetRenderGolden <track> <scene> bench <frames>` draws the same scene repeatedly and reports the mean and slowest frame (the whole race
view: scene, craft, HUD). ClassicH, mid-race:

| Size | Native, Release | WebAssembly under Node |
| --- | --- | --- |
| 640x480 | 0.56 ms | 0.95 ms |
| 1280x720 | 2.7 ms | 3.7 ms |
| 1920x1080 | 6.0 ms | 7.8 ms |

(A Debug build is about three times slower than these Release figures; the golden hashes are identical in both.) Tidal Causeway, the
heaviest of the three tracks tried, costs about 25% more. The software rasteriser is therefore not what limits a wide or high-resolution picture
on a desktop; it is untested on phones and headsets (the Quest build keeps 640x480 because it draws every frame twice). This is what the
**Race view** setting relies on: Widescreen draws 1280x720.

## What this does not cover

The last step to a screen, scaling and presenting by the graphics backend (SDL, or the browser canvas), is not in these hashes, nor is the
menu and settings UI (drawn by Dear ImGui in window coordinates; it has its own screenshot tests). A layer for those would need a window.
