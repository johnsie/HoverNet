// ModernGraphics.cpp -- see ModernGraphics.h.
#include "ModernGraphics.h"
#include "ColourGrade.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace HoverNetGfx
{
namespace
{
inline int Clamp255(int v) { return std::min(std::max(v, 0), 255); }
inline int Luma(int r, int g, int b) { return (r * 77 + g * 150 + b * 29) >> 8; }
inline int Abs(int v) { return v < 0 ? -v : v; }
inline float Smooth01(float t) { t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); return t * t * (3.0f - 2.0f * t); }

// Adds colour to a pixel (light, so it can only brighten).
inline void AddPixel(std::uint8_t* p, int r, int g, int b)
{
    p[0] = static_cast<std::uint8_t>(Clamp255(p[0] + r));
    p[1] = static_cast<std::uint8_t>(Clamp255(p[1] + g));
    p[2] = static_cast<std::uint8_t>(Clamp255(p[2] + b));
}

// A soft round light: full strength at the centre, nothing at the radius.
void AddGlow(std::uint8_t* rgb, int width, int height, float cx, float cy, float rx, float ry, int r, int g, int b, float strength)
{
    if (rx < 1.0f || ry < 1.0f || strength <= 0.0f) return;
    const int x0 = std::max(0, static_cast<int>(cx - rx)), x1 = std::min(width - 1, static_cast<int>(cx + rx) + 1);
    const int y0 = std::max(0, static_cast<int>(cy - ry)), y1 = std::min(height - 1, static_cast<int>(cy + ry) + 1);
    for (int y = y0; y <= y1; ++y) {
        const float dy = (y - cy) / ry;
        for (int x = x0; x <= x1; ++x) {
            const float dx = (x - cx) / rx;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= 1.0f) continue;
            const float f = (1.0f - d2) * (1.0f - d2) * strength;
            AddPixel(rgb + (static_cast<std::size_t>(y) * width + x) * 3, static_cast<int>(r * f), static_cast<int>(g * f), static_cast<int>(b * f));
        }
    }
}
}

Options Presenter::OptionsForLevel(const Options& allowed, int level)
{
    Options o = allowed;
    level = std::max(0, std::min(kMaxLevel, level));
    o.quality = std::min(allowed.quality, level >= 5 ? 2 : level >= 3 ? 1 : 0);
    if (level < 1) o.shadows = false;
    if (level < 2) { o.lighting = false; o.halos = false; }
    if (level < 3) { o.atmosphere = false; o.occlusion = false; o.deband = false; o.exposure = false; }
    if (level < 4) { o.trueColour = false; o.reflections = false; o.edgeAA = false; }
    if (level < 5) { o.smoothing = false; o.bloom = false; o.sharpen = false; o.craftShading = false; o.linearLight = false; }
    return o;
}

Options OptionsForPreset(Preset preset)
{
    Options o;
    switch (preset) {
    case Preset::eOff:
        o.enabled = false;
        break;
    case Preset::eModern:       // shadows, grade, effects, lighting and haze, de-banding
        o.enabled = true;
        o.quality = 1;
        break;
    case Preset::eModernHigh:   // everything, including edge smoothing and bloom: for a fast desktop CPU (see docs/render-golden.md for the cost)
        o.enabled = true;
        o.quality = 2;
        break;
    case Preset::eAuto:         // everything allowed, the machine decides how much it runs (see Presenter::OptionsForLevel)
        o.enabled = true;
        o.quality = 2;
        o.automatic = true;
        break;
    case Preset::eModernLite:   // for phones and slow machines: shadows, grade and effects only (under a millisecond at 640x480)
        o.enabled = true;
        o.quality = 0;
        o.trueColour = false;   // filtered textures cost the rasteriser more
        break;
    }
    return o;
}

const char* PresetLabel(Preset preset)
{
    switch (preset) {
    case Preset::eOff: return "Off (plain picture)";
    case Preset::eModern: return "Modern";
    case Preset::eModernHigh: return "Modern (high quality, adds glow)";
    case Preset::eAuto: return "Automatic (the best this computer can run)";
    case Preset::eModernLite: return "Modern (light, for slow devices)";
    }
    return "";
}

void BuildShadowLut(const std::uint8_t* palette, int percent, std::uint8_t lut[256])
{
    for (int i = 0; i < 256; ++i) {
        // Real shadows are lit by the sky, so they are cooler than the surface: the target loses a little red and green.
        const int tr = palette[i * 3] * percent / 100 * 97 / 100, tg = palette[i * 3 + 1] * percent / 100 * 99 / 100, tb = palette[i * 3 + 2] * percent / 100;   // only a hint of cool: a strong tint turns a shadow into a blue puddle
        int best = i, bestDistance = 1 << 30;
        for (int j = 0; j < 256; ++j) {
            const int dr = palette[j * 3] - tr, dg = palette[j * 3 + 1] - tg, db = palette[j * 3 + 2] - tb;
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestDistance) { bestDistance = d; best = j; }
        }
        lut[i] = static_cast<std::uint8_t>(best);
    }
}

std::uint32_t Presenter::Random()
{
    mRandom ^= mRandom << 13;
    mRandom ^= mRandom >> 17;
    mRandom ^= mRandom << 5;
    return mRandom;
}

void Presenter::Reset()
{
    mParticles.clear();
}

std::size_t Presenter::ApplyTrueColour(std::uint8_t* rgb, const std::uint8_t* indexed, int width, int height, const FrameHints& hints)
{
    if (rgb == nullptr || indexed == nullptr || hints.colour == nullptr || hints.finalIndexed == nullptr || hints.colourWidth != width || hints.colourHeight != height) return 0;
    std::size_t changed = 0;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < pixels; ++i) {
        if (indexed[i] != hints.finalIndexed[i]) continue;   // drawn after the observer finished (chat, the replay bar): keep the palette colour
        const std::uint32_t c = hints.colour[i];
        std::uint8_t* o = rgb + i * 3;
        const std::uint8_t r = static_cast<std::uint8_t>(c >> 16), g = static_cast<std::uint8_t>(c >> 8), b = static_cast<std::uint8_t>(c);
        if (o[0] != r || o[1] != g || o[2] != b) { o[0] = r; o[1] = g; o[2] = b; ++changed; }
    }
    return changed;
}

int Presenter::BlockShift(int width)
{
    return width >= 960 ? 3 : 2;
}

Presenter::Presenter()
{
    auto add = [this](const char* name, std::function<bool(const Options&, const FrameHints&)> wanted, std::function<void(PassContext&)> run) {
        mPasses.push_back(std::unique_ptr<Pass>(new FunctionPass(name, std::move(wanted), std::move(run))));
    };
    add("filter",
        [](const Options& o, const FrameHints&) { return (o.quality >= 1 && o.deband) || (o.quality >= 2 && o.smoothing); },
        [this](PassContext& c) { Filter(c.rgb, c.width, c.height, c.options.quality >= 1 && c.options.deband, c.options.quality >= 2 && c.options.smoothing, c.hudMask); });
    add("edge-aa",   [](const Options& o, const FrameHints& h) { return h.valid && o.quality >= 1 && o.edgeAA; },        [this](PassContext& c) { EdgeAA(c); });
    add("atmosphere",[](const Options& o, const FrameHints& h) { return h.valid && o.quality >= 1 && o.atmosphere && o.lighting; }, [this](PassContext& c) { Atmosphere(c); });
    add("lighting",
        [](const Options& o, const FrameHints& h) { return h.valid && o.lighting; },
        [this](PassContext& c) { Lighting(c.rgb, c.width, c.height, c.options, c.hints, c.hudMask); });
    add("craft",     [](const Options& o, const FrameHints& h) { return h.valid && o.quality >= 2 && o.craftShading && !h.cockpit; }, [this](PassContext& c) { CraftShading(c); });
    add("reflection",[](const Options& o, const FrameHints& h) { return h.valid && o.quality >= 1 && o.reflections && !h.cockpit && h.craftVisible; }, [this](PassContext& c) { Reflection(c); });
    add("sharpen",   [](const Options& o, const FrameHints&) { return o.quality >= 2 && o.sharpen; },                    [this](PassContext& c) { Sharpen(c); });
    add("bloom",
        [](const Options& o, const FrameHints&) { return o.quality >= 2 && o.bloom; },
        [this](PassContext& c) { Bloom(c.rgb, c.width, c.height); });
    add("effects",
        [](const Options& o, const FrameHints& h) { return h.valid && o.effects; },
        [this](PassContext& c) { Effects(c.rgb, c.width, c.height, c.options, c.hints); });
    add("halos",     [](const Options& o, const FrameHints& h) { return h.valid && o.halos && o.lighting; },              [this](PassContext& c) { Halos(c); });
    add("exposure",  [](const Options& o, const FrameHints&) { return o.quality >= 1 && o.exposure; },                    [this](PassContext& c) { Exposure(c); });
    add("hud-outline",[](const Options& o, const FrameHints& h) { return h.valid && o.hudOutline; },                      [this](PassContext& c) { HudOutline(c); });
    add("vignette",
        [](const Options& o, const FrameHints&) { return o.grade; },
        [this](PassContext& c) { Vignette(c.rgb, c.width, c.height, 0.0f); });   // steady: it does not react to speed
}

namespace
{
std::chrono::steady_clock::time_point gFrameStart;
bool gFrameStartKnown = false;
}
namespace { bool gVsyncActive = false; }
void SetVsyncActive(bool active) { gVsyncActive = active; }
bool VsyncActive() { return gVsyncActive; }
void NoteFrameStart() { gFrameStart = std::chrono::steady_clock::now(); gFrameStartKnown = true; }
bool MillisecondsSinceFrameStart(double& milliseconds)
{
    if (!gFrameStartKnown) return false;
    gFrameStartKnown = false;
    milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gFrameStart).count();
    return true;
}

void Presenter::NoteWorkMs(double milliseconds)
{
    if (milliseconds <= 0.0 || milliseconds > 250.0) return;
    milliseconds += mCostMs;
    mFrameMs = mFrameMs <= 0.0 ? milliseconds : mFrameMs * 0.9 + milliseconds * 0.1;
}

void Presenter::NoteFrameInterval(double milliseconds)
{
    if (milliseconds <= 0.0 || milliseconds > 250.0) return;
    mFrameMs = mFrameMs <= 0.0 ? milliseconds : mFrameMs * 0.9 + milliseconds * 0.1;
}

void Presenter::AddPass(std::unique_ptr<Pass> pass, const char* before)
{
    if (!pass) return;
    if (before != nullptr) {
        for (std::size_t i = 0; i < mPasses.size(); ++i) {
            if (std::strcmp(mPasses[i]->Name(), before) == 0) { mPasses.insert(mPasses.begin() + static_cast<std::ptrdiff_t>(i), std::move(pass)); return; }
        }
    }
    mPasses.push_back(std::move(pass));
}

const std::int8_t* PassContext::Normals(int& gridW, int& gridH)
{
    return presenter.EnsureNormals(hints, width, height, gridW, gridH);
}

