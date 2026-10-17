# Render golden frames (the classic preset's reference)

Stage 3 of the [3.0 roadmap](roadmap-3.0.md) replaces the 1990s software renderer behind the simulation boundary and promises a **classic
preset that matches the 2.0 output, verified by automated frame captures**. This is that verification, built first so that any change to
the renderer, now or later, shows up as a changed hash instead of as a surprise on screen.

## What is checked

`HoverNetRenderGolden <track> <scene>` plays a scripted two-craft race (the same inputs as the determinism and replay tools) on one of the
seven bundled tracks, stops at a fixed tick, and draws the **whole race view the way the game does**: the 3D scene, both craft, the
track map and the HUD, through `MR_Observer::RenderNormalDisplay` into the 8-bit framebuffer. It prints a hash of the framebuffer bytes.
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

## Results (2026-10-17, one Linux x86-64 machine)

- Native (GCC, Release): 28 frames, identical on repeated runs.
- WebAssembly under Node (Emscripten): the same 28 hashes. So the software rasteriser, the HUD and the sprites draw the same pixels in
  the browser's build as in the desktop build.
- Not yet run: Windows x64 and Win32 (the tool needs the headless object plug-in there; add it to the Windows CI job next), and a
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

The final picture on a screen also depends on the palette and gamma conversion and on scaling by the graphics backend (SDL, or the browser
canvas); those are not in these hashes. A second layer (the converted RGBA frame) is the next step before changing the renderer.
