// CraftPatterns.h -- ready-made patterns for the Garage's paint layer (stripes, checks, chevrons, flames ...). A pattern is only a stencil: the Garage fills the
// covered cells of the 32 x 32 livery with the selected paint colour, exactly as if the player had painted them, so a design that uses one is an ordinary
// design (nothing new is stored or sent, and it is cosmetic only like all of the paint).
#ifndef HOVERNET_CRAFT_PATTERNS_H
#define HOVERNET_CRAFT_PATTERNS_H

#include <cmath>
#include <cstdlib>

namespace HoverNetCraft
{
constexpr int kPatternCount = 14;
constexpr int kPatternSize = 32;

inline const char* PatternName(int id)
{
    static const char* const names[kPatternCount] = {"Racing stripe", "Twin stripes", "Hoops", "Checker", "Chevrons", "Diamonds", "Dots",
                                                      "Zigzag", "Diagonals", "Flames", "Rings", "Pinstripes", "Bars", "Fade dots"};
    return id >= 0 && id < kPatternCount ? names[id] : "";
}

// True where the pattern paints cell (x, y), both 0 to 31. Patterns that are left-right symmetric use the distance from the middle, so with Mirror on they stay whole.
inline bool PatternCovers(int id, int x, int y)
{
    const int mx = x < kPatternSize / 2 ? x : kPatternSize - 1 - x;   // 0 at the edges .. 15 in the middle
    switch (id) {
    case 0: return mx >= 13;                                           // one wide stripe down the middle
    case 1: return mx >= 7 && mx <= 9;                                 // two stripes, either side of the middle
    case 2: return y % 8 < 3;                                          // bands round the hull
    case 3: return ((x / 4) + (y / 4)) % 2 == 0;
    case 4: return ((y + std::abs(15 - mx)) / 4) % 2 == 0;             // V shapes pointing along the hull
    case 5: return std::abs(x % 8 - 4) + std::abs(y % 8 - 4) <= 3;
    case 6: { const int dx = x % 8 - 4, dy = y % 8 - 4; return dx * dx + dy * dy <= 5; }
    case 7: return (x + std::abs(y % 8 - 4)) % 8 < 3;                  // a zigzag line repeated
    case 8: return (x + y) % 8 < 3;
    case 9: {                                                          // tongues of flame rising from the bottom edge
        const int p = mx % 8;
        const int tongue = 5 + (p < 4 ? p * 3 : (7 - p) * 3);
        return y >= kPatternSize - tongue;
    }
    case 10: { const double r = std::sqrt(static_cast<double>((x - 16) * (x - 16) + (y - 16) * (y - 16))); return std::fmod(r, 7.0) < 2.5; }
    case 11: return x % 6 == 0;                                        // thin lines
    case 12: return (y / 3) % 3 == 0 && mx >= 4;                       // short bars
    case 13: { const int dx = x % 6 - 3, dy = y % 6 - 3; return dx * dx + dy * dy <= (y < 16 ? 1 : (y < 24 ? 3 : 5)); }   // dots that grow towards the bottom
    default: return false;
    }
}
}

#endif
