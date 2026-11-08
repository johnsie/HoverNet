// TextureEnhanceTest -- the texture enhancement (VideoServices/TextureEnhance.h) on synthetic textures with a known answer.
#include "../VideoServices/TextureEnhance.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using namespace HoverNetTex;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFailures; }

int R(std::uint32_t c) { return static_cast<int>((c >> 16) & 255); }
int G(std::uint32_t c) { return static_cast<int>((c >> 8) & 255); }
int B(std::uint32_t c) { return static_cast<int>(c & 255); }
std::uint32_t Pack(int r, int g, int b) { return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b); }

// A palette whose colours all differ and that changes smoothly, as the game's colour ramps do: index i is (i, 255 - i, 40 + i / 2).
void Palette(std::uint8_t* p) { for (int i = 0; i < 256; ++i) { p[i * 3] = static_cast<std::uint8_t>(i); p[i * 3 + 1] = static_cast<std::uint8_t>(255 - i); p[i * 3 + 2] = static_cast<std::uint8_t>(40 + i / 2); } }

double Variance(const std::vector<std::uint32_t>& t, int x0, int x1, int y0, int y1, int yres)
{
    double sum = 0, sum2 = 0; int n = 0;
    for (int x = x0; x < x1; ++x) for (int y = y0; y < y1; ++y) { const double v = G(t[static_cast<std::size_t>(x) * yres + y]); sum += v; sum2 += v * v; ++n; }
    const double mean = sum / n;
    return sum2 / n - mean * mean;
}
}

