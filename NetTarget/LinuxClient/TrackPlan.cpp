#include "TrackPlan.h"

#include "../Model/GameSession.h"
#include "../Model/RaceEffects.h"
#include "../Util/DllObjectFactory.h"

#include <algorithm>
#include <cmath>
#include <typeinfo>
#include <utility>

namespace HoverNetPlan
{
namespace
{
const Colours kColours = {{232, 50, 42}, {255, 166, 42}, {255, 214, 90}, {85, 214, 255}};

struct Pt { float x, y; };

// Even-odd scanline fill of one polygon into `value` (a height 0..1 plus one so 0 stays "empty"), at the supersampled size.
void FillPolygon(std::vector<float>& value, int w, int h, const std::vector<Pt>& poly, float v)
{
    float ymin = 1e9f, ymax = -1e9f;
    for (const Pt& p : poly) { ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y); }
    const int y0 = std::max(0, static_cast<int>(std::floor(ymin))), y1 = std::min(h - 1, static_cast<int>(std::ceil(ymax)));
    std::vector<float> xs;
    for (int y = y0; y <= y1; ++y) {
        const float sy = static_cast<float>(y) + 0.5f;
        xs.clear();
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i]; const Pt& b = poly[(i + 1) % poly.size()];
            if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5f))), xb = std::min(w - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5f)));
            for (int x = xa; x <= xb; ++x) value[static_cast<std::size_t>(y) * w + x] = v;
        }
    }
}
}

const Colours& PadColours() { return kColours; }

namespace { TextureReader gReader = nullptr; }
void SetTextureReader(TextureReader reader) { gReader = reader; }

std::mutex& LevelLock() { static std::mutex lock; return lock; }

