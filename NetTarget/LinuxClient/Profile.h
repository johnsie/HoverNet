// Profile.h -- the player's progress on this device: races, wins, experience (XP) and levels, and achievements. Nothing here touches a race; it only counts what happened.
// Kept in <config>/profile (a few lines of text). The numbers are the owner's to tune: see the constants in Profile.cpp.
#ifndef HOVERNET_PROFILE_H
#define HOVERNET_PROFILE_H

#include <string>
#include <vector>

namespace HoverNetProfile
{
enum Achievement
{
    eFirstFinish, ePodium, eWinner, eTenRaces, eFiftyRaces, eNewBest, eOnlineRacer, eGarageDesigner, eTutorialGraduate, eAchievementCount
};
const char* AchievementName(int achievement);
const char* AchievementHint(int achievement);

struct RaceResult
{
    bool finished = false;
    int rank = 1, players = 1, laps = 1;
    bool newRaceBest = false, newLapBest = false;
    bool online = false, tutorial = false;
};

struct Reward
{
    int xp = 0;
    int levelBefore = 1, levelAfter = 1;
    std::vector<int> unlocked;       // achievements earned by this race (or action)
    std::string breakdown;           // "Finished +125, Win +100, New best +75"
};

struct Profile
{
    long xp = 0;
    int races = 0, finishes = 0, wins = 0, podiums = 0;
    unsigned achievements = 0;       // a bit per Achievement

    Reward Apply(const RaceResult& result);        // counts a race and gives its XP
    Reward Unlock(int achievement, int xp = 50);   // an achievement earned by doing something else (saving a craft, finishing the tutorial)
    bool Has(int achievement) const { return (achievements >> achievement & 1u) != 0; }
    int Level() const { return LevelForXp(xp); }
    float LevelProgress() const;                    // 0..1 of the way to the next level

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    static int LevelForXp(long xp);
    static long XpForLevel(int level);              // the XP a player needs to be this level
};
}

#endif
