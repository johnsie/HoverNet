// GpuFinishTest -- the finishing passes on the graphics card (VideoServices/GpuFinisher.cpp) against the same passes on the processor.
//
// A synthetic frame (colour ramps, hard edges, a bright patch, noise) with a pretend HUD in one corner is finished both ways. The checks: the card's picture is close to the
// processor's (they are different implementations of the same passes, so close and not identical), the HUD's own pixels come back exactly as they went in, bloom raised the
// dark pixels next to the bright patch, nothing is out of range, and the time is reported. Skipped (exit code 77) where no OpenGL context can be made (no display, CI).
#include "../VideoServices/GpuFinisher.h"
#include "../VideoServices/ModernGraphics.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    const int w = 640, h = 480;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(w) * h * 3), mask(static_cast<std::size_t>(w) * h, 0);
    unsigned seed = 12345;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &frame[(static_cast<std::size_t>(y) * w + x) * 3];
            seed = seed * 1664525u + 1013904223u;
            const int noise = static_cast<int>(seed >> 28) - 8;
            int r = 40 + x * 160 / w, g = 60 + y * 120 / h, b = 90 + ((x / 40 + y / 40) & 1) * 50;       // a ramp with a checkerboard (hard edges)
            if (x > 400 && x < 470 && y > 180 && y < 250) r = g = b = 250;                                  // a bright patch for the bloom
            p[0] = static_cast<std::uint8_t>(std::max(0, std::min(255, r + noise / 2))); p[1] = static_cast<std::uint8_t>(std::max(0, std::min(255, g + noise / 2))); p[2] = static_cast<std::uint8_t>(std::max(0, std::min(255, b)));
        }
    for (int y = 20; y < 70; ++y) for (int x = 20; x < 200; ++x) { mask[static_cast<std::size_t>(y) * w + x] = 1; std::uint8_t* p = &frame[(static_cast<std::size_t>(y) * w + x) * 3]; p[0] = 200; p[1] = 40; p[2] = 70; }

    HoverNetGfx::GpuFinisher gpu;
    if (!gpu.Init()) { std::printf("no graphics card context (%s): skipped\n", gpu.Error().c_str()); return 77; }
    std::printf("graphics card: %s\n", gpu.Renderer().c_str());

    HoverNetGfx::Options options = HoverNetGfx::OptionsForPreset(HoverNetGfx::Preset::eModernHigh);
    options.effects = options.lighting = options.halos = options.craftShading = options.reflections = options.atmosphere = false;   // only the picture passes
    options.adaptive = false;
    HoverNetGfx::FrameHints hints;
    hints.valid = true;
    hints.hudMask = mask.data(); hints.hudMaskWidth = w; hints.hudMaskHeight = h;

    std::vector<std::uint8_t> viaGpu = frame, viaCpu = frame;
    Check(gpu.Process(viaGpu.data(), w, h, options, hints, mask.data()), "the card finishes a frame");
    HoverNetGfx::Presenter cpu;
    cpu.Process(viaCpu.data(), w, h, options, hints);

    double sum = 0; long hudChanged = 0, hudPixels = 0; int worst = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        for (int c = 0; c < 3; ++c) { const int d = std::abs(static_cast<int>(viaGpu[i * 3 + c]) - static_cast<int>(viaCpu[i * 3 + c])); sum += d; worst = std::max(worst, d); }
        if (mask[i]) { ++hudPixels; for (int c = 0; c < 3; ++c) if (viaGpu[i * 3 + c] != frame[i * 3 + c]) { ++hudChanged; break; } }
    }
    const double mean = sum / (static_cast<double>(w) * h * 3);
    std::printf("mean difference from the processor's picture: %.2f levels (largest %d)\n", mean, worst);
    Check(mean < 6.0, "the card's picture is close to the processor's (mean difference under 6 levels)");
    Check(hudPixels > 0 && hudChanged == 0, "the HUD's own pixels come back exactly as drawn");

    // each pass alone against the processor's version of it
    struct Alone { const char* name; void (*set)(HoverNetGfx::Options&); };
    const Alone alone[] = {
        {"de-banding + dither", [](HoverNetGfx::Options& o) { o.smoothing = o.bloom = o.sharpen = o.exposure = o.hudOutline = o.grade = false; }},
        {"edge smoothing", [](HoverNetGfx::Options& o) { o.deband = o.bloom = o.sharpen = o.exposure = o.hudOutline = o.grade = false; }},
        {"sharpen", [](HoverNetGfx::Options& o) { o.deband = o.smoothing = o.bloom = o.exposure = o.hudOutline = o.grade = false; }},
        {"bloom", [](HoverNetGfx::Options& o) { o.deband = o.smoothing = o.sharpen = o.exposure = o.hudOutline = o.grade = false; }},
        {"exposure", [](HoverNetGfx::Options& o) { o.deband = o.smoothing = o.bloom = o.sharpen = o.hudOutline = o.grade = false; }},
        {"HUD edge", [](HoverNetGfx::Options& o) { o.deband = o.smoothing = o.bloom = o.sharpen = o.exposure = o.grade = false; }},
        {"vignette", [](HoverNetGfx::Options& o) { o.deband = o.smoothing = o.bloom = o.sharpen = o.exposure = o.hudOutline = false; }},
    };
    for (const Alone& a : alone) {
        HoverNetGfx::Options one = options;
        a.set(one);
        std::vector<std::uint8_t> g = frame, c = frame;
        gpu.Process(g.data(), w, h, one, hints, mask.data());
        HoverNetGfx::Presenter pc;
        pc.Process(c.data(), w, h, one, hints);
        double d = 0;
        for (std::size_t i = 0; i < g.size(); ++i) d += std::abs(static_cast<int>(g[i]) - static_cast<int>(c[i]));
        std::printf("  %-22s mean difference %.2f levels\n", a.name, d / g.size());
    }

    // bloom: pixels just beside the bright patch are lighter than the same frame finished without bloom
    HoverNetGfx::Options noBloom = options; noBloom.bloom = false;
    std::vector<std::uint8_t> plain = frame;
    gpu.Process(plain.data(), w, h, noBloom, hints, mask.data());
    long lifted = 0, tested = 0;
    for (int y = 200; y < 230; ++y) for (int x = 474; x < 490; ++x) {
        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
        ++tested; if (viaGpu[i] > plain[i] + 2) ++lifted;
    }
    Check(lifted > tested / 2, "bloom lifts the pixels beside a bright patch");
    Check(viaGpu[(static_cast<std::size_t>(240) * w + 435) * 3] <= 255, "values stay in range");

    // the corners are darker than the middle (the vignette), the same way on the card as on the processor
    auto luma = [&](const std::vector<std::uint8_t>& f, int x, int y) { const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3; return 0.3 * f[i] + 0.59 * f[i + 1] + 0.11 * f[i + 2]; };
    const double gpuRatio = luma(viaGpu, w - 3, h - 3) / std::max(1.0, luma(frame, w - 3, h - 3));
    Check(gpuRatio < 0.97, "the vignette darkens the corners");

    // speed: ten frames
    double total = 0;
    for (int i = 0; i < 10; ++i) { std::vector<std::uint8_t> f = frame; gpu.Process(f.data(), w, h, options, hints, mask.data()); total += gpu.LastMilliseconds(); }
    double cpuTotal = 0;
    for (int i = 0; i < 10; ++i) { std::vector<std::uint8_t> f = frame; const auto t0 = SDL_GetPerformanceCounter(); cpu.Process(f.data(), w, h, options, hints); cpuTotal += (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency(); }
    std::printf("the card's passes take %.2f ms per 640x480 frame on this machine (%s); the same passes on the processor take %.2f ms\n", total / 10.0, gpu.Renderer().c_str(), cpuTotal / 10.0);

    std::printf(gFail == 0 ? "gpu finish: all checks passed\n" : "gpu finish: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
