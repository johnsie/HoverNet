# The graphics engine: how a frame is made, and how to add to it

*Written 8 November 2026. It describes the code as it is; the parts that are plans are marked as plans.* The player-facing side (the presets, what
each part looks like, the measured costs and what was and was not run) is in [render-golden.md](render-golden.md#the-modern-look); the plan is
[roadmap-3.0.md](roadmap-3.0.md#graphics-improvements).

> **The camera is off limits.** Nothing in this engine may change how the race camera works (CLAUDE.md rule 7, [camera.md](camera.md)): no field-of-view or zoom change,
> no shake, no look-ahead, no smoothing. A pass changes what is drawn in the picture, never where it is drawn from. `HoverNetCameraRule` and the golden frames guard it.

## The pipeline

```
 simulation (fixed step, deterministic; never sees any of this)
      |
 MR_Observer  ----  the software rasteriser draws the race view into an 8-bit palette picture, with a 16-bit depth buffer (4 mm units)
      |             and the HUD on top. For the Modern look it also: draws craft shadows into the 8-bit scene, copies the depth buffer,
      |             records which pixels the HUD drew, and works out a list of lights in camera space and a few hints (craft on screen,
      |             speed, a collision, a trail).                                           [FrameHints, VideoServices/ModernGraphics.h]
      |
 SDL2GraphicsBackend::Present  ----  expands the 8-bit picture to RGB24 through the (graded) palette, then hands it to the Presenter
      |
 HoverNetGfx::Presenter  ----  an ordered list of Passes run over the RGB frame:
      |        filter -> edge-aa -> atmosphere -> lighting -> craft -> reflection -> sharpen -> bloom -> effects -> halos -> exposure -> hud-outline -> vignette
      |                                                                       ("off", used for the headset, runs none of them)
      |
 SDL texture -> screen (desktop) / canvas (browser)
```

Everything after the observer is presentation only. It cannot change a race or a replay, and it does not touch the rasteriser's own output (the golden frames).

## What the rasteriser gives (and how to make it give more)

The software rasteriser (`VideoServices/3DViewportRendering.cpp`) used to produce only two things per pixel: a palette index and a 16-bit depth. It now
has three optional extra abilities, all off by default and all leaving the 8-bit picture exactly as it was:

- **Surface tags.** `SetTagBuffer(buffer)` gives it a byte per pixel and `SetSurfaceTag(tag)` says what the surfaces drawn next are; every pixel a wall,
  floor, ceiling or object lands on (under the same depth test as its colour) is marked. The observer tags floor, ceiling, walls, objects and craft
  (`HoverNetGfx::SurfaceTag`); the passes see them in `hints.tags`.
- **True-colour output.** `SetColourBuffer(words, palette)` gives it a second picture: one 32-bit word `0x00RRGGBB` per pixel, found through a palette at
  the moment the pixel is written. Pixels it does not draw (the background) and the HUD are filled in by the observer, and the backend lays the result over
  the palette expansion of the 8-bit picture (`Presenter::ApplyTrueColour`), except where text was drawn after the observer finished.
- **Texture filtering.** With `SetTextureFiltering(TRUE)` (and a colour buffer) magnified textures are filtered bilinearly in true colour (columns of walls
  between a texel and its neighbour in both directions; floors, ceilings and objects by their texel fractions) instead of picking the nearest texel. This
  is what takes the blockiness out of walls seen close up. It costs the rasteriser roughly two to three times what it takes without it, measured on this
  machine under load (see below), so it is part of the medium and high looks and switches off when the adaptive quality has dropped to the lowest.

All three are written with the same pattern, so adding a fourth is the same small job: a buffer laid out like the video buffer, thread-local settings at
the start of each `Render*` call (`gsTagOn`, `gsRgbOn` ...), and one guarded line at each of the six drawing sites (`BltPlainColumn`, `BltColumn`,
`BltPlainLineNoZCheck`, `BltLineNoZCheck` and the two triangle loops in `RenderPatch`).

**Threads.** The rasteriser's working state (the parameters of the blit being done, the patch tables, the output settings) is now `thread_local`, so two
viewports can be drawn at the same time on two threads. `HoverNetViewportThreads` draws eight different views, once on one thread and then 160 times
on four threads at once, and requires every frame to match; before the change 63 of the 160 came out wrong. A single viewport is still drawn by one
thread at a time, and the observer and the level it draws are not shared between threads by this change.

What it still cannot do: its **textures are stored as 8-bit palette indices** (true colour comes from the palette at the moment of drawing, so a texture
cannot hold more than 256 colours or be replaced by a higher-resolution one without changing the resource loader); its maths is **4096-scaled fixed
point** (changing it would change every pixel and mean regenerating the golden frames, and is a rewrite of the inner loops rather than an edit); and it
draws walls, floors and patches only (no general triangles or transparency). Lifting those is the GPU renderer in the roadmap.

## Textures

Every texture in the game is a grid of palette indices with a few smaller copies. With true colour on, each one also gets a **true-colour copy** (built lazily, the first
time it is drawn, by `MR_ResBitmap`; `VideoServices/TextureEnhance.*`): the largest level is rebuilt from the palette with the dither and banding smoothed
away by an edge-preserving filter (a bilateral filter, so a gentle dither is averaged out and a real edge is not blurred) and sharpened a little; the
smaller levels are made from it by averaging in linear light (so a distant wall keeps its brightness and does not shimmer); and every level is doubled in
size in both directions by an edge-directed upscale (the diagonal staircases of an edge become staircases of half-texels; flat areas and gradients are copied).
The rasteriser samples that copy, bilinearly, instead of the 8-bit texel and the palette (`GetColourColumnBuffer`). The copy is rebuilt when the palette
changes (a new track, a change of brightness) and costs four times the memory of the 8-bit level while it exists; it exists only for the textures drawn.

The largest level also gets a trace of surface variation (`detail`, about +-3% brightness, stronger on flat fields and absent at edges, from the texel position only so it is repeatable and tiles; the mips average it out so it does not shimmer). It and the grade's lower saturation are the "less cartoony" changes.

What this is and is not: the colours are a better reconstruction of what the artist drew from what survived in 8 bits (the 8-bit data is still the only source), and the
doubled resolution makes edges finer, but it adds no detail that was not in the original. Replacing a texture with a real higher-resolution image would be a content job
and needs a loader for it. Checked by `HoverNetTextureEnhance` (the filter, the mips and the upscale on synthetic textures) and `HoverNetViewportColour` (the rasteriser:
the 8-bit picture is unchanged, and the picture has more colours than the palette has entries). Seen on a frame of the game (Tidal Causeway, ClassicH): floor tile
lines and wall edges are crisp where the palette version has stair-steps and banding. Cost: the rasteriser takes about as long with these textures as with the palette
ones (the true-colour path was already being paid for); not measured on an idle machine.

## What a pass can use

The built-in passes, in order: **filter** (de-banding with an 8x8 dither; edge smoothing at high quality), **edge-aa** (anti-aliases the edges of surfaces and objects from the rasteriser's tags and depth, leaving texture alone), **atmosphere** (sky-tinted ambient light by surface direction, mist on the floor, colours fading with distance, a horizon glow), **lighting** (haze, contact shading, lights with diffuse, glint, a spotlight cone for the headlight, light blocked by walls, and at high quality blending in linear space), **craft** (rim light, sky sheen and glints on craft; high quality), **reflection** (the craft mirrored in the floor), **sharpen** (high quality), **bloom** (high quality), **effects** (engine glow, trail, sparks, and the emitters: exhaust, dust, missile smoke, power-up sparkle), **halos** (glows on missiles, mines, power-ups and other craft), **exposure** (a soft highlight roll-off; it does not adapt to the view), **hud-outline** (a dark edge beside HUD text) and **vignette** (steady).

A pass receives a `PassContext` (`VideoServices/ModernGraphics.h`):

| Available | What it is |
| --- | --- |
| `rgb`, `width`, `height` | the frame, RGB24, improved in place |
| `options` | the player's settings (preset, quality 0-2, each part on or off, `reducedMotion`) |
| `hints.tags` | optional: what the rasteriser drew each pixel as (floor, ceiling, wall, object, craft; 0 is the sky) |
| `hints.depth` and its window | the renderer's own depth buffer: 4 mm units, `0xFFFF` where nothing was drawn (the sky) |
| `hints.projKx/Kz/CenterX/CenterY` | the camera: turns a pixel and its depth back into a point in camera space (mm; x forward, y left, z up) |
| `hints.lights` | up to 12 lights in camera space (position, radius, colour, strength): the viewing craft's engine and headlight, the impact flash, other craft, the ghost, missiles, mines, power-ups |
| `Normals(gridW, gridH)` | a surface normal per block (4x4 pixels; 8x8 from 960 pixels wide), recovered from the depth buffer once per frame and shared |
| `hudMask` | 1 where the HUD drew: a pass must leave those pixels as they are |
| `hints.craft*`, `speed`, `impact`, `trail*`, `cockpit` | where the viewing craft is on screen, how fast it goes, a sudden loss of speed, where it just was |

## Adding a pass

```cpp
presenter.AddPass(std::unique_ptr<HoverNetGfx::Pass>(new HoverNetGfx::FunctionPass(
    "my-effect",
    [](const HoverNetGfx::Options& o, const HoverNetGfx::FrameHints& h) { return o.enabled && h.valid && !o.reducedMotion; },   // wanted this frame?
    [](HoverNetGfx::PassContext& c) { /* read c.hints / c.Normals, change c.rgb, skip c.hudMask pixels */ })),
    "bloom");   // run before the built-in "bloom" pass; omit to run last
```

`Presenter::SetProfiling(true)` records each pass's time (`LastPassTimes()`); `HoverNetRenderGolden <track> <scene> modern` prints them
(`MODERNPASSES`). A new pass should: leave HUD pixels alone; respect `reducedMotion` if it moves or flashes; say what it costs; and come with a
test in `NetTarget/LinuxClient/ModernGraphicsTest.cpp` and a line in render-golden.md. The built-in passes are written the same way
(`Presenter::Presenter`), and a restructure of them into this form was shown to change no output (24 hashes, before and after).

## What the passes cost (measured 2026-11-08, Tidal Causeway, one thread, a machine under heavy load)

The machine was loaded by another program (load average about 11 on 8 cores), so the plain rasterised frame took 7.5 ms at 1280x720 and 3.0 ms at 640x480 where it takes about 2.7 and 0.6 ms on an idle machine. Read all of this as relative, and as an upper bound.

| Pass | 640x480 modern | 1280x720 modern | 1280x720 high |
| --- | --- | --- | --- |
| filter | 1.0 ms | 2.9 ms | 18 ms (edge smoothing) |
| edge-aa | 3.5 ms | 9.0 ms | 9.1 ms |
| atmosphere | 2.5 ms | 6.7 ms | 7.0 ms |
| lighting | 5.3 ms | 10.5 ms | 13.5 ms |
| craft | - | - | 15.8 ms |
| sharpen | - | - | 2.5 ms |
| bloom | - | - | 12.2 ms |
| effects, halos, reflection | 0.6 ms | 1.2 ms | 1.2 ms |
| exposure, hud-outline, vignette | 2.6 ms | 7.5 ms | 8.5 ms |
| **the passes, together** | **about 15 ms** | **about 37 ms** | **about 88 ms** |
| the rasteriser with true colour and filtering (without) | 7.9 (3.0) ms | 22.7 (9.2) ms | 24.4 (9.2) ms |

**That is too slow.** On this machine the medium look costs several times the plain frame at 640x480 and far more at 720p, and the high look is not usable above 640x480 on one CPU thread. This is what the adaptive quality is for: when the passes cost more than their budget (12 ms by default, `Options::budgetMs`) the quality drops one tier at a time (high, medium, light) after 45 slow frames, and rises again after 240 frames with room to spare, and at the lowest the filtered textures are switched off as well; the player can turn that off. On most machines the real outcome at 720p and above will be the light look. The ways to make it cheaper are known and are listed in the table below: vectorise the lighting and atmosphere passes the way the de-banding and the sharpening already are (both SSE2 with a portable fallback that gives the same bytes, checked by a test), evaluate more of them per block, and in the end move the passes to a GPU.

## Techniques this makes possible, and what each still needs

| Technique | Uses | Missing |
| --- | --- | --- |
| Floor reflections of the craft and the sky (screen-space) | depth, tags (the floor is known exactly), rgb | a "wet floor" setting per track; cost |
| Volumetric fog and light shafts | depth, lights, camera | cost; nothing else |
| Better ambient occlusion (more samples, a blur-free filter) | depth, normals | cost |
| Lens flares and glows on lights | lights, camera, depth (to hide them behind walls) | none |
| Colour grading by a look-up table | rgb | an authored table per track |
| Temporal anti-aliasing, motion blur by velocity | a previous frame's depth and camera | a history buffer and the previous camera in the hints (not there yet) |
| Per-surface materials (shiny floor, matte wall) | the surface tags | finer ids (which track surface type) are one more channel written the same way, and need the observer to set them |
| Shadows from walls (not only craft) | depth, lights | shadow maps need a second render of the scene from each light, which the software rasteriser can do but not cheaply |

Things the owner has said they do not want: a blur of the scenery (the speed blur was removed). Techniques that soften the picture should be
optional and off unless asked for.

## The way to a GPU (a plan, not started)

The hints are the interface. A GPU backend would draw the same observer's output through shaders and would need:

| Now (CPU) | On the GPU |
| --- | --- |
| the 8-bit picture and the palette | the picture as an RGBA texture (or the geometry itself, once the rasteriser is replaced) |
| the 16-bit depth buffer | a depth texture |
| `FrameHints` lights and camera | uniforms and a light buffer |
| each `Pass` | a fragment shader (the same maths; the per-block light grid becomes per-pixel or clustered lighting) |
| the HUD mask | the HUD drawn into a separate layer composited after the passes |

It is the Stage 3 renderer replacement in the roadmap. The 28 golden frames (the rasteriser's regression test) and the scene checks are its safety net, and
the Presenter's pass list means the techniques written now carry over one at a time. OpenGL ES 2 / WebGL 2 is the common ground between the
desktop and the browser; SDL2's renderer does not expose shaders, so the context would be created directly.

## Limits worth knowing

- The light is worked out per block and the haze and contact shading assume the depth buffer is right: a light can spill a few pixels over a depth edge.
- Light is blocked by walls only approximately: three samples along the ray are tested against the depth buffer, per block, so a thin or small blocker can be missed, and the craft's own body is deliberately ignored.
- The floating-point parts (sparks, glints) are only promised to repeat on the same machine; they are checked by property, not by committed hashes.
- The browser runs every pass without SIMD, and is several times slower than the desktop.