void Presenter::Process(std::uint8_t* rgb, int width, int height, const Options& requested, const FrameHints& hints)
{
    if (rgb == nullptr || !requested.enabled || width < 64 || height < 48) return;
    ++mFrame;
    mNormalsReady = false;
    mPassTimes.clear();

    // Adaptive quality: the quality used is the one asked for, lowered while the passes cost more than the budget, and raised again (one step at a
    // time, after a long stretch with room to spare) when they do not.
    if (requested.quality != mLastRequestedQuality) { mLastRequestedQuality = requested.quality; mQualityCap = requested.quality; mSlowFrames = mFastFrames = 0; mRaiseAfter = 240; }
    Options options = requested;
    const bool automatic = requested.automatic && requested.adaptive;
    if (automatic) {
        // The first frame: a machine with a graphics card doing the passes starts at the top, any other at the level the caller gave (a phone or a browser starts lower).
        if (!mLevelStarted) { mLevelStarted = true; mLevel = (requested.gpuFinish && FinisherActive()) ? kMaxLevel : std::max(0, std::min(kMaxLevel, requested.startLevel)); mSlowFrames = mFastFrames = 0; mRaiseAfter = 240; }
        options = OptionsForLevel(requested, mLevel);
        mQualityCap = options.quality;
    }
    else if (requested.adaptive) options.quality = std::min(requested.quality, mQualityCap);
    mEffective = options;
    const auto frameStart = std::chrono::steady_clock::now();

    const std::uint8_t* mask = hints.hudMask != nullptr && hints.hudMaskWidth == width && hints.hudMaskHeight == height ? hints.hudMask : nullptr;
    PassContext context{rgb, width, height, options, hints, mask, *this};
    // The picture passes a finisher can take over (names of built-in passes); the others need the scene's depth and stay here.
    auto isFinishingPass = [](const char* name) {
        return std::strcmp(name, "filter") == 0 || std::strcmp(name, "edge-aa") == 0 || std::strcmp(name, "atmosphere") == 0 || std::strcmp(name, "lighting") == 0 || std::strcmp(name, "craft") == 0 ||
               std::strcmp(name, "reflection") == 0 || std::strcmp(name, "halos") == 0 || std::strcmp(name, "sharpen") == 0 || std::strcmp(name, "bloom") == 0 || std::strcmp(name, "exposure") == 0 ||
               std::strcmp(name, "hud-outline") == 0 || std::strcmp(name, "vignette") == 0;
    };
    const bool useFinisher = options.gpuFinish && FinisherActive();
    mSprites.clear();
    mDeferGlows = useFinisher && hints.gpuScene;   // the card's scene replaces the processor's, so it draws the glows after it
    auto run = [&](Pass& pass) {
        if (mProfiling) {
            const auto start = std::chrono::steady_clock::now();
            pass.Run(context);
            mPassTimes.push_back({pass.Name(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()});
        }
        else pass.Run(context);
    };
    for (const std::unique_ptr<Pass>& pass : mPasses) {
        if (!pass->Wanted(options, hints)) continue;
        if (useFinisher && isFinishingPass(pass->Name())) continue;
        run(*pass);
    }
    if (useFinisher) {
        const auto start = std::chrono::steady_clock::now();
        FrameHints handed;
        const FrameHints* toFinisher = &hints;
        if (!mSprites.empty()) { handed = hints; handed.sprites = mSprites.data(); handed.spriteCount = static_cast<int>(mSprites.size()); toFinisher = &handed; }
        const bool done = mFinisher(rgb, width, height, options, *toFinisher, mask);
        if (mProfiling) mPassTimes.push_back({"gpu-finish", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()});
        if (!done) {   // it could not: this frame gets the CPU passes, and the finisher is not asked again
            mFinisherBroken = true;
            for (const std::unique_ptr<Pass>& pass : mPasses) if (pass->Wanted(options, hints) && isFinishingPass(pass->Name())) run(*pass);
        }
    }
    if (!(hints.valid && options.effects)) mParticles.clear();

    // The first frames only watch: they include shader compilation, texture uploads and the stall of a track that has just loaded (hundreds of milliseconds on a graphics card), which says
    // nothing about what the machine can sustain.
    constexpr std::uint32_t kWarmupFrames = 45;
    if (requested.adaptive && mFrame <= kWarmupFrames) { mCostMs = 0.0; mSlowFrames = mFastFrames = 0; }
    else if (requested.adaptive) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        mCostMs = mFrame <= kWarmupFrames + 1 ? ms : mCostMs * 0.9 + ms * 0.1;
        const double kFrameBudgetMs = 21.0, kFrameRoomMs = 19.0;   // whole frames: under about 47 a second is too slow; over 52 leaves room
        const bool frameTooSlow = mFrameMs > kFrameBudgetMs;   // (whole frames count even when the passes are cheap: the observer's shadows and lights are not in the passes' cost)
        const bool tooSlow = mCostMs > requested.budgetMs || frameTooSlow;
        const bool roomToSpare = mCostMs < requested.budgetMs * 0.45 && (mFrameMs <= 0.0 || mFrameMs < kFrameRoomMs);
        if (tooSlow) { ++mSlowFrames; mFastFrames = 0; } else if (roomToSpare) { ++mFastFrames; mSlowFrames = 0; } else { mSlowFrames = mFastFrames = 0; }
        int& step = automatic ? mLevel : mQualityCap;                 // the thing that is stepped: the level (automatic) or the quality
        const int ceiling = automatic ? kMaxLevel : requested.quality;
        if (mSlowFrames >= 45 && step > 0) {
            mStepCostMs = mCostMs; mStepFrameMs = mFrameMs;
            --step; mSlowFrames = 0; mCostMs = requested.budgetMs * 0.8;
            if (frameTooSlow) { mRaiseAfter = std::min(mRaiseAfter * 2, 4800); mFrameMs = kFrameBudgetMs * 0.9; }   // wait longer each time before trying again
        }
        else if (mFastFrames >= mRaiseAfter && step < ceiling) { mStepCostMs = mCostMs; mStepFrameMs = mFrameMs; ++step; mFastFrames = 0; mCostMs = requested.budgetMs * 0.6; }
    }
}

// De-banding and edge smoothing.
//  - The palette's colour ramps step in visible bands. Where a colour channel of a pixel differs from its four neighbours (above, below, left,
//    right) by a little but not by nothing, it is mixed with them (a small blur that leaves texture edges, text and the HUD alone); then the
//    picture is dithered with a 4x4 ordered pattern so the remaining steps break up. This is the same integer arithmetic on every platform; on
//    x86-64 it runs sixteen bytes at a time (SSE2), elsewhere one byte at a time, with identical results.
//  - The sub-pixel part of FXAA: where a pixel differs sharply from the average of its neighbours (a stair-step on an edge) it is pulled
//    towards that average, never by more than 50%. This is the costly part and is only run at the high quality setting.
namespace
{
// An 8x8 ordered-dither matrix (0..63): a finer pattern than 4x4, so the dither is harder to see.
const int kBayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21};
const int kBandStep = 12;   // a channel that differs from a neighbour by more than this is detail, not a band

inline int DitherLevel(int x, int y) { return ((kBayer8[(y & 7) * 8 + (x & 7)] * 2 - 63) * 3) / 64; }   // -2 .. +2 levels

// One byte (one colour channel) of an interior pixel: mix it with its neighbours when they differ a little, then dither.
inline std::uint8_t DebandByte(const std::uint8_t* c, std::ptrdiff_t stride, int dither)
{
    const int v = c[0], n0 = c[-stride], n1 = c[stride], n2 = c[-3], n3 = c[3];
    const int worst = std::max(std::max(Abs(n0 - v), Abs(n1 - v)), std::max(Abs(n2 - v), Abs(n3 - v)));
    int out = v;
    if (worst != 0 && worst <= kBandStep) out = ((v * 8 + n0 + n1 + n2 + n3) * 5462) >> 16;   // / 12
    return static_cast<std::uint8_t>(Clamp255(out + dither));
}
}

#if !defined(HOVERNET_GFX_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define HOVERNET_GFX_SSE2 1
#include <emmintrin.h>
#endif

void Presenter::Filter(std::uint8_t* rgb, int width, int height, bool deband, bool smooth, const std::uint8_t* hudMask)
{
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width) * 3;
    mScratch.assign(rgb, rgb + static_cast<std::size_t>(stride) * height);
    const std::uint8_t* src = mScratch.data();

    if (deband) {
        // The dither for each of the four row phases, as bytes to add and to take away (so the saturating byte adds do the clamping).
        mDitherUp.resize(static_cast<std::size_t>(stride) * 8);
        mDitherDown.resize(static_cast<std::size_t>(stride) * 8);
        for (int phase = 0; phase < 8; ++phase) {
            for (int x = 0; x < width; ++x) {
                const int d = DitherLevel(x, phase);
                for (int k = 0; k < 3; ++k) {
                    mDitherUp[static_cast<std::size_t>(phase) * stride + x * 3 + k] = static_cast<std::uint8_t>(d > 0 ? d : 0);
                    mDitherDown[static_cast<std::size_t>(phase) * stride + x * 3 + k] = static_cast<std::uint8_t>(d < 0 ? -d : 0);
                }
            }
        }
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* row = src + y * stride;
            std::uint8_t* out = rgb + y * stride;
            const std::uint8_t* up = mDitherUp.data() + static_cast<std::size_t>(y & 7) * stride;
            const std::uint8_t* down = mDitherDown.data() + static_cast<std::size_t>(y & 7) * stride;
            if (y == 0 || y == height - 1) {   // the picture's outermost rows: only dithered
                for (std::ptrdiff_t i = 0; i < stride; ++i) out[i] = static_cast<std::uint8_t>(Clamp255(row[i] + up[i] - down[i]));
                continue;
            }
            for (int k = 0; k < 3; ++k) {   // the first and last pixel of the row
                out[k] = static_cast<std::uint8_t>(Clamp255(row[k] + up[k] - down[k]));
                out[stride - 3 + k] = static_cast<std::uint8_t>(Clamp255(row[stride - 3 + k] + up[stride - 3 + k] - down[stride - 3 + k]));
            }
            std::ptrdiff_t i = 3;
#ifdef HOVERNET_GFX_SSE2
            const __m128i zero = _mm_setzero_si128();
            const __m128i step = _mm_set1_epi8(static_cast<char>(kBandStep));
            for (; i + 16 <= stride - 3; i += 16) {
                const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i));
                const __m128i n0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i - stride));
                const __m128i n1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i + stride));
                const __m128i n2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i - 3));
                const __m128i n3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i + 3));
                auto absdiff = [](__m128i a, __m128i b) { return _mm_or_si128(_mm_subs_epu8(a, b), _mm_subs_epu8(b, a)); };
                const __m128i worst = _mm_max_epu8(_mm_max_epu8(absdiff(n0, c), absdiff(n1, c)), _mm_max_epu8(absdiff(n2, c), absdiff(n3, c)));
                const __m128i isFlat = _mm_cmpeq_epi8(worst, zero);
                const __m128i isBand = _mm_cmpeq_epi8(_mm_subs_epu8(worst, step), zero);   // worst <= step
                const __m128i mixMask = _mm_andnot_si128(isFlat, isBand);                  // 0 < worst <= step
                // (8c + n0 + n1 + n2 + n3) / 12 in 16 bits: x * 5462 >> 16, exact for x <= 3060
                __m128i mixedLo = _mm_slli_epi16(_mm_unpacklo_epi8(c, zero), 3), mixedHi = _mm_slli_epi16(_mm_unpackhi_epi8(c, zero), 3);
                for (const __m128i n : {n0, n1, n2, n3}) {
                    mixedLo = _mm_add_epi16(mixedLo, _mm_unpacklo_epi8(n, zero));
                    mixedHi = _mm_add_epi16(mixedHi, _mm_unpackhi_epi8(n, zero));
                }
                const __m128i factor = _mm_set1_epi16(5462);
                const __m128i mixed = _mm_packus_epi16(_mm_mulhi_epu16(mixedLo, factor), _mm_mulhi_epu16(mixedHi, factor));
                __m128i result = _mm_or_si128(_mm_and_si128(mixMask, mixed), _mm_andnot_si128(mixMask, c));
                result = _mm_adds_epu8(result, _mm_loadu_si128(reinterpret_cast<const __m128i*>(up + i)));
                result = _mm_subs_epu8(result, _mm_loadu_si128(reinterpret_cast<const __m128i*>(down + i)));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), result);
            }