Plan BuildPlan(const std::string& trackPath, const std::string& trackName, int maxSide, bool withTextures)
{
    Plan plan;
    std::lock_guard<std::mutex> guard(LevelLock());
    MR_RecordFile* track = new MR_RecordFile;   // the session takes it over once it has loaded
    if (!track->OpenForRead(trackPath.c_str())) { delete track; return plan; }
    MR_GameSession session(FALSE);
    if (!session.LoadNew(trackName.c_str(), track) || session.GetCurrentLevel() == nullptr) return plan;
    const MR_Level* level = session.GetCurrentLevel();
    const int roomCount = level->GetRoomCount();
    if (roomCount <= 0) return plan;
    plan.rooms = roomCount;

    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    int zMin = 1 << 30, zMax = -(1 << 30);
    for (int r = 0; r < roomCount; ++r) {
        for (int v = 0; v < level->GetRoomVertexCount(r); ++v) {
            const MR_2DCoordinate& c = level->GetRoomVertex(r, v);
            x0 = std::min<double>(x0, c.mX); x1 = std::max<double>(x1, c.mX); y0 = std::min<double>(y0, c.mY); y1 = std::max<double>(y1, c.mY);
        }
        const int z = level->GetRoomBottomLevel(r);
        zMin = std::min(zMin, z); zMax = std::max(zMax, z);
    }
    if (!(x1 > x0) || !(y1 > y0)) return plan;

    const int kSS = 2, pad = 6;
    const double span = std::max(x1 - x0, y1 - y0);
    const double scale = (maxSide - 2 * pad) * kSS / span;
    const int W = static_cast<int>((x1 - x0) * scale) + 2 * pad * kSS, H = static_cast<int>((y1 - y0) * scale) + 2 * pad * kSS;
    auto map = [&](const MR_2DCoordinate& c) { return Pt{static_cast<float>(pad * kSS + (c.mX - x0) * scale), static_cast<float>(pad * kSS + (y1 - c.mY) * scale)}; };

    const double cxm = (x0 + x1) * 0.5, cym = (y0 + y1) * 0.5;
    plan.spanMm = span; plan.cxMm = cxm; plan.cyMm = cym;
    std::vector<std::pair<const void*, int>> seen;   // each surface's picture is read once
    auto textureOf = [&](const void* element) -> int {
        if (!withTextures || gReader == nullptr || element == nullptr) return -1;
        for (const auto& s : seen) if (s.first == element) return s.second;
        Texture tex;
        int index = -1;
        if (gReader(element, tex) && tex.width > 0 && tex.height > 0 && plan.textures.size() < 64) { plan.textures.push_back(std::move(tex)); index = static_cast<int>(plan.textures.size()) - 1; }
        seen.push_back({element, index});
        return index;
    };
    auto norm = [&](const MR_2DCoordinate& c, float& nx, float& ny) { nx = static_cast<float>((c.mX - cxm) / span); ny = static_cast<float>((c.mY - cym) / span); };
    // rooms, lowest floor first so a bridge is drawn over the road under it
    std::vector<int> order(static_cast<std::size_t>(roomCount));
    for (int r = 0; r < roomCount; ++r) order[static_cast<std::size_t>(r)] = r;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return level->GetRoomBottomLevel(a) < level->GetRoomBottomLevel(b); });
    std::vector<float> height(static_cast<std::size_t>(W) * H, -1.0f);   // -1 = nothing here
    for (int r : order) {
        const int n = level->GetRoomVertexCount(r);
        if (n < 3) continue;
        std::vector<Pt> poly;
        for (int v = 0; v < n; ++v) poly.push_back(map(level->GetRoomVertex(r, v)));
        const float t = zMax > zMin ? static_cast<float>(level->GetRoomBottomLevel(r) - zMin) / static_cast<float>(zMax - zMin) : 0.0f;
        FillPolygon(height, W, H, poly, t);
        for (int v = 0; v < n; ++v) {   // the edges with no room beyond them
            if (level->GetNeighbor(r, v) >= 0) continue;
            Plan::Wall wall; wall.h = t; wall.texture = textureOf(level->GetRoomWallElement(r, v));
            norm(level->GetRoomVertex(r, v), wall.x0, wall.y0);
            norm(level->GetRoomVertex(r, (v + 1) % n), wall.x1, wall.y1);
            plan.walls.push_back(wall);
        }
        Plan::Shape shape; shape.h = t; shape.texture = textureOf(level->GetRoomBottomElement(r));
        for (int v = 0; v < n; ++v) { float nx, ny; norm(level->GetRoomVertex(r, v), nx, ny); shape.xy.push_back(nx); shape.xy.push_back(ny); }
        plan.shapes.push_back(shape);
    }

    plan.relief = static_cast<float>((zMax - zMin) / span);
    // pads, from the elements in the rooms
    struct Pad { Pt at; const std::uint8_t* colour; float radius; };
    auto roomHeight = [&](int r) { return zMax > zMin ? static_cast<float>(level->GetRoomBottomLevel(r) - zMin) / static_cast<float>(zMax - zMin) : 0.0f; };
    std::vector<Pad> pads;
    for (int r = 0; r < roomCount; ++r) {
        for (MR_FreeElementHandle handle = level->GetFirstFreeElement(r); handle != nullptr; handle = MR_Level::GetNextFreeElement(handle)) {
            MR_FreeElement* element = MR_Level::GetFreeElement(handle);
            if (element == nullptr) continue;
            const MR_ContactEffectList* effects = element->GetEffectList();
            if (effects == nullptr) continue;
            const std::uint8_t* colour = nullptr;
            int kind = 0;
            float radius = 5.0f;
            for (POSITION pos = effects->GetHeadPosition(); pos != nullptr;) {
                const MR_ContactEffect* effect = effects->GetNext(pos);
                if (dynamic_cast<const MR_FuelGain*>(effect) != nullptr) { colour = kColours.fuel; kind = 0; ++plan.fuelPads; }
                else if (dynamic_cast<const MR_SpeedDoubler*>(effect) != nullptr) { colour = kColours.boost; kind = 1; ++plan.boostPads; }
                else if (const MR_CheckPoint* check = dynamic_cast<const MR_CheckPoint*>(effect)) { if (check->mType == MR_CheckPoint::eFinishLine) { colour = kColours.finish; kind = 2; ++plan.finishLines; radius = 6.5f; } }
                else if (dynamic_cast<const MR_PowerUpEffect*>(effect) != nullptr) { colour = kColours.power; kind = 3; ++plan.powerUps; radius = 3.4f; }
            }
            if (colour != nullptr) { Plan::Mark mark; mark.kind = kind; mark.h = roomHeight(r); norm(MR_2DCoordinate{element->mPosition.mX, element->mPosition.mY}, mark.x, mark.y); plan.marks.push_back(mark); }
            if (colour != nullptr) pads.push_back({map(MR_2DCoordinate{element->mPosition.mX, element->mPosition.mY}), colour, radius * kSS});
        }
    }

    // down to the output size, drawing the colour, the edge and the pads
    const int ow = W / kSS, oh = H / kSS;
    plan.width = ow; plan.height = oh; plan.rgba.assign(static_cast<std::size_t>(ow) * oh * 4, 0);
    auto cover = [&](int x, int y, float& hsum) {   // how much of the output pixel is road, and the average floor height
        float c = 0; hsum = 0;
        for (int sy = 0; sy < kSS; ++sy) for (int sx = 0; sx < kSS; ++sx) { const float v = height[static_cast<std::size_t>(y * kSS + sy) * W + x * kSS + sx]; if (v >= 0) { c += 1; hsum += v; } }
        if (c > 0) hsum /= c;
        return c / (kSS * kSS);
    };
    std::vector<float> alpha(static_cast<std::size_t>(ow) * oh), hh(static_cast<std::size_t>(ow) * oh);
    for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) { float hs; alpha[static_cast<std::size_t>(y) * ow + x] = cover(x, y, hs); hh[static_cast<std::size_t>(y) * ow + x] = hs; }
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * ow + x;
            const float a = alpha[i];
            if (a <= 0.0f) continue;
            auto at = [&](int xx, int yy) { return xx < 0 || yy < 0 || xx >= ow || yy >= oh ? 0.0f : alpha[static_cast<std::size_t>(yy) * ow + xx]; };
            const float edge = std::min(1.0f, std::max(0.0f, 1.0f - std::min(std::min(at(x - 1, y), at(x + 1, y)), std::min(at(x, y - 1), at(x, y + 1)))));
            const float t = hh[i];
            const float r = 44 + 48 * t, g = 40 + 44 * t, b = 108 + 68 * t;   // deep indigo, lighter where the floor is higher
            const float er = 110, eg = 240, eb = 255;   // a neon cyan edge
            plan.rgba[i * 4] = static_cast<std::uint8_t>(r + (er - r) * edge);
            plan.rgba[i * 4 + 1] = static_cast<std::uint8_t>(g + (eg - g) * edge);
            plan.rgba[i * 4 + 2] = static_cast<std::uint8_t>(b + (eb - b) * edge);
            plan.rgba[i * 4 + 3] = static_cast<std::uint8_t>(255.0f * std::min(1.0f, a * 1.2f));
        }
    }
    {   // a soft cyan glow round the road, so a small picture still reads as lit
        std::vector<float> blur(alpha.size()), tmp(alpha.size());
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<float>& src = pass == 0 ? alpha : blur;
            for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) { float sum = 0; int n = 0; for (int k = -3; k <= 3; ++k) { const int xx = x + k; if (xx >= 0 && xx < ow) { sum += src[static_cast<std::size_t>(y) * ow + xx]; ++n; } } tmp[static_cast<std::size_t>(y) * ow + x] = sum / std::max(1, n); }
            for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) { float sum = 0; int n = 0; for (int k = -3; k <= 3; ++k) { const int yy = y + k; if (yy >= 0 && yy < oh) { sum += tmp[static_cast<std::size_t>(yy) * ow + x]; ++n; } } blur[static_cast<std::size_t>(y) * ow + x] = sum / std::max(1, n); }
        }
        for (std::size_t i = 0; i < alpha.size(); ++i) {
            if (alpha[i] >= 0.999f) continue;
            const float g = std::min(1.0f, blur[i] * 1.4f) * (1.0f - alpha[i]) * 0.75f;
            if (g <= 0.01f || plan.rgba[i * 4 + 3] >= static_cast<std::uint8_t>(g * 255.0f)) continue;
            plan.rgba[i * 4] = 70; plan.rgba[i * 4 + 1] = 205; plan.rgba[i * 4 + 2] = 255; plan.rgba[i * 4 + 3] = static_cast<std::uint8_t>(g * 255.0f);
        }
    }
    for (const Pad& p : pads) {   // a coloured dot with a dark ring so it reads on any floor
        const float cx = p.at.x / kSS, cy = p.at.y / kSS, R = p.radius / kSS;
        for (int y = std::max(0, static_cast<int>(cy - R - 2)); y <= std::min(oh - 1, static_cast<int>(cy + R + 2)); ++y) {
            for (int x = std::max(0, static_cast<int>(cx - R - 2)); x <= std::min(ow - 1, static_cast<int>(cx + R + 2)); ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                const std::size_t i = (static_cast<std::size_t>(y) * ow + x) * 4;
                if (d <= R) { plan.rgba[i] = p.colour[0]; plan.rgba[i + 1] = p.colour[1]; plan.rgba[i + 2] = p.colour[2]; plan.rgba[i + 3] = 255; }
                else if (d <= R + 1.2f) { plan.rgba[i] = 12; plan.rgba[i + 1] = 10; plan.rgba[i + 2] = 24; plan.rgba[i + 3] = std::max<std::uint8_t>(plan.rgba[i + 3], 220); }
            }
        }
    }
    plan.ok = true;
    return plan;
}
}
