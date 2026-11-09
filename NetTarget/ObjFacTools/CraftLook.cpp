#include "StdAfx.h"
#include "CraftLook.h"

#include "ResActor.h"
#include "ResBitmap.h"
#include "../ObjFac1/ObjFac1Res.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace HoverNetCraft
{
namespace
{
struct Variant
{
    std::unique_ptr<MR_ResActor> actor;
    std::vector<std::unique_ptr<MR_ResBitmap>> bitmaps;
    MR_ResBitmap* cockpit = nullptr;
};

std::mutex gMutex;
std::map<int, Authority::CraftDesign> gDesigns;   // by slot
std::map<std::uint64_t, std::unique_ptr<Variant>> gVariants;

inline int R(std::uint32_t c) { return (c >> 16) & 255; }
inline int G(std::uint32_t c) { return (c >> 8) & 255; }
inline int B(std::uint32_t c) { return c & 255; }
inline std::uint32_t Pack(int r, int g, int b) { return (static_cast<std::uint32_t>((std::max)(0, (std::min)(255, r))) << 16) | (static_cast<std::uint32_t>((std::max)(0, (std::min)(255, g))) << 8) | static_cast<std::uint32_t>((std::max)(0, (std::min)(255, b))); }
inline int Luma(std::uint32_t c) { return (R(c) * 299 + G(c) * 587 + B(c) * 114) / 1000; }

// The tinted colour for a pixel: the shading of the pixel kept, the hue of the colour, highlights going towards white.
std::uint32_t Shaded(const std::uint8_t rgb[3], int luma)
{
    const int f = luma * 256 / 128;   // 256 = the colour as it is, at mid grey
    int c[3];
    for (int i = 0; i < 3; ++i) c[i] = f <= 256 ? rgb[i] * f / 256 : rgb[i] + (255 - rgb[i]) * (f - 256) / 256;
    return Pack(c[0], c[1], c[2]);
}
}

std::uint32_t TintColour(std::uint32_t pixel, const std::uint8_t rgb[3], int strength)
{
    if (strength <= 0) return pixel;
    const std::uint32_t t = Shaded(rgb, Luma(pixel));
    const int s = (std::min)(255, strength);
    return Pack((R(pixel) * (255 - s) + R(t) * s) / 255, (G(pixel) * (255 - s) + G(t) * s) / 255, (B(pixel) * (255 - s) + B(t) * s) / 255);
}

std::uint32_t PaintColour(std::uint32_t pixel, const std::uint8_t rgb[3])
{
    return Shaded(rgb, 40 + Luma(pixel) * 3 / 4 + 40);   // paint covers the hull colour but keeps its light and shade
}

Part PartOfBitmap(int id)
{
    if (id == MR_CAR_COCKPIT || id == MR_CAR2_COCKPIT || id == MR_ECAR_COCKPIT || (id >= MR_CAR_COCKPIT1 && id < MR_CAR_COCKPIT1 + 10) || (id >= MR_CAR_COCKPIT21 && id < MR_CAR_COCKPIT21 + 10)) return Part::eCockpit;
    if (id == MR_CAR_MOTOR || id == MR_CAR_MOTOR_ARM || id == MR_CAR_MOTOR_FIRE || id == MR_CAR_MOTOR2 || id == MR_CAR_DRIVER_HEAD || id == MR_CAR_DRIVER_NECK) return Part::eOther;
    return Part::eHull;
}

void SetSlotDesign(int slot, const Authority::CraftDesign* design)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (design == nullptr || design->IsPlain()) gDesigns.erase(slot);
    else gDesigns[slot] = Authority::SanitiseCraftDesign(*design);
}

void ClearSlotDesigns()
{
    std::lock_guard<std::mutex> lock(gMutex);
    gDesigns.clear();
}

bool HasSlotDesign(int slot)
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gDesigns.count(slot) != 0;
}

void ClearCache()
{
    std::lock_guard<std::mutex> lock(gMutex);
    gVariants.clear();
}

Look Resolve(int slot, const MR_ResActor* stockActor, const MR_ResBitmap* stockCockpit)
{
    Look stock{stockActor, stockCockpit};
    if (stockActor == nullptr) return stock;
    std::lock_guard<std::mutex> lock(gMutex);
    const auto found = gDesigns.find(slot);
    if (found == gDesigns.end()) return stock;
    const Authority::CraftDesign& d = found->second;

    std::uint64_t key = Authority::CraftDesignHash(d) * 1099511628211ULL;
    key ^= static_cast<std::uint64_t>(MR_ResBitmap::GetBakePaletteVersion()) * 0x2545F4914F6CDD1DULL;   // a new palette makes new copies
    key ^= reinterpret_cast<std::uintptr_t>(stockActor) * 0x9e3779b97f4a7c15ULL;
    key ^= reinterpret_cast<std::uintptr_t>(stockCockpit) * 0xc2b2ae3d27d4eb4fULL;
    auto at = gVariants.find(key);
    if (at == gVariants.end()) {
        // A bounded cache: the designs of one race at most; a long session that sees many players starts afresh rather than growing.
        if (gVariants.size() >= 48) gVariants.clear();
        std::unique_ptr<Variant> v(new Variant);
        std::map<const MR_ResBitmap*, MR_ResBitmap*> made;
        const Authority::CraftDesign design = d;
        const auto make = [&](const MR_ResBitmap* source, MR_ResBitmap::Recolour recolour) -> MR_ResBitmap* {
            const auto it = made.find(source);
            if (it != made.end()) return it->second;
            v->bitmaps.emplace_back(source->CloneWithRecolour(recolour));
            made[source] = v->bitmaps.back().get();
            return v->bitmaps.back().get();
        };
        const auto mapBitmap = [&](const MR_ResBitmap* source) -> const MR_ResBitmap* {
            if (source == nullptr) return source;
            const Part part = PartOfBitmap(source->GetResourceId());
            if (part == Part::eOther) return source;
            if (part == Part::eCockpit) {
                if (design.accentStrength == 0) return source;
                return make(source, [design](std::vector<MR_UInt32>& words, int, int) { for (MR_UInt32& w : words) w = TintColour(w, design.accent.data(), design.accentStrength); });
            }
            bool painted = false;
            for (std::uint8_t p : design.paint) if (p != 0) { painted = true; break; }
            if (design.bodyStrength == 0 && !painted) return source;
            return make(source, [design, painted](std::vector<MR_UInt32>& words, int w, int h) {
                for (int x = 0; x < w; ++x)
                    for (int y = 0; y < h; ++y) {
                        MR_UInt32& px = words[static_cast<std::size_t>(x) * h + y];
                        px = TintColour(px, design.body.data(), design.bodyStrength);
                        if (painted) {
                            const int pu = x * Authority::kPaintSize / w, pv = y * Authority::kPaintSize / h;
                            const std::uint8_t index = design.paint[static_cast<std::size_t>(pv) * Authority::kPaintSize + pu];
                            if (index != 0) px = PaintColour(px, design.palette[index].data());
                        }
                    }
            });
        };
        v->actor.reset(stockActor->CreateVariant(d.length, d.width, d.height, d.taper, mapBitmap));
        if (stockCockpit != nullptr) v->cockpit = const_cast<MR_ResBitmap*>(mapBitmap(stockCockpit));
        at = gVariants.emplace(key, std::move(v)).first;
    }
    Look look;
    look.actor = at->second->actor.get();
    look.cockpit = at->second->cockpit != nullptr ? at->second->cockpit : stockCockpit;
    return look;
}
}
