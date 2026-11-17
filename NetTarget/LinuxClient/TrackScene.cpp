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

void Fan(SDL_Renderer* r, const std::vector<SDL_FPoint>& poly, SDL_Color colour)
{
    if (poly.size() < 3) return;
    std::vector<SDL_Vertex> v(poly.size());
    for (std::size_t i = 0; i < poly.size(); ++i) { v[i].position = poly[i]; v[i].color = colour; v[i].tex_coord = SDL_FPoint{0, 0}; }
    std::vector<int> idx;
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) { idx.push_back(0); idx.push_back(static_cast<int>(i)); idx.push_back(static_cast<int>(i + 1)); }
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
}

void Quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_Color ca, SDL_Color cb, SDL_Color cc, SDL_Color cd)
{
    const SDL_Vertex v[4] = {{a, ca, {0, 0}}, {b, cb, {0, 0}}, {c, cc, {0, 0}}, {d, cd, {0, 0}}};
    const int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void Thick(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, float width, SDL_Color colour)
{
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    Quad(r, SDL_FPoint{a.x + nx, a.y + ny}, SDL_FPoint{a.x - nx, a.y - ny}, SDL_FPoint{b.x + nx, b.y + ny}, SDL_FPoint{b.x - nx, b.y - ny}, colour, colour, colour, colour);
}

void Disc(SDL_Renderer* r, SDL_FPoint centre, float rx, float ry, SDL_Color inner, SDL_Color outer)
{
    const int n = 20;
    std::vector<SDL_Vertex> v(static_cast<std::size_t>(n) + 1);
    v[0].position = centre; v[0].color = inner; v[0].tex_coord = SDL_FPoint{0, 0};
    std::vector<int> idx;
    for (int i = 0; i < n; ++i) {
        const float a = static_cast<float>(i) / n * 6.2831853f;
        v[static_cast<std::size_t>(i) + 1].position = SDL_FPoint{centre.x + std::cos(a) * rx, centre.y + std::sin(a) * ry};
        v[static_cast<std::size_t>(i) + 1].color = outer; v[static_cast<std::size_t>(i) + 1].tex_coord = SDL_FPoint{0, 0};
        idx.push_back(0); idx.push_back(i + 1); idx.push_back((i + 1) % n + 1);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), n + 1, idx.data(), static_cast<int>(idx.size()));
}


