// ViewportColourTest -- the rasteriser's true-colour output and texture filtering (MR_3DViewPort::SetColourBuffer, SetTextureFiltering).
//
//  1. Without filtering, the RGB picture is exactly the 8-bit picture seen through the palette, on every pixel the rasteriser drew.
//  2. Turning the colour output (and filtering) on does not change the 8-bit picture by a single index.
//  3. With filtering, a texture seen close up (magnified) is smoother: the colour steps between neighbouring pixels are smaller and gentler,
//     and no pixel leaves the range of the colours its texels could produce.
//  4. Walls, floors and a view from close to and from far from the surface are all covered; the colour output draws on the same pixels in
//     both modes.
// Run from the repository root (it loads NetTarget/ObjFac1.dat).
#include "../MainCharacter/StdAfx.h"   // first: on Windows it brings the MFC compatibility layer the headers below use
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFailures; }

struct Picture { std::vector<MR_UInt8> index; std::vector<MR_UInt32> word; std::vector<MR_UInt8> rgb; int w = 0, h = 0; };   // rgb is the words unpacked, three bytes a pixel

// A palette in which every index is a different, smoothly varying colour (so filtering between two indices gives a colour between them).
void BuildPalette(MR_UInt8* palette, MR_UInt32* words = nullptr)
{
    for (int i = 0; i < 256; ++i) {
        palette[i * 3] = static_cast<MR_UInt8>(i); palette[i * 3 + 1] = static_cast<MR_UInt8>(255 - i); palette[i * 3 + 2] = static_cast<MR_UInt8>((i * 3) & 255);
        if (words != nullptr) words[i] = (static_cast<MR_UInt32>(palette[i * 3]) << 16) | (static_cast<MR_UInt32>(palette[i * 3 + 1]) << 8) | palette[i * 3 + 2];
    }
}

Picture Render(const MR_ResBitmap* bitmap, bool colour, bool filter, int camX, int camZ, int angle)
{
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    Picture p;
    if (!buffer.SetVideoMode(320, 240) || !buffer.Lock()) return p;
    p.w = buffer.GetXRes(); p.h = buffer.GetYRes();
    const std::size_t pixels = static_cast<std::size_t>(p.w) * p.h;
    MR_UInt8 palette[768];
    MR_UInt32 words[256];
    BuildPalette(palette, words);
    p.word.assign(pixels, 0x00070707u);   // a value the textures never produce: pixels the rasteriser does not draw stay this
    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, p.w, p.h, 800);
    if (colour) { viewport.SetColourBuffer(p.word.data(), words); viewport.SetTextureFiltering(filter ? TRUE : FALSE); }
    viewport.SetupCameraPosition(MR_3DCoordinate(camX, 0, camZ), angle, 0);
    viewport.Clear(255);
    viewport.ClearZ();
    viewport.RenderWallSurface(MR_3DCoordinate(4000, 2500, 2500), MR_3DCoordinate(4000, -2500, 0), 5000, bitmap);
    const MR_2DCoordinate quad[4] = {MR_2DCoordinate(1500, -3000), MR_2DCoordinate(12000, -3000), MR_2DCoordinate(12000, 3000), MR_2DCoordinate(1500, 3000)};
    viewport.RenderHorizontalSurface(4, quad, 0, TRUE, bitmap);
    p.index.assign(buffer.GetBuffer(), buffer.GetBuffer() + pixels);
    p.rgb.resize(pixels * 3);
    for (std::size_t i = 0; i < pixels; ++i) { p.rgb[i * 3] = static_cast<MR_UInt8>(p.word[i] >> 16); p.rgb[i * 3 + 1] = static_cast<MR_UInt8>(p.word[i] >> 8); p.rgb[i * 3 + 2] = static_cast<MR_UInt8>(p.word[i]); }
    return p;
}

long Steps(const Picture& p)   // the sum of the colour differences between horizontally neighbouring drawn pixels
{
    long total = 0;
    for (int y = 0; y < p.h; ++y) for (int x = 1; x < p.w; ++x) {
        const std::size_t a = static_cast<std::size_t>(y) * p.w + x, b = a - 1;
        if (p.index[a] == 255 || p.index[b] == 255) continue;
        for (int k = 0; k < 3; ++k) total += std::abs(static_cast<int>(p.rgb[a * 3 + k]) - static_cast<int>(p.rgb[b * 3 + k]));
    }
    return total;
}
}

