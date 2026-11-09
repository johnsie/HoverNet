// CraftLookTest -- a craft design changes how a craft is drawn and nothing else. Run from the repository root (it loads NetTarget/ObjFac1.dat).
#include "../MainCharacter/StdAfx.h"
#include "../ObjFac1/ObjFac1Res.h"
#include "CraftPreview.h"
#include "../ObjFacTools/CraftLook.h"
#include "../ObjFacTools/ResActor.h"
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../ObjFacTools/ResSprite.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <array>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; }

struct Picture { std::vector<MR_UInt8> indices; std::vector<MR_UInt32> colour; int drawn = 0; };

std::array<MR_UInt32, 256> gWords;

Picture Draw(const MR_ResActor* actor)
{
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    Picture p;
    if (!buffer.SetVideoMode(320, 240) || !buffer.Lock()) return p;
    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, 320, 240, 800);
    std::vector<MR_UInt32> colour(320 * 240, 0);
    viewport.SetColourBuffer(colour.data(), gWords.data());
    viewport.SetTextureFiltering(TRUE);
    viewport.SetupCameraPosition(MR_3DCoordinate(-4500, 0, 700), 0, 0);
    viewport.Clear(255);
    viewport.ClearZ();
    MR_PositionMatrix matrix;
    if (!viewport.ComputePositionMatrix(matrix, MR_3DCoordinate(0, 0, 0), 0, 10000000)) return p;
    actor->Draw(&viewport, matrix, 0, 0);
    p.indices.assign(buffer.GetBuffer(), buffer.GetBuffer() + 320 * 240);
    p.colour = colour;
    for (MR_UInt8 v : p.indices) if (v != 255) ++p.drawn;
    return p;
}
}