#endif
            for (; i < stride - 3; ++i) {
                const int d = static_cast<int>(up[i]) - static_cast<int>(down[i]);
                out[i] = DebandByte(row + i, stride, d);
            }
        }
    }

    if (hudMask != nullptr && deband) {   // the HUD is left exactly as drawn: put back what the de-banding and the dither changed
        for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i) {
            if (hudMask[i]) std::memcpy(rgb + i * 3, src + i * 3, 3);
        }
    }
    if (!smooth) return;
    // Find the strong edges (a colour difference of more than 36 from a neighbour, summed over the channels) and keep each pixel's brightness.
    mEdge.assign(static_cast<std::size_t>(width) * height, 0);
    mLuma.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = src + y * stride;
        std::uint8_t* edge = mEdge.data() + static_cast<std::size_t>(y) * width;
        std::uint8_t* luma = mLuma.data() + static_cast<std::size_t>(y) * width;
        const bool interior = y > 0 && y < height - 1;
        for (int x = 0; x < width; ++x, row += 3) {
            const int r = row[0], g = row[1], b = row[2];
            luma[x] = static_cast<std::uint8_t>(Luma(r, g, b));
            if (!interior || x == 0 || x == width - 1) continue;
            const int d0 = Abs(row[-stride] - r) + Abs(row[-stride + 1] - g) + Abs(row[-stride + 2] - b);
            const int d1 = Abs(row[stride] - r) + Abs(row[stride + 1] - g) + Abs(row[stride + 2] - b);
            const int d2 = Abs(row[-3] - r) + Abs(row[-2] - g) + Abs(row[-1] - b);
            const int d3 = Abs(row[3] - r) + Abs(row[4] - g) + Abs(row[5] - b);
            edge[x] = std::max(std::max(d0, d1), std::max(d2, d3)) > 36 && (hudMask == nullptr || !hudMask[static_cast<std::size_t>(y) * width + x]) ? 1 : 0;
        }
    }
    static int reciprocal[256];   // 65536 / range
    static bool reciprocalReady = false;
    if (!reciprocalReady) { reciprocal[0] = 0; for (int i = 1; i < 256; ++i) reciprocal[i] = 65536 / i; reciprocalReady = true; }
    for (int y = 1; y < height - 1; ++y) {
        const std::uint8_t* edge = mEdge.data() + static_cast<std::size_t>(y) * width;
        const std::uint8_t* lum = mLuma.data() + static_cast<std::size_t>(y) * width;
        for (int x = 1; x < width - 1; ++x) {
            if (edge[x] == 0) continue;
            const int lc = lum[x], ln = lum[x - width], ls = lum[x + width], lw = lum[x - 1], le = lum[x + 1];
            const int range = std::max(std::max(std::max(lc, ln), std::max(ls, lw)), le) - std::min(std::min(std::min(lc, ln), std::min(ls, lw)), le);
            if (range < 24) continue;
            const int diag = lum[x - width - 1] + lum[x - width + 1] + lum[x + width - 1] + lum[x + width + 1];
            const int avgLuma = ((2 * (ln + ls + lw + le) + diag) * 5462) >> 16;   // / 12
            int t = (Abs(avgLuma - lc) * reciprocal[range]) >> 8;   // 0 .. 256: how far the pixel is from its neighbours, as a share of the local contrast
            t = std::min(t * 13 / 8, 256);   // a pixel half-way between its neighbours' average and the extreme counts as a full step
            const int smoothT = (((768 - 2 * t) * t) * t) >> 16;   // smoothstep, 0 .. 256
            const int blend = smoothT / 2;   // at most 50%: 0 .. 128
            if (blend <= 0) continue;
            const std::uint8_t* c = src + y * stride + x * 3;
            const std::uint8_t* n0 = c - stride; const std::uint8_t* n1 = c + stride; const std::uint8_t* n2 = c - 3; const std::uint8_t* n3 = c + 3;
            const std::uint8_t* q0 = n0 - 3; const std::uint8_t* q1 = n0 + 3; const std::uint8_t* q2 = n1 - 3; const std::uint8_t* q3 = n1 + 3;
            const int d = deband ? DitherLevel(x, y) : 0;
            std::uint8_t* o = rgb + y * stride + x * 3;
            for (int k = 0; k < 3; ++k) {
                const int avg = ((2 * (n0[k] + n1[k] + n2[k] + n3[k]) + q0[k] + q1[k] + q2[k] + q3[k]) * 5462) >> 16;
                o[k] = static_cast<std::uint8_t>(Clamp255(c[k] + (((avg - c[k]) * blend) >> 8) + d));
            }
        }
    }
}

// Glow: keep what is brighter than 70% grey, shrink it to a quarter of the size, blur it twice, and add it back.
void Presenter::Bloom(std::uint8_t* rgb, int width, int height)
{
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    const std::size_t cells = static_cast<std::size_t>(bw) * bh * 3;
    mBloomA.assign(cells, 0);
    mBloomB.assign(cells, 0);
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width) * 3;
    bool any = false;
    for (int by = 0; by < bh; ++by) {   // each cell samples four of its sixteen pixels
        const int y0 = std::min(by * 4 + 1, height - 1), y1 = std::min(by * 4 + 3, height - 1);
        for (int bx = 0; bx < bw; ++bx) {
            const int x0 = std::min(bx * 4 + 1, width - 1), x1 = std::min(bx * 4 + 3, width - 1);
            const std::uint8_t* samples[4] = {rgb + y0 * stride + x0 * 3, rgb + y0 * stride + x1 * 3, rgb + y1 * stride + x0 * 3, rgb + y1 * stride + x1 * 3};
            int sr = 0, sg = 0, sb = 0;
            for (const std::uint8_t* p : samples) {
                const int l = Luma(p[0], p[1], p[2]);
                if (l <= 178) continue;
                const int w = l - 178;   // 0 .. 77
                sr += p[0] * w / 77; sg += p[1] * w / 77; sb += p[2] * w / 77;
            }
            if ((sr | sg | sb) == 0) continue;
            any = true;
            std::uint16_t* cell = &mBloomA[(static_cast<std::size_t>(by) * bw + bx) * 3];
            cell[0] = static_cast<std::uint16_t>(sr * 4); cell[1] = static_cast<std::uint16_t>(sg * 4); cell[2] = static_cast<std::uint16_t>(sb * 4);   // 0 .. 16*255
        }
    }
    if (!any) return;   // nothing bright in this picture
    static const int kTaps[5] = {1, 4, 6, 4, 1};
    for (int pass = 0; pass < 2; ++pass) {
        for (int by = 0; by < bh; ++by) {   // horizontal, A -> B
            for (int bx = 0; bx < bw; ++bx) {
                for (int k = 0; k < 3; ++k) {
                    int sum = 0;
                    for (int t = -2; t <= 2; ++t) sum += kTaps[t + 2] * mBloomA[(static_cast<std::size_t>(by) * bw + std::min(std::max(bx + t, 0), bw - 1)) * 3 + k];
                    mBloomB[(static_cast<std::size_t>(by) * bw + bx) * 3 + k] = static_cast<std::uint16_t>(sum / 16);
                }
            }
        }
        for (int by = 0; by < bh; ++by) {   // vertical, B -> A
            for (int bx = 0; bx < bw; ++bx) {
                for (int k = 0; k < 3; ++k) {
                    int sum = 0;
                    for (int t = -2; t <= 2; ++t) sum += kTaps[t + 2] * mBloomB[(static_cast<std::size_t>(std::min(std::max(by + t, 0), bh - 1)) * bw + bx) * 3 + k];
                    mBloomA[(static_cast<std::size_t>(by) * bw + bx) * 3 + k] = static_cast<std::uint16_t>(sum / 16);
                }
            }
        }
    }
    // Add back with bilinear sampling (cells are 4 pixels wide). For each picture row the two cell rows are mixed once, then each pixel mixes two
    // cells; pixels whose two cells are empty (most of the picture) are skipped.
    std::vector<int> mixed(static_cast<std::size_t>(bw) * 3);
    for (int y = 0; y < height; ++y) {
        const int fy = y * 4 + 2 - 8;   // in 1/16 of a cell, relative to cell 0's centre
        const int cy0 = std::min(std::max(fy >> 4, 0), bh - 1), cy1 = std::min(cy0 + 1, bh - 1);
        const int wy = std::min(std::max(fy - (cy0 << 4), 0), 16);
        bool rowAny = false;
        for (int i = 0; i < bw * 3; ++i) {
            const int v = mBloomA[static_cast<std::size_t>(cy0) * bw * 3 + i] * (16 - wy) + mBloomA[static_cast<std::size_t>(cy1) * bw * 3 + i] * wy;   // x16
            mixed[static_cast<std::size_t>(i)] = v;
            rowAny = rowAny || v != 0;
        }
        if (!rowAny) continue;
        std::uint8_t* o = rgb + y * stride;
        for (int x = 0; x < width; ++x, o += 3) {
            const int fx = x * 4 + 2 - 8;
            const int cx0 = std::min(std::max(fx >> 4, 0), bw - 1), cx1 = std::min(cx0 + 1, bw - 1);
            const int wx = std::min(std::max(fx - (cx0 << 4), 0), 16);
            const int* m0 = &mixed[static_cast<std::size_t>(cx0) * 3];
            const int* m1 = &mixed[static_cast<std::size_t>(cx1) * 3];
            if ((m0[0] | m0[1] | m0[2] | m1[0] | m1[1] | m1[2]) == 0) continue;
            for (int k = 0; k < 3; ++k) {
                const int v = (m0[k] * (16 - wx) + m1[k] * wx) >> 8;   // 0 .. 16*255, i.e. sixteen times a colour level
                o[k] = static_cast<std::uint8_t>(Clamp255(o[k] + ((v * 58) >> 10)));   // x 0.9 of the cell average (58/1024 = 0.9/16)
            }
        }
    }
}

