// Racing against a replay: every player of a saved race becomes a ghost (GhostRun.h: a path in time that is only drawn) for a new race on the same track. The replay is run once through the
// headless race (Authority::RaceWorld) with its recorded inputs, which gives each player's position, heading and room over the whole race; nothing of it enters the new race, so the new race
// is an ordinary one and nothing about it can differ from a race without ghosts (docs/ghosts.md).
#pragma once

#include <string>
#include <vector>

#include "GhostRun.h"
#include "../Authority/ReplayFormat.h"

struct ReplayGhostField
{
    std::string track;
    int laps = 0;
    bool weapons = true;
    std::vector<GhostRun> runs;               // by the replay's player slot
    std::vector<std::string> names;           // who drove each slot where known (may be empty)
    int winner = -1;                          // the slot that finished first, -1 if none did
    bool Active() const { return !runs.empty(); }
    void Clear() { track.clear(); laps = 0; runs.clear(); names.clear(); winner = -1; }
};

// Runs the replay and fills `out`. False with a reason in `error` when the track cannot be loaded or the replay does not fit it.
bool BuildReplayGhosts(const ReplayFormat::Replay& replay, const std::string& trackPath, ReplayGhostField& out, std::string& error);
