// Racing against a replay: the paths built from a replay equal what the race did. A two-player replay of ClassicH is made with the scripted inputs, run through BuildReplayGhosts, and each
// player's ghost is compared with the headless race stepped on its own: the same positions at the same times. Run from the repository root.
#include <cstdio>
#include <cstdlib>

#include "ReplayGhosts.h"
#include "SimHarness.h"
#include "Sha256.h"
#include "../Authority/RaceWorld.h"
#include "../MainCharacter/StdAfx.h"
#include "../MainCharacter/MainCharacter.h"
#include "../ObjFac1/SimElements.h"
#include "../Util/DllObjectFactory.h"

int main()
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();   // no plug-in or resource file needed, as in the replay tool
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const std::string track = "ClassicH";
    const std::string path = SimHarness::TrackPath(track);
    ReplayFormat::Replay r;
    r.track = track; r.trackSha256 = Sha256::HashFile(path); r.players = 2; r.ticks = 1500; r.laps = 3; r.weapons = true;
    for (std::uint32_t t = 0; t < r.ticks; ++t) for (int p = 0; p < r.players; ++p) r.inputs.push_back(SimHarness::ScriptedInput(p, t));
    ReplayGhostField field;
    std::string error;
    if (!BuildReplayGhosts(r, path, field, error)) { std::printf("FAIL: %s\n", error.c_str()); return 1; }
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; };
    check(field.runs.size() == 2, "one ghost for each player of the replay");
    check(field.track == track && field.laps == 3, "the track and the laps come from the replay");
    // the same race stepped here
    Authority::RaceWorld world;
    world.Build(path, track, 2, 3, true, error, ReplayFormat::StartTimeMs(r.rules));
    bool same = true;
    int compared = 0;
    for (std::uint32_t t = 0; t < r.ticks; ++t) {
        world.Step(&r.inputs[static_cast<std::size_t>(t) * 2]);
        if ((t + 1) % 3 != 0) continue;
        const std::int64_t when = ReplayFormat::StartTimeMs(r.rules) + static_cast<std::int64_t>(t + 1) * ReplayFormat::kFrameMs;
        for (int p = 0; p < 2; ++p) {
            GhostRun::Sample at;
            if (!field.runs[static_cast<std::size_t>(p)].At(when, at)) continue;
            const MR_MainCharacter* craft = world.Craft(p);
            ++compared;
            if (at.x != craft->mPosition.mX || at.y != craft->mPosition.mY || at.z != craft->mPosition.mZ || at.room != craft->mRoom) same = false;
        }
    }
    check(compared > 400 && same, "every ghost sample equals the race's craft at that time (position and room)");
    GhostRun::Sample before, start;
    check(field.runs[0].At(ReplayFormat::StartTimeMs(r.rules), before), "the ghosts exist during the countdown (they wait on the grid)");
    check(field.runs[0].At(2000, start) && (start.x != before.x || start.y != before.y), "and move once the race has started");
    std::printf(failures == 0 ? "replay ghosts: all passed\n" : "replay ghosts: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