// Lighting from the depth buffer, in true colour (so it does not snap to palette colours).
//  - Haze: the farther a surface, the more it is mixed with the colour of the sky (sampled from the picture), up to 35% at the far end. Sky
//    pixels (nothing drawn) are not touched. Medium quality and up.
//  - Lights: for each light (engine pools under the craft, a headlight ahead of the viewing craft, a flash on an impact) every surface pixel inside
//    its radius is lit by its distance in 3D, so a light pools on the floor and climbs the walls beside it. Lighting multiplies the surface colour
//    (so texture stays) and adds a little (so dark surfaces still pick up the colour).
// HUD pixels are never touched.
// Surface normals from the depth buffer: for each 4x4 block, the camera-space positions of the surface a few pixels either side give two tangent
// directions, and their cross product is the normal (pointing at the camera). Where the neighbours are not part of the same surface (a depth edge,
// the sky, the edge of the view) the normal is taken to face the camera.
const std::int8_t* Presenter::EnsureNormals(const FrameHints& hints, int width, int height, int& gridW, int& gridH)
{
    const int shift = BlockShift(width), bs = 1 << shift;
    gridW = (width + bs - 1) >> shift;
    gridH = (height + bs - 1) >> shift;
    if (hints.depth == nullptr || hints.depthWidth <= 0 || hints.depthHeight <= 0 || hints.projKx <= 0.0f || hints.projKz <= 0.0f) return nullptr;
    if (mNormalsReady) return mNormalGrid.data();
    const int x0 = std::max(0, hints.depthX0), y0 = std::max(0, hints.depthY0);
    const int x1 = std::min(width, hints.depthX0 + hints.depthWidth), y1 = std::min(height, hints.depthY0 + hints.depthHeight);
    const float centreX = static_cast<float>(hints.projCenterX), centreY = static_cast<float>(hints.projCenterY);
    auto depthAt = [&](int x, int y) { return hints.depth[static_cast<std::size_t>(y - hints.depthY0) * hints.depthWidth + (x - hints.depthX0)]; };
    mNormalGrid.assign(static_cast<std::size_t>(gridW) * gridH * 3, 0);
    if (x1 > x0 && y1 > y0) {
        auto position = [&](int px, int py, float& X, float& Y, float& Z) {
            X = depthAt(px, py) * 4.0f;
            Y = (centreX - px) * X * hints.projKx;
            Z = (centreY - py) * X * hints.projKz;
        };
        for (int y = y0 & ~(bs - 1); y < y1; y += bs) {
            for (int x = x0 & ~(bs - 1); x < x1; x += bs) {
                const int sx = std::max(x, x0), sy = std::max(y, y0);
                std::int8_t* n = &mNormalGrid[(static_cast<std::size_t>(y >> shift) * gridW + (x >> shift)) * 3];
                const int zc = depthAt(sx, sy);
                if (zc == 0xFFFF) continue;
                float cx3, cy3, cz3;
                position(sx, sy, cx3, cy3, cz3);
                float nx = -cx3, ny = -cy3, nz = -cz3;   // the fallback: facing the camera
                const std::uint8_t tag = hints.tags != nullptr ? hints.tags[static_cast<std::size_t>(sy - hints.depthY0) * hints.depthWidth + (sx - hints.depthX0)] : 0;
                const bool wall = tag == kTagWall;   // a wall is vertical: its normal is horizontal, whatever the depth noise says
                if (wall) nz = 0.0f;
                if (tag == kTagFloor || tag == kTagCeiling) {   // the rasteriser knows: a floor faces straight up, a ceiling straight down
                    n[0] = 0; n[1] = 0; n[2] = tag == kTagFloor ? 127 : -127;
                    continue;
                }
                if (sx - bs >= x0 && sx + bs < x1 && sy - bs >= y0 && sy + bs < y1) {
                    const int zl = depthAt(sx - bs, sy), zr = depthAt(sx + bs, sy), zu = depthAt(sx, sy - bs), zd = depthAt(sx, sy + bs);
                    const int limit = 100 + zc / 12;   // 4 mm units: 40 cm plus a twelfth of the distance
                    if (zl != 0xFFFF && zr != 0xFFFF && zu != 0xFFFF && zd != 0xFFFF && Abs(zl - zc) < limit && Abs(zr - zc) < limit && Abs(zu - zc) < limit && Abs(zd - zc) < limit) {
                        float lx, ly, lz, rx, ry, rz, ux, uy, uz, dx, dy, dz;
                        position(sx - bs, sy, lx, ly, lz); position(sx + bs, sy, rx, ry, rz);
                        position(sx, sy - bs, ux, uy, uz); position(sx, sy + bs, dx, dy, dz);
                        const float ax = rx - lx, ay = ry - ly, az = rz - lz, bx = dx - ux, by = dy - uy, bz = dz - uz;
                        float cxn = ay * bz - az * by, cyn = az * bx - ax * bz, czn = ax * by - ay * bx;
                        if (cxn * cx3 + cyn * cy3 + czn * cz3 > 0.0f) { cxn = -cxn; cyn = -cyn; czn = -czn; }   // towards the camera
                        nx = cxn; ny = cyn; nz = wall ? 0.0f : czn;
                    }
                }
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length < 1e-3f) continue;
                n[0] = static_cast<std::int8_t>(nx / length * 127.0f); n[1] = static_cast<std::int8_t>(ny / length * 127.0f); n[2] = static_cast<std::int8_t>(nz / length * 127.0f);
            }
        }
    }
    mNormalsReady = true;
    return mNormalGrid.data();
}

namespace {
const std::int8_t kNoNormal[3] = {0, 0, 0};
std::uint8_t* sqrtSmall() { static std::uint8_t t[1024]; return t; }
std::uint8_t* sqrtBig() { static std::uint8_t t[16384]; return t; }
// The square root of a value up to 262143 (255 squared times a little over four), as a colour level 0..255.
inline int SquareRoot(int v) { return v < 1024 ? sqrtSmall()[v] : sqrtBig()[std::min(v >> 4, 16383)]; }
}

