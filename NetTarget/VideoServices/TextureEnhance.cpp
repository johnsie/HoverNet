// TextureEnhance.cpp -- see TextureEnhance.h.
#include "TextureEnhance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace HoverNetTex
{
namespace
{
inline int Clamp255(int v) { return std::min(std::max(v, 0), 255); }
inline std::uint32_t Pack(int r, int g, int b) { return (static_cast<std::uint32_t>(Clamp255(r)) << 16) | (static_cast<std::uint32_t>(Clamp255(g)) << 8) | static_cast<std::uint32_t>(Clamp255(b)); }
inline int R(std::uint32_t c) { return static_cast<int>((c >> 16) & 255); }
inline int G(std::uint32_t c) { return static_cast<int>((c >> 8) & 255); }
inline int B(std::uint32_t c) { return static_cast<int>(c & 255); }
inline int Wrap(int v, int n) { return v < 0 ? v + n : (v >= n ? v - n : v); }
}

namespace
{
// A hash of a texel position to -1 .. 1 (deterministic, no state).
inline float Hash(int x, int y, int seed)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f;
}

// Soft mottling: value noise on a lattice 4 texels apart (wrapping, so the texture still tiles), interpolated smoothly.
float Mottle(int x, int y, int xres, int yres)
{
    const int cell = 4;
    const int gx = std::max(1, xres / cell), gy = std::max(1, yres / cell);
    const float fx = static_cast<float>(x) / cell, fy = static_cast<float>(y) / cell;
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = fx - ix, ty = fy - iy;
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    auto v = [&](int cx, int cy) { return Hash(cx % gx, cy % gy, 7); };
    const float a = v(ix, iy) + (v(ix + 1, iy) - v(ix, iy)) * sx, b = v(ix, iy + 1) + (v(ix + 1, iy + 1) - v(ix, iy + 1)) * sx;
    return a + (b - a) * sy;
}
}

void EnhanceBase(const std::uint8_t* indices, int xres, int yres, const std::uint8_t* palette768, std::uint32_t* out, bool enhance, int detail)
{
    const std::size_t count = static_cast<std::size_t>(xres) * yres;
    std::vector<std::uint32_t> plain(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* c = palette768 + indices[i] * 3;
        plain[i] = Pack(c[0], c[1], c[2]);
    }
    if (!enhance || xres < 2 || yres < 2) { std::copy(plain.begin(), plain.end(), out); return; }

    // Step 1: an edge-preserving smooth over the 3x3 neighbourhood (a bilateral filter). A neighbour counts in proportion to how close its colour is
    // to the texel's: a dither or a band step (a small difference) is averaged away, a real edge (a big difference) is not blurred across.
    std::vector<std::uint32_t> smooth(count);
    static const int kSpatial[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
    const int tolerance = 66;   // the colour difference (sum over the channels, of 765) beyond which a neighbour no longer counts
    for (int x = 0; x < xres; ++x) {
        for (int y = 0; y < yres; ++y) {
            const std::uint32_t c = plain[static_cast<std::size_t>(x) * yres + y];
            long sr = 0, sg = 0, sb = 0, sw = 0;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const std::uint32_t n = plain[static_cast<std::size_t>(Wrap(x + dx, xres)) * yres + Wrap(y + dy, yres)];
                    const int diff = std::abs(R(n) - R(c)) + std::abs(G(n) - G(c)) + std::abs(B(n) - B(c));
                    if (diff >= tolerance) continue;
                    const int w = kSpatial[dx + 1][dy + 1] * (tolerance - diff);
                    sr += static_cast<long>(R(n)) * w; sg += static_cast<long>(G(n)) * w; sb += static_cast<long>(B(n)) * w; sw += w;
                }
            }
            smooth[static_cast<std::size_t>(x) * yres + y] = sw > 0 ? Pack(static_cast<int>((sr + sw / 2) / sw), static_cast<int>((sg + sw / 2) / sw), static_cast<int>((sb + sw / 2) / sw)) : c;
        }
    }

    // Step 2: a gentle sharpen of what is left (about 0.4 of the difference from the mean of the four neighbours), so the edges that survived are crisp.
    for (int x = 0; x < xres; ++x) {
        for (int y = 0; y < yres; ++y) {
            const std::uint32_t c = smooth[static_cast<std::size_t>(x) * yres + y];
            const std::uint32_t n0 = smooth[static_cast<std::size_t>(Wrap(x - 1, xres)) * yres + y], n1 = smooth[static_cast<std::size_t>(Wrap(x + 1, xres)) * yres + y];
            const std::uint32_t n2 = smooth[static_cast<std::size_t>(x) * yres + Wrap(y - 1, yres)], n3 = smooth[static_cast<std::size_t>(x) * yres + Wrap(y + 1, yres)];
            const int mr = (R(n0) + R(n1) + R(n2) + R(n3)) / 4, mg = (G(n0) + G(n1) + G(n2) + G(n3)) / 4, mb = (B(n0) + B(n1) + B(n2) + B(n3)) / 4;
            out[static_cast<std::size_t>(x) * yres + y] = Pack(R(c) + (R(c) - mr) * 2 / 5, G(c) + (G(c) - mg) * 2 / 5, B(c) + (B(c) - mb) * 2 / 5);
        }
    }

    // Step 3: a trace of surface variation where the texture is a flat field (see EnhanceBase's declaration).
    if (detail > 0) {
        for (int x = 0; x < xres; ++x) {
            for (int y = 0; y < yres; ++y) {
                const std::size_t i = static_cast<std::size_t>(x) * yres + y;
                const std::uint32_t c = out[i];
                const std::uint32_t n0 = out[static_cast<std::size_t>(Wrap(x - 1, xres)) * yres + y], n1 = out[static_cast<std::size_t>(Wrap(x + 1, xres)) * yres + y];
                const std::uint32_t n2 = out[static_cast<std::size_t>(x) * yres + Wrap(y - 1, yres)], n3 = out[static_cast<std::size_t>(x) * yres + Wrap(y + 1, yres)];
                const int contrast = std::max(std::max(std::abs(R(c) - R(n0)) + std::abs(G(c) - G(n0)) + std::abs(B(c) - B(n0)), std::abs(R(c) - R(n1)) + std::abs(G(c) - G(n1)) + std::abs(B(c) - B(n1))),
                                              std::max(std::abs(R(c) - R(n2)) + std::abs(G(c) - G(n2)) + std::abs(B(c) - B(n2)), std::abs(R(c) - R(n3)) + std::abs(G(c) - G(n3)) + std::abs(B(c) - B(n3))));
                const float flat = contrast >= 36 ? 0.0f : 1.0f - contrast / 36.0f;   // 1 on a flat field, 0 at an edge
                if (flat <= 0.0f) continue;
                const float n = (0.6f * Mottle(x, y, xres, yres) + 0.4f * Hash(x, y, 3)) * 0.03f * detail * flat;   // about +-3% at detail 1
                out[i] = Pack(static_cast<int>(R(c) * (1.0f + n) + 0.5f), static_cast<int>(G(c) * (1.0f + n) + 0.5f), static_cast<int>(B(c) * (1.0f + n) + 0.5f));
            }
        }
    }
}

