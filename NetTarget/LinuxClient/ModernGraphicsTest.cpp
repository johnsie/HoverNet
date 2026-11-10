// ModernGraphicsTest -- headless checks of the modern look's picture passes and effects (VideoServices/ModernGraphics.h).
//
// Synthetic frames with a known answer: each pass is shown to do what it says, and the rules the look keeps are shown to hold: the "off"
// preset leaves a frame untouched, the same inputs give the same picture, "Reduced motion" turns off the moving effects, and odd sizes and
// off-screen hints do not crash or write outside the frame. The scene-level checks (a real race frame) are in scripts/test-modern-graphics.sh.
#include "../VideoServices/ModernGraphics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace HoverNetGfx;

namespace
{
int gFailures = 0;

void Check(bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++gFailures;
}

using Frame = std::vector<std::uint8_t>;

Frame Solid(int w, int h, int r, int g, int b)
{
    Frame f(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < f.size(); i += 3) { f[i] = static_cast<std::uint8_t>(r); f[i + 1] = static_cast<std::uint8_t>(g); f[i + 2] = static_cast<std::uint8_t>(b); }
    return f;
}

int At(const Frame& f, int w, int x, int y, int channel = 1) { return f[(static_cast<std::size_t>(y) * w + x) * 3 + channel]; }

// The look as the older checks below were written for it: the later additions are switched off here and each has its own check.
Options Modern(Preset preset = Preset::eModern)
{
    Options o = OptionsForPreset(preset);
    o.atmosphere = o.occlusion = o.craftShading = o.reflections = o.halos = o.edgeAA = o.hudOutline = o.sharpen = o.exposure = o.linearLight = o.adaptive = false;
    return o;
}

FrameHints Hints(int w, int h)
{
    FrameHints hints;
    hints.valid = true;
    hints.craftVisible = true;
    hints.craftX = w / 2;
    hints.craftY = h * 3 / 4;
    hints.craftRadius = h / 8;
    hints.speed = 0.9f;
    return hints;
}

int MaxStep(const Frame& f, int w, int y)   // the biggest jump between neighbouring pixels along a row
{
    int worst = 0;
    for (int x = 1; x < w; ++x) { const int d = std::abs(At(f, w, x, y) - At(f, w, x - 1, y)); if (d > worst) worst = d; }
    return worst;
}
}