void Presenter::Lighting(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints, const std::uint8_t* hudMask)
{
    if (hints.depth == nullptr || hints.depthWidth <= 0 || hints.depthHeight <= 0) return;
    const int x0 = std::max(0, hints.depthX0), y0 = std::max(0, hints.depthY0);
    const int x1 = std::min(width, hints.depthX0 + hints.depthWidth), y1 = std::min(height, hints.depthY0 + hints.depthHeight);
    if (x1 <= x0 || y1 <= y0) return;
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width) * 3;
    auto depthAt = [&](int x, int y) { return hints.depth[static_cast<std::size_t>(y - hints.depthY0) * hints.depthWidth + (x - hints.depthX0)]; };

    if (options.quality >= 1) {
        // The haze colour: the average of some sky pixels from the upper part of the view.
        int sr = 0, sg = 0, sb = 0, count = 0;
        for (int y = y0; y < y0 + (y1 - y0) / 2; y += 6) {
            for (int x = x0; x < x1; x += 9) {
                if (depthAt(x, y) != 0xFFFF || (hudMask != nullptr && hudMask[static_cast<std::size_t>(y) * width + x])) continue;
                const std::uint8_t* p = rgb + y * stride + x * 3;
                sr += p[0]; sg += p[1]; sb += p[2]; ++count;
            }
        }
        if (count >= 8) {
            const int fogR = sr / count, fogG = sg / count, fogB = sb / count;
            // Haze factor by depth, 0..89 (35% of 256): nothing up to 12 m, full strength at 120 m. Depth is in 4 mm units; the table is by depth / 64.
            static std::uint8_t hazeTable[1024];
            static bool hazeReady = false;
            if (!hazeReady) {
                for (int i = 0; i < 1024; ++i) {
                    const float metres = i * 64 * 4 / 1000.0f;
                    hazeTable[i] = static_cast<std::uint8_t>(Smooth01((metres - 12.0f) / 108.0f) * 89.0f);
                }
                hazeReady = true;
            }
            for (int y = y0; y < y1; ++y) {
                const std::uint16_t* depthRow = hints.depth + static_cast<std::size_t>(y - hints.depthY0) * hints.depthWidth - hints.depthX0;
                const std::uint8_t* maskRow = hudMask != nullptr ? hudMask + static_cast<std::size_t>(y) * width : nullptr;
                std::uint8_t* p = rgb + y * stride + x0 * 3;
                for (int x = x0; x < x1; ++x, p += 3) {
                    const int z = depthRow[x];
                    if (z == 0xFFFF) continue;
                    const int f = hazeTable[z >> 6];
                    if (f == 0 || (maskRow != nullptr && maskRow[x])) continue;
                    p[0] = static_cast<std::uint8_t>(p[0] + (((fogR - p[0]) * f) >> 8));
                    p[1] = static_cast<std::uint8_t>(p[1] + (((fogG - p[1]) * f) >> 8));
                    p[2] = static_cast<std::uint8_t>(p[2] + (((fogB - p[2]) * f) >> 8));
                }
            }
        }
    }

    // Light and contact shading vary smoothly, so they are worked out per block (4x4 pixels; 8x8 for frames 960 pixels wide or more) from the block's top-left surface, all lights added together
    // per block, and then applied once to the block's pixels (a light's edge can spill up to three pixels past a depth edge, which a soft light hides).
    // Each block holds seven numbers: a brightness multiplier for each channel, a colour to add for each channel, and the contact darkening.
    const float centreX = static_cast<float>(hints.projCenterX), centreY = static_cast<float>(hints.projCenterY);
    const int shift = BlockShift(width), bs = 1 << shift, mask = bs - 1;
    const int gridW = (width + mask) >> shift, gridH = (height + mask) >> shift;
    const bool contact = options.quality >= 1;
    bool anyLight = false;
    bool gridReady = false;
    auto prepareGrid = [&]() { if (!gridReady) { mLightGrid.assign(static_cast<std::size_t>(gridW) * gridH * 7, 0); gridReady = true; } };
    if (contact) {
        // Contact shading: a surface with something close in front of it at a little distance (a wall beside the floor, the floor under a craft, a
        // crease) is darkened up to 27%. It looks at four neighbours 8 pixels away and only counts those that are nearer by 15 cm to 2.5 m: a
        // far-away edge is a silhouette, not a contact.
        prepareGrid();
        for (int y = y0 & ~mask; y < y1; y += bs) {
            const int cy = std::min(std::max(y + bs / 2, y0), y1 - 1);
            for (int x = x0 & ~mask; x < x1; x += bs) {
                const int cx = std::min(std::max(x + bs / 2, x0), x1 - 1);
                const int zc = depthAt(cx, cy);
                if (zc == 0xFFFF) continue;
                int occlusion = 0;
                const int reach = 2 * bs;
                const int sx[4] = {cx - reach, cx + reach, cx, cx}, sy[4] = {cy, cy, cy - reach, cy + reach};
                for (int k = 0; k < 4; ++k) {
                    if (sx[k] < x0 || sx[k] >= x1 || sy[k] < y0 || sy[k] >= y1) continue;
                    const int zn = depthAt(sx[k], sy[k]);
                    if (zn == 0xFFFF) continue;
                    const int delta = (zc - zn) * 4;   // mm nearer
                    if (delta <= 150 || delta >= 2500) continue;
                    occlusion += delta < 1000 ? std::min(256, (delta - 150) * 256 / 500) : std::max(0, 256 - (delta - 1000) * 256 / 1500);
                }
                mLightGrid[(static_cast<std::size_t>(y >> shift) * gridW + (x >> shift)) * 7 + 6] = static_cast<std::int16_t>(occlusion * 70 / (4 * 256));
            }
        }
    }
    // Surface normals (see PassContext::Normals); only worked out when there is a light to use them.
    bool anyActiveLight = false;
    for (int li = 0; li < hints.lightCount && li < kMaxLights; ++li) anyActiveLight = anyActiveLight || (hints.lights[li].strength > 0.01f && hints.lights[li].radius >= 50.0f);
    int normalsW = 0, normalsH = 0;
    const std::int8_t* normals = anyActiveLight ? EnsureNormals(hints, width, height, normalsW, normalsH) : nullptr;
    const bool castShadows = options.occlusion && options.quality >= 1;
    for (int li = 0; li < hints.lightCount && li < kMaxLights; ++li) {
        const Light& light = hints.lights[li];
        if (light.strength <= 0.01f || light.radius < 50.0f || hints.projKx <= 0.0f || hints.projKz <= 0.0f) continue;
        prepareGrid();
        anyLight = true;
        // The light's footprint on the screen: where its centre projects, and how many pixels its radius is there (the whole view when it is near).
        int bx0 = x0, bx1 = x1, by0 = y0, by1 = y1;
        const float nearest = light.x - light.radius;   // the closest a lit surface can be
        if (nearest > light.radius * 0.25f) {
            const float px = centreX - light.y / (light.x * hints.projKx), py = centreY - light.z / (light.x * hints.projKz);
            const float rx = light.radius / (nearest * hints.projKx) + 2.0f, ry = light.radius / (nearest * hints.projKz) + 2.0f;
            bx0 = std::max(x0, static_cast<int>(px - rx)); bx1 = std::min(x1, static_cast<int>(px + rx) + 1);
            by0 = std::max(y0, static_cast<int>(py - ry)); by1 = std::min(y1, static_cast<int>(py + ry) + 1);
        }
        const float inv2 = 1.0f / (light.radius * light.radius);
        for (int y = std::max(by0, 0) & ~mask; y < by1; y += bs) {
            const float up = centreY - y;
            const std::uint16_t* depthRow = hints.depth + static_cast<std::size_t>(std::max(y, y0) - hints.depthY0) * hints.depthWidth - hints.depthX0;
            for (int x = std::max(bx0, 0) & ~mask; x < bx1; x += bs) {
                const int z = depthRow[std::max(x, x0)];
                if (z == 0xFFFF) continue;
                const float depth = z * 4.0f;
                const float dx = depth - light.x;
                if (dx > light.radius || dx < -light.radius) continue;
                const float dy = (centreX - x) * depth * hints.projKx - light.y;
                const float dz = up * depth * hints.projKz - light.z;
                const float d2 = (dx * dx + dy * dy + dz * dz) * inv2;
                if (d2 >= 1.0f) continue;
                float f = (1.0f - d2) * (1.0f - d2) * light.strength;
                if (light.coneCos > -1.0f) {   // a spotlight: full strength along its axis, fading out at the edge of the cone
                    const float along = (dx * light.dirx + dy * light.diry + dz * light.dirz) / (std::sqrt(d2) * light.radius + 1.0f);
                    f *= Smooth01((along - light.coneCos) / 0.18f);
                    if (f <= 0.0f) continue;
                }
                if (castShadows && f > 0.01f) {   // walls and objects between the surface and the light block it (the craft's own body does not)
                    const float sx = depth, sy = (centreX - x) * depth * hints.projKx, sz = up * depth * hints.projKz;
                    const float span = std::sqrt((light.x - sx) * (light.x - sx) + (light.y - sy) * (light.y - sy) + (light.z - sz) * (light.z - sz));
                    if (span > 1500.0f) {
                        int blocked = 0;
                        for (int k = 0; k < 3; ++k) {
                            const float t = 0.3f + 0.25f * k;
                            const float px3 = sx + (light.x - sx) * t, py3 = sy + (light.y - sy) * t, pz3 = sz + (light.z - sz) * t;
                            if (px3 < 300.0f) continue;
                            if ((1.0f - t) * span < 1500.0f) continue;   // too near the light to say
                            const int qx = static_cast<int>(centreX - py3 / (px3 * hints.projKx)), qy = static_cast<int>(centreY - pz3 / (px3 * hints.projKz));
                            if (qx < x0 || qx >= x1 || qy < y0 || qy >= y1) continue;
                            const std::size_t at = static_cast<std::size_t>(qy - hints.depthY0) * hints.depthWidth + (qx - hints.depthX0);
                            if (hints.tags != nullptr && hints.tags[at] == kTagCraft) continue;
                            if (hints.depth[at] != 0xFFFF && hints.depth[at] * 4.0f < px3 - 350.0f) ++blocked;
                        }
                        f *= 1.0f - 0.8f * blocked / 3.0f;
                    }
                }
                // The surface's orientation: lit from the front it gets all of the light (and a glint), lit from behind a quarter of it.
                const std::int8_t* n = normals != nullptr ? &normals[(static_cast<std::size_t>(y >> shift) * gridW + (x >> shift)) * 3] : kNoNormal;
                const float dist = std::sqrt(d2) * light.radius + 1.0f;
                const float lx = -dx / dist, ly = -dy / dist, lz = -dz / dist;   // from the surface to the light
                const float nx = n[0] * (1.0f / 127.0f), ny = n[1] * (1.0f / 127.0f), nz = n[2] * (1.0f / 127.0f);
                const float ndotl = nx * lx + ny * ly + nz * lz;
                const float diffuse = 0.25f + 0.75f * (ndotl > 0.0f ? (ndotl > 1.0f ? 1.0f : ndotl) : 0.0f);
                // Glint: the half-vector between the light and the eye, raised to the 16th power.
                const float vx = -depth, vy = -((centreX - x) * depth * hints.projKx), vz = -(up * depth * hints.projKz);
                const float vlen = std::sqrt(vx * vx + vy * vy + vz * vz) + 1.0f;
                float hx = lx + vx / vlen, hy = ly + vy / vlen, hz = lz + vz / vlen;
                const float hlen = std::sqrt(hx * hx + hy * hy + hz * hz) + 1e-4f;
                float nh = (nx * hx + ny * hy + nz * hz) / hlen;
                nh = nh > 0.0f ? (nh > 1.0f ? 1.0f : nh) : 0.0f;
                nh *= nh; nh *= nh; nh *= nh; nh *= nh;   // ^16
                const float glint = ndotl > 0.0f ? nh * f * 0.6f : 0.0f;
                const int mul = static_cast<int>(f * diffuse * 150.0f);   // up to +60% of the surface colour at full strength...
                std::int16_t* cell = &mLightGrid[(static_cast<std::size_t>(y >> shift) * gridW + (x >> shift)) * 7];
                cell[0] = static_cast<std::int16_t>(cell[0] + mul * light.r / 255);
                cell[1] = static_cast<std::int16_t>(cell[1] + mul * light.g / 255);
                cell[2] = static_cast<std::int16_t>(cell[2] + mul * light.b / 255);
                cell[3] = static_cast<std::int16_t>(cell[3] + static_cast<int>(light.r * (f * diffuse * 0.12f + glint * 0.5f)));   // ...a little colour even on dark surfaces, and the glint
                cell[4] = static_cast<std::int16_t>(cell[4] + static_cast<int>(light.g * (f * diffuse * 0.12f + glint * 0.5f)));
                cell[5] = static_cast<std::int16_t>(cell[5] + static_cast<int>(light.b * (f * diffuse * 0.12f + glint * 0.5f)));
            }
        }
    }
    (void)anyLight;
    if (!gridReady) return;
    const bool linear = options.linearLight && options.quality >= 2;
    static int squareTable[256];
    static bool tablesReady = false;
    if (!tablesReady) {
        for (int i = 0; i < 256; ++i) squareTable[i] = i * i;
        for (int i = 0; i < 1024; ++i) sqrtSmall()[i] = static_cast<std::uint8_t>(std::min(255.0, std::sqrt(static_cast<double>(i)) + 0.5));
        for (int i = 0; i < 16384; ++i) sqrtBig()[i] = static_cast<std::uint8_t>(std::min(255.0, std::sqrt(static_cast<double>(i) * 16.0 + 8.0) + 0.5));
        tablesReady = true;
    }
    for (int y = y0; y < y1; ++y) {
        const std::uint16_t* rowZ = hints.depth + static_cast<std::size_t>(y - hints.depthY0) * hints.depthWidth - hints.depthX0;
        const std::uint8_t* maskRow = hudMask != nullptr ? hudMask + static_cast<std::size_t>(y) * width : nullptr;
        const std::int16_t* gridRow = &mLightGrid[static_cast<std::size_t>(y >> shift) * gridW * 7];
        std::uint8_t* p = rgb + y * stride + x0 * 3;
        for (int x = x0; x < x1; ++x, p += 3) {
            const std::int16_t* cell = gridRow + (x >> shift) * 7;
            if ((cell[0] | cell[1] | cell[2] | cell[3] | cell[4] | cell[5] | cell[6]) == 0) {   // an unlit block: skip to its end
                const int skip = mask - (x & mask);
                x += skip; p += skip * 3;
                continue;
            }
            if (rowZ[x] == 0xFFFF || (maskRow != nullptr && maskRow[x])) continue;
            const int keep = 256 - cell[6];
            if (linear) {   // light adds up as light does: in linear space (squares), then back
                for (int k = 0; k < 3; ++k) {
                    int v = squareTable[p[k]];
                    v = (v * (256 + cell[k])) >> 8;
                    v = (v * keep) >> 8;
                    p[k] = static_cast<std::uint8_t>(Clamp255(SquareRoot(v) + cell[3 + k]));
                }
            }
            else {
                p[0] = static_cast<std::uint8_t>(Clamp255((((p[0] * (256 + cell[0])) >> 8) * keep >> 8) + cell[3]));
                p[1] = static_cast<std::uint8_t>(Clamp255((((p[1] * (256 + cell[1])) >> 8) * keep >> 8) + cell[4]));
                p[2] = static_cast<std::uint8_t>(Clamp255((((p[2] * (256 + cell[2])) >> 8) * keep >> 8) + cell[5]));
            }
        }
    }
}

void Presenter::Glow(std::uint8_t* rgb, int width, int height, float cx, float cy, float rx, float ry, int r, int g, int b, float strength)
{
    if (!mDeferGlows) { AddGlow(rgb, width, height, cx, cy, rx, ry, r, g, b, strength); return; }
    if (rx < 1.0f || ry < 1.0f || strength <= 0.0f || mSprites.size() >= 512) return;
    mSprites.push_back(GlowSprite{cx, cy, rx, ry, static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), strength});
}

void Presenter::Effects(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints)
{
    const float scale = height / 480.0f;
    const float speed = std::min(hints.speed, 1.3f);
    const bool still = options.reducedMotion;

    // (There used to be speed lines here; they were removed with the other effects that follow the speed.)

    if (!hints.cockpit && hints.craftVisible) {
        // (There used to be a glowing trail along where the craft had been, and exhaust and dust streaming behind it. They were removed: a ribbon that
        // stretches and swings as the craft turns reads as an elastic band attached to it.)
        // Engine glow under the craft, brighter the faster it goes.
        const float flicker = 1.0f;   // steady: a glow that shimmers makes the craft look as if it is vibrating
        const float r = std::max(6.0f, hints.craftRadius * 1.25f);
        Glow(rgb, width, height, static_cast<float>(hints.craftX), hints.craftY + hints.craftRadius * 0.35f, r, r * 0.6f, 80, 160, 255, (0.22f + 0.5f * speed) * flicker);
    }

    // Impact sparks.
    if (!still && !hints.cockpit && hints.craftVisible && hints.impact > 0.12f) {
        const int count = 10 + static_cast<int>(30 * std::min(1.0f, hints.impact));
        for (int i = 0; i < count && mParticles.size() < 160; ++i) {
            const float angle = -3.14159265f * ((Random() & 1023) / 1023.0f);   // up and sideways
            const float v = (80.0f + (Random() & 255)) * scale;
            Particle p;
            p.x = static_cast<float>(hints.craftX); p.y = hints.craftY + hints.craftRadius * 0.3f;
            p.vx = std::cos(angle) * v; p.vy = std::sin(angle) * v;
            p.maxLife = p.life = 0.35f + (Random() & 255) / 255.0f * 0.45f;
            p.size = (1.2f + (Random() & 127) / 127.0f * 1.6f) * scale;
            p.r = 255; p.g = static_cast<std::uint8_t>(170 + (Random() & 63)); p.b = 70;
            p.strength = 1.0f; p.gravity = 520.0f;
            mParticles.push_back(p);
        }
    }
    if (!still) Emitters(width, height, options, hints);
    const float dt = 1.0f / 60.0f;
    for (std::size_t i = 0; i < mParticles.size();) {
        Particle& p = mParticles[i];
        p.life -= dt;
        if (p.life <= 0.0f || still) { mParticles[i] = mParticles.back(); mParticles.pop_back(); continue; }
        p.x += p.vx * dt; p.y += p.vy * dt;
        p.vy += p.gravity * scale * dt;
        const float f = p.life / p.maxLife;
        Glow(rgb, width, height, p.x, p.y, p.size * 2.2f, p.size * 2.2f, p.r, p.g, p.b, f * p.strength);
        ++i;
    }
}

