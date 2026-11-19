#include "ReplayGhosts.h"

#include "../Authority/RaceWorld.h"
#include "../MainCharacter/StdAfx.h"
#include "../MainCharacter/MainCharacter.h"

bool BuildReplayGhosts(const ReplayFormat::Replay& replay, const std::string& trackPath, ReplayGhostField& out, std::string& error)
{
    out.Clear();
    if (replay.players < 1 || replay.players > ReplayFormat::kMaxPlayers || replay.inputs.size() < static_cast<std::size_t>(replay.ticks) * replay.players) { error = "The replay is incomplete."; return false; }
    Authority::RaceWorld world;
    const std::int64_t startMs = ReplayFormat::StartTimeMs(replay.rules);
    if (!world.Build(trackPath, replay.track, replay.players, replay.laps, replay.weapons, error, startMs)) return false;
    ReplayGhostField field;
    field.track = replay.track; field.laps = replay.laps; field.weapons = replay.weapons;
    field.runs.resize(static_cast<std::size_t>(replay.players));
    std::vector<bool> done(static_cast<std::size_t>(replay.players), false);
    std::int32_t bestFinish = 0;
    auto note = [&](int player, std::int64_t timeMs) {
        const MR_MainCharacter* craft = world.Craft(player);
        GhostRun& run = field.runs[static_cast<std::size_t>(player)];
        GhostRun::Sample s;
        s.t = static_cast<std::int32_t>(timeMs);
        s.x = craft->mPosition.mX; s.y = craft->mPosition.mY; s.z = craft->mPosition.mZ;
        s.orientation = static_cast<std::int16_t>(craft->GetCabinOrientation());
        s.room = static_cast<std::int16_t>(craft->mRoom);
        run.samples.push_back(s);   // (directly, not Record: the countdown is before time 0)
    };
    for (int p = 0; p < replay.players; ++p) {
        if (replay.rules < 7) {   // rules 6 have no countdown: the ghosts wait on the grid for the six seconds a local race counts down
            GhostRun::Sample wait; wait.t = -6000; 
            const MR_MainCharacter* craft = world.Craft(p);
            wait.x = craft->mPosition.mX; wait.y = craft->mPosition.mY; wait.z = craft->mPosition.mZ; wait.orientation = static_cast<std::int16_t>(craft->GetCabinOrientation()); wait.room = static_cast<std::int16_t>(craft->mRoom);
            field.runs[static_cast<std::size_t>(p)].samples.push_back(wait);
        }
        note(p, startMs);
    }
    for (std::uint32_t tick = 0; tick < replay.ticks; ++tick) {
        world.Step(&replay.inputs[static_cast<std::size_t>(tick) * replay.players]);
        const std::int64_t now = startMs + static_cast<std::int64_t>(tick + 1) * ReplayFormat::kFrameMs;
        if ((tick + 1) % 3 != 0) continue;   // one point every 48 ms, as the recorded ghost keeps
        bool anyLeft = false;
        for (int p = 0; p < replay.players; ++p) {
            if (done[static_cast<std::size_t>(p)]) continue;
            note(p, now);
            if (world.Craft(p)->HasFinish()) {
                done[static_cast<std::size_t>(p)] = true;
                const std::int32_t total = static_cast<std::int32_t>(world.Craft(p)->GetTotalTime());
                field.runs[static_cast<std::size_t>(p)].finishMs = total;
                field.runs[static_cast<std::size_t>(p)].laps = replay.laps;
                if (total > 0 && (bestFinish == 0 || total < bestFinish)) { bestFinish = total; field.winner = p; }
            }
            else anyLeft = true;
        }
        if (!anyLeft) break;
    }
    for (int p = 0; p < replay.players; ++p) {
        GhostRun& run = field.runs[static_cast<std::size_t>(p)];
        run.track = replay.track;
        if (run.laps == 0) run.laps = replay.laps;
        if (run.samples.size() < 2) { error = "The replay has no race in it."; return false; }
    }
    out = std::move(field);
    return true;
}
