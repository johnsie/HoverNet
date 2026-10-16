// SimHarness.h -- a headless race for tools and tests: a track, a few craft, scripted inputs, one state hash.
// Needs MR_RegisterSimElements() (or a plug-in) and MR_InitTrigoTables() to have been called.
#ifndef SIM_HARNESS_H
#define SIM_HARNESS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../MainCharacter/StdAfx.h"
#define protected public
#define private public
#include "../MainCharacter/MainCharacter.h"
#undef private
#undef protected
#include "../Model/GameSession.h"
#include "../Model/StateHash.h"
#include "../Util/RecordFile.h"

namespace SimHarness
{
constexpr int kFrameMs = 16;

// ---- the simulation ---------------------------------------------------------------------------------------
struct World
{
    std::unique_ptr<MR_GameSession> session;
    std::vector<MR_MainCharacter*> craft;

    bool Build(const std::string& path, const std::string& name, int players, int laps = 5, bool weapons = true)
    {
        MR_RecordFile* track = new MR_RecordFile;
        if (!track->OpenForRead(path.c_str())) { delete track; return false; }
        session.reset(new MR_GameSession(FALSE));
        if (!session->LoadNew(name.c_str(), track) || session->GetCurrentLevel() == nullptr) return false;
        MR_Level* level = session->GetCurrentLevel();
        if (level->GetPlayerCount() < players) return false;
        for (int i = 0; i < players; ++i) {
            const int room = level->GetStartingRoom(i);
            MR_MainCharacter* c = MR_MainCharacter::New(laps, weapons ? TRUE : FALSE);
            if (c == nullptr || room < 0 || room >= level->GetRoomCount()) return false;
            c->mPosition = level->GetStartingPos(i);
            c->SetOrientation(level->GetStartingOrientation(i));
            c->mRoom = room;
            c->SetHoverId(i);   // hover id = grid slot, as in the game and on the server
            if (level->InsertElement(c, room, FALSE) == nullptr) return false;
            craft.push_back(c);
        }
        session->SetSimulationTime(0);
        return true;
    }

    void Step(const std::uint16_t* inputs)
    {
        for (std::size_t i = 0; i < craft.size(); ++i) craft[i]->SetControlState(inputs[i], session->GetSimulationTime());
        session->SimulateStep(kFrameMs);
    }

    // The whole state of the race, hashed by the simulation library (docs/specs/simulation-state.md).
    std::uint64_t Hash() const { return MR_HashLevelState(session->GetCurrentLevel(), session->GetSimulationTime()); }
};

inline std::string TrackPath(const std::string& name)
{
    return name.find('/') != std::string::npos ? name : "NetTarget/Tracks/" + name + ".trk";
}

inline std::uint16_t ScriptedInput(int player, std::uint32_t tick)
{
    int c = MR_MainCharacter::eMotorOn;
    const int period = 37 + 16 * player;
    const int phase = static_cast<int>(tick / period) % 3;
    if (phase == 0) c |= MR_MainCharacter::eLeft;
    else if (phase == 1) c |= MR_MainCharacter::eRight;
    const std::uint32_t jumpPeriod = 190 + 40 * player;
    if (tick % jumpPeriod >= 150 && tick % jumpPeriod < 160) c |= MR_MainCharacter::eJump;
    if (tick % (70 + 20 * player) == 30) c |= MR_MainCharacter::eFire;
    if (tick % (410 + 30 * player) == 350) c |= MR_MainCharacter::eSelectWeapon;
    return static_cast<std::uint16_t>(c);
}
}

#endif