// The tone curve and the saturation, for one palette colour. The picture is 8-bit and palettised, so the grade is applied to the 256 palette
// colours once, instead of to every pixel of every frame.
void Presenter::GradePalette(const std::uint8_t* in768, std::uint8_t* out768, float brightness)
{
    const ColourGrade grade(brightness);   // the same grade the true-colour textures go through
    for (int i = 0; i < 256; ++i) {
        int r, g, b;
        grade.Apply(in768[i * 3], in768[i * 3 + 1], in768[i * 3 + 2], r, g, b);
        out768[i * 3] = static_cast<std::uint8_t>(r); out768[i * 3 + 1] = static_cast<std::uint8_t>(g); out768[i * 3 + 2] = static_cast<std::uint8_t>(b);
    }
}

// A vignette that darkens the corners by up to a quarter. Only the outer part of the picture is touched: inside the curve the factor is 1.
void Presenter::Vignette(std::uint8_t* rgb, int width, int height, float speed)
{
    if (mVignetteWidth != width || mVignetteHeight != height) {
        mVignette.assign(static_cast<std::size_t>(width) * height, 0);   // how dark each pixel gets, 0..64
        mVignetteFrom.assign(static_cast<std::size_t>(height), 0);
        mVignetteTo.assign(static_cast<std::size_t>(height), 0);
        for (int y = 0; y < height; ++y) {
            const float dy = (y + 0.5f) / height * 2.0f - 1.0f;
            int from = width, to = -1;
            for (int x = 0; x < width; ++x) {
                const float dx = (x + 0.5f) / width * 2.0f - 1.0f;
                const float r2 = (dx * dx + dy * dy) * 0.5f;   // 0 centre .. 1 corner
                const int d = static_cast<int>(64.0f * r2 * r2);
                mVignette[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(d);
                if (d >= 2) { from = std::min(from, x); to = std::max(to, x); }
            }
            mVignetteFrom[static_cast<std::size_t>(y)] = from;
            mVignetteTo[static_cast<std::size_t>(y)] = to;
        }
        mVignetteWidth = width;
        mVignetteHeight = height;
    }
    // (speed is kept as a parameter but the pass is called with 0: the vignette is steady and does not follow the speed.)
    const int scale = 256 + static_cast<int>(std::min(std::max(speed, 0.0f), 1.3f) * 80.0f);
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width) * 3;
    for (int y = 0; y < height; ++y) {
        const int from = mVignetteFrom[static_cast<std::size_t>(y)], to = mVignetteTo[static_cast<std::size_t>(y)];
        if (to < from) continue;
        const std::uint8_t* dark = mVignette.data() + static_cast<std::size_t>(y) * width;
        std::uint8_t* row = rgb + y * stride;
        int leftEnd = width / 2, rightStart = width / 2;
        while (leftEnd > from && dark[leftEnd - 1] >= 2) --leftEnd;   // walk in from each end of the dark span to the untouched middle
        while (rightStart < to && dark[rightStart + 1] >= 2) ++rightStart;
        for (int x = from; x <= to; ++x) {
            if (x >= leftEnd && x <= rightStart && dark[x] < 2) { x = rightStart; continue; }
            const int keep = 256 - (dark[x] * scale >> 8);
            std::uint8_t* p = row + x * 3;
            p[0] = static_cast<std::uint8_t>((p[0] * keep) >> 8); p[1] = static_cast<std::uint8_t>((p[1] * keep) >> 8); p[2] = static_cast<std::uint8_t>((p[2] * keep) >> 8);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------
// The passes added in the second round of graphics work. Each reads the depth, the surface tags and the lights through the PassContext.
// ---------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct SceneWindow
{
    int x0, y0, x1, y1;
    bool Contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

SceneWindow WindowOf(const PassContext& c)
{
    const FrameHints& h = c.hints;
    return {std::max(0, h.depthX0), std::max(0, h.depthY0), std::min(c.width, h.depthX0 + h.depthWidth), std::min(c.height, h.depthY0 + h.depthHeight)};
}

inline int DepthAt(const FrameHints& h, int x, int y) { return h.depth[static_cast<std::size_t>(y - h.depthY0) * h.depthWidth + (x - h.depthX0)]; }
inline int TagAt(const FrameHints& h, int x, int y) { return h.tags != nullptr ? h.tags[static_cast<std::size_t>(y - h.depthY0) * h.depthWidth + (x - h.depthX0)] : 0; }

// The haze at a depth (4 mm units): 0 up to 12 m, rising to 89 (35%) at 120 m. Shared by the haze in the lighting pass and the desaturation.
inline int HazeAt(int z)
{
    static std::uint8_t table[1024];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 1024; ++i) table[i] = static_cast<std::uint8_t>(Smooth01((i * 64 * 4 / 1000.0f - 12.0f) / 108.0f) * 89.0f);
        ready = true;
    }
    return z == 0xFFFF ? 0 : table[z >> 6];
}
}

// Atmosphere: the colour of the sky is sampled from the picture and used to tint the ambient light by surface direction (floors catch a little of
// the sky, ceilings are a little darker), to fade distant surfaces towards grey as well as towards the sky (colours lose their strength with
// distance), to add extra mist near the floor far away, and to glow along the horizon.
void Presenter::Atmosphere(PassContext& c)
{
    const FrameHints& h = c.hints;
    if (h.depth == nullptr || h.depthWidth <= 0 || h.depthHeight <= 0) return;
    const SceneWindow w = WindowOf(c);
    if (w.x1 <= w.x0 || w.y1 <= w.y0) return;
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    // The sky colour, from sky pixels in the upper half; kept from frame to frame so it does not flicker when the sky is hidden for a moment.
    int sr = 0, sg = 0, sb = 0, count = 0;
    for (int y = w.y0; y < w.y0 + (w.y1 - w.y0) / 2; y += 6)
        for (int x = w.x0; x < w.x1; x += 9) {
            if (DepthAt(h, x, y) != 0xFFFF || (c.hudMask != nullptr && c.hudMask[static_cast<std::size_t>(y) * c.width + x])) continue;
            const std::uint8_t* p = c.rgb + y * stride + x * 3;
            sr += p[0]; sg += p[1]; sb += p[2]; ++count;
        }
    if (count >= 8) {
        const int r = sr / count, g = sg / count, b = sb / count;
        if (!mSkyKnown) { mSkyR = r; mSkyG = g; mSkyB = b; mSkyKnown = true; }
        else { mSkyR += (r - mSkyR) / 4; mSkyG += (g - mSkyG) / 4; mSkyB += (b - mSkyB) / 4; }
    }
    if (!mSkyKnown) return;
    const int skyR = mSkyR, skyG = mSkyG, skyB = mSkyB;
    for (int y = w.y0; y < w.y1; ++y) {
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        std::uint8_t* p = c.rgb + y * stride + w.x0 * 3;
        for (int x = w.x0; x < w.x1; ++x, p += 3) {
            if (maskRow != nullptr && maskRow[x]) continue;
            const int z = DepthAt(h, x, y);
            if (z == 0xFFFF) continue;
            const int tag = TagAt(h, x, y);
            int r = p[0], g = p[1], b = p[2];
            if (tag == kTagFloor) { r += skyR * 9 / 256; g += skyG * 9 / 256; b += skyB * 9 / 256; }                 // floors catch a little of the sky
            else if (tag == kTagCeiling) { r = r * 236 >> 8; g = g * 236 >> 8; b = b * 236 >> 8; }                  // ceilings are in their own shade
            const int haze = HazeAt(z);
            if (haze != 0) {
                const int l = Luma(r, g, b);
                const int desat = haze * 100 >> 8;                                                                   // up to 14%: colours fade with distance
                r += ((l - r) * desat) >> 8; g += ((l - g) * desat) >> 8; b += ((l - b) * desat) >> 8;
                if (tag == kTagFloor) {                                                                              // mist lies on the floor
                    const int mist = haze * 140 >> 8;
                    r += ((skyR - r) * mist) >> 8; g += ((skyG - g) * mist) >> 8; b += ((skyB - b) * mist) >> 8;
                }
            }
            p[0] = static_cast<std::uint8_t>(Clamp255(r)); p[1] = static_cast<std::uint8_t>(Clamp255(g)); p[2] = static_cast<std::uint8_t>(Clamp255(b));
        }
    }
    // A glow on the horizon: warm, strongest at the horizon line, fading over a tenth of the picture's height above and below it.
    const int band = std::max(4, (w.y1 - w.y0) / 10), horizon = h.projCenterY;
    for (int y = std::max(w.y0, horizon - band); y < std::min(w.y1, horizon + band); ++y) {
        const float t = 1.0f - std::fabs(static_cast<float>(y - horizon)) / band;
        const int amount = static_cast<int>(t * t * 46.0f);   // 0..46 of 256
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        std::uint8_t* p = c.rgb + y * stride + w.x0 * 3;
        for (int x = w.x0; x < w.x1; ++x, p += 3) {
            if (DepthAt(h, x, y) != 0xFFFF || (maskRow != nullptr && maskRow[x])) continue;
            p[0] = static_cast<std::uint8_t>(Clamp255(p[0] + ((255 - p[0]) * amount * 7 >> 11)));   // a warm white
            p[1] = static_cast<std::uint8_t>(Clamp255(p[1] + ((236 - p[1]) * amount * 7 >> 11)));
            p[2] = static_cast<std::uint8_t>(Clamp255(p[2] + ((200 - p[2]) * amount * 7 >> 11)));
        }
    }
}

// Craft shading: a rim of light round the edge of a craft (where its surface turns away from the eye), the sky reflected in its hull, and glints from
// the engine light, headlight and impact flash. Craft pixels only, with a normal worked out per pixel from the depth round it.
void Presenter::CraftShading(PassContext& c)
{
    const FrameHints& h = c.hints;
    if (h.depth == nullptr || h.tags == nullptr || h.projKx <= 0.0f || h.projKz <= 0.0f) return;
    const SceneWindow w = WindowOf(c);
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    const float cx = static_cast<float>(h.projCenterX), cy = static_cast<float>(h.projCenterY);
    auto position = [&](int px, int py, float& X, float& Y, float& Z) { X = DepthAt(h, px, py) * 4.0f; Y = (cx - px) * X * h.projKx; Z = (cy - py) * X * h.projKz; };
    const int sky[3] = {mSkyKnown ? mSkyR : 150, mSkyKnown ? mSkyG : 180, mSkyKnown ? mSkyB : 220};
    for (int y = std::max(w.y0, 2); y < std::min(w.y1, c.height - 2); ++y) {
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        for (int x = std::max(w.x0, 2); x < std::min(w.x1, c.width - 2); ++x) {
            if (TagAt(h, x, y) != kTagCraft || (maskRow != nullptr && maskRow[x])) continue;
            const int z = DepthAt(h, x, y);
            if (z == 0xFFFF) continue;
            float X, Y, Z;
            position(x, y, X, Y, Z);
            // The normal from the depth two pixels either side, when both belong to the craft and are near; otherwise the surface faces the eye.
            float nx = -X, ny = -Y, nz = -Z;
            if (TagAt(h, x - 2, y) == kTagCraft && TagAt(h, x + 2, y) == kTagCraft && TagAt(h, x, y - 2) == kTagCraft && TagAt(h, x, y + 2) == kTagCraft) {
                const int limit = 40 + z / 20;
                if (Abs(DepthAt(h, x - 2, y) - z) < limit && Abs(DepthAt(h, x + 2, y) - z) < limit && Abs(DepthAt(h, x, y - 2) - z) < limit && Abs(DepthAt(h, x, y + 2) - z) < limit) {
                    float lx, ly, lz, rx, ry, rz, ux, uy, uz, dx, dy, dz;
                    position(x - 2, y, lx, ly, lz); position(x + 2, y, rx, ry, rz); position(x, y - 2, ux, uy, uz); position(x, y + 2, dx, dy, dz);
                    const float ax = rx - lx, ay = ry - ly, az = rz - lz, bx = dx - ux, by = dy - uy, bz = dz - uz;
                    float qx = ay * bz - az * by, qy = az * bx - ax * bz, qz = ax * by - ay * bx;
                    if (qx * X + qy * Y + qz * Z > 0.0f) { qx = -qx; qy = -qy; qz = -qz; }
                    nx = qx; ny = qy; nz = qz;
                }
            }
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-3f;
            nx /= nlen; ny /= nlen; nz /= nlen;
            const float vlen = std::sqrt(X * X + Y * Y + Z * Z) + 1.0f;
            const float vx = -X / vlen, vy = -Y / vlen, vz = -Z / vlen;   // from the surface to the eye
            float nv = nx * vx + ny * vy + nz * vz;
            nv = nv < 0.0f ? 0.0f : (nv > 1.0f ? 1.0f : nv);
            const float edge = 1.0f - nv;
            const float rim = edge * edge * edge;
            float addR = 150.0f * rim * 0.30f, addG = 190.0f * rim * 0.30f, addB = 255.0f * rim * 0.30f;   // a cool rim light
            const float sheen = edge * edge * 0.16f;                                                          // the sky in the hull
            addR += sky[0] * sheen; addG += sky[1] * sheen; addB += sky[2] * sheen;
            for (int li = 0; li < h.lightCount && li < kMaxLights; ++li) {
                const Light& light = h.lights[li];
                if (light.kind != kLightEngine && light.kind != kLightHeadlight && light.kind != kLightFlash) continue;
                const float dx = light.x - X, dy = light.y - Y, dz = light.z - Z;
                const float dist2 = dx * dx + dy * dy + dz * dz;
                if (dist2 >= light.radius * light.radius) continue;
                const float dist = std::sqrt(dist2) + 1.0f;
                const float lx = dx / dist, ly = dy / dist, lz = dz / dist;
                float hx = lx + vx, hy = ly + vy, hz = lz + vz;
                const float hl = std::sqrt(hx * hx + hy * hy + hz * hz) + 1e-4f;
                float nh = (nx * hx + ny * hy + nz * hz) / hl;
                nh = nh > 0.0f ? (nh > 1.0f ? 1.0f : nh) : 0.0f;
                nh *= nh; nh *= nh; nh *= nh; nh *= nh; nh *= nh;   // ^32: a tight glint
                const float fall = 1.0f - dist2 / (light.radius * light.radius);
                const float spec = nh * fall * fall * light.strength * 0.9f;
                addR += light.r * spec * 0.7f; addG += light.g * spec * 0.7f; addB += light.b * spec * 0.7f;
            }
            std::uint8_t* p = c.rgb + y * stride + x * 3;
            p[0] = static_cast<std::uint8_t>(Clamp255(p[0] + static_cast<int>(addR)));
            p[1] = static_cast<std::uint8_t>(Clamp255(p[1] + static_cast<int>(addG)));
            p[2] = static_cast<std::uint8_t>(Clamp255(p[2] + static_cast<int>(addB)));
        }
    }
}

// The craft mirrored in the floor under it: floor pixels just below the craft's contact row take a fading copy of the craft pixels at the mirrored
// row above, up to 30%.
void Presenter::Reflection(PassContext& c)
{
    const FrameHints& h = c.hints;
    if (h.depth == nullptr || h.tags == nullptr || h.craftRadius < 4) return;
    const SceneWindow w = WindowOf(c);
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    const int ground = h.craftGroundY, reach = static_cast<int>(h.craftRadius * 1.3f), half = static_cast<int>(h.craftRadius * 1.7f);
    for (int y = std::max(w.y0, ground); y < std::min(w.y1, ground + reach); ++y) {
        const int srcY = ground - (y - ground);
        if (srcY < w.y0) break;
        const float fade = 1.0f - static_cast<float>(y - ground) / reach;
        const int amount = static_cast<int>(fade * fade * 78.0f);   // up to 30% of 256
        if (amount <= 0) continue;
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        for (int x = std::max(w.x0, h.craftX - half); x < std::min(w.x1, h.craftX + half); ++x) {
            if (TagAt(h, x, y) != kTagFloor || TagAt(h, x, srcY) != kTagCraft || (maskRow != nullptr && maskRow[x])) continue;
            const std::uint8_t* src = c.rgb + srcY * stride + x * 3;
            std::uint8_t* dst = c.rgb + y * stride + x * 3;
            for (int k = 0; k < 3; ++k) dst[k] = static_cast<std::uint8_t>(dst[k] + (((src[k] - dst[k]) * amount) >> 8));
        }
    }
}

// Glows around the small bright things in the scene (a missile, a mine, a power-up, another craft): a soft round glow, hidden when something nearer
// is in front of the light.
void Presenter::Halos(PassContext& c)
{
    const FrameHints& h = c.hints;
    if (h.depth == nullptr || h.projKx <= 0.0f || h.projKz <= 0.0f) return;
    const SceneWindow w = WindowOf(c);
    for (int li = 0; li < h.lightCount && li < kMaxLights; ++li) {
        const Light& light = h.lights[li];
        int cr, cg, cb;
        float strength, scale;
        switch (light.kind) {
        case kLightMissile: cr = 255; cg = 205; cb = 130; strength = 0.85f; scale = 0.16f; break;
        case kLightMine:    cr = 255; cg = 80;  cb = 70;  strength = 0.55f; scale = 0.14f; break;
        case kLightPowerUp: cr = 120; cg = 255; cb = 190; strength = 0.6f;  scale = 0.14f; break;
        case kLightCraft:   cr = 120; cg = 185; cb = 255; strength = 0.38f; scale = 0.11f; break;
        case kLightGhost:   cr = 190; cg = 210; cb = 255; strength = 0.3f;  scale = 0.11f; break;
        case kLightBurst:   cr = light.r; cg = light.g; cb = light.b; strength = std::min(1.0f, light.strength); scale = 0.20f; break;   // a muzzle flash or a pickup
        case kLightExplosion: cr = light.r; cg = std::min(255, light.g + 50); cb = std::min(255, light.b + 70); strength = std::min(1.0f, light.strength * 1.1f); scale = 0.30f; break;
        default: continue;
        }
        if (light.x < 500.0f) continue;
        const int px = static_cast<int>(h.projCenterX - light.y / (light.x * h.projKx)), py = static_cast<int>(h.projCenterY - light.z / (light.x * h.projKz));
        if (!w.Contains(px, py)) continue;
        if (DepthAt(h, px, py) * 4.0f < light.x - 700.0f) continue;   // something nearer hides it
        const float radius = std::min(70.0f, std::max(4.0f, light.radius * scale / (light.x * h.projKx)));
        AddGlow(c.rgb, c.width, c.height, static_cast<float>(px), static_cast<float>(py), radius, radius, cr, cg, cb, strength);
        if (light.kind == kLightExplosion) {   // a bright core inside the glow, and a flare that stretches sideways along the floor
            AddGlow(c.rgb, c.width, c.height, static_cast<float>(px), static_cast<float>(py), radius * 0.45f, radius * 0.45f, 255, 245, 220, strength);
            AddGlow(c.rgb, c.width, c.height, static_cast<float>(px), static_cast<float>(py), radius * 2.2f, radius * 0.3f, cr, cg, cb, strength * 0.6f);
        }
        if (light.kind == kLightMissile) {   // a missile also gets a short cross-shaped flare
            AddGlow(c.rgb, c.width, c.height, static_cast<float>(px), static_cast<float>(py), radius * 2.6f, radius * 0.25f, cr, cg, cb, strength * 0.7f);
            AddGlow(c.rgb, c.width, c.height, static_cast<float>(px), static_cast<float>(py), radius * 0.25f, radius * 2.6f, cr, cg, cb, strength * 0.5f);
        }
    }
}

// Anti-aliasing of the edges of surfaces and objects. The rasteriser knows where one thing ends and another begins (a change of surface tag, a jump
// in depth, the sky behind something), so the stair-steps along those edges are softened and the texture inside a surface is not touched.
void Presenter::EdgeAA(PassContext& c)
{
    const FrameHints& h = c.hints;
    if (h.depth == nullptr || h.depthWidth <= 0 || h.depthHeight <= 0) return;
    const SceneWindow w = WindowOf(c);
    if (w.x1 - w.x0 < 4 || w.y1 - w.y0 < 4) return;
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    // Pass one: for every pixel, is there an edge between it and the pixel to its right (bit 1) or below it (bit 2)? Each pair is compared once.
    mEdge.assign(static_cast<std::size_t>(c.width) * c.height, 0);
    auto differ = [&](int za, int zb, int ta, int tb) {
        if ((za == 0xFFFF) != (zb == 0xFFFF)) return true;                       // the sky behind something
        if (za != 0xFFFF && Abs(za - zb) > 120 + za / 10) return true;           // a jump in depth: one thing in front of another
        return ta != tb && ta != kTagNone && tb != kTagNone;                     // a different kind of surface
    };
    bool any = false;
    for (int y = w.y0; y < w.y1 - 1; ++y) {
        const std::uint16_t* zr = h.depth + static_cast<std::size_t>(y - h.depthY0) * h.depthWidth - h.depthX0;
        const std::uint16_t* zd = zr + h.depthWidth;
        const std::uint8_t* tr = h.tags != nullptr ? h.tags + static_cast<std::size_t>(y - h.depthY0) * h.depthWidth - h.depthX0 : nullptr;
        const std::uint8_t* td = tr != nullptr ? tr + h.depthWidth : nullptr;
        std::uint8_t* flags = mEdge.data() + static_cast<std::size_t>(y) * c.width;
        for (int x = w.x0; x < w.x1 - 1; ++x) {
            const int za = zr[x], ta = tr != nullptr ? tr[x] : 0;
            std::uint8_t f = 0;
            if (differ(za, zr[x + 1], ta, tr != nullptr ? tr[x + 1] : 0)) f |= 1;
            if (differ(za, zd[x], ta, td != nullptr ? td[x] : 0)) f |= 2;
            if (f) { flags[x] = f; any = true; }
        }
    }
    if (!any) return;
    mScratch.assign(c.rgb, c.rgb + static_cast<std::size_t>(stride) * c.height);
    const std::uint8_t* src = mScratch.data();
    // Pass two: a pixel with an edge on any side takes a share of the colour on the other side of it.
    for (int y = w.y0 + 1; y < w.y1 - 1; ++y) {
        const std::uint8_t* flags = mEdge.data() + static_cast<std::size_t>(y) * c.width;
        const std::uint8_t* above = flags - c.width;
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        for (int x = w.x0 + 1; x < w.x1 - 1; ++x) {
            const int right = flags[x] & 1, below = (flags[x] >> 1) & 1, left = flags[x - 1] & 1, up = (above[x] >> 1) & 1;
            if (!(right | below | left | up)) continue;
            if (maskRow != nullptr && maskRow[x]) continue;
            int sr = 0, sg = 0, sb = 0, n = 0;
            auto take = [&](int nx, int ny) {
                if (c.hudMask != nullptr && c.hudMask[static_cast<std::size_t>(ny) * c.width + nx]) return;
                const std::uint8_t* q = src + ny * stride + nx * 3;
                sr += q[0]; sg += q[1]; sb += q[2]; ++n;
            };
            if (right) take(x + 1, y);
            if (left) take(x - 1, y);
            if (below) take(x, y + 1);
            if (up) take(x, y - 1);
            if (n == 0) continue;
            const std::uint8_t* p = src + y * stride + x * 3;
            std::uint8_t* o = c.rgb + y * stride + x * 3;
            const int blend = n >= 2 ? 120 : 84;   // 47% or 33% towards the other side
            o[0] = static_cast<std::uint8_t>(p[0] + (((sr / n - p[0]) * blend) >> 8));
            o[1] = static_cast<std::uint8_t>(p[1] + (((sg / n - p[1]) * blend) >> 8));
            o[2] = static_cast<std::uint8_t>(p[2] + (((sb / n - p[2]) * blend) >> 8));
        }
    }
}

// Sharpening: puts back the crispness the filtering and smoothing take out of textures (an unsharp mask of about 0.4 of the difference between a
// pixel and the mean of its four neighbours). SSE2 sixteen bytes at a time where there is SSE2, one at a time elsewhere, with identical results.
void Presenter::Sharpen(PassContext& c)
{
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    mScratch.assign(c.rgb, c.rgb + static_cast<std::size_t>(stride) * c.height);
    const std::uint8_t* src = mScratch.data();
    for (int y = 1; y < c.height - 1; ++y) {
        const std::uint8_t* row = src + y * stride;
        std::uint8_t* out = c.rgb + y * stride;
        std::ptrdiff_t i = 3;
#ifdef HOVERNET_GFX_SSE2
        const __m128i k = _mm_set1_epi16(100);
        for (; i + 16 <= stride - 3; i += 16) {
            const __m128i zero = _mm_setzero_si128();
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i));
            const __m128i n0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i - stride)), n1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i + stride));
            const __m128i n2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i - 3)), n3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + i + 3));
            __m128i lo = _mm_sub_epi16(_mm_slli_epi16(_mm_unpacklo_epi8(v, zero), 2), _mm_add_epi16(_mm_add_epi16(_mm_unpacklo_epi8(n0, zero), _mm_unpacklo_epi8(n1, zero)), _mm_add_epi16(_mm_unpacklo_epi8(n2, zero), _mm_unpacklo_epi8(n3, zero))));
            __m128i hi = _mm_sub_epi16(_mm_slli_epi16(_mm_unpackhi_epi8(v, zero), 2), _mm_add_epi16(_mm_add_epi16(_mm_unpackhi_epi8(n0, zero), _mm_unpackhi_epi8(n1, zero)), _mm_add_epi16(_mm_unpackhi_epi8(n2, zero), _mm_unpackhi_epi8(n3, zero))));
            lo = _mm_srai_epi16(_mm_mullo_epi16(_mm_srai_epi16(lo, 2), k), 8);
            hi = _mm_srai_epi16(_mm_mullo_epi16(_mm_srai_epi16(hi, 2), k), 8);
            const __m128i res = _mm_packus_epi16(_mm_add_epi16(_mm_unpacklo_epi8(v, zero), lo), _mm_add_epi16(_mm_unpackhi_epi8(v, zero), hi));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), res);
        }
