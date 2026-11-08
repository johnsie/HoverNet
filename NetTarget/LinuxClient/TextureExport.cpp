// TextureExport -- a development tool: writes every bitmap in the game's resource file (ObjFac1.dat) as an image, for looking at them or for improving
// them offline (docs/texture-pipeline.md). Not part of the game.
//
//   HoverNetTextureExport <ObjFac1.dat> <output directory>
//
// For each bitmap it writes <id>.ppm (the largest level, through the game's palette) and one line in index.tsv:
//   id  width_mm  height_mm  levels  xres  yres  [xres yres of each further level ...]  distinct_colours
#include "../MainCharacter/StdAfx.h"   // first: on Windows it brings the MFC compatibility layer the headers below use
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/ColorPalette.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <ObjFac1.dat> <output directory>\n", argv[0]); return 2; }
    MR_InitTrigoTables();
    MR_ResourceLib resources(argv[1]);
    std::array<std::uint8_t, MR_NB_COLORS * 3> palette{};
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);   // the colours the game shows (gamma and intensity as in the client)
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int at = MR_RESERVED_COLORS_BEGINNING + index;
            palette[at * 3] = colors[index].peRed; palette[at * 3 + 1] = colors[index].peGreen; palette[at * 3 + 2] = colors[index].peBlue;
        }
        delete[] colors;
    }
    const std::string out = argv[2];
    std::FILE* index = std::fopen((out + "/index.tsv").c_str(), "w");
    if (index == nullptr) { std::fprintf(stderr, "cannot write into %s\n", argv[2]); return 1; }
    int written = 0;
    for (int id = 0; id < 20000; ++id) {
        MR_ResBitmap* bitmap = resources.GetBitmap(id);
        if (bitmap == nullptr || bitmap->GetNbSubBitmap() <= 0) continue;
        int top = 0;
        for (int i = 1; i < bitmap->GetNbSubBitmap(); ++i) if (bitmap->GetXRes(i) * bitmap->GetYRes(i) > bitmap->GetXRes(top) * bitmap->GetYRes(top)) top = i;
        const int w = bitmap->GetXRes(top), h = bitmap->GetYRes(top);
        const MR_UInt8* data = bitmap->GetBuffer(top);   // column-major: texel (x, y) at [x * h + y]
        std::set<int> used;
        char name[64];
        std::snprintf(name, sizeof name, "/%d.ppm", id);
        if (std::FILE* f = std::fopen((out + name).c_str(), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const int v = data[static_cast<std::size_t>(x) * h + y];
                used.insert(v);
                std::fputc(palette[v * 3], f); std::fputc(palette[v * 3 + 1], f); std::fputc(palette[v * 3 + 2], f);
            }
            std::fclose(f);
        }
        std::fprintf(index, "%d\t%d\t%d\t%d", id, bitmap->GetWidth(), bitmap->GetHeight(), bitmap->GetNbSubBitmap());
        for (int i = 0; i < bitmap->GetNbSubBitmap(); ++i) std::fprintf(index, "\t%d\t%d", bitmap->GetXRes(i), bitmap->GetYRes(i));
        std::fprintf(index, "\t%zu\n", used.size());
        ++written;
    }
    std::fclose(index);
    std::printf("%d bitmaps written to %s (of %d in the file)\n", written, argv[2], resources.GetBitmapCount());
    return 0;
}
