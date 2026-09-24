// RaceSession.h : Single race session management

#pragma once

#include "GameSimulation.h"

class RaceSession
{
public:
    RaceSession();
    ~RaceSession();

    int mRaceId;
    char mRaceName[33];
    GameSimulation mSimulation;
    std::vector<int> mPlayerClientIds;   // Connected player IDs
    time_t mCreatedTime;
    BOOL mRaceStarted;
    int mCreatorClientId;   // The player who created this race -- only they may start it

    // Initialize race
    BOOL Initialize(
        int raceId,
        const char* raceName,
        const char* trackName,
        int numLaps,
        BOOL allowWeapons,
        int creatorClientId);

    BOOL IsCreator(int clientId) const { return clientId == mCreatorClientId; }

    // Player joins this race
    int AddPlayer(int clientId, const char* playerName);

    // Player leaves this race
    void RemovePlayer(int clientId);

    // Called each frame
    void Update(float deltaTime);

    // Start the race
    void StartRace();

    // Get count of active players
    int GetActivePlayerCount() const { return (int)mPlayerClientIds.size(); }

    // Check if race should be cleaned up
    BOOL ShouldCleanup(time_t currentTime, int idleTimeoutSec) const;
};