#endif
        for (; i < stride - 3; ++i) {
            const int v = row[i];
            const int d = (4 * v - (row[i - stride] + row[i + stride] + row[i - 3] + row[i + 3])) >> 2;
            out[i] = static_cast<std::uint8_t>(Clamp255(v + ((d * 100) >> 8)));
        }
    }
    if (c.hudMask != nullptr) {   // the HUD stays as drawn
        for (std::size_t i = 0, n = static_cast<std::size_t>(c.width) * c.height; i < n; ++i)
            if (c.hudMask[i]) std::memcpy(c.rgb + i * 3, src + i * 3, 3);
    }
}

// Exposure: the brightest highlights roll off softly instead of clipping. (There used to be an adaptation of the gain to the picture's average
// brightness; it was removed because the picture brightened and dimmed as the view turned, which felt like a film camera.)
void Presenter::Exposure(PassContext& c)
{
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    mExposure = 1.0f;   // the eye does not adapt: the brightness of the picture must not change as the view turns; only the highlights roll off
    std::uint8_t lut[256];
    for (int v = 0; v < 256; ++v) {
        const float x = v / 255.0f * mExposure;
        const float y = x <= 0.78f ? x : 0.78f + 0.22f * (1.0f - std::exp(-(x - 0.78f) / 0.22f));   // a soft shoulder above 78%
        lut[v] = static_cast<std::uint8_t>(Clamp255(static_cast<int>(y * 255.0f + 0.5f)));
    }
    for (int y = 0; y < c.height; ++y) {
        const std::uint8_t* maskRow = c.hudMask != nullptr ? c.hudMask + static_cast<std::size_t>(y) * c.width : nullptr;
        std::uint8_t* p = c.rgb + y * stride;
        for (int x = 0; x < c.width; ++x, p += 3) {
            if (maskRow != nullptr && maskRow[x]) continue;
            p[0] = lut[p[0]]; p[1] = lut[p[1]]; p[2] = lut[p[2]];
        }
    }
}

