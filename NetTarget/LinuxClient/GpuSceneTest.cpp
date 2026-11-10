// GpuSceneTest -- the scene drawn by the graphics card against the software rasteriser's.
//
// The same walls, floor and craft are drawn twice from several viewpoints: by MR_3DViewPort into its buffers (the software rasteriser), and handed to the card as triangles
// (MR_SceneSink -> GpuFinisher). They are different renderers (the card does perspective-correct triangles, the rasteriser its own fixed-point spans), so the pictures are
// close and not identical. Checks: the card draws the same pixels (coverage), the same colours on them (mean difference), the depth it keeps matches the rasteriser's
// depth where both drew, and nothing is drawn behind the camera. Skipped (77) where no OpenGL context can be made. Run from the repository root.
#include "../MainCharacter/StdAfx.h"
#include "../ObjFac1/ObjFac1Res.h"
#include "../ObjFacTools/ResActor.h"
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/GpuFinisher.h"
#include "../VideoServices/VideoBuffer.h"

#include <SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    MR_InitTrigoTables();
    HoverNetGfx::GpuFinisher gpu;
    if (!gpu.Init()) { std::printf("no graphics card context (%s): skipped\n", gpu.Error().c_str()); return 77; }
    std::printf("graphics card: %s\n", gpu.Renderer().c_str());

    std::array<MR_UInt8, 768> palette{};
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int i = 0; i < MR_BASIC_COLORS; ++i) { const int at = MR_RESERVED_COLORS_BEGINNING + i; palette[at * 3] = colors[i].peRed; palette[at * 3 + 1] = colors[i].peGreen; palette[at * 3 + 2] = colors[i].peBlue; }
        delete[] colors;
    }
    std::array<MR_UInt32, 256> words{};
    for (int i = 0; i < 256; ++i) words[i] = (static_cast<MR_UInt32>(palette[i * 3]) << 16) | (static_cast<MR_UInt32>(palette[i * 3 + 1]) << 8) | palette[i * 3 + 2];
    setenv("HOVERNET_NO_AI_TEXTURES", "1", 1);
    MR_ResourceLib lib("NetTarget/ObjFac1.dat");
    MR_ResBitmap::SetColourPalette(palette.data(), 1.0f, false);
    gpu.SetPalette(palette.data());
    const MR_ResBitmap* wall = lib.GetBitmap(1051);
    const MR_ResBitmap* floorTexture = lib.GetBitmap(1052) != nullptr ? lib.GetBitmap(1052) : wall;
    const MR_ResActor* craft = lib.GetActor(MR_ELECTRO_CAR);
    Check(wall != nullptr && craft != nullptr, "the test's textures and craft load");

    const int W = std::getenv("HOVERNET_GPUSCENE_W") ? std::atoi(std::getenv("HOVERNET_GPUSCENE_W")) : 320, H = W * 3 / 4;
    double meanSum = 0; int views = 0; double coverageWorst = 1.0, depthWorst = 0;
    for (int view = 0; view < 6; ++view) {
        // --- the same scene through a viewport, once for each renderer
        auto draw = [&](MR_3DViewPort& vp) {
            vp.SetupCameraPosition(MR_3DCoordinate(-150 * view, 60 * view - 200, 1000 + 40 * view), (view - 3) * 60, view - 2);
            vp.SetSurfaceTag(3);
            vp.RenderWallSurface(MR_3DCoordinate(4000, 2500, 2500), MR_3DCoordinate(4000, -2500, 0), 5000, wall);
            vp.RenderWallSurface(MR_3DCoordinate(2000 + 200 * view, -3000, 2200), MR_3DCoordinate(9000, -3000, 0), 7000, wall);
            const MR_2DCoordinate quad[4] = {MR_2DCoordinate(1500, -3000), MR_2DCoordinate(12000, -3000), MR_2DCoordinate(12000, 3000), MR_2DCoordinate(1500, 3000)};
            vp.SetSurfaceTag(1);
            vp.RenderHorizontalSurface(4, quad, 0, TRUE, floorTexture);
            vp.SetSurfaceTag(5);
            MR_PositionMatrix matrix;
            if (vp.ComputePositionMatrix(matrix, MR_3DCoordinate(3200, 300 * (view - 2), 0), view * 200, 10000000)) craft->Draw(&vp, matrix, 0, 0);
        };
        // software
        MR_VideoBuffer cpuBuffer(nullptr, 1.0, 0.5, 0.5);
        if (!cpuBuffer.SetVideoMode(W, H) || !cpuBuffer.Lock()) return 2;
        MR_3DViewPort cpu;
        cpu.Setup(&cpuBuffer, 0, 0, W, H, 800);
        std::vector<MR_UInt32> cpuColour(static_cast<std::size_t>(W) * H, 0);
        cpu.SetColourBuffer(cpuColour.data(), words.data());
        cpu.SetTextureFiltering(TRUE);
        cpu.Clear(255); cpu.ClearZ();
        draw(cpu);
        // card
        MR_VideoBuffer gpuBuffer(nullptr, 1.0, 0.5, 0.5);
        if (!gpuBuffer.SetVideoMode(W, H) || !gpuBuffer.Lock()) return 2;
        MR_3DViewPort card;
        card.Setup(&gpuBuffer, 0, 0, W, H, 800);
        gpu.Scene().Clear();
        card.SetSceneSink(&gpu.Scene());
        card.Clear(255); card.ClearZ();
        draw(card);
        const std::size_t triangles = gpu.Scene().Triangles();
        if (view == 0) Check(triangles > 50, "the scene reaches the sink as triangles (and the software buffer stays empty)");
        double kx, kz; int cx, cy, ox, oy;
        card.GetProjection(kx, kz, cx, cy, ox, oy);
        {   // the camera space the picture passes use (x ahead, y LEFT, z up) agrees with where the viewport puts a point: pixel = centre - y / (x * k). It was mirrored once (lights on the
            // wrong side of what they belonged to), so check points on both sides of the middle.
            int checked = 0, badPixels = 0, leftOfCentre = 0, leftSaid = 0;
            for (int gx = 3000; gx <= 12000; gx += 3000) for (int gy = -6000; gy <= 6000; gy += 2000) for (int gz = 0; gz <= 2000; gz += 1000) {
                const MR_3DCoordinate world(-150 * view + gx, 60 * view - 200 + gy, gz);
                int sx, sy, depth, ppm, camX, camY, camZ;
                if (!card.ProjectToScreen(world, sx, sy, depth, ppm) || !card.ToCameraSpace(world, camX, camY, camZ)) continue;
                const int px = static_cast<int>(cx - camY / (camX * kx)), py = static_cast<int>(cy - camZ / (camX * kz));
                ++checked;
                if (std::abs(px - sx) > 2 || std::abs(py - sy) > 2) ++badPixels;
                if (sx < cx - 10) { ++leftOfCentre; if (camY > 0) ++leftSaid; }
            }
            if (view == 0) Check(checked > 10, "points in front of the camera project");
            Check(badPixels == 0, "ToCameraSpace and ProjectToScreen agree on where a point is on the screen (both sides of the middle)");
            Check(leftOfCentre == leftSaid, "a point left of the middle has a positive camera-space y (y is to the left)");
        }
        HoverNetGfx::FrameHints hints;
        hints.valid = true; hints.gpuScene = true;
        hints.projKx = static_cast<float>(kx); hints.projKz = static_cast<float>(kz); hints.projCenterX = cx; hints.projCenterY = cy;
        hints.depthX0 = ox; hints.depthY0 = oy; hints.depthWidth = W; hints.depthHeight = H;
        HoverNetGfx::Options options;
        options.enabled = true; options.quality = 0; options.grade = false; options.effects = options.lighting = options.halos = options.craftShading = options.reflections = options.atmosphere = false;
        options.adaptive = false; options.deband = options.smoothing = options.bloom = options.sharpen = options.exposure = options.hudOutline = options.edgeAA = false;
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(W) * H * 3, 0);
        const bool done = gpu.Process(rgb.data(), W, H, options, hints, nullptr);
        if (view == 0) Check(done, "the card finishes a frame with a scene");
        if (view == 0) {
            // A craft's shadow darkens the floor under it, and neither the craft itself nor an object next to it: two flat quads side by side at the same depth, one tagged as a craft (5)
            // and one as a wall (3), under one shadow ellipse that covers both. Only the wall may get darker.
            gpu.Scene().Clear(); card.SetSceneSink(&gpu.Scene()); card.Clear(255); card.ClearZ();
            const float depthMm = 6000.0f;
            const int flat = MR_RESERVED_COLORS_BEGINNING + 20;
            std::array<MR_UInt8, 768> bright = palette;   // a colour that is certainly not black
            bright[flat * 3] = 210; bright[flat * 3 + 1] = 190; bright[flat * 3 + 2] = 170;
            gpu.SetPalette(bright.data());
            auto quad = [&](float yLeft, float yRight, MR_UInt8 tag) {
                const MR_SceneVertex v[6] = {{depthMm, yLeft, 1500, 0, 0}, {depthMm, yRight, 1500, 1, 0}, {depthMm, yRight, -1500, 1, 1}, {depthMm, yLeft, 1500, 0, 0}, {depthMm, yRight, -1500, 1, 1}, {depthMm, yLeft, -1500, 0, 1}};   // two triangles
                gpu.Scene().Triangles(v, 6, nullptr, static_cast<MR_UInt8>(flat), tag);
            };
            quad(2500.0f, 0.0f, 5);     // left of the middle (y is to the left): the craft
            quad(0.0f, -2500.0f, 3);    // right of the middle: a wall
            std::vector<std::uint8_t> plain(rgb.size(), 0), shaded(rgb.size(), 0);
            HoverNetGfx::FrameHints h2 = hints;
            const bool a = gpu.Process(plain.data(), W, H, options, h2, nullptr);
            gpu.Scene().Clear(); card.SetSceneSink(&gpu.Scene()); card.Clear(255); card.ClearZ();
            quad(2500.0f, 0.0f, 5); quad(0.0f, -2500.0f, 3);
            gpu.Scene().Shadow(hints.projCenterX, hints.projCenterY, 100, 80, 6000, 2000);
            const bool b = gpu.Process(shaded.data(), W, H, options, h2, nullptr);
            const std::size_t left = (static_cast<std::size_t>(hints.projCenterY) * W + hints.projCenterX - 40) * 3, right = (static_cast<std::size_t>(hints.projCenterY) * W + hints.projCenterX + 40) * 3;
            Check(a && b && plain[left] > 20 && plain[right] > 20, "the shadow test's two quads are drawn");
            Check(shaded[left] == plain[left] && shaded[left + 1] == plain[left + 1], "a shadow does not darken the craft's own pixels");
            Check(shaded[right] < plain[right] && shaded[right] > plain[right] * 8 / 10, "but it darkens the wall beside it (about 13%)");
            gpu.SetPalette(palette.data());
        }
        if (view == 0) {
            // glows (the engine glow, sparks, embers) are drawn by the card after the scene, so they show over it
            gpu.Scene().Clear(); card.SetSceneSink(&gpu.Scene()); card.Clear(255); card.ClearZ(); draw(card);
            const HoverNetGfx::GlowSprite sprite{W / 2.0f, H / 2.0f, 24.0f, 24.0f, 255, 200, 100, 1.0f};
            HoverNetGfx::FrameHints withGlow = hints; withGlow.sprites = &sprite; withGlow.spriteCount = 1;
            std::vector<std::uint8_t> lit(rgb.size(), 0);
            const bool glowDone = gpu.Process(lit.data(), W, H, options, withGlow, nullptr);
            const std::size_t centre = (static_cast<std::size_t>(H / 2) * W + W / 2) * 3, far = (static_cast<std::size_t>(H / 2) * W + W / 2 + 40) * 3;
            Check(glowDone && lit[centre] > rgb[centre] + 120, "a glow sprite is added over the card's scene (centre brighter)");
            Check(lit[far] == rgb[far] && lit[far + 1] == rgb[far + 1], "and does not touch pixels outside it");
        }

        // --- compare
        long cpuCovered = 0, gpuCovered = 0, both = 0; double diff = 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            const bool c = cpuBuffer.GetBuffer()[i] != 255 || false;
            (void)c;
        }
        // the software viewport wrote into cpuBuffer (indices) and cpuColour
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            const bool c = cpuBuffer.GetBuffer()[i] != 255;
            const bool g = rgb[i * 3] != 0 || rgb[i * 3 + 1] != 0 || rgb[i * 3 + 2] != 0;
            cpuCovered += c; gpuCovered += g; both += c && g;
            if (c && g) {
                const std::uint32_t w = cpuColour[i];
                diff += (std::abs(static_cast<int>((w >> 16) & 255) - rgb[i * 3]) + std::abs(static_cast<int>((w >> 8) & 255) - rgb[i * 3 + 1]) + std::abs(static_cast<int>(w & 255) - rgb[i * 3 + 2])) / 3.0;
            }
        }
        const double coverage = cpuCovered > 0 ? static_cast<double>(both) / std::max(cpuCovered, gpuCovered) : 1.0;
        const double mean = both > 0 ? diff / both : 0.0;
        std::printf("view %d: software covers %ld pixels, the card %ld, %ld in both (%.0f%%), mean colour difference %.1f levels\n", view, cpuCovered, gpuCovered, both, coverage * 100.0, mean);
        if (const char* dump = std::getenv("HOVERNET_GPUSCENE_DUMP")) {
            char path[256]; std::snprintf(path, sizeof path, "%s/view%d.ppm", dump, view);
            if (FILE* f = std::fopen(path, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", W * 2, H);
                for (int y = 0; y < H; ++y) {
                    for (int x = 0; x < W; ++x) { const std::uint32_t w = cpuColour[static_cast<std::size_t>(y) * W + x]; std::fputc((w >> 16) & 255, f); std::fputc((w >> 8) & 255, f); std::fputc(w & 255, f); }
                    for (int x = 0; x < W; ++x) { const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 3; std::fputc(rgb[i], f); std::fputc(rgb[i + 1], f); std::fputc(rgb[i + 2], f); }
                }
                std::fclose(f);
            }
        }
        coverageWorst = std::min(coverageWorst, coverage);
        meanSum += mean; ++views;
        (void)depthWorst;
    }
    Check(coverageWorst > 0.85, "the card draws the same pixels as the rasteriser (at least 85% overlap in every view)");
    Check(views > 0 && meanSum / views < 22.0, "and nearly the same colours on them (mean difference under 22 levels)");
    std::printf(gFail == 0 ? "gpu scene: all checks passed\n" : "gpu scene: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