bool Downsample(const std::uint32_t* src, int srcXRes, int srcYRes, std::uint32_t* dst, int dstXRes, int dstYRes)
{
    if (dstXRes <= 0 || dstYRes <= 0 || srcXRes % dstXRes != 0 || srcYRes % dstYRes != 0) return false;
    const int bx = srcXRes / dstXRes, by = srcYRes / dstYRes;
    static float toLinear[256];
    static bool ready = false;
    if (!ready) { for (int i = 0; i < 256; ++i) toLinear[i] = static_cast<float>(i * i) / 255.0f; ready = true; }   // gamma 2: close to sRGB, cheap, and invertible
    for (int x = 0; x < dstXRes; ++x) {
        for (int y = 0; y < dstYRes; ++y) {
            float r = 0, g = 0, b = 0;
            for (int i = 0; i < bx; ++i)
                for (int j = 0; j < by; ++j) {
                    const std::uint32_t c = src[static_cast<std::size_t>(x * bx + i) * srcYRes + (y * by + j)];
                    r += toLinear[R(c)]; g += toLinear[G(c)]; b += toLinear[B(c)];
                }
            const float inv = 1.0f / (bx * by);
            dst[static_cast<std::size_t>(x) * dstYRes + y] = Pack(static_cast<int>(std::sqrt(r * inv * 255.0f) + 0.5f), static_cast<int>(std::sqrt(g * inv * 255.0f) + 0.5f), static_cast<int>(std::sqrt(b * inv * 255.0f) + 0.5f));
        }
    }
    return true;
}

void Upscale2x(const std::uint32_t* src, int srcXRes, int srcYRes, std::uint32_t* dst)
{
    const int dy2 = srcYRes * 2;
    auto at = [&](int x, int y) { return src[static_cast<std::size_t>(Wrap(x, srcXRes)) * srcYRes + Wrap(y, srcYRes)]; };
    const int close = 36;   // two colours closer than this (sum over the channels) are "the same" for deciding where an edge runs
    auto same = [&](std::uint32_t a, std::uint32_t b) { return std::abs(R(a) - R(b)) + std::abs(G(a) - G(b)) + std::abs(B(a) - B(b)) < close; };
    auto mix = [&](std::uint32_t a, std::uint32_t b) { return Pack((R(a) + R(b)) / 2, (G(a) + G(b)) / 2, (B(a) + B(b)) / 2); };
    // Edge-directed: each source texel becomes a 2x2 block, and a corner of the block moves to the colour of the diagonal edge that runs through it. If
    // the neighbours above and to the left of a texel agree with each other and not with the ones below and to the right, an edge cuts the corner
    // there, and that quarter takes the neighbours' colour (halfway, so the edge is smooth): a staircase of whole texels becomes a staircase of
    // half-texels. Flat areas and gradients are copied unchanged (so they stay smooth when sampled), and a straight edge is untouched.
    for (int x = 0; x < srcXRes; ++x) {
        for (int y = 0; y < srcYRes; ++y) {
            const std::uint32_t c = at(x, y), up = at(x, y - 1), down = at(x, y + 1), left = at(x - 1, y), right = at(x + 1, y);
            std::uint32_t tl = c, tr = c, bl = c, br = c;   // x grows to the right, y grows down
            if (same(left, up) && !same(left, down) && !same(up, right)) tl = mix(c, mix(left, up));
            if (same(up, right) && !same(up, left) && !same(right, down)) tr = mix(c, mix(up, right));
            if (same(down, left) && !same(down, right) && !same(left, up)) bl = mix(c, mix(down, left));
            if (same(right, down) && !same(right, up) && !same(down, left)) br = mix(c, mix(right, down));
            dst[static_cast<std::size_t>(x * 2) * dy2 + (y * 2)] = tl;
            dst[static_cast<std::size_t>(x * 2 + 1) * dy2 + (y * 2)] = tr;
            dst[static_cast<std::size_t>(x * 2) * dy2 + (y * 2 + 1)] = bl;
            dst[static_cast<std::size_t>(x * 2 + 1) * dy2 + (y * 2 + 1)] = br;
        }
    }
}
}
