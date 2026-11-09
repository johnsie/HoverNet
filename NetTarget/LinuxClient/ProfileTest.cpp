#include "Profile.h"

#include <cstdio>
#include <cstdlib>

using namespace HoverNetProfile;
namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    Check(Profile::LevelForXp(0) == 1 && Profile::LevelForXp(199) == 1 && Profile::LevelForXp(200) == 2 && Profile::LevelForXp(599) == 2 && Profile::LevelForXp(600) == 3, "levels come at 0, 200, 600, 1200 XP");
    Profile p;
    RaceResult solo; solo.finished = true; solo.laps = 3;
    Reward r = p.Apply(solo);
    Check(r.xp == 100 + 75 + 50 && p.races == 1 && p.finishes == 1 && p.Has(eFirstFinish) && r.unlocked.size() == 1, "a solo three-lap finish earns 175 XP and the first-finish achievement (+50)");
    r = p.Apply(solo);
    Check(r.xp == 175 && r.unlocked.empty(), "the same again earns no second achievement");
    RaceResult win; win.finished = true; win.laps = 2; win.rank = 1; win.players = 4; win.online = true; win.newRaceBest = true;
    r = p.Apply(win);
    Check(p.wins == 1 && p.podiums == 1 && p.Has(eWinner) && p.Has(ePodium) && p.Has(eOnlineRacer) && p.Has(eNewBest), "an online win with a new best earns winner, podium, online and best");
    Check(r.breakdown.find("Win") != std::string::npos && r.breakdown.find("New best") != std::string::npos && r.xp > 400, "and says why");
    RaceResult dnf; dnf.finished = false; dnf.rank = 4; dnf.players = 4;
    const long before = p.xp;
    r = p.Apply(dnf);
    Check(r.xp == 0 && p.xp == before && p.races == 4, "a race not finished counts but earns nothing");
    RaceResult practice; practice.finished = true; practice.tutorial = true; practice.laps = 1;
    const int racesBefore = p.races;
    r = p.Apply(practice);
    Check(r.xp == 0 && p.races == racesBefore, "the tutorial lap is not a race on the record");
    Check(p.Unlock(eTutorialGraduate).unlocked.size() == 1 && p.Unlock(eTutorialGraduate).unlocked.empty(), "an achievement for doing something else is earned once");
    for (int i = 0; i < 12; ++i) p.Apply(solo);
    Check(p.Has(eTenRaces) && !p.Has(eFiftyRaces), "ten finishes earn Ten races, not Fifty");
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/hovernet_profile_test";
    Check(p.Save(path), "saves");
    Profile q;
    Check(q.Load(path) && q.xp == p.xp && q.races == p.races && q.achievements == p.achievements && q.wins == p.wins, "and loads the same");
    std::remove(path.c_str());
    Check(q.Level() >= 2 && q.LevelProgress() >= 0.0f && q.LevelProgress() <= 1.0f, "the level and its progress make sense");
    std::printf(gFail == 0 ? "profile: all checks passed\n" : "profile: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
