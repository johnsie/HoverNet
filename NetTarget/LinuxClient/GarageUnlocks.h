// GarageUnlocks.h -- what the Garage offers at each level and for each achievement. COSMETIC ONLY: every item here is paint (a stencil, a set of swatches, a stamp);
// none changes how a craft drives, and hulls stay open to everyone. Nothing is enforced on a server: a locked item is only greyed out in this player's Garage,
// and a design that uses one is an ordinary design that every client draws.
#ifndef HOVERNET_GARAGE_UNLOCKS_H
#define HOVERNET_GARAGE_UNLOCKS_H

#include "CraftPatterns.h"

#include <cstdint>
#include <string>

namespace HoverNetCraft
{
// The level at which each pattern (CraftPatterns.h, in that order) opens.
inline int PatternLevel(int pattern)
{
    static const int levels[kPatternCount] = {1, 1, 1, 2, 3, 4, 4, 5, 6, 7, 8, 3, 6, 9};
    return pattern >= 0 && pattern < kPatternCount ? levels[pattern] : 99;
}

// Sets of the 15 paint swatches (palette entries 1 to 15). The first is the Garage's own starting set.
constexpr int kThemeCount = 5;
inline const char* ThemeName(int theme) { static const char* const names[kThemeCount] = {"Classic", "Metallic", "Neon", "Pastel", "Earth"}; return theme >= 0 && theme < kThemeCount ? names[theme] : ""; }
inline int ThemeLevel(int theme) { static const int levels[kThemeCount] = {1, 3, 5, 7, 9}; return theme >= 0 && theme < kThemeCount ? levels[theme] : 99; }
inline const std::uint8_t* ThemeColour(int theme, int swatch /* 1..15 */)
{
    static const std::uint8_t sets[kThemeCount][15][3] = {
        {{255, 255, 255}, {20, 20, 20}, {220, 30, 30}, {240, 120, 20}, {245, 215, 30}, {40, 180, 70}, {30, 190, 190}, {40, 90, 220}, {120, 60, 200}, {230, 70, 170}, {150, 150, 150}, {90, 55, 30}, {250, 200, 160}, {10, 40, 90}, {255, 240, 200}},
        {{192, 192, 200}, {80, 84, 92}, {212, 175, 55}, {176, 120, 60}, {184, 100, 70}, {110, 140, 180}, {130, 140, 150}, {150, 150, 140}, {40, 44, 52}, {200, 140, 130}, {225, 225, 230}, {181, 166, 66}, {20, 22, 28}, {180, 210, 230}, {110, 110, 120}},
        {{255, 20, 80}, {255, 110, 0}, {255, 240, 0}, {120, 255, 0}, {0, 255, 120}, {0, 255, 230}, {0, 170, 255}, {60, 60, 255}, {160, 0, 255}, {255, 0, 220}, {255, 255, 255}, {20, 20, 20}, {255, 60, 60}, {0, 255, 60}, {255, 170, 0}},
        {{255, 179, 186}, {255, 223, 186}, {255, 255, 186}, {186, 255, 201}, {186, 225, 255}, {218, 186, 255}, {255, 190, 230}, {200, 200, 200}, {255, 255, 255}, {150, 210, 210}, {230, 210, 180}, {210, 180, 160}, {180, 200, 255}, {255, 200, 150}, {190, 240, 190}},
        {{92, 64, 51}, {139, 90, 43}, {160, 120, 70}, {101, 125, 70}, {70, 95, 60}, {120, 130, 90}, {180, 160, 110}, {210, 190, 140}, {90, 90, 80}, {60, 60, 55}, {150, 70, 50}, {190, 110, 70}, {230, 220, 190}, {40, 50, 40}, {100, 110, 120}}};
    return sets[theme][swatch - 1];
}

// Decals: 9 x 9 stamps, one for each achievement (HoverNetProfile::Achievement, in that order), earned by doing the thing.
constexpr int kDecalCount = 9;
constexpr int kDecalSize = 9;
inline const char* DecalName(int decal)
{
    static const char* const names[kDecalCount] = {"Star", "Trophy", "Crown", "Flag", "Flame", "Bolt", "Globe", "Spanner", "Arrow"};
    return decal >= 0 && decal < kDecalCount ? names[decal] : "";
}
inline const char* DecalEarnedBy(int decal)   // shown on a locked decal
{
    static const char* const how[kDecalCount] = {"Finish a race", "Finish on the podium", "Win a race", "Finish 10 races", "Finish 50 races", "Set a personal best", "Race online", "Save a craft", "Finish the tutorial"};
    return decal >= 0 && decal < kDecalCount ? how[decal] : "";
}
inline bool DecalCovers(int decal, int x, int y)
{
    static const char* const bitmaps[kDecalCount][kDecalSize] = {
        {"....#....", "....#....", "...###...", "#########", ".#######.", "..#####..", "..##.##..", ".##...##.", ".#.....#."},
        {"#########", "#.#####.#", "#.#####.#", ".#######.", "..#####..", "...###...", "....#....", "....#....", "..#####.."},
        {"#...#...#", "##..#..##", "###.#.###", "#########", "#########", "#########", "#########", "#########", "........."},
        {"#.#.#.#.#", ".#.#.#.#.", "#.#.#.#.#", ".#.#.#.#.", "#........", "#........", "#........", "#........", "#........"},
        {"....#....", "...##....", "...###...", "..####.#.", ".#######.", ".#######.", "#########", "#########", ".#######."},
        {"....###..", "...###...", "..###....", ".######..", "...###...", "..###....", ".###.....", ".##......", ".#......."},
        {"..#####..", ".#.#.#.#.", "#..#.#..#", "#########", "#..#.#..#", "#########", "#..#.#..#", ".#.#.#.#.", "..#####.."},
        {"..##.##..", ".###.###.", ".###.###.", "..#####..", "...###...", "...###...", "...###...", "...###...", "...###..."},
        {"....#....", "...###...", "..#####..", ".#######.", "....#....", "....#....", "....#....", "....#....", "....#...."}};
    return decal >= 0 && decal < kDecalCount && x >= 0 && y >= 0 && x < kDecalSize && y < kDecalSize && bitmaps[decal][y][x] == '#';
}

// A title for each stretch of levels, shown beside the level on the main menu and the results screen (local to the player: it is not sent to anyone).
inline const char* LevelTitle(int level)
{
    return level >= 15 ? "Legend" : level >= 12 ? "Champion" : level >= 9 ? "Ace" : level >= 7 ? "Veteran" : level >= 5 ? "Pro" : level >= 3 ? "Contender" : level >= 2 ? "Racer" : "Rookie";
}

// What reaching this level adds to the Garage ("" when nothing): patterns and colour sets that open at it, and a wider shape range at levels 3, 6 and 9.
inline std::string UnlockedAtLevel(int level)
{
    std::string out;
    auto add = [&](const std::string& item) { if (!out.empty()) out += ", "; out += item; };
    for (int p = 0; p < kPatternCount; ++p) if (level > 1 && PatternLevel(p) == level) add(std::string(PatternName(p)) + " pattern");
    for (int t = 1; t < kThemeCount; ++t) if (ThemeLevel(t) == level) add(std::string(ThemeName(t)) + " colours");
    if (level == 3 || level == 6 || level == 9) add("a wider shape range");
    return out;
}
}

#endif
