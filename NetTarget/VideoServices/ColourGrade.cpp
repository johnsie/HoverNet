// ColourGrade.cpp -- see ColourGrade.h.
#include "ColourGrade.h"

#include <algorithm>
#include <cmath>

namespace HoverNetGfx
{
namespace
{
inline int Clamp255(int v) { return std::min(std::max(v, 0), 255); }
inline int Luma(int r, int g, int b) { return (r * 77 + g * 150 + b * 29) >> 8; }
}

ColourGrade::ColourGrade(float brightness)
{
    const float inverseGamma = 1.0f / std::min(1.6f, std::max(0.6f, brightness));   // brightness above 1 lifts the middle tones
    for (int i = 0; i < 256; ++i) {
        float x = i / 255.0f;
        x = std::pow(x, inverseGamma);
        const float sm = x * x * (3.0f - 2.0f * x);   // a very gentle S-curve: 18% of the way to smoothstep (a stronger one is what makes a picture look cartoon-contrasty)
        mCurve[i] = static_cast<std::uint8_t>(Clamp255(static_cast<int>((x * 0.82f + sm * 0.18f) * 255.0f + 0.5f)));
    }
}

void ColourGrade::Apply(int r, int g, int b, int& outR, int& outG, int& outB) const
{
    int c[3] = {mCurve[Clamp255(r)], mCurve[Clamp255(g)], mCurve[Clamp255(b)]};
    const int l = Luma(c[0], c[1], c[2]);
    for (int k = 0; k < 3; ++k) c[k] = Clamp255(l + (((c[k] - l) * 214) >> 8));   // saturation x 0.84: under the palette's, natural rather than cartoon-bright
    // Split toning: the faintest cool in the shadows and warmth in the highlights (at most +-3 levels); stronger looks like a film grade.
    const float shadow = std::min(l / 24.0f, 1.0f) * std::max(0.0f, 1.0f - l / 110.0f), highlight = std::max(0.0f, (l - 150) / 105.0f);   // pure black stays black
    c[0] = Clamp255(c[0] + static_cast<int>(3.0f * highlight - 1.5f * shadow));
    c[1] = Clamp255(c[1] + static_cast<int>(0.5f * highlight));
    c[2] = Clamp255(c[2] + static_cast<int>(2.5f * shadow - 2.0f * highlight));
    outR = c[0]; outG = c[1]; outB = c[2];
}

std::uint32_t ColourGrade::Apply(std::uint32_t word) const
{
    int r, g, b;
    Apply(static_cast<int>((word >> 16) & 255), static_cast<int>((word >> 8) & 255), static_cast<int>(word & 255), r, g, b);
    return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}
}