// A dark edge on the scene beside the HUD's text and meters (the scene pixels touching a HUD pixel are darkened by 45%), so they read against a
// bright wall or sky as well as a dark one. The HUD itself is not touched.
void Presenter::HudOutline(PassContext& c)
{
    if (c.hudMask == nullptr) return;
    const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(c.width) * 3;
    mEdge.assign(static_cast<std::size_t>(c.width) * c.height, 0);
    for (int y = 0; y < c.height; ++y) {
        const std::uint8_t* row = c.hudMask + static_cast<std::size_t>(y) * c.width;
        for (int x = 0; x < c.width; ++x) {
            if (!row[x]) continue;
            static const int kDx[4] = {-1, 1, 0, 0}, kDy[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nx = x + kDx[k], ny = y + kDy[k];
                if (nx < 0 || ny < 0 || nx >= c.width || ny >= c.height) continue;
                const std::size_t at = static_cast<std::size_t>(ny) * c.width + nx;
                if (c.hudMask[at] || mEdge[at]) continue;
                mEdge[at] = 1;
                std::uint8_t* p = c.rgb + ny * stride + nx * 3;
                p[0] = static_cast<std::uint8_t>(p[0] * 141 >> 8); p[1] = static_cast<std::uint8_t>(p[1] * 141 >> 8); p[2] = static_cast<std::uint8_t>(p[2] * 141 >> 8);
            }
        }
    }
}

// Things that come off the scene as time passes: smoke from a missile, sparkles from a power-up. (Exhaust and dust behind the craft were removed.) All go through the same particle list as the impact sparks; none when Reduced motion is on.
void Presenter::Emitters(int width, int height, const Options& options, const FrameHints& hints)
{
    (void)width;
    if (options.reducedMotion || !hints.valid) return;
    const float scale = height / 480.0f;
    auto push = [&](float x, float y, float vx, float vy, float life, float size, int r, int g, int b, float strength, float gravity) {
        if (mParticles.size() >= 320) return;
        Particle p;
        p.x = x; p.y = y; p.vx = vx * scale; p.vy = vy * scale; p.maxLife = p.life = life; p.size = size * scale;
        p.r = static_cast<std::uint8_t>(r); p.g = static_cast<std::uint8_t>(g); p.b = static_cast<std::uint8_t>(b); p.strength = strength; p.gravity = gravity;
        mParticles.push_back(p);
    };
    auto jitter = [&]() { return (static_cast<int>(Random() & 1023) - 512) / 512.0f; };   // -1 .. 1
    for (int li = 0; li < hints.lightCount && li < kMaxLights; ++li) {
        const Light& light = hints.lights[li];
        if ((light.kind != kLightMissile && light.kind != kLightPowerUp && light.kind != kLightExplosion && light.kind != kLightBurst) || light.x < 500.0f || hints.projKx <= 0.0f || hints.projKz <= 0.0f) continue;
        const float px = hints.projCenterX - light.y / (light.x * hints.projKx), py = hints.projCenterY - light.z / (light.x * hints.projKz);
        if (hints.depth != nullptr) {
            const int ix = static_cast<int>(px), iy = static_cast<int>(py);
            if (ix < hints.depthX0 || iy < hints.depthY0 || ix >= hints.depthX0 + hints.depthWidth || iy >= hints.depthY0 + hints.depthHeight) continue;
        }
        const float radius = std::min(40.0f, std::max(3.0f, light.radius * 0.12f / (light.x * hints.projKx)));
        if (light.kind == kLightBurst) {   // a few sparks in the light's own colour while it is bright
            if (light.strength > 0.45f) {
                for (int k = 0; k < 3; ++k) {
                    const float angle = 6.2831853f * ((Random() & 1023) / 1023.0f), v = 30.0f + (Random() & 127) * 0.8f;
                    push(px, py, std::cos(angle) * v, std::sin(angle) * v * 0.8f - 30.0f, 0.25f + (Random() & 255) / 255.0f * 0.3f, 1.2f + (Random() & 127) / 127.0f * 1.2f, light.r, light.g, light.b, 1.0f, 260.0f);
                }
            }
            continue;
        }
        if (light.kind == kLightExplosion) {   // embers thrown out while the fire is young, then dark smoke rising from it
            if (light.strength > 0.55f) {
                for (int k = 0; k < 7; ++k) {
                    const float angle = 6.2831853f * ((Random() & 1023) / 1023.0f), v = 40.0f + (Random() & 255) * 0.9f;
                    push(px, py, std::cos(angle) * v, std::sin(angle) * v * 0.8f - 40.0f, 0.4f + (Random() & 255) / 255.0f * 0.5f, 1.4f + (Random() & 127) / 127.0f * 1.8f,
                         255, 150 + static_cast<int>(Random() & 85), 60, 1.0f, 380.0f);
                }
            }
            if (light.strength < 0.9f && (mFrame % 2) == 0) {
                push(px + jitter() * radius * 0.4f, py + jitter() * radius * 0.2f, jitter() * 12.0f, -18.0f - (Random() & 31), 0.9f, 4.0f + radius * 0.08f, 130, 85, 55, 0.4f, -10.0f);
            }
            continue;
        }
        if (light.kind == kLightMissile) {   // smoke
            push(px + jitter() * 2.0f, py + jitter() * 2.0f, jitter() * 14.0f, jitter() * 14.0f, 0.6f, 2.6f, 190, 190, 190, 0.28f, -8.0f);
        }
        else if ((mFrame % 3) == 0) {        // sparkle
            push(px + jitter() * radius * 1.4f, py + jitter() * radius * 1.4f, jitter() * 10.0f, -22.0f, 0.5f, 1.5f, 175, 255, 205, 1.0f, 0.0f);
        }
    }
}
}
