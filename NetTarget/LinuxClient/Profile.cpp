#include "Profile.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace HoverNetProfile
{
namespace
{
constexpr int kFinishXp = 100, kPerLapXp = 25, kWinXp = 100, kPodiumXp = 50, kBestRaceXp = 75, kBestLapXp = 50, kOnlineXp = 50, kAchievementXp = 50;
}

const char* AchievementName(int a)
{
    static const char* const names[eAchievementCount] = {"First finish", "Podium", "Winner", "Ten races", "Fifty races", "Personal best", "Online racer", "Garage designer", "Tutorial graduate"};
    return a >= 0 && a < eAchievementCount ? names[a] : "";
}

const char* AchievementHint(int a)
{
    static const char* const hints[eAchievementCount] = {"Finish a race.", "Finish in the top three against other racers.", "Win a race against other racers.", "Finish ten races.",
                                                         "Finish fifty races.", "Beat your best time on a track.", "Finish a race online.", "Save a craft design in the Garage.", "Finish the tutorial."};
    return a >= 0 && a < eAchievementCount ? hints[a] : "";
}

long Profile::XpForLevel(int level) { return level <= 1 ? 0 : 100L * level * (level - 1); }

int Profile::LevelForXp(long xp)
{
    int level = 1;
    while (XpForLevel(level + 1) <= xp && level < 99) ++level;
    return level;
}

float Profile::LevelProgress() const
{
    const int level = Level();
    const long low = XpForLevel(level), high = XpForLevel(level + 1);
    return high > low ? static_cast<float>(xp - low) / static_cast<float>(high - low) : 1.0f;
}

Reward Profile::Apply(const RaceResult& r)
{
    Reward reward;
    reward.levelBefore = Level();
    ++races;
    if (r.finished) {
        ++finishes;
        reward.xp += kFinishXp + kPerLapXp * std::max(1, r.laps);
        reward.breakdown = "Finished +" + std::to_string(reward.xp);
        const bool against = r.players > 1;
        if (against && r.rank == 1) { ++wins; ++podiums; reward.xp += kWinXp; reward.breakdown += ", Win +" + std::to_string(kWinXp); }
        else if (against && r.rank <= 3) { ++podiums; reward.xp += kPodiumXp; reward.breakdown += ", Podium +" + std::to_string(kPodiumXp); }
        if (r.newRaceBest) { reward.xp += kBestRaceXp; reward.breakdown += ", New best +" + std::to_string(kBestRaceXp); }
        if (r.newLapBest) { reward.xp += kBestLapXp; reward.breakdown += ", Best lap +" + std::to_string(kBestLapXp); }
        if (r.online) { reward.xp += kOnlineXp; reward.breakdown += ", Online +" + std::to_string(kOnlineXp); }
        if (r.tutorial) { reward.xp = 0; reward.breakdown.clear(); }   // the tutorial's lap earns its own achievement, not racing XP
    }
    auto earn = [&](int a) { if (!Has(a)) { achievements |= 1u << a; reward.unlocked.push_back(a); reward.xp += kAchievementXp; } };
    if (r.finished && !r.tutorial) {
        earn(eFirstFinish);
        if (r.players > 1 && r.rank <= 3) earn(ePodium);
        if (r.players > 1 && r.rank == 1) earn(eWinner);
        if (finishes >= 10) earn(eTenRaces);
        if (finishes >= 50) earn(eFiftyRaces);
        if (r.newRaceBest || r.newLapBest) earn(eNewBest);
        if (r.online) earn(eOnlineRacer);
    }
    if (r.tutorial) { --races; if (r.finished) --finishes; }   // a practice lap is not a race on the record
    xp += reward.xp;
    reward.levelAfter = Level();
    return reward;
}

Reward Profile::Unlock(int a, int bonus)
{
    Reward reward;
    reward.levelBefore = Level();
    if (a >= 0 && a < eAchievementCount && !Has(a)) { achievements |= 1u << a; reward.unlocked.push_back(a); reward.xp = bonus; xp += bonus; }
    reward.levelAfter = Level();
    return reward;
}

bool Profile::Load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string key;
    long value;
    Profile loaded;
    while (in >> key >> value) {
        if (key == "xp") loaded.xp = std::max(0L, value);
        else if (key == "races") loaded.races = static_cast<int>(std::max(0L, value));
        else if (key == "finishes") loaded.finishes = static_cast<int>(std::max(0L, value));
        else if (key == "wins") loaded.wins = static_cast<int>(std::max(0L, value));
        else if (key == "podiums") loaded.podiums = static_cast<int>(std::max(0L, value));
        else if (key == "achievements") loaded.achievements = static_cast<unsigned>(value) & ((1u << eAchievementCount) - 1u);
    }
    *this = loaded;
    return true;
}

bool Profile::Save(const std::string& path) const
{
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp);
        if (!out) return false;
        out << "xp " << xp << "\nraces " << races << "\nfinishes " << finishes << "\nwins " << wins << "\npodiums " << podiums << "\nachievements " << achievements << '\n';
        if (!out) return false;
    }
    return std::rename(temp.c_str(), path.c_str()) == 0;
}
}
