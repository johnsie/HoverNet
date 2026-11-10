// ModernGraphics.h -- the optional "Modern" look for the race view (HoverNet 3.0, stage 3; docs/roadmap-3.0.md "Graphics improvements").
//
// The software rasteriser draws an 8-bit picture. This module turns that picture, after the palette has been applied, into a nicer one:
// de-banded colour ramps, edge smoothing, bloom, a tone curve and vignette, and effects drawn over the scene (engine glow, a
// trail behind the craft, impact sparks). It is pure presentation: it reads the finished frame and a few hints from the
// observer, never touches the simulation, and with Preset::eOff it is skipped entirely (the headset, and the rasteriser's regression tool). It has no SDL and no platform code, so the same source runs natively, in the browser and in the headless tests.
#ifndef HOVERNET_MODERN_GRAPHICS_H
#define HOVERNET_MODERN_GRAPHICS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace HoverNetGfx
{
enum { kMaxTrailPoints = 24, kMaxLights = 12 };

// What the rasteriser marks each pixel with when surface tags are on (MR_3DViewPort::SetTagBuffer). 0 is nothing drawn: the sky.
enum SurfaceTag : std::uint8_t { kTagNone = 0, kTagFloor = 1, kTagCeiling = 2, kTagWall = 3, kTagObject = 4, kTagCraft = 5 };

// A point of coloured light in the scene, in camera space (mm: x forward, y left, z up).
enum LightKind : std::uint8_t { kLightGeneric = 0, kLightEngine, kLightHeadlight, kLightFlash, kLightCraft, kLightMissile, kLightMine, kLightPowerUp, kLightGhost, kLightExplosion };

struct Light
{
    std::uint8_t kind = kLightGeneric;
    float dirx = 0, diry = 0, dirz = 0;   // for a spotlight: the direction it points in (camera space, unit length)
    float coneCos = -1.0f;                // -1: lights all round; otherwise the cosine of the cone's half angle (the edge is soft)
    float x = 0, y = 0, z = 0;
    float radius = 1000;          // mm: beyond this it has no effect
    std::uint8_t r = 255, g = 255, b = 255;
    float strength = 0.5f;        // 0..1
};

struct Options
{
    bool enabled = false;         // false: the plain picture, untouched; nothing below applies
    bool deband = true;           // smooth the steps of the palette's colour ramps and dither them
    bool smoothing = true;        // edge smoothing (the sub-pixel part of FXAA)
    bool bloom = true;            // glow around bright areas
    bool grade = true;            // tone curve and a little more colour (through the palette, see Presenter::GradePalette), and a vignette
    bool effects = true;          // engine glow, trail, sparks, exhaust, dust, smoke
    bool lighting = true;         // light pools from the craft and a headlight on the scene, and (medium quality and up) depth haze
    bool shadows = true;          // a soft shadow under craft (drawn by the observer, in the scene)
    bool atmosphere = true;       // sky-tinted ambient light by surface direction, floor mist, colour fading with distance, a glow on the horizon (medium quality up)
    bool occlusion = true;        // walls and objects block a light's path (medium quality up)
    bool craftShading = true;     // a rim light, glints and a sky sheen on craft (high quality)
    bool reflections = true;      // the craft mirrored faintly in the floor under it (medium quality up)
    bool halos = true;            // glows around missiles, mines, power-ups and other craft
    bool edgeAA = true;           // anti-aliasing of the edges of surfaces and objects, using what the rasteriser drew (medium quality up)
    bool hudOutline = true;       // a dark edge on the scene beside HUD text and meters, so they read against any background
    bool sharpen = true;          // restores the crispness of textures (high quality only)
    bool exposure = true;         // a soft roll-off of the brightest highlights (medium quality up); it does not adapt to the view
    bool linearLight = true;      // lights add up as light does (in linear space) rather than as numbers: no washed-out look (high quality only)
    bool trueColour = true;       // the rasteriser draws in true colour with filtered textures (see MR_3DViewPort::SetColourBuffer) instead of 8-bit nearest-texel
    bool gpuFinish = false;       // set by the caller when a finisher is installed: the picture passes (de-banding, smoothing, sharpening, bloom, highlight roll-off, HUD edge, vignette) are done there, not on the CPU
    bool adaptive = true;         // lowers the quality when the passes cost more than budgetMs per frame, and raises it again when there is room
    float budgetMs = 12.0f;
    // Automatic: the presenter chooses how much of the look to run (a "level" from 0 to Presenter::kMaxLevel, see Presenter::OptionsForLevel) from how fast the machine draws, starting at
    // startLevel (or at the top when the graphics card does the picture passes) and stepping down when frames are slow and up again when there is room. The other options are then
    // what the player allows: an option that is off is never switched on. Needs adaptive.
    bool automatic = false;
    int startLevel = 3;
    float brightness = 1.0f;      // 0.7 .. 1.4: the player's brightness setting, applied through the palette
    bool reducedMotion = false;   // no sparks, exhaust, dust or smoke, no impact flash, steady glow: the player's "Reduced motion" setting
    int quality = 2;              // 0 low (grade and effects only), 1 medium (adds de-banding and depth haze), 2 high (adds edge smoothing and bloom)

    bool operator==(const Options& o) const
    {
        return enabled == o.enabled && deband == o.deband && smoothing == o.smoothing && bloom == o.bloom && grade == o.grade &&
               effects == o.effects && lighting == o.lighting && shadows == o.shadows && reducedMotion == o.reducedMotion && quality == o.quality &&
               atmosphere == o.atmosphere && occlusion == o.occlusion && craftShading == o.craftShading && reflections == o.reflections && halos == o.halos &&
               edgeAA == o.edgeAA && hudOutline == o.hudOutline && sharpen == o.sharpen && exposure == o.exposure && linearLight == o.linearLight &&
               trueColour == o.trueColour && adaptive == o.adaptive && budgetMs == o.budgetMs && brightness == o.brightness && automatic == o.automatic && startLevel == o.startLevel;
    }
};

// A soft round glow in framebuffer pixels (strength 0..1, colour 0..255), added to the picture: the engine glow and every particle. The processor paints them itself
// (AddGlow); when the scene is drawn on the graphics card they are handed to the finisher instead, which draws them after the scene so they are not replaced by it.
struct GlowSprite { float x, y, rx, ry; std::uint8_t r, g, b; float strength; };

// What the observer knows about the frame it just drew, in framebuffer pixels.
struct FrameHints
{
    bool gpuScene = false;        // the scene was handed to the graphics card as triangles (MR_SceneSink): there is no depth or tag buffer on the processor; the card has them
    bool valid = false;           // false when no race view was drawn (menus, loading): only the picture passes are applied
    bool cockpit = false;         // the camera is in the cockpit, so the craft itself is not on screen
    bool craftVisible = false;
    int craftX = 0, craftY = 0;   // the craft's centre
    int craftRadius = 0;          // roughly how big the craft is on screen
    int craftGroundY = 0;         // the screen row of the floor directly under the craft (for its reflection)
    float speed = 0.0f;           // the craft's speed as a fraction of its top speed (can exceed 1)
    float impact = 0.0f;          // 0..1: a sudden loss of speed this frame (a collision)
    // 1 for every pixel the HUD drew (meters, map, text), 0 for the 3D scene; width*height bytes, valid for the Process call it is passed to. The
    // picture passes that smooth (de-banding, smoothing) leave HUD pixels exactly as drawn. Null: treat everything as scene.
    const std::uint8_t* hudMask = nullptr;
    int hudMaskWidth = 0, hudMaskHeight = 0;
    // The depth of the 3D scene (4 mm units, 0xFFFF where nothing was drawn: sky), for the viewport at (depthX0, depthY0) of depthWidth x
    // depthHeight pixels; valid for the Process call it is passed to. With it, light and haze are worked out per pixel in camera space:
    // sideways = (pixel x - projCenterX) * depth * projKx (to the left, negative x), up = (projCenterY - pixel y) * depth * projKz.
    const std::uint16_t* depth = nullptr;
    int depthX0 = 0, depthY0 = 0, depthWidth = 0, depthHeight = 0;
    // The rasteriser's own true-colour picture (a word 0x00RRGGBB per pixel, width*height), and the 8-bit picture as it was when the observer finished: wherever the
    // frame being presented still has that 8-bit value, the true-colour pixel is the better one (Presenter::ApplyTrueColour). Null when off.
    const std::uint32_t* colour = nullptr;
    const std::uint8_t* finalIndexed = nullptr;
    int colourWidth = 0, colourHeight = 0;
    const std::uint8_t* tags = nullptr;   // optional: a SurfaceTag per pixel, for the same window as the depth (null when the rasteriser was not asked for them)
    float projKx = 0, projKz = 0;
    int projCenterX = 0, projCenterY = 0;
    const GlowSprite* sprites = nullptr;   // set by the Presenter for the finisher only, valid for that call
    int spriteCount = 0;
    int lightCount = 0;
    Light lights[kMaxLights];
    int trailCount = 0;           // where the craft was a moment ago, oldest first
    int trailX[kMaxTrailPoints] = {};
    int trailY[kMaxTrailPoints] = {};
};

// Settings: the four presets the options screen offers, and the options each one stands for.
// eOff is no processing at all: the plain palette picture. It is not a look players choose (the VR headset uses it, and the rasteriser's own
// regression tool); the looks are the other three.
enum class Preset { eOff, eModern, eModernLite, eModernHigh, eAuto };   // eAuto: everything the machine can run, chosen and followed automatically (Options::automatic)
Options OptionsForPreset(Preset preset);
const char* PresetLabel(Preset preset);

// For each palette index, the index of the palette colour nearest to that colour at percent% brightness (the observer's shadow table).
// palette is 256 RGB triples.
void BuildShadowLut(const std::uint8_t* palette768, int percent, std::uint8_t lut[256]);   // shadows are a little cool in tone (bluer), as real shadows are

class Presenter;

// What a pass is given. The frame is RGB24 (rgb, width*height*3 bytes, no padding) and is improved in place. Everything else is read-only scene
// information the observer supplied (see FrameHints): the depth buffer and camera, the lights, the speed, where the HUD is.
struct PassContext
{
    std::uint8_t* rgb;
    int width, height;
    const Options& options;
    const FrameHints& hints;
    const std::uint8_t* hudMask;   // null, or width*height bytes: 1 where the HUD drew (a pass should leave those pixels as they are)
    Presenter& presenter;

    // Surface normals recovered from the depth buffer, one per block of the frame (4x4 pixels, 8x8 for frames 960 pixels wide or more: see
    // Presenter::BlockShift) (gridW x gridH blocks, three signed bytes each: camera-space
    // x forward, y left, z up, scaled to -127..127, pointing at the camera; a block with no surface is 0,0,0). Worked out the first time any
    // pass asks and shared by the rest of the frame. Null without a depth buffer.
    const std::int8_t* Normals(int& gridW, int& gridH);
};

// One step of the picture pipeline. The built-in passes (filter, lighting, bloom, effects, vignette) are written the same way, so a new technique
// (screen-space reflections, volumetric fog, a lens effect ...) is a class that implements these three functions and is added with
// Presenter::AddPass. docs/graphics-engine.md describes the pipeline, what a pass can use, and how it maps onto a GPU.
class Pass
{
public:
    virtual ~Pass() {}
    virtual const char* Name() const = 0;
    virtual bool Wanted(const Options& options, const FrameHints& hints) const = 0;   // false: skipped this frame
    virtual void Run(PassContext& context) = 0;
};

// A pass made from two functions, for small passes and for tests.
class FunctionPass : public Pass
{
public:
    FunctionPass(const char* name, std::function<bool(const Options&, const FrameHints&)> wanted, std::function<void(PassContext&)> run)
        : mName(name), mWanted(std::move(wanted)), mRun(std::move(run)) {}
    const char* Name() const override { return mName; }
    bool Wanted(const Options& options, const FrameHints& hints) const override { return mWanted(options, hints); }
    void Run(PassContext& context) override { mRun(context); }
private:
    const char* mName;
    std::function<bool(const Options&, const FrameHints&)> mWanted;
    std::function<void(PassContext&)> mRun;
};

struct PassTime { const char* name; double milliseconds; };

// The picture pipeline: an ordered list of passes run over each frame. Keep one Presenter alive for the life of the window: it owns the working
// buffers and the spark particles.
class Presenter
{
public:
    Presenter();   // with the built-in passes, in order: "filter" (de-banding, edge smoothing), "lighting" (haze, contact shading, lights),
                   // "bloom", "effects" (engine glow, trail, sparks, exhaust, dust, smoke), "vignette"

    // Improves a finished RGB24 frame in place (width*height*3 bytes, no padding). With options.enabled false it does nothing.
    void Process(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints);

    // A finisher takes over the picture passes (see Options::gpuFinish): after the scene passes it is called with the frame, the options, the hints and the HUD mask and
    // returns false if it could not (then the CPU passes run for that frame and the finisher is not used again until SetFinisher is called).
    typedef std::function<bool(std::uint8_t*, int, int, const Options&, const FrameHints&, const std::uint8_t*)> Finisher;
    void SetFinisher(Finisher finisher) { mFinisher = std::move(finisher); mFinisherBroken = false; }
    bool FinisherActive() const { return static_cast<bool>(mFinisher) && !mFinisherBroken; }

    // Adds a pass: before the built-in pass called `before`, or at the end when that is null or not found.
    void AddPass(std::unique_ptr<Pass> pass, const char* before = nullptr);

    // When on, the time each pass takes is recorded for every frame (LastPassTimes); off by default.
    void SetProfiling(bool on) { mProfiling = on; }
    const std::vector<PassTime>& LastPassTimes() const { return mPassTimes; }

    // The "grade" option's tone curve and extra colour, applied to a palette (256 RGB triples) instead of to every pixel: the caller expands the
    // frame through the graded palette when options.grade is set, and Process only adds the vignette.
    static void GradePalette(const std::uint8_t* in768, std::uint8_t* out768, float brightness = 1.0f);

    // Puts the rasteriser's true-colour picture into an RGB frame that was made by expanding the 8-bit picture through the palette: every pixel the
    // 8-bit picture has not changed since the observer finished (so not text drawn afterwards) takes the true colour. Returns how many pixels
    // now differ from the palette expansion. Does nothing without hints.colour.
    static std::size_t ApplyTrueColour(std::uint8_t* rgb, const std::uint8_t* indexed, int width, int height, const FrameHints& hints);

    // The quality actually used: what the options ask for, lowered while the passes are too slow for options.budgetMs (see Options::adaptive).
    int EffectiveQuality() const { return mQualityCap; }

    // Automatic quality (Options::automatic). The ladder, each level keeping what the one below has: 0 colour grade and engine/spark effects; 1 craft shadows; 2 light from the craft and glows
    // round things; 3 de-banding, depth haze and walls that block light; 4 filtered true-colour textures, reflections and edge smoothing; 5 bloom, sharpening and the rest.
    static constexpr int kMaxLevel = 5;
    static Options OptionsForLevel(const Options& allowed, int level);   // `allowed` with everything above `level` switched off (nothing is ever switched on)
    int Level() const { return mLevel; }
    double PassCostMs() const { return mCostMs; }    // the passes' average cost per frame, and the whole frame's (what the governor weighs)
    double FrameMs() const { return mFrameMs; }
    // What was actually used for the last frame: the player's options as limited by the level. The observer's shadows and lights and the rasteriser's true-colour textures follow this.
    const Options& EffectiveOptions() const { return mEffective; }

    // Tells the presenter how long the whole frame took (the time since the previous frame was shown, in ms). A craft that shudders is a craft drawn at an
    // uneven or slow pace, and the passes are only part of the cost of a frame (the rasteriser and the machine's own load are the rest), so with adaptive
    // quality on the quality is also lowered when whole frames take more than about 21 ms (under 47 a second), and raised again only after a long stretch
    // that gets longer every time it had to be lowered. Gaps over a quarter of a second (a menu, a pause) are ignored.
    void NoteFrameInterval(double milliseconds);

    // The size of the blocks that light, contact shading and normals are worked out in, as a shift: 2 (4x4 pixels) up to 959 pixels wide, 3 (8x8) above.
    static int BlockShift(int width);

    // Forgets the particles (a new race, a menu).
    void Reset();

    std::size_t ParticleCount() const { return mParticles.size(); }

private:
    struct Particle { float x, y, vx, vy, life, maxLife, size; std::uint8_t r, g, b; float strength; float gravity; };

    void Filter(std::uint8_t* rgb, int width, int height, bool deband, bool smooth, const std::uint8_t* hudMask);
    void Bloom(std::uint8_t* rgb, int width, int height);
    void Lighting(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints, const std::uint8_t* hudMask);
    const std::int8_t* EnsureNormals(const FrameHints& hints, int width, int height, int& gridW, int& gridH);
    friend struct PassContext;
    void Effects(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints);
    void Atmosphere(PassContext& c);
    void CraftShading(PassContext& c);
    void Reflection(PassContext& c);
    void Halos(PassContext& c);
    void EdgeAA(PassContext& c);
    void Sharpen(PassContext& c);
    void Exposure(PassContext& c);
    void HudOutline(PassContext& c);
    void Emitters(int width, int height, const Options& options, const FrameHints& hints);
    void Vignette(std::uint8_t* rgb, int width, int height, float speed);
    std::uint32_t Random();

    std::vector<std::uint8_t> mScratch;
    std::vector<std::int8_t> mNormalGrid;          // per block: surface normal in camera space (x, y, z as -127..127) from the depth buffer
    std::vector<std::int16_t> mLightGrid;          // per block: light multiplier and added colour, three channels each
    std::vector<std::uint16_t> mBloomA, mBloomB;   // three 16-bit channels per cell, at a quarter of the size
    std::vector<std::uint8_t> mVignette;           // 0..255 per pixel, for the size it was built for
    std::vector<int> mVignetteFrom, mVignetteTo;   // per row, the span that can be darkened
    std::vector<std::uint8_t> mLuma;
    std::vector<std::uint8_t> mDitherUp, mDitherDown;   // four rows of dither bytes (the pattern repeats every four rows)
    std::vector<std::uint8_t> mEdge;               // 1 where the filter pass found a strong edge
    int mVignetteWidth = 0, mVignetteHeight = 0;
    std::vector<Particle> mParticles;
    std::vector<GlowSprite> mSprites;     // glows to hand to the finisher this frame (empty when the processor paints them)
    bool mDeferGlows = false;
    void Glow(std::uint8_t* rgb, int width, int height, float cx, float cy, float rx, float ry, int r, int g, int b, float strength);
    std::uint32_t mRandom = 0x2545F491u;
    std::uint32_t mFrame = 0;
    std::vector<std::unique_ptr<Pass>> mPasses;
    Finisher mFinisher;
    bool mFinisherBroken = false;
    std::vector<PassTime> mPassTimes;
    bool mProfiling = false;
    bool mNormalsReady = false;
    // Things that carry over from frame to frame: the sky colour, the eye's adaptation, and the adaptive quality.
    int mSkyR = 0, mSkyG = 0, mSkyB = 0;
    bool mSkyKnown = false;
    float mExposure = 1.0f;
    int mQualityCap = 2, mLastRequestedQuality = 2, mSlowFrames = 0, mFastFrames = 0, mRaiseAfter = 240;
    int mLevel = 3;
    bool mLevelStarted = false;
    Options mEffective;
    double mCostMs = 0.0, mFrameMs = 0.0;
};
}

#endif