// A digest of the picture the presets make from a synthetic frame (textured, with soft ramps, edges and bright patches). The test is built twice,
// once with the SSE2 de-banding and once without (HOVERNET_GFX_NO_SIMD); the two digests must be identical (see CMakeLists.txt).
int Digest()
{
    for (const int size : {320, 641}) {
        const int w = size, h = size * 3 / 4 + 1;
        Frame base(static_cast<std::size_t>(w) * h * 3);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &base[(static_cast<std::size_t>(y) * w + x) * 3];
            const int ramp = 30 + (x / 9) * 3 + (y / 13) * 2;   // slow steps: bands
            p[0] = static_cast<std::uint8_t>(ramp + ((x * 7 + y * 3) % 5 == 0 ? 40 : 0));
            p[1] = static_cast<std::uint8_t>(ramp + 10 + ((x ^ y) % 17 == 0 ? 60 : 0));
            p[2] = static_cast<std::uint8_t>((x > y ? 220 : 20) + ((x / 40 + y / 40) % 2) * 8);
            if (x > w / 2 && x < w / 2 + 20 && y > h / 3 && y < h / 3 + 20) p[0] = p[1] = p[2] = 255;
        }
        for (const Preset preset : {Preset::eModernLite, Preset::eModern, Preset::eModernHigh}) {
            Frame f = base;
            FrameHints hints = Hints(w, h);
            hints.impact = 0.6f;
            Presenter p;
            for (int frame = 0; frame < 3; ++frame) { p.Process(f.data(), w, h, OptionsForPreset(preset), hints); hints.impact = 0.0f; }
            unsigned long long hash = 1469598103934665603ULL;
            for (const std::uint8_t b : f) { hash ^= b; hash *= 1099511628211ULL; }
            std::printf("digest %dx%d preset %d %016llx\n", w, h, static_cast<int>(preset), hash);
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "digest") == 0) return Digest();
    const int W = 320, H = 240;

    // 0. Automatic graphics: a ladder of levels that only ever takes features away from what the player allows.
    {
        const Options a = OptionsForPreset(Preset::eAuto);
        Check(a.enabled && a.automatic && a.adaptive && a.quality == 2, "the Automatic preset is on, automatic and adaptive, at top quality");
        Options none = a; none.shadows = false; none.bloom = false;
        bool monotone = true, neverMoreThanAllowed = true;
        Options previous = Presenter::OptionsForLevel(a, 0);
        int features0 = 0;
        auto count = [](const Options& o) { return int(o.shadows) + int(o.lighting) + int(o.halos) + int(o.atmosphere) + int(o.occlusion) + int(o.deband) + int(o.exposure) + int(o.trueColour) + int(o.reflections) + int(o.edgeAA) + int(o.smoothing) + int(o.bloom) + int(o.sharpen) + int(o.craftShading) + int(o.linearLight) + o.quality; };
        features0 = count(previous);
        for (int level = 1; level <= Presenter::kMaxLevel; ++level) {
            const Options o = Presenter::OptionsForLevel(a, level);
            if (count(o) < count(previous)) monotone = false;
            const Options n = Presenter::OptionsForLevel(none, level);
            if ((n.shadows && !none.shadows) || (n.bloom && !none.bloom)) neverMoreThanAllowed = false;
            previous = o;
        }
        Check(monotone, "each level has at least what the level below has");
        Check(neverMoreThanAllowed, "a feature the player switched off is never switched on by a level");
        Check(Presenter::OptionsForLevel(a, 0).grade && Presenter::OptionsForLevel(a, 0).effects && !Presenter::OptionsForLevel(a, 0).shadows && !Presenter::OptionsForLevel(a, 0).lighting && Presenter::OptionsForLevel(a, 0).quality == 0, "level 0 keeps the grade and the effects and nothing costly");
        Check(Presenter::OptionsForLevel(a, 1).shadows && !Presenter::OptionsForLevel(a, 1).lighting, "level 1 adds the craft shadows");
        Check(Presenter::OptionsForLevel(a, 2).lighting && Presenter::OptionsForLevel(a, 2).halos && !Presenter::OptionsForLevel(a, 2).trueColour, "level 2 adds the lights");
        Check(Presenter::OptionsForLevel(a, 3).quality == 1 && Presenter::OptionsForLevel(a, 3).atmosphere && !Presenter::OptionsForLevel(a, 3).bloom, "level 3 adds haze and de-banding");
        Check(Presenter::OptionsForLevel(a, 4).trueColour && Presenter::OptionsForLevel(a, 4).edgeAA && !Presenter::OptionsForLevel(a, 4).bloom, "level 4 adds true-colour textures and edge smoothing");
        Check(Presenter::OptionsForLevel(a, 5).bloom && Presenter::OptionsForLevel(a, 5).smoothing && Presenter::OptionsForLevel(a, 5).quality == 2, "level 5 has everything");
        Check(count(Presenter::OptionsForLevel(a, 9)) == count(Presenter::OptionsForLevel(a, 5)) && count(Presenter::OptionsForLevel(a, -3)) == features0, "levels outside 0 to 5 are held to the ends");
        // The governor, on its own: it starts at the level it is given and stays there while frames are fine; the effective options follow the level.
        Options start = a; start.startLevel = 2; start.adaptive = true;
        Presenter p;
        std::vector<std::uint8_t> frame(static_cast<std::size_t>(W) * H * 3, 60);
        FrameHints h;
        p.Process(frame.data(), W, H, start, h);
        Check(p.Level() == 2 && p.EffectiveOptions().lighting && !p.EffectiveOptions().trueColour, "Automatic starts at the level it is given and the effective options follow it");
        Options manual = a; manual.automatic = false;
        Presenter q;
        q.Process(frame.data(), W, H, manual, h);
        Check(q.EffectiveOptions().shadows == manual.shadows && q.EffectiveOptions().bloom == manual.bloom, "without Automatic the player's options are used as they are");
    }

    // 1. The "off" preset does nothing at all.
    {
        Frame f(static_cast<std::size_t>(W) * H * 3);
        for (std::size_t i = 0; i < f.size(); ++i) f[i] = static_cast<std::uint8_t>((i * 37) ^ (i >> 3));
        const Frame original = f;
        Presenter p;
        FrameHints hints = Hints(W, H);
        hints.impact = 1.0f;
        p.Process(f.data(), W, H, OptionsForPreset(Preset::eOff), hints);
        Check(f == original, "the off preset leaves the frame byte-for-byte unchanged");
        Check(p.ParticleCount() == 0, "the off preset makes no particles");
    }

    // 2. Repeatable: the same inputs from a fresh presenter give the same picture, for every preset.
    for (const Preset preset : {Preset::eModern, Preset::eModernHigh, Preset::eModernLite}) {
        Frame base(static_cast<std::size_t>(W) * H * 3);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { std::uint8_t* p = &base[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = static_cast<std::uint8_t>(x); p[1] = static_cast<std::uint8_t>(y); p[2] = static_cast<std::uint8_t>((x ^ y) & 255); }
        Frame a = base, b = base;
        FrameHints hints = Hints(W, H);
        hints.impact = 0.8f;
        hints.trailCount = 6;
        for (int i = 0; i < 6; ++i) { hints.trailX[i] = W / 2; hints.trailY[i] = H * 3 / 4 + (6 - i) * 5; }
        Presenter one, two;
        for (int frame = 0; frame < 5; ++frame) { one.Process(a.data(), W, H, Modern(preset), hints); two.Process(b.data(), W, H, Modern(preset), hints); hints.impact = 0.0f; }
        Check(a == b, preset == Preset::eModern ? "modern: same inputs, same picture" : preset == Preset::eModernHigh ? "modern high: same inputs, same picture" : "modern light: same inputs, same picture");
        Check(a != base, "the look changes the picture");
    }

    // 3. De-banding: a staircase gradient has smaller jumps afterwards, and the picture stays close to the original.
    {
        Frame f = Solid(W, H, 0, 0, 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { std::uint8_t* p = &f[(static_cast<std::size_t>(y) * W + x) * 3]; const int v = 40 + (x / 16) * 6; p[0] = p[1] = p[2] = static_cast<std::uint8_t>(v); }
        const Frame original = f;
        Options options = Modern();
        options.grade = false; options.effects = false; options.bloom = false;
        FrameHints none;
        Presenter p;
        p.Process(f.data(), W, H, options, none);
        const int before = MaxStep(original, W, H / 2), after = MaxStep(f, W, H / 2);
        // Dither adds up to +-2 levels, so compare the steps the palette produced (6) with the stepped result: the step edge is mixed with its neighbours.
        int stepEdge = 0;   // the jump across the middle of the first band edge, ignoring the dither
        for (int x = 14; x < 18; ++x) stepEdge = std::max(stepEdge, std::abs(At(f, W, x, H / 2) - At(f, W, x - 1, H / 2)));
        Check(before == 6, "the staircase starts with steps of 6 levels");
        Check(stepEdge <= 6 + 2 && after <= 6 + 4, "de-banding keeps the steps within the dither of the original");
        long drift = 0;
        for (std::size_t i = 0; i < f.size(); ++i) drift += std::abs(static_cast<int>(f[i]) - static_cast<int>(original[i]));
        Check(drift / static_cast<long>(f.size()) <= 3, "de-banding keeps the picture within a few levels of the original");
        // The dither breaks up flat runs: a flat band is no longer all one value.
        bool varies = false;
        for (int x = 1; x < 14 && !varies; ++x) varies = At(f, W, x, H / 2) != At(f, W, 0, H / 2);
        Check(varies, "dither breaks up a flat band");
    }

    // 4. Edge smoothing (high quality): a black and white diagonal gets in-between greys; text-sized detail is kept (the edge stays an edge).
    {
        Frame f = Solid(W, H, 0, 0, 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (x > y) { std::uint8_t* p = &f[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = p[1] = p[2] = 255; }
        int greysBefore = 0, greysAfter = 0;
        for (int y = 20; y < 200; ++y) for (int x = 20; x < 200; ++x) { const int v = At(f, W, x, y); if (v > 10 && v < 245) ++greysBefore; }
        Options options = Modern(Preset::eModernHigh);
        options.grade = false; options.effects = false; options.bloom = false;
        Presenter p;
        p.Process(f.data(), W, H, options, FrameHints());
        for (int y = 20; y < 200; ++y) for (int x = 20; x < 200; ++x) { const int v = At(f, W, x, y); if (v > 10 && v < 245) ++greysAfter; }
        Check(greysBefore == 0 && greysAfter > 100, "edge smoothing adds in-between greys along a stair-stepped edge");
        Check(At(f, W, 250, 20) > 200 && At(f, W, 20, 200) < 55, "the two sides of the edge stay black and white");
        Frame g = Solid(W, H, 0, 0, 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (x > y) { std::uint8_t* q = &g[(static_cast<std::size_t>(y) * W + x) * 3]; q[0] = q[1] = q[2] = 255; }
        Options modern = Modern(Preset::eModern);
        modern.grade = false; modern.effects = false;
        Presenter p2;
        p2.Process(g.data(), W, H, modern, FrameHints());
        int greys = 0;
        for (int y = 20; y < 200; ++y) for (int x = 20; x < 200; ++x) { const int v = At(g, W, x, y); if (v > 10 && v < 245) ++greys; }
        Check(greys == 0, "plain modern (not high) does not smooth edges");
    }

    // 5. Bloom (high quality): light spills beside a bright patch on a dark picture; nothing changes on a picture with nothing bright.
    {
        Frame f = Solid(W, H, 10, 10, 10);
        for (int y = 100; y < 116; ++y) for (int x = 150; x < 166; ++x) { std::uint8_t* p = &f[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = p[1] = p[2] = 255; }
        Options options = Modern(Preset::eModernHigh);
        options.grade = false; options.effects = false; options.deband = false; options.smoothing = false;
        Presenter p;
        const int beside = At(f, W, 172, 108);
        p.Process(f.data(), W, H, options, FrameHints());
        Check(At(f, W, 172, 108) > beside + 5, "bloom lights the dark area beside a bright patch");
        Check(At(f, W, 20, 20) == 10 && At(f, W, 300, 220) == 10, "bloom leaves areas far from the light alone");
        Frame dark = Solid(W, H, 60, 60, 60);
        const Frame darkOriginal = dark;
        Presenter q;
        q.Process(dark.data(), W, H, options, FrameHints());
        Check(dark == darkOriginal, "bloom does nothing when nothing is bright");
    }

    // 6. Grade: the palette grade keeps black and white, adds colour; the vignette darkens the corners and not the middle.
    {
        std::uint8_t palette[768] = {}, graded[768] = {};
        palette[3] = palette[4] = palette[5] = 255;                      // 1: white
        palette[6] = 180; palette[7] = 90; palette[8] = 90;               // 2: a muted red
        Presenter::GradePalette(palette, graded);
        Check(graded[0] == 0 && graded[1] == 0 && graded[2] == 0, "grade keeps black black");
        Check(graded[3] >= 250 && graded[4] >= 250 && graded[5] >= 250, "grade keeps white white");
        {
            auto saturation = [](const std::uint8_t* c) { const int mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2])); return mx ? static_cast<double>(mx - mn) / mx : 0.0; };
            const double before = saturation(palette + 6), after = saturation(graded + 6);
            char what[120]; std::snprintf(what, sizeof what, "the grade keeps colour natural: less saturated than the palette (%.0f%% against %.0f%%), not more", after * 100, before * 100);
            Check(after < before && after > before * 0.7, what);
        }
        Frame f = Solid(W, H, 128, 128, 128);
        Options options = Modern();
        options.deband = false; options.effects = false;
        Presenter p;
        p.Process(f.data(), W, H, options, FrameHints());
        Check(At(f, W, W / 2, H / 2) == 128, "the vignette leaves the middle alone");
        Check(At(f, W, 0, 0) < 128 - 20 && At(f, W, W - 1, H - 1) < 128 - 20, "the vignette darkens the corners");
        Check(At(f, W, W / 2, 0) <= At(f, W, W / 2, H / 2) && At(f, W, 0, H / 2) < At(f, W, W / 2, H / 2), "the vignette grows towards the edges");
    }

    // 7. Effects: the engine glow brightens the craft's place; the trail lights where the craft was; they never darken anything.
    {
        Frame base = Solid(W, H, 30, 30, 30);
        FrameHints hints = Hints(W, H);
        hints.trailCount = 8;
        for (int i = 0; i < 8; ++i) { hints.trailX[i] = W / 2; hints.trailY[i] = H * 3 / 4 + (8 - i) * 6; }
        Options options = Modern();
        options.grade = false; options.deband = false;
        Frame f = base;
        Presenter p;
        p.Process(f.data(), W, H, options, hints);
        Check(At(f, W, hints.craftX, hints.craftY + 5, 2) > 30 + 20, "the engine glow brightens the craft's place (blue most)");
        Check(At(f, W, hints.craftX, hints.craftY + 45, 2) == 30 && At(f, W, hints.craftX + 60, hints.craftY + 30, 2) == 30, "nothing is drawn along where the craft was (no trail), only the engine glow at the craft");
        bool darker = false;
        for (std::size_t i = 0; i < f.size(); ++i) darker = darker || f[i] < base[i];
        Check(!darker, "light effects only ever add light");
        FrameHints cockpit = hints;
        cockpit.cockpit = true;
        Frame g = base;
        Presenter q;
        options.effects = true;
        q.Process(g.data(), W, H, options, cockpit);
        Check(At(g, W, cockpit.craftX, cockpit.craftY + 5, 2) == 30, "no craft effects in the cockpit view (the craft is not on screen)");
        Frame h = base;
        FrameHints menu;   // valid = false: no race view was drawn
        Presenter r;
        r.Process(h.data(), W, H, options, menu);
        Check(h == base, "without a race frame (hints invalid) only the picture passes may touch the frame");
    }

    // 8. Reduced motion: no sparks and no speed lines; the view is still improved.
    {
        FrameHints hints = Hints(W, H);
        hints.impact = 1.0f;
        hints.speed = 1.3f;
        Options moving = Modern();
        Options still = Modern();
        still.reducedMotion = true;
        Frame a = Solid(W, H, 60, 60, 60), b = a;
        Presenter pm, ps;
        pm.Process(a.data(), W, H, moving, hints);
        ps.Process(b.data(), W, H, still, hints);
        Check(pm.ParticleCount() > 0, "an impact makes sparks");
        Check(ps.ParticleCount() == 0, "reduced motion makes no sparks");
    }

    // 8b. The HUD is left exactly as drawn by the passes that smooth (de-banding, smoothing).
    {
        Frame c = Solid(W, H, 60, 60, 60);
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H, 0);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if ((x / 4) & 1) { std::uint8_t* p = &c[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = p[1] = p[2] = 200; }
        for (int y = 10; y < 60; ++y) for (int x = 0; x < 40; ++x) mask[static_cast<std::size_t>(y) * W + x] = 1;   // a HUD panel at the top left
        Frame f = c;
        FrameHints hints = Hints(W, H);
        hints.speed = 1.3f;
        hints.hudMask = mask.data(); hints.hudMaskWidth = W; hints.hudMaskHeight = H;
        Options options = Modern(Preset::eModernHigh);
        options.grade = false; options.bloom = false;
        Presenter p;
        p.Process(f.data(), W, H, options, hints);
        bool same = true;
        for (int y = 10; y < 60; ++y) for (int x = 0; x < 40; ++x) for (int k = 0; k < 3; ++k) same = same && At(f, W, x, y, k) == At(c, W, x, y, k);
        Check(same, "HUD pixels come out exactly as drawn (no de-banding or smoothing)");
        Frame g = c;
        FrameHints noMask = hints;
        noMask.hudMask = nullptr;
        Presenter q;
        q.Process(g.data(), W, H, options, noMask);
        bool changed = false;
        for (int y = 10; y < 60 && !changed; ++y) for (int x = 0; x < 40 && !changed; ++x) changed = At(g, W, x, y) != At(c, W, x, y);
        Check(changed, "the same area is changed when it is not marked as HUD");
    }

    // 8c. Lighting from the depth buffer: haze mixes far surfaces towards the sky colour, a light brightens the surfaces near it by their 3D
    //     distance, sky and HUD pixels are never lit, and without depth nothing happens.
    {
        // A viewport filling the frame: the top third is sky (nothing drawn), below it a wall, nearer at the left (6 m) and farther at the right (110 m).
        std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 0xFFFF);
        Frame base = Solid(W, H, 120, 150, 200);   // sky
        for (int y = H / 3; y < H; ++y) for (int x = 0; x < W; ++x) {
            const int metres = 6 + (104 * x) / W;
            depth[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>(metres * 250);   // 4 mm units
            std::uint8_t* p = &base[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = 90; p[1] = 90; p[2] = 90;
        }
        FrameHints hints;
        hints.valid = true;
        hints.depth = depth.data(); hints.depthX0 = 0; hints.depthY0 = 0; hints.depthWidth = W; hints.depthHeight = H;
        hints.projKx = 0.0004f; hints.projKz = 0.0004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
        Options options = Modern();
        options.grade = false; options.deband = false; options.effects = false;
        Frame hazy = base;
        Presenter p;
        p.Process(hazy.data(), W, H, options, hints);
        Check(At(hazy, W, 4, H - 10, 0) == 90, "haze leaves a near surface alone");
        Check(At(hazy, W, W - 4, H - 10, 2) > 90 + 25, "haze mixes a far surface towards the sky colour (the sky is bluer)");
        Check(At(hazy, W, 100, 10, 0) == 120, "haze leaves the sky alone");
        Options lite = Modern(Preset::eModernLite);
        lite.grade = false; lite.effects = false;
        Frame lit = base;
        FrameHints withLight = hints;
        withLight.lightCount = 1;
        withLight.lights[0].x = 10000.0f; withLight.lights[0].y = 0.0f; withLight.lights[0].z = -1000.0f;   // 10 m ahead, a metre below the view
        withLight.lights[0].radius = 6000.0f; withLight.lights[0].r = 255; withLight.lights[0].g = 200; withLight.lights[0].b = 100; withLight.lights[0].strength = 0.8f;
        Presenter q;
        q.Process(lit.data(), W, H, lite, withLight);
        int brightest = 0, brightestX = 0;
        for (int x = 0; x < W; ++x) { const int v = At(lit, W, x, H - 10, 0); if (v > brightest) { brightest = v; brightestX = x; } }
        Check(brightest > 90 + 15, "a light brightens the surface near it");
        Check(brightestX < W / 4, "the pool of light is where the wall is about as far as the light (10 m, the left part of this wall)");
        Check(At(lit, W, W / 2, H - 10, 0) == 90 && At(lit, W, W - 4, H - 10, 0) == 90, "surfaces outside the light's radius (farther than 16 m) are not lit");
        Check(At(lit, W, 100, 10, 0) == 120, "the sky is not lit");
        Check(At(lit, W, brightestX, H - 10, 0) > At(lit, W, brightestX, H - 10, 2) - 100 && At(lit, W, brightestX, H - 10, 0) > At(lit, W, brightestX, H - 10, 2), "a warm light adds more red than blue");
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H, 0);
        for (int y = 0; y < H; ++y) for (int x = std::max(0, brightestX - 20); x < std::min(W, brightestX + 20); ++x) mask[static_cast<std::size_t>(y) * W + x] = 1;
        Frame masked = base;
        FrameHints maskedHints = withLight;
        maskedHints.hudMask = mask.data(); maskedHints.hudMaskWidth = W; maskedHints.hudMaskHeight = H;
        Presenter r;
        r.Process(masked.data(), W, H, lite, maskedHints);
        Check(At(masked, W, brightestX, H - 10, 0) == 90, "HUD pixels are not lit");
        Frame none = base;
        FrameHints noDepth = withLight;
        noDepth.depth = nullptr;
        Presenter t;
        t.Process(none.data(), W, H, lite, noDepth);
        Check(none == base, "without a depth buffer lighting does nothing");
        Options off = lite;
        off.lighting = false;
        Frame unlit = base;
        Presenter u;
        u.Process(unlit.data(), W, H, off, withLight);
        Check(unlit == base, "lighting can be switched off");
    }

    // 8d. Contact shading: a surface just behind something nearer is darkened a little; a flat wall, and a surface far behind (a silhouette), are not.
    {
        std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 5000);   // a flat wall 20 m away
        for (int y = 80; y < 160; ++y) for (int x = 150; x < 190; ++x) depth[static_cast<std::size_t>(y) * W + x] = 4800;   // a box 80 cm in front of it
        for (int y = 200; y < 230; ++y) for (int x = 20; x < 60; ++x) depth[static_cast<std::size_t>(y) * W + x] = 3000;      // a box 8 m in front of it
        Frame base = Solid(W, H, 150, 150, 150);
        FrameHints hints;
        hints.valid = true;
        hints.depth = depth.data(); hints.depthWidth = W; hints.depthHeight = H;
        hints.projKx = 0.0004f; hints.projKz = 0.0004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
        Options options = Modern();
        options.grade = false; options.deband = false; options.effects = false;
        Frame f = base;
        Presenter p;
        p.Process(f.data(), W, H, options, hints);
        Check(At(f, W, 145, 120, 0) < 150 - 3, "the wall beside a box standing 80 cm in front of it is darkened");
        Check(At(f, W, 100, 40, 0) == 150, "a flat wall far from anything is not darkened");
        Check(At(f, W, 15, 215, 0) == 150, "a surface far behind something (a silhouette, not a contact) is not darkened");
        Options lite = Modern(Preset::eModernLite);
        lite.grade = false; lite.effects = false;
        Frame g = base;
        Presenter q;
        q.Process(g.data(), W, H, lite, hints);
        Check(g == base, "the light preset has no contact shading");
    }

    // 8e. Surface orientation: the same surface, tilted to face a light, is lit more than when it is tilted away from it; and a surface seen
    //     almost straight on gets a glint where the eye, the light and the normal line up.
    {
        auto wall = [&](float slope, std::vector<std::uint16_t>& depth) {   // a wall 10 m ahead at the centre, tilted by slope (sideways metres per metre)
            const float kx = 0.0004f;
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const float X = 10000.0f / (1.0f + slope * (W / 2 - x) * kx);
                depth[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>(X / 4.0f);
            }
        };
        std::vector<std::uint16_t> facing(static_cast<std::size_t>(W) * H), away(static_cast<std::size_t>(W) * H);
        wall(-0.9f, facing);   // normal has a +y (left) part: faces a light on the left
        wall(+0.9f, away);
        Frame base = Solid(W, H, 100, 100, 100);
        FrameHints hints;
        hints.valid = true;
        hints.depthWidth = W; hints.depthHeight = H;
        hints.projKx = 0.0004f; hints.projKz = 0.0004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
        hints.lightCount = 1;
        hints.lights[0].x = 8000.0f; hints.lights[0].y = 4000.0f; hints.lights[0].z = 0.0f;   // 8 m ahead, 4 m to the left
        hints.lights[0].radius = 9000.0f; hints.lights[0].r = hints.lights[0].g = hints.lights[0].b = 255; hints.lights[0].strength = 0.9f;
        Options options = Modern(Preset::eModernLite);
        options.grade = false; options.effects = false;
        Frame a = base, b = base;
        FrameHints h1 = hints, h2 = hints;
        h1.depth = facing.data(); h2.depth = away.data();
        Presenter p1, p2;
        p1.Process(a.data(), W, H, options, h1);
        p2.Process(b.data(), W, H, options, h2);
        Check(At(a, W, W / 2, H / 2, 0) > At(b, W, W / 2, H / 2, 0) + 4, "a surface tilted towards the light is lit more than one tilted away");
        // A flat wall straight ahead, a light just in front of the eye: the middle of the wall gets a glint, the sides do not.
        std::vector<std::uint16_t> flat(static_cast<std::size_t>(W) * H, 2500);   // 10 m
        FrameHints h3 = hints;
        h3.projKx = 0.004f; h3.projKz = 0.004f;   // a wide view (about 6 m either side at 10 m), so the glint can fall off towards the sides
        h3.depth = flat.data();
        h3.lights[0].x = 3000.0f; h3.lights[0].y = 0.0f; h3.lights[0].z = 0.0f; h3.lights[0].radius = 12000.0f;
        Frame c = base;
        Presenter p3;
        p3.Process(c.data(), W, H, options, h3);
        Check(At(c, W, W / 2, H / 2, 1) > At(c, W, W / 2 - 150, H / 2, 1) + 3, "a glint where the eye, the light and the surface normal line up");
    }

    // 8f. The pipeline: a pass added with AddPass runs where it was asked to, is skipped when it does not want the frame, can use the shared
    //     normals, and profiling records the passes that ran, in order.
    {
        std::vector<std::string> order;
        Presenter p;
        p.AddPass(std::unique_ptr<Pass>(new FunctionPass("first-custom", [](const Options&, const FrameHints&) { return true; },
            [&order](PassContext& c) { order.push_back("first-custom"); c.rgb[0] = 11; })), "filter");
        p.AddPass(std::unique_ptr<Pass>(new FunctionPass("last-custom", [](const Options&, const FrameHints&) { return true; },
            [&order](PassContext&) { order.push_back("last-custom"); })));
        p.AddPass(std::unique_ptr<Pass>(new FunctionPass("never", [](const Options&, const FrameHints&) { return false; },
            [&order](PassContext&) { order.push_back("never"); })));
        int normalsSeen = -1;
        std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 2500);
        p.AddPass(std::unique_ptr<Pass>(new FunctionPass("normals", [](const Options&, const FrameHints&) { return true; },
            [&normalsSeen](PassContext& c) {
                int gw = 0, gh = 0;
                const std::int8_t* n = c.Normals(gw, gh);
                normalsSeen = n != nullptr && gw == (c.width + 3) / 4 && gh == (c.height + 3) / 4 ? n[(gh / 2 * gw + gw / 2) * 3] : -2;   // x of a block's normal in the middle
            })));
        Frame f = Solid(W, H, 80, 80, 80);
        FrameHints hints = Hints(W, H);
        hints.depth = depth.data(); hints.depthWidth = W; hints.depthHeight = H;
        hints.projKx = 0.0004f; hints.projKz = 0.0004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
        Options options = Modern();
        p.SetProfiling(true);
        p.Process(f.data(), W, H, options, hints);
        Check(!order.empty() && order.front() == "first-custom" && order.back() == "last-custom", "a pass added before a built-in one runs first; one added without a position runs last");
        Check(std::find(order.begin(), order.end(), "never") == order.end(), "a pass that does not want the frame is skipped");
        Check(normalsSeen < -100, "a pass can ask for the surface normals (a wall facing the camera points back at it: x is strongly negative)");
        const std::vector<PassTime>& times = p.LastPassTimes();
        bool timesOk = times.size() >= 5;   // first-custom, filter, lighting, normals, last-custom at least, plus the rest of the built-ins that ran
        for (const PassTime& t : times) timesOk = timesOk && t.name != nullptr && t.milliseconds >= 0.0;
        Check(timesOk && std::strcmp(times.front().name, "first-custom") == 0 && std::strcmp(times[1].name, "filter") == 0, "profiling records each pass that ran, in order, with its time");
        bool sawLighting = false, sawNever = false;
        for (const PassTime& t : times) { sawLighting = sawLighting || std::strcmp(t.name, "lighting") == 0; sawNever = sawNever || std::strcmp(t.name, "never") == 0; }
        Check(sawLighting && !sawNever, "profiling lists the built-in lighting pass and not the skipped one");
        Frame h = Solid(W, H, 80, 80, 80);
        Presenter r;
        r.AddPass(std::unique_ptr<Pass>(new FunctionPass("paint", [](const Options&, const FrameHints&) { return true; },
            [](PassContext& c) { for (int i = 0; i < 30; ++i) c.rgb[i] = 200; })));
        Options bare = Modern(Preset::eModernLite);
        bare.grade = false; bare.effects = false; bare.lighting = false;
        r.Process(h.data(), W, H, bare, FrameHints());
        Check(h[0] == 200 && h[29] == 200 && h[30] == 80, "a custom pass can change the frame");
        Presenter q;
        Frame g = Solid(W, H, 80, 80, 80);
        q.Process(g.data(), W, H, Modern(Preset::eOff), hints);
        Check(g == Solid(W, H, 80, 80, 80), "the off preset still runs no passes");
    }

    // 8g. Surface tags from the rasteriser: a pixel tagged floor has a normal straight up and one tagged ceiling straight down, whatever the depth
    //     buffer says; untagged surfaces still get their normal from the depth.
    {
        std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 2500);   // a wall facing the camera, as far as the depth buffer knows
        std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagWall);
        for (int y = 0; y < H / 2; ++y) for (int x = 0; x < W; ++x) tags[static_cast<std::size_t>(y) * W + x] = kTagCeiling;
        for (int y = H / 2; y < H; ++y) for (int x = 0; x < W / 2; ++x) tags[static_cast<std::size_t>(y) * W + x] = kTagFloor;
        FrameHints hints = Hints(W, H);
        hints.depth = depth.data(); hints.depthWidth = W; hints.depthHeight = H;
        hints.projKx = 0.0004f; hints.projKz = 0.0004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
        int ceilingZ = 0, floorZ = 0, wallX = 0;
        Presenter p;
        p.AddPass(std::unique_ptr<Pass>(new FunctionPass("probe", [](const Options&, const FrameHints&) { return true; },
            [&](PassContext& c) {
                int gw = 0, gh = 0;
                const std::int8_t* n = c.Normals(gw, gh);
                if (n == nullptr) return;
                auto at = [&](int x, int y, int k) { return static_cast<int>(n[((y / 4) * gw + x / 4) * 3 + k]); };
                ceilingZ = at(W / 2, H / 4, 2); floorZ = at(W / 4, H * 3 / 4, 2); wallX = at(W * 3 / 4, H * 3 / 4, 0);
            })));
        Frame f = Solid(W, H, 80, 80, 80);
        hints.tags = tags.data();
        p.Process(f.data(), W, H, Modern(), hints);
        Check(floorZ == 127, "a pixel the rasteriser tagged floor has a normal straight up");
        Check(ceilingZ == -127, "a pixel tagged ceiling has a normal straight down");
        Check(wallX < -100, "a pixel tagged wall gets its normal from the depth buffer (a wall facing the camera)");
    }

    // ---- The second round of graphics work: one group of checks for each addition. "Bare" turns everything off so a check sees only its own feature.
    {
        auto bare = [](int quality) {
            Options o;
            o.enabled = true; o.quality = quality;
            o.deband = o.smoothing = o.bloom = o.grade = o.effects = o.lighting = o.shadows = o.atmosphere = o.occlusion = o.craftShading = false;
            o.reflections = o.halos = o.edgeAA = o.hudOutline = o.sharpen = o.exposure = o.linearLight = o.trueColour = o.adaptive = false;
            return o;
        };
        // A scene to test on: a wall 10 m ahead tagged "wall", the viewport filling the frame, a wide view (about 6 m either side at 10 m).
        std::vector<std::uint16_t> wallDepth(static_cast<std::size_t>(W) * H, 2500);
        std::vector<std::uint8_t> wallTags(static_cast<std::size_t>(W) * H, kTagWall);
        auto sceneHints = [&](const std::vector<std::uint16_t>& depth, const std::vector<std::uint8_t>* tags) {
            FrameHints hints;
            hints.valid = true;
            hints.depth = depth.data(); hints.depthWidth = W; hints.depthHeight = H;
            hints.tags = tags != nullptr ? tags->data() : nullptr;
            hints.projKx = 0.004f; hints.projKz = 0.004f; hints.projCenterX = W / 2; hints.projCenterY = H / 2;
            return hints;
        };

        // Brightness (through the palette): up lifts the middle, down lowers it, black and white stay put.
        {
            std::uint8_t pal[768] = {}, up[768], down[768], normal[768];
            for (int i = 0; i < 256; ++i) pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = static_cast<std::uint8_t>(i);
            Presenter::GradePalette(pal, normal, 1.0f); Presenter::GradePalette(pal, up, 1.3f); Presenter::GradePalette(pal, down, 0.8f);
            Check(up[100 * 3] > normal[100 * 3] + 6 && down[100 * 3] < normal[100 * 3] - 5, "brightness up lifts the middle tones and down lowers them");
            Check(up[0] == 0 && up[255 * 3] >= 250 && down[255 * 3] >= 245, "brightness keeps black and white where they are");
            Check(normal[30 * 3 + 2] >= normal[30 * 3] && normal[240 * 3] >= normal[240 * 3 + 2], "the grade is, very slightly, cool in the shadows and warm in the highlights");
        }
        // 8x8 dither: a flat band is not repeated every four pixels.
        {
            Frame f = Solid(W, H, 120, 120, 120);
            Options o = bare(1); o.deband = true;
            Presenter p; p.Process(f.data(), W, H, o, FrameHints());
            bool breaksFour = false;
            for (int x = 8; x < 40; ++x) breaksFour = breaksFour || At(f, W, x, 20) != At(f, W, x + 4, 20);
            Check(breaksFour, "the dither pattern does not repeat every four pixels");
        }
        // The vignette is steady: it does not follow the speed (a picture that changes as you go faster or turn feels like a film camera).
        {
            Frame a = Solid(W, H, 128, 128, 128), b = a;
            Options o = bare(0); o.grade = true;
            FrameHints slow, fast; slow.valid = fast.valid = true; slow.speed = 0.0f; fast.speed = 1.2f;
            Presenter p, q; p.Process(a.data(), W, H, o, slow); q.Process(b.data(), W, H, o, fast);
            Check(a == b, "the vignette does not change with the speed");
            Check(At(a, W, 0, 0) < 128 - 5 && At(a, W, W / 2, H / 2) == 128, "it still darkens the corners and leaves the middle alone");
        }
        // Wall normals are horizontal even when the depth buffer says otherwise.
        {
            std::vector<std::uint16_t> slope(static_cast<std::size_t>(W) * H);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) slope[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>(2000 + y * 4);   // deeper lower down: it looks like a floor
            FrameHints hints = sceneHints(slope, &wallTags);
            int nz = 99;
            Presenter p;
            p.AddPass(std::unique_ptr<Pass>(new FunctionPass("probe", [](const Options&, const FrameHints&) { return true; }, [&](PassContext& c) { int gw, gh; const std::int8_t* n = c.Normals(gw, gh); if (n) nz = n[((H / 2 / 4) * gw + W / 2 / 4) * 3 + 2]; })));
            Frame f = Solid(W, H, 80, 80, 80);
            p.Process(f.data(), W, H, bare(1), hints);
            Check(nz == 0, "a surface the rasteriser tagged wall has a horizontal normal");
        }
        // A spotlight lights along its axis and not far off it.
        {
            FrameHints hints = sceneHints(wallDepth, &wallTags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.x = 3000; l.y = 0; l.z = 0; l.radius = 16000; l.r = l.g = l.b = 255; l.strength = 0.9f; l.dirx = 1; l.diry = 0; l.dirz = 0; l.coneCos = 0.95f;
            Options o = bare(0); o.lighting = true;
            Frame f = Solid(W, H, 90, 90, 90);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, W / 2, H / 2) > 90 + 12, "a spotlight lights the surface on its axis");
            Check(At(f, W, W / 2 - 150, H / 2) < 90 + 3, "and not the surface far off its axis");
        }
        // Walls block light.
        {
            std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 5000);   // a wall 20 m away
            for (int y = 0; y < H; ++y) for (int x = W / 2 - 17; x <= W / 2 + 17; ++x) depth[static_cast<std::size_t>(y) * W + x] = 1500;   // a box 6 m away in front of its middle
            std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagWall);
            FrameHints hints = sceneHints(depth, &tags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.x = 2000; l.y = 0; l.z = 0; l.radius = 30000; l.r = l.g = l.b = 255; l.strength = 0.9f;
            Options open = bare(1); open.lighting = true;
            Options blocked = open; blocked.occlusion = true;
            Frame a = Solid(W, H, 90, 90, 90), b = a;
            Presenter p, q; p.Process(a.data(), W, H, open, hints); q.Process(b.data(), W, H, blocked, hints);
            Check(At(b, W, W / 2 + 21, H / 2) < At(a, W, W / 2 + 21, H / 2) - 2, "a wall in the way of a light puts the surface behind it in shadow");
            Check(At(b, W, W / 2 + 80, H / 2) >= At(a, W, W / 2 + 80, H / 2) - 1, "and a surface with a clear line to the light is lit as before");
        }
        // Linear light blending differs from adding numbers, and still only brightens.
        {
            FrameHints hints = sceneHints(wallDepth, &wallTags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.x = 4000; l.y = 0; l.z = 0; l.radius = 20000; l.r = l.g = l.b = 255; l.strength = 0.9f;
            Options plain = bare(2); plain.lighting = true;
            Options linear = plain; linear.linearLight = true;
            Frame a = Solid(W, H, 100, 100, 100), b = a;
            Presenter p, q; p.Process(a.data(), W, H, plain, hints); q.Process(b.data(), W, H, linear, hints);
            Check(At(b, W, W / 2, H / 2) != At(a, W, W / 2, H / 2) && At(b, W, W / 2, H / 2) > 100, "light blended in linear space differs from adding numbers and still brightens");
        }
        // Atmosphere: ambient tint on floors, darker ceilings, distant floors misted towards the sky, a glow on the horizon.
        {
            std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 0xFFFF);
            std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagNone);
            Frame base = Solid(W, H, 120, 150, 200);   // sky
            for (int y = H / 2; y < H; ++y) for (int x = 0; x < W; ++x) {   // a floor below the horizon: near at the bottom (5 m) and far at the horizon (110 m)
                const int metres = 5 + (105 * (H - 1 - y)) / (H / 2);
                depth[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>(metres * 250); tags[static_cast<std::size_t>(y) * W + x] = kTagFloor;
                std::uint8_t* p = &base[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = 160; p[1] = 150; p[2] = 130;
            }
            for (int y = 0; y < H / 8; ++y) for (int x = 0; x < W; ++x) { depth[static_cast<std::size_t>(y) * W + x] = 3000; tags[static_cast<std::size_t>(y) * W + x] = kTagCeiling; std::uint8_t* p = &base[(static_cast<std::size_t>(y) * W + x) * 3]; p[0] = p[1] = p[2] = 150; }
            FrameHints hints = sceneHints(depth, &tags);
            Options o = bare(1); o.atmosphere = true; o.lighting = true;
            Frame f = base;
            Presenter p; p.Process(f.data(), W, H, o, hints);
            const int near = H - 6, far = H / 2 + 3;
            Check(At(f, W, W / 2, near, 2) > At(base, W, W / 2, near, 2), "floors catch a little of the sky's colour");
            Check(At(f, W, W / 2, far, 2) > At(base, W, W / 2, far, 2) + 4, "a distant floor is misted towards the (bluer) sky");
            Check(At(f, W, W / 2, 4) < At(base, W, W / 2, 4), "a ceiling is a little darker");
            Check(At(f, W, W / 2, H / 2 - 2) > At(f, W, W / 2, H / 3 + 8) + 3, "the sky glows near the horizon");
            Frame g = base;
            Options off = o; off.atmosphere = false;
            Presenter q; q.Process(g.data(), W, H, off, hints);
            Check(At(g, W, W / 2, near, 2) <= At(base, W, W / 2, near, 2) + 4, "with the atmosphere off the floor is not tinted");
        }
        // Craft shading: a rim at the silhouette, none off the craft.
        {
            std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 0xFFFF);
            std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagNone);
            const int cx = W / 2, cy = H / 2, R = 60;
            for (int y = cy - R; y <= cy + R; ++y) for (int x = cx - R; x <= cx + R; ++x) {   // a dome, 10 m away, 1.6 m high at the middle
                const float r2 = static_cast<float>((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (R * R);
                if (r2 >= 1.0f) continue;
                depth[static_cast<std::size_t>(y) * W + x] = static_cast<std::uint16_t>((10000.0f + 1600.0f * r2 * r2) / 4.0f * 1.0f + 0.5f * 0 + 1);
                tags[static_cast<std::size_t>(y) * W + x] = kTagCraft;
            }
            FrameHints hints = sceneHints(depth, &tags);
            hints.projKx = 0.0004f; hints.projKz = 0.0004f;
            Options o = bare(2); o.craftShading = true;
            Frame f = Solid(W, H, 60, 60, 60);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, cx + R - 3, cy, 2) > At(f, W, cx, cy, 2) + 3, "the edge of a craft gets a rim light");
            Check(At(f, W, 10, 10) == 60 && At(f, W, cx + R + 6, cy) == 60, "and nothing off the craft is touched");
        }
        // Reflection: the floor under the craft shows it.
        {
            std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 2500);
            std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagFloor);
            Frame f = Solid(W, H, 100, 100, 100);
            const int ground = H * 3 / 4;
            for (int y = ground - 40; y < ground; ++y) for (int x = W / 2 - 30; x < W / 2 + 30; ++x) { tags[static_cast<std::size_t>(y) * W + x] = kTagCraft; std::uint8_t* q = &f[(static_cast<std::size_t>(y) * W + x) * 3]; q[0] = 230; q[1] = 40; q[2] = 40; }
            FrameHints hints = sceneHints(depth, &tags);
            hints.craftVisible = true; hints.craftX = W / 2; hints.craftY = ground - 20; hints.craftRadius = 40; hints.craftGroundY = ground;
            Options o = bare(1); o.reflections = true;
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, W / 2, ground + 6, 0) > 100 + 10 && At(f, W, W / 2, ground + 6, 1) < 100, "the floor under the craft shows a reddish reflection of it");
            Check(At(f, W, W / 2 + 100, ground + 6, 0) == 100, "and not away from the craft");
        }
        // Halos: a glow round a missile, hidden when something is in front of it.
        {
            FrameHints hints = sceneHints(wallDepth, &wallTags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.kind = kLightMissile; l.x = 10000; l.y = 0; l.z = 0; l.radius = 4500; l.r = l.g = l.b = 255; l.strength = 0.5f;
            Options o = bare(0); o.halos = true; o.lighting = true;
            Frame f = Solid(W, H, 40, 40, 40);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, W / 2 + 2, H / 2) > 40 + 20, "a missile has a glow round it");
            std::vector<std::uint16_t> covered = wallDepth;
            for (int y = H / 2 - 30; y < H / 2 + 30; ++y) for (int x = W / 2 - 30; x < W / 2 + 30; ++x) covered[static_cast<std::size_t>(y) * W + x] = 800;   // something 3 m away in front
            FrameHints h2 = sceneHints(covered, &wallTags); h2.lightCount = 1; h2.lights[0] = l;
            Frame g = Solid(W, H, 40, 40, 40);
            Presenter q; q.Process(g.data(), W, H, o, h2);
            Check(At(g, W, W / 2 + 2, H / 2) == 40, "and the glow is hidden by something in front of it");
        }
        // Explosions: a bright glow with a hot core, embers and smoke; hidden behind a wall; nothing with Reduced motion.
        {
            FrameHints hints = sceneHints(wallDepth, &wallTags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.kind = kLightExplosion; l.x = 10000; l.y = 0; l.z = 0; l.radius = 9000; l.r = 255; l.g = 175; l.b = 90; l.strength = 1.0f;
            Options o = bare(1); o.halos = true; o.lighting = true; o.effects = true;
            Frame f = Solid(W, H, 40, 40, 40);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, W / 2, H / 2) > 40 + 120, "an explosion has a bright core");
            Check(At(f, W, W / 2 + 6, H / 2) > 40 + 40, "and a glow round it");
            Check(p.ParticleCount() >= 7, "and throws embers");
            hints.lights[0].strength = 0.3f;
            Frame g = Solid(W, H, 40, 40, 40);
            Presenter q; for (int i = 0; i < 4; ++i) q.Process(g.data(), W, H, o, hints);
            Check(q.ParticleCount() >= 1, "an old explosion lets off smoke, not embers");
            o.reducedMotion = true;
            Presenter r; Frame k = Solid(W, H, 40, 40, 40); hints.lights[0].strength = 1.0f; r.Process(k.data(), W, H, o, hints);
            Check(r.ParticleCount() == 0, "with Reduced motion there are no embers");
        }
        // Bursts (a muzzle flash, a power-up taken): a glow in the light's own colour and a few sparks; none with Reduced motion.
        {
            FrameHints hints = sceneHints(wallDepth, &wallTags);
            hints.lightCount = 1;
            Light& l = hints.lights[0];
            l.kind = kLightBurst; l.x = 10000; l.y = 0; l.z = 0; l.radius = 4000; l.r = 120; l.g = 255; l.b = 175; l.strength = 0.9f;
            Options o = bare(0); o.halos = true; o.lighting = true; o.effects = true;
            Frame f = Solid(W, H, 40, 40, 40);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            int greenest = 0;
            for (int dx = 4; dx <= 18; dx += 2) greenest = std::max(greenest, At(f, W, W / 2 + dx, H / 2, 1) - At(f, W, W / 2 + dx, H / 2, 0));
            Check(At(f, W, W / 2, H / 2, 1) > 40 + 50 && greenest > 15, "a burst glows in its own colour (green here: the edge of the glow is greener than red)");
            Check(p.ParticleCount() >= 3, "and throws a few sparks");
            o.reducedMotion = true;
            Presenter r; Frame k = Solid(W, H, 40, 40, 40); r.Process(k.data(), W, H, o, hints);
            Check(r.ParticleCount() == 0, "with Reduced motion there are no sparks");
        }
        // Emitters: exhaust and dust behind a fast craft, smoke from a missile, sparkles on a power-up; none with Reduced motion.
        {
            FrameHints hints = Hints(W, H);
            hints.craftGroundY = H * 3 / 4 + 20;
            Options o = bare(1); o.effects = true;
            Frame f = Solid(W, H, 60, 60, 60);
            Presenter p; for (int i = 0; i < 6; ++i) p.Process(f.data(), W, H, o, hints);
            Check(p.ParticleCount() == 0, "a fast craft leaves no exhaust or dust behind it");
            FrameHints m = sceneHints(wallDepth, &wallTags);
            m.lightCount = 2;
            m.lights[0].kind = kLightMissile; m.lights[0].x = 10000; m.lights[0].radius = 4500; m.lights[0].strength = 0.5f;
            m.lights[1].kind = kLightPowerUp; m.lights[1].x = 12000; m.lights[1].y = 800; m.lights[1].radius = 3200; m.lights[1].strength = 0.35f;
            Presenter r; for (int i = 0; i < 6; ++i) r.Process(f.data(), W, H, o, m);
            Check(r.ParticleCount() >= 6, "a missile trails smoke and a power-up sparkles (with Reduced motion off)");
        }
        // A missile that crosses the screen between two pictures leaves a continuous trail of smoke along the way, not one puff at the end of it.
        {
            Options o = bare(1); o.effects = true;
            Frame f = Solid(W, H, 60, 60, 60);
            Presenter single, trail;
            FrameHints near = sceneHints(wallDepth, &wallTags); near.lightCount = 1; near.lights[0].kind = kLightMissile; near.lights[0].x = 10000; near.lights[0].y = 2000; near.lights[0].radius = 4500; near.lights[0].strength = 0.5f;
            FrameHints far = near; far.lights[0].y = -2000;   // a long way across the picture in one step
            single.Process(f.data(), W, H, o, near);
            const std::size_t before = single.ParticleCount();
            single.Process(f.data(), W, H, o, far);
            const std::size_t jumped = single.ParticleCount() - before;
            Check(jumped >= 4, "a missile that moves far between pictures leaves several puffs along its way");
            (void)trail;
        }
        // Edge anti-aliasing: a staircase edge between two surfaces is softened; the texture inside a surface is not touched.
        {
            std::vector<std::uint16_t> depth(static_cast<std::size_t>(W) * H, 2500);
            std::vector<std::uint8_t> tags(static_cast<std::size_t>(W) * H, kTagWall);
            Frame f = Solid(W, H, 30, 30, 30);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                if (x > y + 40) { depth[static_cast<std::size_t>(y) * W + x] = 0xFFFF; tags[static_cast<std::size_t>(y) * W + x] = kTagNone; std::uint8_t* q = &f[(static_cast<std::size_t>(y) * W + x) * 3]; q[0] = q[1] = q[2] = 220; }
                else if (x < 60 && ((x / 2 + y / 2) & 1)) { std::uint8_t* q = &f[(static_cast<std::size_t>(y) * W + x) * 3]; q[0] = q[1] = q[2] = 90; }   // a checker texture inside the surface
            }
            const Frame before = f;
            FrameHints hints = sceneHints(depth, &tags);
            Options o = bare(1); o.edgeAA = true;
            Presenter p; p.Process(f.data(), W, H, o, hints);
            int greys = 0;
            for (int y = 20; y < 200; ++y) for (int x = 20; x < W - 20; ++x) { const int v = At(f, W, x, y); if (v > 50 && v < 200 && At(before, W, x, y) != v) ++greys; }
            Check(greys > 100, "the stair-stepped edge between a surface and the sky is softened");
            bool textureSame = true;
            for (int y = 100; y < 140; ++y) for (int x = 5; x < 50; ++x) textureSame = textureSame && At(f, W, x, y) == At(before, W, x, y);
            Check(textureSame, "the texture inside a surface is not touched");
        }
        // HUD outline: scene pixels beside HUD pixels are darkened; HUD pixels are not.
        {
            std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H, 0);
            for (int y = 50; y < 60; ++y) for (int x = 50; x < 70; ++x) mask[static_cast<std::size_t>(y) * W + x] = 1;
            FrameHints hints; hints.valid = true; hints.hudMask = mask.data(); hints.hudMaskWidth = W; hints.hudMaskHeight = H;
            Options o = bare(0); o.hudOutline = true;
            Frame f = Solid(W, H, 200, 200, 200);
            Presenter p; p.Process(f.data(), W, H, o, hints);
            Check(At(f, W, 49, 55) < 200 - 40 && At(f, W, 60, 60) < 200 - 40, "scene pixels beside HUD pixels get a dark edge");
            Check(At(f, W, 60, 55) == 200 && At(f, W, 30, 30) == 200 && At(f, W, 47, 55) == 200, "HUD pixels, and the scene away from them, are untouched");
        }
        // Sharpen: a soft edge gets steeper (it overshoots a little at both ends of the ramp); the HUD does not.
        {
            Frame f = Solid(W, H, 0, 0, 0);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
                const int v = x <= 150 ? 60 : (x == 151 ? 130 : 200);   // a ramp from 60 to 200 over two pixels
                std::uint8_t* q = &f[(static_cast<std::size_t>(y) * W + x) * 3]; q[0] = q[1] = q[2] = static_cast<std::uint8_t>(v);
            }
            const Frame before = f;
            std::vector<std::uint8_t> mask(static_cast<std::size_t>(W) * H, 0);
            for (int y = 0; y < H; ++y) for (int x = 146; x < 160; ++x) mask[static_cast<std::size_t>(y) * W + x] = 1;
            FrameHints hints; hints.valid = true; hints.hudMask = mask.data(); hints.hudMaskWidth = W; hints.hudMaskHeight = H;
            Options o = bare(2); o.sharpen = true;
            Frame g = before; Presenter p; p.Process(g.data(), W, H, o, FrameHints());
            Frame h = before; Presenter q; q.Process(h.data(), W, H, o, hints);
            Check(At(g, W, 152, 50) > At(before, W, 152, 50) + 4 && At(g, W, 150, 50) < At(before, W, 150, 50) - 4, "sharpening steepens a soft edge");
            Check(At(h, W, 152, 50) == At(before, W, 152, 50) && At(h, W, 150, 50) == At(before, W, 150, 50) && At(h, W, 100, 50) == At(before, W, 100, 50), "and leaves the HUD, and flat areas, as they were");
        }

        // Exposure: the picture's brightness does not adapt to the view (it must not pump as the view turns); the brightest highlights roll off.
        {
            Frame dark = Solid(W, H, 40, 40, 40), hot = Solid(W, H, 252, 252, 252);
            Options o = bare(1); o.exposure = true;
            Presenter p; Frame d = dark; for (int i = 0; i < 80; ++i) { d = dark; p.Process(d.data(), W, H, o, FrameHints()); }
            Check(d == dark, "a dark view stays as dark as it is: no adaptation");
            Frame m = Solid(W, H, 100, 100, 100), hh = hot;
            Presenter q; q.Process(m.data(), W, H, o, FrameHints()); q.Process(hh.data(), W, H, o, FrameHints());
            Check(std::abs(At(m, W, 50, 50) - 100) <= 1, "the middle tones are left alone");
            Check(At(hh, W, 50, 50) < 252 && At(hh, W, 50, 50) > 230, "and the brightest highlights are rolled off, not clipped");
        }
        // Adaptive quality: too slow for the budget, the quality drops; with room to spare it comes back.
        {
            Frame f = Solid(64, 48, 90, 90, 90);
            Options o = Modern(Preset::eModernHigh);
            o.adaptive = true; o.budgetMs = 0.000001f;
            Presenter p;
            for (int i = 0; i < 160; ++i) p.Process(f.data(), 64, 48, o, FrameHints());
            Check(p.EffectiveQuality() == 0, "a presenter that is over budget lowers its quality, step by step, to the lowest");
            o.budgetMs = 1.0e6f;
            for (int i = 0; i < 560; ++i) p.Process(f.data(), 64, 48, o, FrameHints());
            Check(p.EffectiveQuality() == 2, "and raises it again when there is room to spare");
            Options fixed = o; fixed.adaptive = false; fixed.budgetMs = 0.000001f;
            Presenter q; for (int i = 0; i < 160; ++i) q.Process(f.data(), 64, 48, fixed, FrameHints());
            Check(q.EffectiveQuality() == 2, "with adaptive quality off the quality is never lowered");
        }
        // Whole-frame time: frames that take too long lower the quality even when the passes themselves are cheap, and it comes back only after a long wait.
        {
            Frame f = Solid(64, 48, 90, 90, 90);
            Options o = Modern(Preset::eModernHigh);
            o.adaptive = true; o.budgetMs = 1.0e6f;   // the passes are never the problem here
            Presenter p;
            for (int i = 0; i < 200; ++i) { p.NoteFrameInterval(40.0); p.Process(f.data(), 64, 48, o, FrameHints()); }
            Check(p.EffectiveQuality() < 2, "frames that take 40 ms lower the quality even though the passes are cheap");
            const int lowered = p.EffectiveQuality();
            for (int i = 0; i < 250; ++i) { p.NoteFrameInterval(10.0); p.Process(f.data(), 64, 48, o, FrameHints()); }
            Check(p.EffectiveQuality() == lowered, "and it does not come back quickly (it waits longer each time it had to be lowered)");
            for (int i = 0; i < 6000; ++i) { p.NoteFrameInterval(10.0); p.Process(f.data(), 64, 48, o, FrameHints()); }
            Check(p.EffectiveQuality() == 2, "but it does come back after a long stretch of fast frames");
            Presenter q;
            for (int i = 0; i < 300; ++i) { q.NoteFrameInterval(10.0); q.Process(f.data(), 64, 48, o, FrameHints()); }
            Check(q.EffectiveQuality() == 2, "fast frames never lower it");
        }
        // True colour: pixels the 8-bit picture still has as the observer left them take the true colour; pixels drawn later do not.
        {
            std::vector<std::uint32_t> colour(static_cast<std::size_t>(W) * H, 0x00102030u);
            std::vector<std::uint8_t> finalIdx(static_cast<std::size_t>(W) * H, 7), now(static_cast<std::size_t>(W) * H, 7);
            now[100] = 9;   // drawn after the observer finished (a chat line, say)
            Frame rgb = Solid(W, H, 1, 2, 3);
            FrameHints hints; hints.colour = colour.data(); hints.finalIndexed = finalIdx.data(); hints.colourWidth = W; hints.colourHeight = H;
            const std::size_t changed = Presenter::ApplyTrueColour(rgb.data(), now.data(), W, H, hints);
            Check(changed == static_cast<std::size_t>(W) * H - 1 && rgb[0] == 0x10 && rgb[1] == 0x20 && rgb[2] == 0x30, "the true-colour picture replaces the palette colours");
            Check(rgb[300] == 1 && rgb[301] == 2 && rgb[302] == 3, "except where the 8-bit picture changed after the observer finished");
            Frame none = Solid(W, H, 1, 2, 3);
            Check(Presenter::ApplyTrueColour(none.data(), now.data(), W, H, FrameHints()) == 0 && none == Solid(W, H, 1, 2, 3), "and nothing happens without a true-colour picture");
        }
    }

    // 9. Robustness: odd and tiny sizes, hints far outside the frame, every option combination.
    {
        const int sizes[][2] = {{64, 48}, {65, 49}, {321, 241}, {10, 10}, {0, 0}};
        for (const auto& size : sizes) {
            const int w = size[0], h = size[1];
            Frame f(static_cast<std::size_t>(w > 0 ? w : 1) * (h > 0 ? h : 1) * 3 + 64, 77);   // 64 guard bytes after the frame
            const std::size_t used = static_cast<std::size_t>(w) * h * 3;
            for (int mask = 0; mask < 32; ++mask) {
                Options o;
                o.enabled = true; o.deband = mask & 1; o.smoothing = mask & 2; o.bloom = mask & 4; o.grade = mask & 8; o.effects = mask & 16; o.quality = mask % 3;
                FrameHints hints;
                hints.valid = true; hints.craftVisible = true; hints.craftX = -5000; hints.craftY = 90000; hints.craftRadius = 100000; hints.speed = 5.0f; hints.impact = 1.0f;
                hints.trailCount = kMaxTrailPoints;
                for (int i = 0; i < kMaxTrailPoints; ++i) { hints.trailX[i] = i * 400 - 3000; hints.trailY[i] = -i * 300; }
                Presenter p;
                for (int frame = 0; frame < 2; ++frame) p.Process(f.data(), w, h, o, hints);
            }
            bool guard = true;
            for (std::size_t i = used; i < f.size() && guard; ++i) guard = f[i] == 77;
            char what[96];
            std::snprintf(what, sizeof what, "%dx%d: every option combination stays inside the frame", w, h);
            Check(guard, what);
        }
    }

    // 10. The shadow table: every entry is a palette colour no brighter than the one it replaces (a shadow only darkens).
    {
        std::uint8_t palette[768];
        for (int i = 0; i < 256; ++i) { palette[i * 3] = static_cast<std::uint8_t>(i); palette[i * 3 + 1] = static_cast<std::uint8_t>(i * 9 / 10); palette[i * 3 + 2] = static_cast<std::uint8_t>(i * 8 / 10); }   // a warm grey ramp, like a lit surface
        std::uint8_t lut[256];
        BuildShadowLut(palette, 70, lut);
        bool darker = true;
        int total = 0;
        for (int i = 0; i < 256; ++i) {
            const int before = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2], after = palette[lut[i] * 3] + palette[lut[i] * 3 + 1] + palette[lut[i] * 3 + 2];
            darker = darker && after <= before + 6;
            total += before - after;
        }
        Check(darker && total > 256 * 20, "the shadow table maps each colour to a darker palette colour");
    }

    std::printf("%s\n", gFailures == 0 ? "modern graphics: all checks passed" : "modern graphics: FAILED");
    return gFailures == 0 ? 0 : 1;
}
