// TexturePackTest -- the AI-enhanced textures (ObjFac1.textures, docs/texture-pipeline.md) against the game's own.
//
// For every image in the pack: it belongs to a texture of ObjFac1.dat and is that texture's size at a power-of-two scale; it has not strayed from the original
// (averaged back down to the texture's size it is close to it: a network gone wrong would not be); it tiles (no seam where it meets itself); the texture really
// uses it (the true-colour copy has its scale, and the smaller levels are made from it); and HOVERNET_NO_AI_TEXTURES switches it off. Run from the repository root.
#include "../MainCharacter/StdAfx.h"   // first: on Windows it brings the MFC compatibility layer the headers below use
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../ObjFacTools/TexturePack.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/ColorPalette.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFailures; }
}

int main()
{
    MR_InitTrigoTables();
    std::array<MR_UInt8, 768> palette{};
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int at = MR_RESERVED_COLORS_BEGINNING + index;
            palette[at * 3] = colors[index].peRed; palette[at * 3 + 1] = colors[index].peGreen; palette[at * 3 + 2] = colors[index].peBlue;
        }
        delete[] colors;
    }
    std::vector<HoverNetTex::PackImage> images;
    std::string error;
    if (!HoverNetTex::LoadTexturePack(HoverNetTex::TexturePackPathFor("NetTarget/ObjFac1.dat").c_str(), images, &error)) {
        std::printf("no texture pack (%s): nothing to check\n", error.c_str());
        return 77;   // skipped
    }
    Check(!images.empty(), "the pack has images");

    MR_ResourceLib lib("NetTarget/ObjFac1.dat");   // loads the pack beside it
    MR_ResBitmap::SetColourPalette(palette.data(), 1.0f, false);
    int okAll = 0;
    double worstMean = 0, worstSeam = 0;
    std::size_t colourBytes = 0;
    for (const HoverNetTex::PackImage& image : images) {
        MR_ResBitmap* bitmap = lib.GetBitmap(image.id);
        char what[160];
        std::snprintf(what, sizeof what, "texture %d: the image belongs to a texture of ObjFac1.dat", image.id);
        if (bitmap == nullptr) { Check(false, what); continue; }
        int top = 0;
        for (int i = 1; i < bitmap->GetNbSubBitmap(); ++i) if (bitmap->GetXRes(i) * bitmap->GetYRes(i) > bitmap->GetXRes(top) * bitmap->GetYRes(top)) top = i;
        const int w = bitmap->GetXRes(top), h = bitmap->GetYRes(top);
        const int scale = image.width / w;
        const bool sized = image.width % w == 0 && image.height % h == 0 && image.height / h == scale && (scale == 2 || scale == 4 || scale == 8);
        std::snprintf(what, sizeof what, "texture %d: %dx%d is the texture's %dx%d at a power-of-two scale", image.id, image.width, image.height, w, h);
        if (!sized) { Check(false, what); continue; }

        // Not strayed from the original: the image averaged back to the texture's size, against the texture through the palette.
        const MR_UInt8* indices = bitmap->GetBuffer(top);
        double total = 0;
        for (int x = 0; x < w; ++x)
            for (int y = 0; y < h; ++y) {
                double r = 0, g = 0, b = 0;
                for (int i = 0; i < scale; ++i)
                    for (int j = 0; j < scale; ++j) {
                        const std::uint32_t c = image.words[static_cast<std::size_t>(x * scale + i) * image.height + (y * scale + j)];
                        r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
                    }
                const double n = scale * scale;
                const MR_UInt8* p = &palette[indices[static_cast<std::size_t>(x) * h + y] * 3];
                total += (std::fabs(r / n - p[0]) + std::fabs(g / n - p[1]) + std::fabs(b / n - p[2])) / 3.0;
            }
        const double meanDeviation = total / (static_cast<double>(w) * h);
        worstMean = std::max(worstMean, meanDeviation);
        std::snprintf(what, sizeof what, "texture %d: the image stays close to the original (mean difference %.1f levels)", image.id, meanDeviation);
        const bool close = meanDeviation <= 22.0;
        Check(close, what);

        // Tiling: the step across the wrap (last column to first) is no bigger than the steps between columns inside the image.
        double seam = 0, inner = 0;
        for (int y = 0; y < image.height; ++y) {
            const std::uint32_t a = image.words[static_cast<std::size_t>(image.width - 1) * image.height + y], b = image.words[y];
            const std::uint32_t c = image.words[static_cast<std::size_t>(image.width / 2) * image.height + y], d = image.words[static_cast<std::size_t>(image.width / 2 + 1) * image.height + y];
            seam += std::fabs(((a >> 8) & 255) - static_cast<double>((b >> 8) & 255)); inner += std::fabs(((c >> 8) & 255) - static_cast<double>((d >> 8) & 255));
        }
        seam /= image.height; inner /= image.height;
        // Some textures do not tile in the original either (an edge drawn into them): the image may not be worse than the texture it came from.
        double originalSeam = 0;
        for (int y = 0; y < h; ++y)
            originalSeam += std::fabs(static_cast<double>(palette[indices[static_cast<std::size_t>(w - 1) * h + y] * 3 + 1]) - palette[indices[y] * 3 + 1]);
        originalSeam /= h;
        worstSeam = std::max(worstSeam, seam);
        std::snprintf(what, sizeof what, "texture %d: the image tiles (the step across the wrap is %.1f, inside it %.1f, the original's %.1f; levels in green)", image.id, seam, inner, originalSeam);
        const bool tiles = seam <= std::max(inner * 3.0, originalSeam * 1.5) + 12.0;
        Check(tiles, what);

        // The texture uses it.
        const int shift = bitmap->GetColourScaleShift();
        std::snprintf(what, sizeof what, "texture %d: the true-colour copy has the image's scale (x%d)", image.id, scale);
        const bool scaled = (1 << shift) == scale && bitmap->GetColourColumnBuffer(top, 0) != nullptr;
        Check(scaled, what);
        for (int i = 0; i < bitmap->GetNbSubBitmap(); ++i) colourBytes += static_cast<std::size_t>(bitmap->GetXRes(i) << shift) * (bitmap->GetYRes(i) << shift) * 4;
        okAll += sized && close && tiles && scaled;
    }
    std::printf("%d of %zu pack images pass; worst mean difference from the original %.1f levels, worst seam step %.1f; the true-colour copies of all of them take %.1f MB\n", okAll, images.size(), worstMean, worstSeam, colourBytes / 1e6);

    // Switched off by the environment: a second library, built with HOVERNET_NO_AI_TEXTURES set, does not use the pack.
    {
        setenv("HOVERNET_NO_AI_TEXTURES", "1", 1);
        MR_ResourceLib plain("NetTarget/ObjFac1.dat");
        unsetenv("HOVERNET_NO_AI_TEXTURES");
        MR_ResBitmap* bitmap = plain.GetBitmap(images.front().id);
        Check(bitmap != nullptr && bitmap->GetColourScaleShift() == 1, "HOVERNET_NO_AI_TEXTURES switches the AI images off (the texture is 2x, from the palette)");
    }
    MR_ResBitmap::SetColourPalette(nullptr);
    std::printf("%s\n", gFailures == 0 ? "texture pack: all checks passed" : "texture pack: FAILED");
    return gFailures == 0 ? 0 : 1;
}