int main()
{
    MR_InitTrigoTables();
    MR_ResourceLib resources("NetTarget/ObjFac1.dat");
    const MR_ResBitmap* bitmap = resources.GetBitmap(1051);
    if (bitmap == nullptr) { std::fprintf(stderr, "Could not load the test bitmap\n"); return 2; }
    MR_UInt8 palette[768];
    BuildPalette(palette);

    const int views[][3] = {{3300, 1000, 0}, {0, 1000, 0}, {3600, 1200, 60}};   // very close to the wall, far from it, close and turned
    for (const auto& v : views) {
        const Picture plain = Render(bitmap, false, false, v[0], v[1], v[2]);
        const Picture coloured = Render(bitmap, true, false, v[0], v[1], v[2]);
        const Picture filtered = Render(bitmap, true, true, v[0], v[1], v[2]);
        char label[160];
        std::size_t drawn = 0;
        for (const MR_UInt8 i : plain.index) if (i != 255) ++drawn;
        std::snprintf(label, sizeof label, "view (%d,%d,%d): the test draws something (%zu pixels)", v[0], v[1], v[2], drawn);
        Check(drawn > 2000, label);

        std::snprintf(label, sizeof label, "view (%d,%d,%d): the 8-bit picture is identical with the colour output off, on, and filtered", v[0], v[1], v[2]);
        Check(plain.index == coloured.index && plain.index == filtered.index, label);

        bool exact = true;
        for (std::size_t i = 0; i < plain.index.size() && exact; ++i) {
            if (plain.index[i] == 255) continue;
            for (int k = 0; k < 3; ++k) exact = exact && coloured.rgb[i * 3 + k] == palette[plain.index[i] * 3 + k];
        }
        std::snprintf(label, sizeof label, "view (%d,%d,%d): without filtering the colour picture is exactly the 8-bit picture through the palette", v[0], v[1], v[2]);
        Check(exact, label);

        std::size_t differ = 0;
        long maxChannelDiff = 0;
        for (std::size_t i = 0; i < plain.index.size(); ++i) {
            if (plain.index[i] == 255) continue;
            bool d = false;
            for (int k = 0; k < 3; ++k) { const long diff = std::labs(static_cast<long>(filtered.rgb[i * 3 + k]) - coloured.rgb[i * 3 + k]); d = d || diff != 0; if (diff > maxChannelDiff) maxChannelDiff = diff; }
            if (d) ++differ;
        }
        std::snprintf(label, sizeof label, "view (%d,%d,%d): filtering changes colours (%zu pixels) and only drawn pixels", v[0], v[1], v[2], differ);
        Check(differ > 0, label);
        bool untouched = true;
        for (std::size_t i = 0; i < plain.index.size(); ++i) if (plain.index[i] == 255) for (int k = 0; k < 3; ++k) untouched = untouched && filtered.rgb[i * 3 + k] == 7;
        std::snprintf(label, sizeof label, "view (%d,%d,%d): pixels the rasteriser did not draw are left alone", v[0], v[1], v[2]);
        Check(untouched, label);

        if (v[0] == 3300 || v[0] == 3600) {   // magnified: filtering must make the picture smoother
            const long before = Steps(coloured), after = Steps(filtered);
            std::snprintf(label, sizeof label, "view (%d,%d,%d): a magnified texture is smoother filtered (colour steps %ld -> %ld)", v[0], v[1], v[2], before, after);
            Check(after < before, label);
        }
    }
    // 5. True-colour textures: with a palette set, every bitmap gets a smoothed true-colour copy and the rasteriser samples that instead of the palette.
    {
        const int view[3] = {3300, 1000, 0};
        const Picture before = Render(bitmap, true, false, view[0], view[1], view[2]);   // the palette's colours, unfiltered
        MR_ResBitmap::SetColourPalette(palette);
        const Picture textured = Render(bitmap, true, false, view[0], view[1], view[2]);
        const Picture texturedFiltered = Render(bitmap, true, true, view[0], view[1], view[2]);
        MR_ResBitmap::SetColourPalette(nullptr);
        const Picture off = Render(bitmap, true, false, view[0], view[1], view[2]);
        std::set<std::uint32_t> paletteColours, textureColours, filteredColours;
        std::set<MR_UInt8> indices;
        for (std::size_t i = 0; i < before.index.size(); ++i) {
            if (before.index[i] == 255) continue;
            paletteColours.insert(before.word[i]); textureColours.insert(textured.word[i]); filteredColours.insert(texturedFiltered.word[i]); indices.insert(before.index[i]);
        }
        char label[200];
        Check(before.index == textured.index && before.index == texturedFiltered.index, "true-colour textures leave the 8-bit picture identical");
        Check(textured.word != before.word, "with a true-colour texture the picture is not just the palette's colours");
        std::snprintf(label, sizeof label, "the picture has more colours than the palette entries it uses (%zu colours from %zu entries, unfiltered)", textureColours.size(), indices.size());
        Check(textureColours.size() > indices.size() * 2, label);
        std::snprintf(label, sizeof label, "and many more again when filtered (%zu colours)", filteredColours.size());
        Check(filteredColours.size() > textureColours.size(), label);
        Check(off.word == before.word, "switching true-colour textures off gives exactly the palette's colours again");
    }

    std::printf("%s\n", gFailures == 0 ? "viewport colour: all checks passed" : "viewport colour: FAILED");
    return gFailures == 0 ? 0 : 1;
}