int main()
{
    std::uint8_t pal[768];
    Palette(pal);
    const int W = 32, H = 32;

    // 1. Without enhancement the texture is exactly the palette's colours.
    {
        std::vector<std::uint8_t> idx(W * H);
        for (int i = 0; i < W * H; ++i) idx[i] = static_cast<std::uint8_t>(i * 7);
        std::vector<std::uint32_t> out(W * H);
        EnhanceBase(idx.data(), W, H, pal, out.data(), false);
        bool exact = true;
        for (int i = 0; i < W * H; ++i) exact = exact && out[i] == Pack(pal[idx[i] * 3], pal[idx[i] * 3 + 1], pal[idx[i] * 3 + 2]);
        Check(exact, "without enhancement a texel is exactly its palette colour");
    }

    // 2. A dithered area (two close colours in a checker) is smoothed; a hard edge elsewhere is kept.
    {
        std::vector<std::uint8_t> idx(W * H);
        for (int x = 0; x < W; ++x) for (int y = 0; y < H; ++y) {
            if (x < 16) idx[static_cast<std::size_t>(x) * H + y] = static_cast<std::uint8_t>(((x + y) & 1) ? 100 : 104);   // a dither between two nearby colours
            else idx[static_cast<std::size_t>(x) * H + y] = 250;                                                            // a very different flat colour
        }
        std::vector<std::uint32_t> plain(W * H), enh(W * H);
        EnhanceBase(idx.data(), W, H, pal, plain.data(), false);
        EnhanceBase(idx.data(), W, H, pal, enh.data(), true);
        const double before = Variance(plain, 3, 13, 3, 29, H), after = Variance(enh, 3, 13, 3, 29, H);
        char what[160];
        std::snprintf(what, sizeof what, "a dither is smoothed away (the variation of the green channel falls from %.2f to %.2f)", before, after);
        Check(after < before * 0.25, what);
        // The edge at x = 16 (and the wrap at x = 0): the colours on each side keep their values to within the sharpening's overshoot.
        const std::uint32_t leftPlain = plain[static_cast<std::size_t>(14) * H + 10], rightPlain = plain[static_cast<std::size_t>(18) * H + 10];
        const std::uint32_t leftEnh = enh[static_cast<std::size_t>(14) * H + 10], rightEnh = enh[static_cast<std::size_t>(18) * H + 10];
        Check(std::abs(G(rightEnh) - G(rightPlain)) <= 3 && std::abs(R(rightEnh) - R(rightPlain)) <= 3, "a flat area away from the edge keeps its colour");
        Check(std::abs(G(leftEnh) - G(leftPlain)) <= 6 + 4, "the smoothed side stays near the colour it was (within a few levels)");
        // The edge is not blurred: the step between the two sides stays at least 90% of what it was.
        const std::uint32_t aEnh = enh[static_cast<std::size_t>(15) * H + 10], bEnh = enh[static_cast<std::size_t>(16) * H + 10];
        const std::uint32_t aPlain = plain[static_cast<std::size_t>(15) * H + 10], bPlain = plain[static_cast<std::size_t>(16) * H + 10];
        Check(std::abs(G(aEnh) - G(bEnh)) >= std::abs(G(aPlain) - G(bPlain)) * 9 / 10, "a hard edge is not blurred (the step across it keeps at least 90% of its size)");
    }

    // 3. Beyond the palette: a banded gradient and a dither of palette entries give more distinct colours than the palette has.
    {
        std::vector<std::uint8_t> idx(W * H);
        std::set<std::uint8_t> used;
        for (int x = 0; x < W; ++x) for (int y = 0; y < H; ++y) {
            const int base = 60 + (x / 4) * 3;                                       // eight bands, three palette steps apart
            const std::uint8_t v = static_cast<std::uint8_t>(base + (((x + 2 * y) % 5) == 0 ? 3 : 0));   // dithered towards the next band
            idx[static_cast<std::size_t>(x) * H + y] = v; used.insert(v);
        }
        std::vector<std::uint32_t> enh(W * H);
        EnhanceBase(idx.data(), W, H, pal, enh.data(), true);
        std::set<std::uint32_t> colours(enh.begin(), enh.end());
        char what[160];
        std::snprintf(what, sizeof what, "the true-colour texture has more colours than the palette entries it came from (%zu colours from %zu entries)", colours.size(), used.size());
        Check(colours.size() > used.size() * 2, what);
    }

    // 4. Tiling: a texel on the edge of the texture is smoothed against the opposite edge, like any other.
    {
        std::vector<std::uint8_t> idx(W * H);
        for (int x = 0; x < W; ++x) for (int y = 0; y < H; ++y) idx[static_cast<std::size_t>(x) * H + y] = static_cast<std::uint8_t>(((x + y) & 1) ? 100 : 104);
        std::vector<std::uint32_t> enh(W * H);
        EnhanceBase(idx.data(), W, H, pal, enh.data(), true);
        const double edge = Variance(enh, 0, 1, 0, H, H) + Variance(enh, W - 1, W, 0, H, H) + Variance(enh, 0, W, 0, 1, H), middle = Variance(enh, 8, 24, 8, 24, H) * 3;
        Check(edge <= middle + 1.0 + 0.5, "texels on the edge of the texture are smoothed like the rest (it tiles)");
    }

    // 5. Mips: averaged in linear light, so a black and white checker is about 180, not 128; flat stays flat; a bad size is refused.
    {
        std::vector<std::uint32_t> checker(W * H), half(16 * 16);
        for (int x = 0; x < W; ++x) for (int y = 0; y < H; ++y) checker[static_cast<std::size_t>(x) * H + y] = ((x + y) & 1) ? 0x00FFFFFFu : 0x00000000u;
        Check(Downsample(checker.data(), W, H, half.data(), 16, 16), "a mip level is made when the size divides");
        Check(std::abs(G(half[5 * 16 + 5]) - 180) <= 2, "a black and white checker averages to about 180 (linear light), not 128");
        std::vector<std::uint32_t> flat(W * H, Pack(70, 90, 110)), flatHalf(8 * 8);
        Downsample(flat.data(), W, H, flatHalf.data(), 8, 8);
        Check(std::abs(R(flatHalf[3]) - 70) <= 1 && std::abs(G(flatHalf[3]) - 90) <= 1 && std::abs(B(flatHalf[3]) - 110) <= 1, "a flat colour stays the same in every mip level");
        std::vector<std::uint32_t> bad(5 * 5);
        Check(!Downsample(flat.data(), W, H, bad.data(), 5, 5), "a size that does not divide is refused");
    }

    // 6. Upscale 2x: flat stays flat, a gradient stays smooth and in order, a hard edge stays hard.
    {
        std::vector<std::uint32_t> flat(8 * 8, Pack(50, 60, 70)), up(16 * 16);
        Upscale2x(flat.data(), 8, 8, up.data());
        bool same = true;
        for (const std::uint32_t c : up) same = same && c == Pack(50, 60, 70);
        Check(same, "a flat texture is flat when doubled");
        std::vector<std::uint32_t> ramp(16 * 4), rampUp(32 * 8);
        for (int x = 0; x < 16; ++x) for (int y = 0; y < 4; ++y) ramp[static_cast<std::size_t>(x) * 4 + y] = Pack(10 + x * 8, 10 + x * 8, 10 + x * 8);
        Upscale2x(ramp.data(), 16, 4, rampUp.data());
        bool ordered = true;
        for (int x = 1; x < 31; ++x) ordered = ordered && G(rampUp[static_cast<std::size_t>(x) * 8 + 3]) >= G(rampUp[static_cast<std::size_t>(x - 1) * 8 + 3]) - 1;
        Check(ordered, "a gradient stays in order when doubled");
        std::vector<std::uint32_t> edge(16 * 4), edgeUp(32 * 8);
        for (int x = 0; x < 16; ++x) for (int y = 0; y < 4; ++y) edge[static_cast<std::size_t>(x) * 4 + y] = x < 8 ? Pack(20, 20, 20) : Pack(230, 230, 230);
        Upscale2x(edge.data(), 16, 4, edgeUp.data());
        Check(G(edgeUp[static_cast<std::size_t>(15) * 8 + 3]) <= 25 && G(edgeUp[static_cast<std::size_t>(16) * 8 + 3]) >= 225, "a hard edge stays hard when doubled");
    }

    // 7. A diagonal staircase edge gets finer steps when doubled: the corner quarters of the stair texels take the colour of the edge's other side.
    {
        std::vector<std::uint32_t> diag(16 * 16), up(32 * 32);
        for (int x = 0; x < 16; ++x) for (int y = 0; y < 16; ++y) diag[static_cast<std::size_t>(x) * 16 + y] = x > y ? Pack(220, 220, 220) : Pack(30, 30, 30);
        Upscale2x(diag.data(), 16, 16, up.data());
        int mixed = 0, copied = 0;
        for (int x = 2; x < 14; ++x) for (int y = 2; y < 14; ++y) {
            const bool onEdge = (x == y || x == y + 1 || x + 1 == y);
            for (int ox = 0; ox < 2; ++ox) for (int oy = 0; oy < 2; ++oy) {
                const std::uint32_t v = up[static_cast<std::size_t>(x * 2 + ox) * 32 + (y * 2 + oy)], c = diag[static_cast<std::size_t>(x) * 16 + y];
                if (onEdge && v != c) ++mixed;
                if (!onEdge && v == c) ++copied;
            }
        }
        Check(mixed > 6, "the corners of the texels along a diagonal edge take the colour of the other side (finer steps)");
        bool farSame = true;
        for (int x = 2; x < 14; ++x) for (int y = 2; y < 14; ++y) {   // the interior: the texture tiles, so its corners meet the opposite side
            if (std::abs(x - y) < 3) continue;   // near the diagonal
            for (int ox = 0; ox < 2; ++ox) for (int oy = 0; oy < 2; ++oy) farSame = farSame && up[static_cast<std::size_t>(x * 2 + ox) * 32 + (y * 2 + oy)] == diag[static_cast<std::size_t>(x) * 16 + y];
        }
        Check(farSame && copied > 0, "and every texel away from the edge is copied unchanged");
        std::vector<std::uint32_t> straight(16 * 16), straightUp(32 * 32);
        for (int x = 0; x < 16; ++x) for (int y = 0; y < 16; ++y) straight[static_cast<std::size_t>(x) * 16 + y] = x >= 8 ? Pack(220, 220, 220) : Pack(30, 30, 30);
        Upscale2x(straight.data(), 16, 16, straightUp.data());
        bool untouched = true;
        for (int x = 0; x < 32; ++x) for (int y = 0; y < 32; ++y) untouched = untouched && straightUp[static_cast<std::size_t>(x) * 32 + y] == straight[static_cast<std::size_t>(x / 2) * 16 + y / 2];
        Check(untouched, "a straight edge is left untouched");
    }

    // 8. Surface detail: a flat field gets a trace of variation (about +-3%), an edge does not, it is repeatable, and it tiles.
    {
        std::vector<std::uint8_t> idx(W * H, 120);
        for (int x = 0; x < W; ++x) for (int y = 0; y < H; ++y) if (x >= 16) idx[static_cast<std::size_t>(x) * H + y] = 40;   // a flat field, then a very different one
        std::vector<std::uint32_t> flat(W * H), detailed(W * H), again(W * H);
        EnhanceBase(idx.data(), W, H, pal, flat.data(), true, 0);
        EnhanceBase(idx.data(), W, H, pal, detailed.data(), true, 1);
        EnhanceBase(idx.data(), W, H, pal, again.data(), true, 1);
        std::set<std::uint32_t> flatColours, detailColours;
        int worst = 0;
        for (int x = 4; x < 12; ++x) for (int y = 4; y < 28; ++y) {
            flatColours.insert(flat[static_cast<std::size_t>(x) * H + y]); detailColours.insert(detailed[static_cast<std::size_t>(x) * H + y]);
            worst = std::max(worst, std::abs(G(detailed[static_cast<std::size_t>(x) * H + y]) - G(flat[static_cast<std::size_t>(x) * H + y])));
        }
        char what[160];
        std::snprintf(what, sizeof what, "a flat field gains variation (%zu colours from %zu) of no more than a few levels (at most %d)", detailColours.size(), flatColours.size(), worst);
        Check(detailColours.size() > flatColours.size() + 4 && worst >= 1 && worst <= 12, what);
        Check(detailed == again, "the surface detail is the same every time");
        // The step between the two fields (x = 15 | 16) keeps its size within a few levels.
        const int stepFlat = std::abs(G(flat[static_cast<std::size_t>(15) * H + 10]) - G(flat[static_cast<std::size_t>(16) * H + 10]));
        const int stepDetail = std::abs(G(detailed[static_cast<std::size_t>(15) * H + 10]) - G(detailed[static_cast<std::size_t>(16) * H + 10]));
        Check(std::abs(stepFlat - stepDetail) <= 8, "an edge is not roughened by the detail");
        // Tiles: on a texture that is flat all the way round, the variation across the wrap (last column to first) is no bigger than anywhere else.
        std::vector<std::uint8_t> flatIdx(W * H, 120);
        std::vector<std::uint32_t> tile(W * H);
        EnhanceBase(flatIdx.data(), W, H, pal, tile.data(), true, 1);
        long edgeSum = 0, innerSum = 0;
        for (int y = 0; y < H; ++y) { edgeSum += std::abs(G(tile[static_cast<std::size_t>(W - 1) * H + y]) - G(tile[static_cast<std::size_t>(0) * H + y])); innerSum += std::abs(G(tile[static_cast<std::size_t>(6) * H + y]) - G(tile[static_cast<std::size_t>(7) * H + y])); }
        Check(edgeSum <= innerSum * 2 + 20, "the detail tiles: no seam where the texture meets itself");
    }

    std::printf("%s\n", gFailures == 0 ? "texture enhance: all checks passed" : "texture enhance: FAILED");
    return gFailures == 0 ? 0 : 1;
}