// A polygon drawn with one of the game's pictures. The picture is tiled four by four on the card, so the polygon repeats it up to about three times across (a bigger surface shows it larger).
void TexturedFan(SDL_Renderer* r, SDL_Texture* picture, const std::vector<SDL_FPoint>& poly, const std::vector<SDL_FPoint>& repeats, SDL_Color colour)
{
    if (poly.size() < 3 || picture == nullptr) return;
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
    for (const SDL_FPoint& p : repeats) { u0 = std::min(u0, p.x); v0 = std::min(v0, p.y); u1 = std::max(u1, p.x); v1 = std::max(v1, p.y); }
    const float ku = std::min(1.0f, 3.6f / std::max(0.001f, u1 - u0)), kv = std::min(1.0f, 3.6f / std::max(0.001f, v1 - v0));
    const float bu = std::floor(u0), bv = std::floor(v0);
    std::vector<SDL_Vertex> v(poly.size());
    for (std::size_t i = 0; i < poly.size(); ++i) {
        v[i].position = poly[i]; v[i].color = colour;
        v[i].tex_coord = SDL_FPoint{std::max(0.0f, std::min(1.0f, (repeats[i].x - bu) * ku / 4.0f)), std::max(0.0f, std::min(1.0f, (repeats[i].y - bv) * kv / 4.0f))};
    }
    std::vector<int> idx;
    for (std::size_t i = 1; i + 1 < poly.size(); ++i) { idx.push_back(0); idx.push_back(static_cast<int>(i)); idx.push_back(static_cast<int>(i + 1)); }
    SDL_RenderGeometry(r, picture, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
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
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // ---- sky, sun and ground ------------------------------------------------------------------------------------------------------------------------------------------------------
    const float tanDown = -cam.fz / std::max(0.2f, std::sqrt(1.0f - cam.fz * cam.fz));
    const float horizon = std::max(0.0f, std::min(static_cast<float>(h), cam.cy - cam.focal * tanDown));
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    Quad(renderer, {0, 0}, {W, 0}, {0, horizon}, {W, horizon}, SDL_Color{7, 4, 26, 255}, SDL_Color{7, 4, 26, 255}, SDL_Color{176, 46, 124, 255}, SDL_Color{176, 46, 124, 255});
    if (horizon > 4.0f) {
        const float sunR = std::min(H * 0.30f, horizon * 1.6f);
        const SDL_FPoint sun = {W * 0.5f - W * 0.10f, horizon};
        Disc(renderer, sun, sunR, sunR, SDL_Color{255, 226, 110, 255}, SDL_Color{255, 70, 150, 255});
        for (int i = 0; i < 5; ++i) {   // dark slats across the lower sun, as on a retro poster
            const float y = horizon - sunR * (0.08f + 0.15f * static_cast<float>(i));
            Quad(renderer, {sun.x - sunR, y}, {sun.x + sunR, y}, {sun.x - sunR, y + sunR * (0.012f + 0.012f * static_cast<float>(i))}, {sun.x + sunR, y + sunR * (0.012f + 0.012f * static_cast<float>(i))},
                 SDL_Color{150, 30, 110, 255}, SDL_Color{150, 30, 110, 255}, SDL_Color{150, 30, 110, 255}, SDL_Color{150, 30, 110, 255});
        }
        SDL_SetRenderDrawColor(renderer, 255, 240, 255, 140);   // a few stars
        for (int i = 0; i < 50; ++i) {
            const float sx = std::fmod(static_cast<float>(i) * 97.31f, W), sy = std::fmod(static_cast<float>(i) * 41.7f, std::max(1.0f, horizon * 0.8f));
            SDL_RenderDrawPointF(renderer, sx, sy);
        }
    }
    Quad(renderer, {0, horizon}, {W, horizon}, {0, H}, {W, H}, SDL_Color{96, 26, 116, 255}, SDL_Color{96, 26, 116, 255}, SDL_Color{5, 3, 15, 255}, SDL_Color{5, 3, 15, 255});

    // ---- the grid on the ground -----------------------------------------------------------------------------------------------------------------------------------------------
    for (int i = -30; i <= 30; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
            const float v = static_cast<float>(i) * 0.1f;
            SDL_FPoint p[2]; float d[2]; bool okp[2];
            for (int e = 0; e < 2; ++e) {
                const float s = e == 0 ? -3.0f : 3.0f;
                okp[e] = axis == 0 ? Project(cam, v, s, -0.004f, p[e], d[e]) : Project(cam, s, v, -0.004f, p[e], d[e]);
            }
            if (!okp[0] || !okp[1]) {   // a line that runs behind the eye: cut it where it crosses the near plane
                if (!okp[0] && !okp[1]) continue;
                const int bad = okp[0] ? 1 : 0, good = 1 - bad;
                float lo = 0.0f, hi = 1.0f;   // along the line, find the point at depth 0.1
                const float sx0 = -3.0f, sx1 = 3.0f;
                for (int it = 0; it < 18; ++it) {
                    const float mid = (lo + hi) * 0.5f, s = sx0 + (sx1 - sx0) * mid;
                    SDL_FPoint q; float dd;
                    const bool ok = axis == 0 ? Project(cam, v, s, -0.004f, q, dd) : Project(cam, s, v, -0.004f, q, dd);
                    const bool goodSide = (good == 0) ? (mid < 0.5f ? true : ok) : ok;   // keep the bisection on the side that is in front
                    (void)goodSide;
                    if (ok == (good == 0 ? true : true)) { if (good == 0) lo = mid; else hi = mid; } else { if (good == 0) hi = mid; else lo = mid; }
                }
                const float s = sx0 + (sx1 - sx0) * (good == 0 ? lo : hi);
                SDL_FPoint q; float dd;
                if (!(axis == 0 ? Project(cam, v, s, -0.004f, q, dd) : Project(cam, s, v, -0.004f, q, dd))) continue;
                p[bad] = q; d[bad] = dd;
            }
            const float depth = std::min(d[0], d[1]);
            const float a = std::max(0.04f, std::min(1.0f, 1.25f - depth / 3.6f));
            const bool major = (i % 5) == 0;
            if (major) SDL_SetRenderDrawColor(renderer, 90, 220, 255, static_cast<Uint8>(120 * a)); else SDL_SetRenderDrawColor(renderer, 220, 70, 230, static_cast<Uint8>(86 * a));
            SDL_RenderDrawLineF(renderer, p[0].x, p[0].y, p[1].x, p[1].y);
        }
    }

    // ---- the track: far to near -----------------------------------------------------------------------------------------------------------------------------------------------
    struct Item { float depth; std::size_t index; };
    std::vector<Item> order;
    for (std::size_t i = 0; i < plan.shapes.size(); ++i) {
        const HoverNetPlan::Plan::Shape& s = plan.shapes[i];
        float cx = 0, cy = 0; int n = 0;
        for (std::size_t k = 0; k + 1 < s.xy.size(); k += 2) { cx += s.xy[k]; cy += s.xy[k + 1]; ++n; }
        if (n == 0) continue;
        SDL_FPoint p; float d;
        if (!Project(cam, cx / n, cy / n, s.h * plan.relief * zScale, p, d)) continue;
        order.push_back({d, i});
    }
    std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
    const bool light = plan.shapes.size() > 1200;   // a very large track: no glow passes
    // sides (a slab down to the ground) first
    for (const HoverNetPlan::Plan::Wall& wall : plan.walls) {
        SDL_FPoint a, b, ga, gb; float da, db, d1, d2;
        const float z = wall.h * plan.relief * zScale;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db) || !Project(cam, wall.x0, wall.y0, -0.004f, ga, d1) || !Project(cam, wall.x1, wall.y1, -0.004f, gb, d2)) continue;
        const SDL_Color c = Fog(SDL_Color{12, 10, 36, 255}, (da + db) * 0.5f);
        Quad(renderer, a, b, ga, gb, c, c, c, c);
    }
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
        const SDL_Color base = Mix(SDL_Color{46, 42, 112, 255}, SDL_Color{104, 92, 200, 255}, s.h);   // deeper indigo low, lighter where the floor is higher
        SDL_Texture* picture = s.texture >= 0 && s.texture < static_cast<int>(pictures.size()) ? pictures[static_cast<std::size_t>(s.texture)] : nullptr;
        if (picture != nullptr) {   // the game's own floor texture, tinted a little towards the scene and fogged with distance
            const HoverNetPlan::Texture& tex = plan.textures[static_cast<std::size_t>(s.texture)];
            std::vector<SDL_FPoint> repeats;
            for (std::size_t k = 0; k + 1 < s.xy.size(); k += 2) repeats.push_back(SDL_FPoint{static_cast<float>((s.xy[k] * plan.spanMm + plan.cxMm) / tex.mmW), static_cast<float>((s.xy[k + 1] * plan.spanMm + plan.cyMm) / tex.mmH)});
            TexturedFan(renderer, picture, poly, repeats, Fog(SDL_Color{236, 226, 255, 255}, item.depth));
        }
        else Fan(renderer, poly, Fog(base, item.depth));
    }
    // neon rails: a faint wall, a bright edge and a glow, in a colour that runs from cyan to magenta round the track
    for (const HoverNetPlan::Plan::Wall& wall : plan.walls) {
        const float z = wall.h * plan.relief * zScale, railTop = z + 0.014f;
        SDL_FPoint a, b, ta, tb; float da, db, d1, d2;
        if (!Project(cam, wall.x0, wall.y0, z, a, da) || !Project(cam, wall.x1, wall.y1, z, b, db) || !Project(cam, wall.x0, wall.y0, railTop, ta, d1) || !Project(cam, wall.x1, wall.y1, railTop, tb, d2)) continue;
        const float mx = (wall.x0 + wall.x1) * 0.5f, my = (wall.y0 + wall.y1) * 0.5f;
        if (wall.texture >= 0 && wall.texture < static_cast<int>(pictures.size()) && pictures[static_cast<std::size_t>(wall.texture)] != nullptr) {   // the game's own wall texture, standing on the floor edge
            const HoverNetPlan::Texture& tex = plan.textures[static_cast<std::size_t>(wall.texture)];
            const float len = std::sqrt((wall.x1 - wall.x0) * (wall.x1 - wall.x0) + (wall.y1 - wall.y0) * (wall.y1 - wall.y0)) * static_cast<float>(plan.spanMm) / tex.mmW;
            const float ur = std::min(3.6f, len) / 4.0f;
            const SDL_Color c = Fog(SDL_Color{240, 232, 255, 255}, (da + db) * 0.5f);
            const SDL_Vertex v[4] = {{ta, c, {0.0f, 0.0f}}, {tb, c, {ur, 0.0f}}, {a, c, {0.0f, 0.22f}}, {b, c, {ur, 0.22f}}};
            const int idx[6] = {0, 1, 2, 2, 1, 3};
            SDL_RenderGeometry(renderer, pictures[static_cast<std::size_t>(wall.texture)], v, 4, idx, 6);
        }
        const float t = 0.5f + 0.5f * std::sin(std::atan2(my, mx) * 1.0f + seconds * 0.35f);
        const SDL_Color neon = Mix(SDL_Color{60, 226, 255, 255}, SDL_Color{255, 70, 206, 255}, t);
        const float near = std::max(0.0f, 1.0f - ((da + db) * 0.5f - 1.0f) / 3.6f);
        const bool realWall = wall.texture >= 0 && wall.texture < static_cast<int>(pictures.size()) && pictures[static_cast<std::size_t>(wall.texture)] != nullptr;
        if (!realWall) { SDL_Color faint = neon; faint.a = static_cast<Uint8>(54 * near + 12); Quad(renderer, a, b, ta, tb, faint, faint, faint, faint); }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
        if (!light) { SDL_Color g = neon; g.a = static_cast<Uint8>((realWall ? 18 : 34) * pulse * (0.4f + near)); Thick(renderer, ta, tb, 8.0f, g); g.a = static_cast<Uint8>((realWall ? 40 : 70) * pulse * (0.4f + near)); Thick(renderer, ta, tb, 3.5f, g); }
        SDL_Color core = neon; core.a = static_cast<Uint8>(255 * pulse * (0.5f + 0.5f * near));
        Thick(renderer, ta, tb, 1.6f, core);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    }

    // ---- the pads --------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    const HoverNetPlan::Colours& colours = HoverNetPlan::PadColours();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    for (const HoverNetPlan::Plan::Mark& m : plan.marks) {
        const std::uint8_t* c = m.kind == 0 ? colours.fuel : m.kind == 1 ? colours.boost : m.kind == 2 ? colours.finish : colours.power;
        SDL_FPoint p; float d;
        if (!Project(cam, m.x, m.y, m.h * plan.relief * zScale + 0.003f, p, d)) continue;
        const float px = cam.focal / d;   // pixels per unit at this depth
        const float core = (m.kind == 2 ? 0.020f : m.kind == 3 ? 0.010f : 0.014f) * px;
        const float tilt = 0.55f;
        Disc(renderer, p, core * 3.2f, core * 3.2f * tilt, SDL_Color{c[0], c[1], c[2], static_cast<Uint8>(90 * pulse)}, SDL_Color{c[0], c[1], c[2], 0});
        Disc(renderer, p, core, core * tilt, SDL_Color{255, 255, 255, 255}, SDL_Color{c[0], c[1], c[2], 255});
        if (m.kind == 2) {   // the finish line: a beam of light straight up
            SDL_FPoint top; float dd;
            if (Project(cam, m.x, m.y, m.h * plan.relief * zScale + 0.09f, top, dd)) { SDL_Color g = {c[0], c[1], c[2], static_cast<Uint8>(120 * pulse)}; Thick(renderer, p, top, core * 1.6f, g); g.a = 255; Thick(renderer, p, top, 1.6f, g); }
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}
}