int main()
{
    MR_InitTrigoTables();
    std::array<MR_UInt8, 768> palette{};
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int i = 0; i < MR_BASIC_COLORS; ++i) { const int at = MR_RESERVED_COLORS_BEGINNING + i; palette[at * 3] = colors[i].peRed; palette[at * 3 + 1] = colors[i].peGreen; palette[at * 3 + 2] = colors[i].peBlue; }
        delete[] colors;
    }
    for (int i = 0; i < 256; ++i) gWords[i] = (static_cast<MR_UInt32>(palette[i * 3]) << 16) | (static_cast<MR_UInt32>(palette[i * 3 + 1]) << 8) | palette[i * 3 + 2];
    setenv("HOVERNET_NO_AI_TEXTURES", "1", 1);
    MR_ResourceLib lib("NetTarget/ObjFac1.dat");
    MR_ResBitmap::SetColourPalette(palette.data(), 1.0f, false);
    MR_ResBitmap::SetBakePalette(palette.data());   // designs are baked into the 8-bit pixels too

    if (std::getenv("HOVERNET_FONT_DUMP")) {
        const MR_ResSprite* font = lib.GetSprite(1);
        std::printf("font: items %d item height %d width %d data %p\n", font->GetNbItem(), font->GetItemHeight(), font->GetWidth(), (const void*)font->GetData());
        long hist[256] = {0};
        const long total = (long)font->GetNbItem() * font->GetItemHeight() * font->GetWidth();
        for (long i = 0; i < total; ++i) hist[font->GetData()[i]]++;
        for (int i = 0; i < 256; ++i) if (hist[i]) std::printf("index %3d  %6ld px  rgb %3d %3d %3d\n", i, hist[i], palette[i*3], palette[i*3+1], palette[i*3+2]);
    }
    const int ids[5] = {MR_ELECTRO_CAR, MR_HITECH_CAR, MR_BITURBO_CAR, 19, MR_MANTA_CRAFT};
    for (int c = 0; c < 5; ++c) {
        const MR_ResActor* actor = lib.GetActor(ids[c]);
        char what[160];
        std::snprintf(what, sizeof what, "craft %d: the model loads", c);
        Check(actor != nullptr, what);
        if (actor == nullptr) continue;
        MR_3DCoordinate lo, hi;
        actor->GetBounds(lo, hi);
        std::printf("craft %d bounds x %d..%d  y %d..%d  z %d..%d (mm)\n", c, lo.mX, hi.mX, lo.mY, hi.mY, lo.mZ, hi.mZ);

        // Shape: every vertex moves as the sliders say, nothing else about the model changes.
        std::unique_ptr<MR_ResActor> shaped(actor->CreateVariant(20, -10, 25, 0, nullptr));
        MR_3DCoordinate slo, shi;
        shaped->GetBounds(slo, shi);
        const double lx = (double)(shi.mX - slo.mX) / (hi.mX - lo.mX), ly = (double)(shi.mY - slo.mY) / (hi.mY - lo.mY), lz = (double)(shi.mZ - slo.mZ) / (hi.mZ - lo.mZ);
        std::snprintf(what, sizeof what, "craft %d: length +20%% width -10%% height +25%% give x%.3f x%.3f x%.3f", c, lx, ly, lz);
        Check(std::fabs(lx - 1.2) < 0.01 && std::fabs(ly - 0.9) < 0.02 && std::fabs(lz - 1.25) < 0.02, what);
        std::snprintf(what, sizeof what, "craft %d: the lowest point stays where it was (the craft still sits on its cushion)", c);
        Check(slo.mZ == lo.mZ, what);
        std::unique_ptr<MR_ResActor> same(actor->CreateVariant(0, 0, 0, 0, nullptr));
        const Picture a = Draw(actor), b = Draw(same.get());
        std::snprintf(what, sizeof what, "craft %d: a copy with no changes draws the same picture (%d pixels)", c, a.drawn);
        Check(a.drawn > 200 && a.indices == b.indices && a.colour == b.colour, what);
        const Picture s = Draw(shaped.get());
        std::snprintf(what, sizeof what, "craft %d: a reshaped copy draws a different picture", c);
        Check(s.indices != a.indices, what);
    }

    // Slots: without a design everything is stock; a plain design is no design; a real one gives a copy, kept for the next call.
    const MR_ResActor* actor = lib.GetActor(MR_ELECTRO_CAR);
    const MR_ResBitmap* cockpit = lib.GetBitmap(MR_CAR_COCKPIT1 + 3);
    Check(HoverNetCraft::Resolve(3, actor, cockpit).actor == actor && HoverNetCraft::Resolve(3, actor, cockpit).cockpit == cockpit, "no design: the stock model and cockpit");
    Authority::CraftDesign plain;
    HoverNetCraft::SetSlotDesign(3, &plain);
    Check(!HoverNetCraft::HasSlotDesign(3) && HoverNetCraft::Resolve(3, actor, cockpit).actor == actor, "a plain design is no design");
    Authority::CraftDesign d;
    d.length = 10; d.body = {{220, 30, 30}}; d.bodyStrength = 220; d.accent = {{30, 200, 60}}; d.accentStrength = 255;
    for (int i = 0; i < Authority::kPaintSize; ++i) d.paint[static_cast<size_t>(i) * Authority::kPaintSize + 8] = 3;   // a stripe
    HoverNetCraft::SetSlotDesign(3, &d);
    const HoverNetCraft::Look look = HoverNetCraft::Resolve(3, actor, cockpit);
    Check(look.actor != actor && look.actor != nullptr && look.cockpit != cockpit, "a design gives its own model and cockpit");
    Check(HoverNetCraft::Resolve(3, actor, cockpit).actor == look.actor, "the copy is made once and kept");
    Check(HoverNetCraft::Resolve(4, actor, cockpit).actor == actor, "another slot is untouched");

    // The picture: the designed craft is red where the stock one is not, and draws where the stock one draws (plus the reshaping).
    const Picture stock = Draw(actor), mine = Draw(look.actor);
    long redder = 0, total = 0;
    for (size_t i = 0; i < stock.colour.size(); ++i) {
        if (stock.indices[i] == 255 || mine.indices[i] == 255) continue;
        ++total;
        const int sr = (stock.colour[i] >> 16) & 255, mr = (mine.colour[i] >> 16) & 255, sb = stock.colour[i] & 255, mb = mine.colour[i] & 255;
        if (mr - mb > sr - sb + 30) ++redder;
    }
    { long diff = 0; for (size_t i = 0; i < stock.colour.size(); ++i) if (stock.colour[i] != mine.colour[i]) ++diff; std::printf("%ld colour words differ; stock drew %d, designed drew %d\n", diff, stock.drawn, mine.drawn); }
    std::printf("%ld of %ld pixels drawn by both are clearly redder\n", redder, total);
    Check(total > 100 && redder > total / 4, "the body tint shows in the picture");

    // The Garage preview draws each craft, stock and designed, and the two differ.
    for (int c = 0; c < 5; ++c) {
        std::vector<std::uint32_t> a, b;
        char what[120];
        std::snprintf(what, sizeof what, "craft %d: the Garage preview draws it and the design shows", c);
        const bool ok = HoverNetCraft::RenderCraftPreview(lib, nullptr, c, 25, gWords.data(), 480, 360, a) && HoverNetCraft::RenderCraftPreview(lib, &d, c, 25, gWords.data(), 480, 360, b);
        Check(ok && a != b, what);
        if (const char* dump = std::getenv("HOVERNET_CRAFT_DUMP")) {
            for (int v = 0; v < 2; ++v) {
                const std::vector<std::uint32_t>& img = v == 0 ? a : b;
                char path[256]; std::snprintf(path, sizeof path, "%s/craft%d_%s.ppm", dump, c, v == 0 ? "stock" : "design");
                if (FILE* f = std::fopen(path, "wb")) { std::fprintf(f, "P6\n480 360\n255\n"); for (std::uint32_t p : img) { std::fputc(p & 255, f); std::fputc((p >> 8) & 255, f); std::fputc((p >> 16) & 255, f); } std::fclose(f); }
            }
        }
    }

    // Every control of the Garage must show on the preview craft: each single change, on every hull, changes a visible number of pixels (a control that
    // did nothing you could see would be a bug). The picture is the one the Garage draws (480x360), at the two angles the turntable passes through.
    {
        struct Change { const char* name; void (*apply)(Authority::CraftDesign&); int minPixels; };
        const Change changes[] = {
            {"length +25%", [](Authority::CraftDesign& x) { x.length = 25; }, 400},
            {"length -25%", [](Authority::CraftDesign& x) { x.length = -25; }, 400},
            {"width +25%", [](Authority::CraftDesign& x) { x.width = 25; }, 400},
            {"width -25%", [](Authority::CraftDesign& x) { x.width = -25; }, 400},
            {"height +25%", [](Authority::CraftDesign& x) { x.height = 25; }, 400},
            {"height -25%", [](Authority::CraftDesign& x) { x.height = -25; }, 400},
            {"nose wide +30%", [](Authority::CraftDesign& x) { x.taper = 30; }, 150},
            {"nose narrow -30%", [](Authority::CraftDesign& x) { x.taper = -30; }, 150},
            {"body colour blue, full", [](Authority::CraftDesign& x) { x.body = {{20, 60, 230}}; x.bodyStrength = 255; }, 1500},
            {"body colour red, half", [](Authority::CraftDesign& x) { x.body = {{230, 30, 30}}; x.bodyStrength = 128; }, 1000},
            {"trim colour green, full", [](Authority::CraftDesign& x) { x.accent = {{20, 220, 60}}; x.accentStrength = 255; }, 60},
            {"paint: a 4-cell stripe", [](Authority::CraftDesign& x) { for (int i = 0; i < Authority::kPaintSize; ++i) for (int j = 12; j < 16; ++j) x.paint[static_cast<size_t>(i) * Authority::kPaintSize + j] = 5; }, 400},
            {"paint: a coarse checkerboard", [](Authority::CraftDesign& x) { for (int i = 0; i < Authority::kPaintSize; ++i) for (int j = 0; j < Authority::kPaintSize; ++j) if (((i / 4) + (j / 4)) % 2 == 0) x.paint[static_cast<size_t>(i) * Authority::kPaintSize + j] = 3; }, 800},
            {"paint: the whole canvas", [](Authority::CraftDesign& x) { x.paint.fill(8); }, 1500},
        };
        int worst = 1 << 30; const char* worstName = ""; int worstCraft = 0; bool allVisible = true;
        for (int c = 0; c < 5; ++c) {
            for (const Change& change : changes) {
                Authority::CraftDesign x;
                change.apply(x);
                int best = 0;
                for (int yaw : {25, 205}) {
                    std::vector<std::uint32_t> a, b;
                    HoverNetCraft::RenderCraftPreview(lib, nullptr, c, yaw, gWords.data(), 480, 360, a);
                    HoverNetCraft::RenderCraftPreview(lib, &x, c, yaw, gWords.data(), 480, 360, b);
                    int diff = 0;
                    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++diff;
                    best = std::max(best, diff);
                }
                if (best < worst) { worst = best; worstName = change.name; worstCraft = c; }
                if (best < change.minPixels) { allVisible = false; std::printf("FAIL: craft %d: '%s' changes only %d pixels of the preview (needs %d)\n", c, change.name, best, change.minPixels); ++gFail; }
            }
        }
        std::printf("least visible change: craft %d, %s: %d pixels\n", worstCraft, worstName, worst);
        Check(allVisible, "every Garage control visibly changes the preview craft, on every hull");
    }

    // Colour functions.
    const std::uint8_t red[3] = {255, 0, 0};
    Check(HoverNetCraft::TintColour(0x00808080, red, 0) == 0x00808080, "tint strength 0 leaves the pixel");
    Check(((HoverNetCraft::TintColour(0x00808080, red, 255) >> 16) & 255) > 200, "full tint of mid grey with red is red");
    Check(HoverNetCraft::PartOfBitmap(MR_CAR_COCKPIT) == HoverNetCraft::Part::eCockpit && HoverNetCraft::PartOfBitmap(MR_CAR_MOTOR_FIRE) == HoverNetCraft::Part::eOther &&
          HoverNetCraft::PartOfBitmap(MR_STD_WALL) == HoverNetCraft::Part::eHull, "parts of a craft are told apart");

    HoverNetCraft::ClearCache();
    HoverNetCraft::SetSlotDesign(3, nullptr);
    MR_ResBitmap::SetColourPalette(nullptr);
    std::printf(gFail == 0 ? "craft look: all checks passed\n" : "craft look: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
