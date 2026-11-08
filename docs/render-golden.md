# Render golden frames (the rasteriser's regression reference)

Stage 3 of the [3.0 roadmap](roadmap-3.0.md) replaces the 1990s software renderer behind the simulation boundary and promises a **classic
preset that matches the 2.0 output, verified by automated frame captures**. This is that verification, built first so that any change to
the renderer, now or later, shows up as a changed hash instead of as a surprise on screen.

**Update, 8 November 2026: the Classic look has been removed** at the owner's request, because keeping a look that matches 2.0 constrained what
the renderer could be improved to do. These frames are now the regression test of the **basic rasteriser's output** (the 8-bit picture before the
look is applied), on native, WebAssembly and Windows. They stay useful for that: a hash that changes is a rasteriser that changed. When a change
to the rasteriser is intended, regenerate them in one reviewed change (`scripts/test-render-golden.sh <command> --update`). The promise below,
of a preset that matches the 2.0 output, is withdrawn.

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

## The modern look

*Added 8 November 2026 (roadmap: [Graphics improvements](roadmap-3.0.md#graphics-improvements)).* **Settings > Graphics** chooses between **Modern**
(the default on a desktop), **Modern (high quality, adds glow)** and **Modern (light, for slow devices)**, which is the default on a phone, a tablet
and in the browser. There is no longer a Classic look; a saved choice of "classic" from before becomes the default for the device. The look is
presentation only: it never touches the simulation.

| Preset | Craft shadows | Tone curve, colour, vignette | Trail, engine glow, sparks, speed lines | Light from the craft and headlight | Depth haze | De-banding + dither | Wider view at speed | Edge smoothing | Bloom |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Light | yes | yes | yes | yes | no | no | yes | no | no |
| Modern | yes | yes | yes | yes | yes | yes | yes | no | no |
| Modern high quality | yes | yes | yes | yes | yes | yes | yes | yes | yes |

There is no speed blur: it was tried and removed (it smeared the scenery and, at first, the HUD, and the owner did not like it).

Each part can also be switched off in the options screen. **Reduced motion** (Settings) switches off every moving effect: the speed
lines, the sparks, the impact flash, the wider view at speed and the engine flicker; the steady parts remain. The headset (VR) gets the plain picture (the look is switched off there, because every frame is drawn twice).

How it works: the observer (`MR_Observer`) draws the craft shadows into the 8-bit scene (a soft ellipse darkened through a palette table, before
the craft, with the depth buffer so a wall in front is left alone) and reports a few hints for the frame (where the craft is on screen, its speed,
a sudden loss of speed, where it was a moment ago). The graphics backend expands the frame through a palette that already has the tone curve and
extra colour applied, and `HoverNetGfx::Presenter` (`NetTarget/VideoServices/ModernGraphics.*`, no SDL, shared by the desktop, browser and tests)
adds the rest. The view widens by up to 8% with speed (an observer setting, not a post effect).

**Lighting** is worked out in true colour from the renderer's own depth buffer, which the observer copies each frame together with the camera's projection
and a list of lights in camera space (an engine light under the viewing craft that brightens with speed, a warm headlight ahead of it, an orange
flash for a moment after an impact, a light under every other craft in view and under the ghost, a hot light that travels with each missile, a red pulsing light on each mine and a green one on each power-up; the pulse stops with Reduced motion). For each pixel the distance to each light is
worked out in 3D, so the light pools on the floor and climbs the walls beside it; it is evaluated per 4x4 block and added together before it is
applied. The lights follow the surface: a surface turned towards a light gets all of it, one turned away a quarter, and a surface facing the eye gets a glint (normals are recovered from the depth buffer; see [graphics-engine.md](graphics-engine.md)). **Haze** mixes far surfaces towards the colour of the sky (sampled from the picture), up to 35% at 120 m. **Contact shading** (medium quality and
up) darkens a surface by up to 27% where something is nearer by 15 cm to 2.5 m a few pixels away (a wall beside the floor, the ground around a craft,
a crease); a far-away edge is a silhouette and is left alone. Sky and HUD pixels are never lit or shaded. The observer also records which pixels the HUD drew (the scene as drawn before the HUD against the frame afterwards), so the de-banding and
the smoothing leave the HUD exactly as drawn.

### The second round (30 improvements, 8 November 2026)

Added in one go at the owner's request, each with its own check in `HoverNetModernGraphics` (the unit test now has 106). Which look has what:

| # | Improvement | Light | Modern | High |
| --- | --- | --- | --- | --- |
| 1 | wall normals are horizontal (the rasteriser knows a wall is vertical) | lights | yes | yes |
| 2, 3 | ambient light tinted by the sky and by surface direction (floors catch some sky, ceilings are darker) | - | yes | yes |
| 4 | shadows are a little cool in tone | yes | yes | yes |
| 5, 6, 24 | craft: rim light, glints from the engine, headlight and flash, sky sheen on the hull | - | - | yes |
| 7 | a soft roll-off of the brightest highlights, so lights do not clip | - | yes | yes |
| 8 | split-toned grade (cool shadows, warm highlights) | yes | yes | yes |
| 9 | automatic exposure: slow eye adaptation to a dark or bright view | - | yes | yes |
| 10, 26 | mist on the far floor; colours fade with distance | - | yes | yes |
| 11 | a glow along the horizon | - | yes | yes |
| 12 | glows on missiles, mines, power-ups and other craft (hidden when something is in front) | yes | yes | yes |
| 13, 14, 15, 27 | exhaust behind the craft, dust and spray, missile smoke, power-up sparkle (none with Reduced motion) | yes | yes | yes |
| 16 | a finer 8x8 dither | - | yes | yes |
| 17 | anti-aliased edges of surfaces and objects, from the rasteriser's tags and depth (texture untouched) | - | yes | yes |
| 18 | a dark edge beside HUD text and meters | yes | yes | yes |
| 19 | adaptive quality: lowers itself when too slow, comes back with room to spare | yes | yes | yes |
| 20 | sharper textures | - | - | yes |
| 21 | the vignette closes in with speed | yes | yes | yes |
| 22 | a softer, three-ring shadow | yes | yes | yes |
| 23 | the headlight is a cone from the nose, not a ball | yes | yes | yes |
| 25 | the craft reflected in the floor under it | - | yes | yes |
| 28 | walls block light (three samples along the ray) | - | yes | yes |
| 29 | light blended in linear space (no washed-out look) | - | - | yes |
| 30 | a brightness setting (through the palette) | yes | yes | yes |

Beyond the thirty, the rasteriser itself changed at the owner's request: it keeps its state per thread (two frames can be drawn at once), and it can
draw a true-colour picture with filtered textures (see [graphics-engine.md](graphics-engine.md)); **Smooth textures** is in Modern and high.
`HoverNetViewportColour` checks that picture (without filtering it is exactly the 8-bit picture through the palette, the 8-bit picture is unchanged, and a
magnified texture is smoother: in the test the colour steps between neighbouring pixels fall from 30,355 to 16,526), and `HoverNetViewportThreads` the threads.

### Checks

- `HoverNetModernGraphics` (ctest): synthetic frames with a known answer: the "off" setting (used by the headset) leaves a frame byte-for-byte alone; the same inputs give the same
  picture for every preset; de-banding keeps the steps within the dither and the picture within a few levels; edge smoothing adds in-between
  greys on a stair-stepped edge and not in plain Modern; bloom lights the dark beside a bright patch and does nothing without one; the grade keeps
  black and white and adds colour; the vignette darkens corners and not the middle; the glow and trail only add light and are absent in the
  cockpit view; Reduced motion makes no sparks and no speed lines; the HUD is left exactly as drawn by the de-banding and the smoothing; haze mixes far surfaces towards the sky and leaves near surfaces and the sky alone; a light brightens the surfaces near it by their 3D distance, more when they face it than when they are turned away, with a glint where the eye, the light and the surface line up, and not the sky, the HUD or surfaces beyond its radius, and nothing happens without a depth buffer; contact shading darkens a wall beside a box just in front of it and not a flat wall or a far silhouette, and the light preset has none; odd and tiny sizes and hints far off the screen stay inside the frame (guard bytes).
- `HoverNetModernGraphicsScalarMatches` (ctest): the same test built without SIMD gives the same digests as the SSE2 build.
- `HoverNetModernGraphicsScenes` (ctest, `scripts/test-modern-graphics.sh`): three bundled tracks in the chase and cockpit views, by property:
  the rasteriser-only frame it prints first equals the committed golden, the look changes the picture and repeats, the craft and a trail are reported in the chase view and
  not the cockpit view, shadows are drawn, and the view widens with speed (and does not when the craft is slow).
- Not checked by committed hashes: the sparks and glow use floating point, which is only promised to repeat on the same machine. Looking at the
  picture is still a manual step (`HOVERNET_RENDER_DUMP=<dir> HoverNetRenderGolden <track> <scene> modern` writes the plain and the modern frame
  as PPM files).

### Results (2026-11-08)

- Native (GCC 15, Release, Linux x86-64): all the checks above pass, and the 28 golden frames are unchanged. The unit test also ran
  clean under AddressSanitizer and UndefinedBehaviorSanitizer, in both the SSE2 and the scalar build (not the whole client under the sanitizers).
  The full ctest suite result is given under "Full suite" below.
- WebAssembly under Node (Emscripten, the project's `.tools/emsdk`): the 28 golden frames still match the committed hashes, and the scene checks
  pass; the shadow and widening pixel counts are identical to native. The browser itself was not run, and nobody has looked at the Modern picture
  in a browser or on a phone.
- Not run: Windows (the new code is portable and builds in the same CMake targets, but it was not built or run there), Win32, the legacy Game2.exe
  (it does not have the look), a headset, the sanitizers on the whole client.
- Looked at (one native frame per scene, ClassicH and Tidal Causeway, chase view, and one frame of the real client run headless with the Modern preset): the colours are richer, the corners darker, the craft has a soft
  shadow ring on the floor and a blue glow under it; the trail is mostly behind the craft (a chase view looks along it) and so is hardly visible
  except in turns. Not judged: whether the settings suit everyone's taste, which is why each part can be switched off.

### Cost

`HoverNetRenderGolden <track> <scene> modern` also times the picture passes (`MODERNTIME`: the mean of 20 runs on one frame, one thread).
**Measured on 8 November 2026 on a machine under heavy load** (load average about 12 on 8 cores, from another program), where the plain rasterised
frame itself took 1.5 ms at 640x480 and 4.9 ms at 1280x720 (the table above measured 0.56 and 2.7 ms on an idle machine), so read the figures as
an upper bound and as relative to that classic frame, not as absolute times. ClassicH, drive scene, with the lighting on:

| Preset | 640x480 (native) | 1280x720 (native) | 640x480 (WebAssembly under Node, no SIMD) |
| --- | --- | --- | --- |
| Plain rasterised frame, for comparison | 1.5 ms | 4.9 ms | 2.2 ms |
| Light (the passes only) | 3.3 ms | 9.8 ms | not measured |
| Modern (the passes only) | 4.6 ms | 16.7 ms | about 15 ms |
| Modern high quality (the passes only) | 15 ms | 46 ms | not measured |

So on this machine Modern costs about three times a plain frame; that is fine at 640x480 to 1024x768 on a desktop, and **marginal at
1280x720 and above**. **High quality is not fast enough for 60 frames a second at 1280x720 and above** (its edge smoothing and bloom are plain
scalar code, the obvious next thing to speed up). **The lighting is about two thirds of Light's cost and about half of Modern's** (the depth haze
and the per-pixel light application are scalar code too; the lights are evaluated per 4x4 block to keep this down). **The browser build runs the
passes without SIMD and is several times slower than the desktop**, so Modern is not suitable for phones yet; Light is the one to try there, and
was not measured in the browser. The de-banding pass alone cost about nine times as much before it used SSE2 (5.6 ms against 0.62 ms of CPU time
at 640x480 on the same loaded machine). A light's colour can spill up to three pixels past a depth edge (the 4x4 blocks), which a soft light hides.

## What this does not cover

The last step to a screen, scaling and presenting by the graphics backend (SDL, or the browser canvas), is not in these hashes, nor is the
menu and settings UI (drawn by Dear ImGui in window coordinates; it has its own screenshot tests). A layer for those would need a window.
