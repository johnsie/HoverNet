// CraftPatternsTest -- every Garage pattern paints a sensible share of the livery (not nothing, not everything), has a name, and differs from the others.
#include "CraftPatterns.h"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
    int failures = 0;
    std::vector<std::string> shapes;
    for (int id = 0; id < HoverNetCraft::kPatternCount; ++id) {
        int covered = 0;
        std::string shape;
        for (int y = 0; y < HoverNetCraft::kPatternSize; ++y)
            for (int x = 0; x < HoverNetCraft::kPatternSize; ++x) {
                const bool on = HoverNetCraft::PatternCovers(id, x, y);
                covered += on ? 1 : 0;
                shape += on ? '1' : '0';
            }
        const double share = covered / 1024.0;
        std::printf("%-14s covers %4.1f%%\n", HoverNetCraft::PatternName(id), share * 100.0);
        if (std::string(HoverNetCraft::PatternName(id)).empty()) { std::printf("  FAIL: no name\n"); ++failures; }
        if (share < 0.08 || share > 0.65) { std::printf("  FAIL: covers an odd share\n"); ++failures; }
        for (const std::string& other : shapes) if (other == shape) { std::printf("  FAIL: the same as another pattern\n"); ++failures; }
        shapes.push_back(shape);
    }
    if (HoverNetCraft::PatternCovers(HoverNetCraft::kPatternCount, 3, 3)) { std::printf("FAIL: an unknown pattern paints\n"); ++failures; }
    std::printf(failures == 0 ? "craft patterns: ok\n" : "craft patterns: FAILED\n");
    return failures == 0 ? 0 : 1;
}
