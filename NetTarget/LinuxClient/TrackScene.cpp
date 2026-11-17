#include "TrackScene.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace HoverNetScene
{
namespace
{
struct Camera
{
    float ex, ey, ez;        // eye
    float rx, ry, rz;        // right
    float ux, uy, uz;        // up
    float fx, fy, fz;        // forward
    float focal, cx, cy;     // focal length in pixels, the screen centre
};

Camera MakeCamera(int w, int h, float yawDegrees)
{
    const float a = yawDegrees * 3.14159265f / 180.0f;
    const float radius = 1.62f, height = 0.60f;
    Camera c;
    c.ex = std::cos(a) * radius; c.ey = std::sin(a) * radius; c.ez = height;
    float fx = -c.ex, fy = -c.ey, fz = -c.ez + 0.02f;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    c.fx = fx / fl; c.fy = fy / fl; c.fz = fz / fl;
    float rx = c.fy, ry = -c.fx, rz = 0.0f;   // forward x up(0,0,1)
    const float rl = std::sqrt(rx * rx + ry * ry);
    c.rx = rx / rl; c.ry = ry / rl; c.rz = rz;
    c.ux = c.ry * c.fz - c.rz * c.fy; c.uy = c.rz * c.fx - c.rx * c.fz; c.uz = c.rx * c.fy - c.ry * c.fx;
    c.focal = (h * 0.5f) / std::tan(27.5f * 3.14159265f / 180.0f);
    c.cx = w * 0.5f; c.cy = h * 0.46f;
    return c;
}

bool Project(const Camera& c, float x, float y, float z, SDL_FPoint& out, float& depth)
{
    const float vx = x - c.ex, vy = y - c.ey, vz = z - c.ez;
    depth = vx * c.fx + vy * c.fy + vz * c.fz;
    if (depth < 0.08f) return false;
    out.x = c.cx + (vx * c.rx + vy * c.ry + vz * c.rz) / depth * c.focal;
    out.y = c.cy - (vx * c.ux + vy * c.uy + vz * c.uz) / depth * c.focal;
    return true;
}

SDL_Color Mix(SDL_Color a, SDL_Color b, float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    return SDL_Color{static_cast<Uint8>(a.r + (b.r - a.r) * t), static_cast<Uint8>(a.g + (b.g - a.g) * t), static_cast<Uint8>(a.b + (b.b - a.b) * t), static_cast<Uint8>(a.a + (b.a - a.a) * t)};
}

// Everything is gathered into a few big SDL_RenderGeometry calls (one per run of the same picture and blend mode) instead of hundreds of small ones: a call costs far more than the
// triangles in it, most of all in the browser, where each goes through WebGL from WebAssembly.
struct Painter
{
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool additive = false;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    void Flush()
    {
        if (vertices.empty()) return;
        SDL_SetRenderDrawBlendMode(renderer, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(renderer, texture, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
        vertices.clear(); indices.clear();
    }
    void Use(SDL_Texture* t, bool add)
    {
        if (t != texture || add != additive) { Flush(); texture = t; additive = add; }
    }
    void Vertex(SDL_FPoint p, SDL_Color c, SDL_FPoint uv = SDL_FPoint{0, 0}) { vertices.push_back(SDL_Vertex{p, c, uv}); }
};
Painter* gPainter = nullptr;

void Fan(const std::vector<SDL_FPoint>& poly, SDL_Color colour, bool add = false)
{
    if (poly.size() < 3) return;
    Painter& p = *gPainter;
    p.Use(nullptr, add);
    const int base = static_cast<int>(p.vertices.size());
    for (const SDL_FPoint& pt : poly) p.Vertex(pt, colour);
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) { p.indices.push_back(base); p.indices.push_back(base + static_cast<int>(i)); p.indices.push_back(base + static_cast<int>(i) + 1); }
}

void Quad(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color ca, SDL_Color cb, SDL_Color cc, SDL_Color cd, bool add = false)
{
    Painter& p = *gPainter;
    p.Use(nullptr, add);
    const int base = static_cast<int>(p.vertices.size());
    p.Vertex(a, ca); p.Vertex(b, cb); p.Vertex(c, cc); p.Vertex(d, cd);
    const int idx[6] = {0, 1, 2, 2, 1, 3};
    for (int i : idx) p.indices.push_back(base + i);
}

void Thick(SDL_FPoint a, SDL_FPoint b, float width, SDL_Color colour, bool add)
{
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    Quad(SDL_FPoint{a.x + nx, a.y + ny}, SDL_FPoint{a.x - nx, a.y - ny}, SDL_FPoint{b.x + nx, b.y + ny}, SDL_FPoint{b.x - nx, b.y - ny}, colour, colour, colour, colour, add);
}

void Disc(SDL_FPoint centre, float rx, float ry, SDL_Color inner, SDL_Color outer, bool add)
{
    Painter& p = *gPainter;
    p.Use(nullptr, add);
    const int n = 14;
    const int base = static_cast<int>(p.vertices.size());
    p.Vertex(centre, inner);
    for (int i = 0; i < n; ++i) {
        const float a = static_cast<float>(i) / n * 6.2831853f;
        p.Vertex(SDL_FPoint{centre.x + std::cos(a) * rx, centre.y + std::sin(a) * ry}, outer);
        p.indices.push_back(base); p.indices.push_back(base + i + 1); p.indices.push_back(base + (i + 1) % n + 1);
    }
}

// A polygon drawn with one of the game's pictures. The picture is tiled four by four on the card, so the polygon repeats it up to about three times across (a bigger surface shows it larger).
void TexturedFan(SDL_Texture* picture, const std::vector<SDL_FPoint>& poly, const std::vector<SDL_FPoint>& repeats, SDL_Color colour)
{
    if (poly.size() < 3 || picture == nullptr) return;
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
    for (const SDL_FPoint& p : repeats) { u0 = std::min(u0, p.x); v0 = std::min(v0, p.y); u1 = std::max(u1, p.x); v1 = std::max(v1, p.y); }
    const float ku = std::min(1.0f, 3.6f / std::max(0.001f, u1 - u0)), kv = std::min(1.0f, 3.6f / std::max(0.001f, v1 - v0));
    const float bu = std::floor(u0), bv = std::floor(v0);
    Painter& p = *gPainter;
    p.Use(picture, false);
    const int base = static_cast<int>(p.vertices.size());
    for (std::size_t i = 0; i < poly.size(); ++i)
        p.Vertex(poly[i], colour, SDL_FPoint{std::max(0.0f, std::min(1.0f, (repeats[i].x - bu) * ku / 4.0f)), std::max(0.0f, std::min(1.0f, (repeats[i].y - bv) * kv / 4.0f))});
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) { p.indices.push_back(base); p.indices.push_back(base + static_cast<int>(i)); p.indices.push_back(base + static_cast<int>(i) + 1); }
}

SDL_Color Fog(SDL_Color c, float depth)
{
    const SDL_Color fog = {58, 18, 86, 255};
    return Mix(c, fog, (depth - 1.1f) / 3.2f);
}
}

void Draw(SDL_Renderer* renderer, int w, int h, const HoverNetPlan::Plan& plan, const std::vector<SDL_Texture*>& pictures, float yawDegrees, float seconds)
{
    if (!plan.ok || plan.shapes.empty()) return;
    const Camera cam = MakeCamera(w, h, yawDegrees);
    const float pulse = 0.86f + 0.14f * std::sin(seconds * 2.2f);
    const float zScale = 1.8f;   // the floors' heights, a little exaggerated so bridges and ramps read
    static thread_local Painter painter;   // kept between frames, so its buffers are not allocated every time
    painter.renderer = renderer; painter.texture = nullptr; painter.additive = false; painter.vertices.clear(); painter.indices.clear();
    gPainter = &painter;
    auto pictureOf = [&](int index) -> SDL_Texture* { return index >= 0 && index < static_cast<int>(pictures.size()) ? pictures[static_cast<std::size_t>(index)] : nullptr; };

    // ---- sky, sun and ground ------------------------------------------------------------------------------------------------------------------------------------------------------
    const float tanDown = -cam.fz / std::max(0.2f, std::sqrt(1.0f - cam.fz * cam.fz));
    const float horizon = std::max(0.0f, std::min(static_cast<float>(h), cam.cy - cam.focal * tanDown));
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    Quad({0, 0}, {W, 0}, {0, horizon}, {W, horizon}, SDL_Color{7, 4, 26, 255}, SDL_Color{7, 4, 26, 255}, SDL_Color{176, 46, 124, 255}, SDL_Color{176, 46, 124, 255});
    if (horizon > 4.0f) {
        for (int i = 0; i < 40; ++i) {   // a few stars
            const float sx = std::fmod(static_cast<float>(i) * 97.31f, W), sy = std::fmod(static_cast<float>(i) * 41.7f, std::max(1.0f, horizon * 0.8f));
            const SDL_Color c = {255, 240, 255, 150};
            Quad({sx, sy}, {sx + 1.6f, sy}, {sx, sy + 1.6f}, {sx + 1.6f, sy + 1.6f}, c, c, c, c);
        }
    }
    Quad({0, horizon}, {W, horizon}, {0, H}, {W, H}, SDL_Color{96, 26, 116, 255}, SDL_Color{96, 26, 116, 255}, SDL_Color{5, 3, 15, 255}, SDL_Color{5, 3, 15, 255});

    // ---- the grid on the ground (thin quads, all in the same run) -----------------------------------------------------------------------------------------------------------
    {
        auto lineEnd = [&](int axis, float v, float s, SDL_FPoint& p, float& d) { return axis == 0 ? Project(cam, v, s, -0.004f, p, d) : Project(cam, s, v, -0.004f, p, d); };
        for (int i = -24; i <= 24; ++i) {
            for (int axis = 0; axis < 2; ++axis) {
                const float v = static_cast<float>(i) * 0.125f;
                float s0 = -3.0f, s1 = 3.0f;
                SDL_FPoint p0, p1; float d0, d1;
                // cut the line where it comes in front of the eye: walk the end that is behind it forward along the line
                if (!lineEnd(axis, v, s0, p0, d0)) { float lo = s0, hi = s1; for (int it = 0; it < 16; ++it) { const float mid = (lo + hi) * 0.5f; SDL_FPoint q; float dd; if (lineEnd(axis, v, mid, q, dd)) hi = mid; else lo = mid; } s0 = hi; if (!lineEnd(axis, v, s0, p0, d0)) continue; }
                if (!lineEnd(axis, v, s1, p1, d1)) { float lo = s0, hi = s1; for (int it = 0; it < 16; ++it) { const float mid = (lo + hi) * 0.5f; SDL_FPoint q; float dd; if (lineEnd(axis, v, mid, q, dd)) lo = mid; else hi = mid; } s1 = lo; if (!lineEnd(axis, v, s1, p1, d1)) continue; }
                const float depth = std::min(d0, d1);
                const float a = std::max(0.04f, std::min(1.0f, 1.25f - depth / 3.6f));
                const bool major = (i % 4) == 0;
                const SDL_Color c = major ? SDL_Color{90, 220, 255, static_cast<Uint8>(120 * a)} : SDL_Color{220, 70, 230, static_cast<Uint8>(86 * a)};
                Thick(p0, p1, major ? 1.6f : 1.2f, c, false);
            }
        }
    }

    // ---- the track ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    struct Item { int bucket; int texture; float depth; std::size_t index; };
    std::vector<Item> order;
    for (std::size_t i = 0; i < plan.shapes.size(); ++i) {
        const HoverNetPlan::Plan::Shape& s = plan.shapes[i];
        float cx = 0, cy = 0; int n = 0;
        for (std::size_t k = 0; k + 1 < s.xy.size(); k += 2) { cx += s.xy[k]; cy += s.xy[k + 1]; ++n; }
        if (n == 0) continue;
        SDL_FPoint p; float d;
        if (!Project(cam, cx / n, cy / n, s.h * plan.relief * zScale, p, d)) continue;
        order.push_back({static_cast<int>(s.h * 50.0f), pictureOf(s.texture) != nullptr ? s.texture : -1, d, i});
    }
    // lowest floors first (a bridge over a road), then by picture (so a run of floors with the same picture is one draw call), then far to near
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { if (a.bucket != b.bucket) return a.bucket < b.bucket; if (a.texture != b.texture) return a.texture < b.texture; return a.depth > b.depth; });
    const bool light = plan.shapes.size() > 1200;   // a very large track: no glow passes

    struct Edge { std::size_t index; float depth; };
    std::vector<Edge> walls;
    for (std::size_t i = 0; i < plan.walls.size(); ++i) {
        const HoverNetPlan::Plan::Wall& wall = plan.walls[i];
        SDL_FPoint a, b; float da, db;
        const float z = wall.h * plan.relief * zScale;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db)) continue;
        walls.push_back({i, (da + db) * 0.5f});
    }
    // sides (a slab down to the ground)
    for (const Edge& e : walls) {
        const HoverNetPlan::Plan::Wall& wall = plan.walls[e.index];
        SDL_FPoint a, b, ga, gb; float da, db, d1, d2;
        const float z = wall.h * plan.relief * zScale;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db) || !Project(cam, wall.x0, wall.y0, -0.004f, ga, d1) || !Project(cam, wall.x1, wall.y1, -0.004f, gb, d2)) continue;
        const SDL_Color c = Fog(SDL_Color{12, 10, 36, 255}, e.depth);
        Quad(a, b, ga, gb, c, c, c, c);
    }
    // floors
    for (const Item& item : order) {
        const HoverNetPlan::Plan::Shape& s = plan.shapes[item.index];
        std::vector<SDL_FPoint> poly;
        bool allOk = true;
        for (std::size_t k = 0; k + 1 < s.xy.size(); k += 2) {
            SDL_FPoint p; float d;
            if (!Project(cam, s.xy[k], s.xy[k + 1], s.h * plan.relief * zScale, p, d)) { allOk = false; break; }
            poly.push_back(p);
        }
        if (!allOk) continue;
        SDL_Texture* picture = item.texture >= 0 ? pictureOf(item.texture) : nullptr;
        if (picture != nullptr) {   // the game's own floor texture, tinted a little towards the scene and fogged with distance
            const HoverNetPlan::Texture& tex = plan.textures[static_cast<std::size_t>(item.texture)];
            std::vector<SDL_FPoint> repeats;
            for (std::size_t k = 0; k + 1 < s.xy.size(); k += 2) repeats.push_back(SDL_FPoint{static_cast<float>((s.xy[k] * plan.spanMm + plan.cxMm) / tex.mmW), static_cast<float>((s.xy[k + 1] * plan.spanMm + plan.cyMm) / tex.mmH)});
            TexturedFan(picture, poly, repeats, Fog(SDL_Color{236, 226, 255, 255}, item.depth));
        }
        else Fan(poly, Fog(Mix(SDL_Color{46, 42, 112, 255}, SDL_Color{104, 92, 200, 255}, s.h), item.depth));
    }
    // the game's own wall textures standing on the floor edges, grouped by picture
    std::vector<Edge> walled;
    for (const Edge& e : walls) if (pictureOf(plan.walls[e.index].texture) != nullptr) walled.push_back(e);
    std::sort(walled.begin(), walled.end(), [&](const Edge& a, const Edge& b) { const int ta = plan.walls[a.index].texture, tb = plan.walls[b.index].texture; if (ta != tb) return ta < tb; return a.depth > b.depth; });
    for (const Edge& e : walled) {
        const HoverNetPlan::Plan::Wall& wall = plan.walls[e.index];
        const float z = wall.h * plan.relief * zScale, railTop = z + 0.014f;
        SDL_FPoint a, b, ta, tb; float da, db, d1, d2;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db) || !Project(cam, wall.x0, wall.y0, railTop, ta, d1) || !Project(cam, wall.x1, wall.y1, railTop, tb, d2)) continue;
        const HoverNetPlan::Texture& tex = plan.textures[static_cast<std::size_t>(wall.texture)];
        const float len = std::sqrt((wall.x1 - wall.x0) * (wall.x1 - wall.x0) + (wall.y1 - wall.y0) * (wall.y1 - wall.y0)) * static_cast<float>(plan.spanMm) / tex.mmW;
        const float ur = std::min(3.6f, len) / 4.0f;
        const SDL_Color c = Fog(SDL_Color{240, 232, 255, 255}, e.depth);
        Painter& p = painter; p.Use(pictureOf(wall.texture), false);
        const int base = static_cast<int>(p.vertices.size());
        p.Vertex(ta, c, {0.0f, 0.0f}); p.Vertex(tb, c, {ur, 0.0f}); p.Vertex(a, c, {0.0f, 0.22f}); p.Vertex(b, c, {ur, 0.22f});
        const int idx[6] = {0, 1, 2, 2, 1, 3};
        for (int i : idx) p.indices.push_back(base + i);
    }
    // neon edges: a faint wall where the floor edge has no picture, then every edge's glow and bright line together (one additive run)
    auto neonOf = [&](const HoverNetPlan::Plan::Wall& wall) {
        const float mx = (wall.x0 + wall.x1) * 0.5f, my = (wall.y0 + wall.y1) * 0.5f;
        const float t = 0.5f + 0.5f * std::sin(std::atan2(my, mx) * 1.0f + seconds * 0.35f);
        return Mix(SDL_Color{60, 226, 255, 255}, SDL_Color{255, 70, 206, 255}, t);
    };
    for (const Edge& e : walls) {
        const HoverNetPlan::Plan::Wall& wall = plan.walls[e.index];
        if (pictureOf(wall.texture) != nullptr) continue;
        const float z = wall.h * plan.relief * zScale, railTop = z + 0.014f;
        SDL_FPoint a, b, ta, tb; float da, db, d1, d2;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db) || !Project(cam, wall.x0, wall.y0, railTop, ta, d1) || !Project(cam, wall.x1, wall.y1, railTop, tb, d2)) continue;
        SDL_Color faint = neonOf(wall); faint.a = static_cast<Uint8>(54 * std::max(0.0f, 1.0f - (e.depth - 1.0f) / 3.6f) + 12);
        Quad(a, b, ta, tb, faint, faint, faint, faint);
    }
    for (const Edge& e : walls) {
        const HoverNetPlan::Plan::Wall& wall = plan.walls[e.index];
        const bool realWall = pictureOf(wall.texture) != nullptr;
        const float railTop = wall.h * plan.relief * zScale + 0.014f;
        SDL_FPoint ta, tb; float d1, d2;
        if (!Project(cam, wall.x0, wall.y0, railTop, ta, d1) || !Project(cam, wall.x1, wall.y1, railTop, tb, d2)) continue;
        const SDL_Color neon = neonOf(wall);
        const float near = std::max(0.0f, 1.0f - (e.depth - 1.0f) / 3.6f);
        if (!light) {
            SDL_Color g = neon; g.a = static_cast<Uint8>((realWall ? 18 : 34) * pulse * (0.4f + near)); Thick(ta, tb, 8.0f, g, true);
            g.a = static_cast<Uint8>((realWall ? 40 : 70) * pulse * (0.4f + near)); Thick(ta, tb, 3.5f, g, true);
        }
        SDL_Color core = neon; core.a = static_cast<Uint8>(255 * pulse * (0.5f + 0.5f * near));
        Thick(ta, tb, 1.6f, core, true);
    }

    // ---- the pads (glow and core, additive) -----------------------------------------------------------------------------------------------------------------------------------
    const HoverNetPlan::Colours& colours = HoverNetPlan::PadColours();
    for (const HoverNetPlan::Plan::Mark& m : plan.marks) {
        const std::uint8_t* c = m.kind == 0 ? colours.fuel : m.kind == 1 ? colours.boost : m.kind == 2 ? colours.finish : colours.power;
        SDL_FPoint p; float d;
        if (!Project(cam, m.x, m.y, m.h * plan.relief * zScale + 0.003f, p, d)) continue;
        const float px = cam.focal / d;
        const float core = (m.kind == 2 ? 0.020f : m.kind == 3 ? 0.010f : 0.014f) * px, tilt = 0.55f;
        Disc(p, core * 3.2f, core * 3.2f * tilt, SDL_Color{c[0], c[1], c[2], static_cast<Uint8>(90 * pulse)}, SDL_Color{c[0], c[1], c[2], 0}, true);
        Disc(p, core, core * tilt, SDL_Color{255, 255, 255, 255}, SDL_Color{c[0], c[1], c[2], 255}, true);
        if (m.kind == 2) {   // the finish line: a beam of light straight up
            SDL_FPoint top; float dd;
            if (Project(cam, m.x, m.y, m.h * plan.relief * zScale + 0.09f, top, dd)) { SDL_Color g = {c[0], c[1], c[2], static_cast<Uint8>(120 * pulse)}; Thick(p, top, core * 1.6f, g, true); g.a = 255; Thick(p, top, 1.6f, g, true); }
        }
    }
    painter.Flush();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    gPainter = nullptr;
}
}
