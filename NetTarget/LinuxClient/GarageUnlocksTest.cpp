// GarageUnlocksTest -- the unlock tables are complete and sensible: the first level offers something of everything, everything opens by level 10, themes have
// 15 distinct swatches, and every decal is a real picture and is tied to its own achievement.
#include "GarageUnlocks.h"
#include "Profile.h"

#include <cstdio>
#include <set>
#include <string>

static int failures = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } }

int main()
{
    int firstPatterns = 0, latest = 0;
    for (int p = 0; p < HoverNetCraft::kPatternCount; ++p) { if (HoverNetCraft::PatternLevel(p) <= 1) ++firstPatterns; if (HoverNetCraft::PatternLevel(p) > latest) latest = HoverNetCraft::PatternLevel(p); }
    Check(firstPatterns >= 3, "a new player has a few patterns");
    Check(latest <= 10, "every pattern opens by level 10");
    Check(HoverNetCraft::ThemeLevel(0) == 1, "the starting colour set is open from the start");
    for (int t = 0; t < HoverNetCraft::kThemeCount; ++t) {
        std::set<std::string> colours;
        for (int s = 1; s <= 15; ++s) { const std::uint8_t* c = HoverNetCraft::ThemeColour(t, s); colours.insert(std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2])); }
        Check(colours.size() == 15, "a colour set has 15 different swatches");
        Check(std::string(HoverNetCraft::ThemeName(t)).size() > 0, "a colour set has a name");
        Check(t == 0 || HoverNetCraft::ThemeLevel(t) > HoverNetCraft::ThemeLevel(t - 1), "colour sets open in order");
    }
    Check(HoverNetCraft::kDecalCount == HoverNetProfile::eAchievementCount, "one decal for each achievement");
    std::set<std::string> pictures;
    for (int d = 0; d < HoverNetCraft::kDecalCount; ++d) {
        int covered = 0;
        std::string picture;
        for (int y = 0; y < HoverNetCraft::kDecalSize; ++y) for (int x = 0; x < HoverNetCraft::kDecalSize; ++x) { const bool on = HoverNetCraft::DecalCovers(d, x, y); covered += on ? 1 : 0; picture += on ? '1' : '0'; }
        Check(covered >= 12 && covered <= 65, "a decal covers a sensible share of its square");
        pictures.insert(picture);
        Check(std::string(HoverNetCraft::DecalName(d)).size() > 0 && std::string(HoverNetCraft::DecalEarnedBy(d)).size() > 0, "a decal has a name and says how it is earned");
    }
    Check(static_cast<int>(pictures.size()) == HoverNetCraft::kDecalCount, "the decals are all different");
    Check(std::string(HoverNetCraft::LevelTitle(1)) == "Rookie" && std::string(HoverNetCraft::LevelTitle(20)) == "Legend", "titles run from Rookie to Legend");
    Check(HoverNetCraft::UnlockedAtLevel(3).find("Chevrons") != std::string::npos && HoverNetCraft::UnlockedAtLevel(3).find("Metallic") != std::string::npos, "level 3 opens chevrons and metallic colours");
    Check(HoverNetCraft::UnlockedAtLevel(1).empty(), "level 1 announces nothing: it is the start");
    std::printf(failures == 0 ? "garage unlocks: ok\n" : "garage unlocks: FAILED\n");
    return failures == 0 ? 0 : 1;
}
