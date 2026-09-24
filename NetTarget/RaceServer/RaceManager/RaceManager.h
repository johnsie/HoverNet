// RaceManager.h : Manages multiple concurrent race sessions

#pragma once

#include "RaceSession.h"

// Lightweight snapshot of a race for lobby listings (see MR_RaceManager::ListRaces).
struct RaceSummary
{
    int mRaceId;
    std::string mName;
    std::string mTrack;
    int mNumLaps;
    int mNumPlayers;
    BOOL mStarted;
};

class MR_RaceManager
{
public:
    MR_RaceManager();
    ~MR_RaceManager();

    // Initialize with max concurrent races
    BOOL Initialize(int maxConcurrentRaces = 50);

    // Create new race
    int CreateRace(
        const char* raceName,
        const char* trackName,
        int numLaps,
        BOOL allowWeapons,
        int creatorClientId);

    // Finds a race by exact name (case-sensitive). Returns -1 if none is open.
    int FindRaceByName(const char* raceName) const;

    // Joins raceName if an open race by that name exists, otherwise creates one.
    // This is what backs the "lobby": clients name the race they want and either
    // land in it or become its host, with no separate create/list/join round trip.
    int FindOrCreateRace(
        const char* raceName,
        const char* trackName,
        int numLaps,
        BOOL allowWeapons,
        int creatorClientId);

    // Snapshot of every currently open race, for a lobby listing.
    void ListRaces(std::vector<RaceSummary>& outRaces) const;

    // Player joins existing race
    BOOL JoinRace(int raceId, int clientId, const char* playerName);

    // Player leaves race
    void LeaveRace(int raceId, int clientId);

    // Start a race
    BOOL StartRace(int raceId);

    // Update all races
    void UpdateAllRaces(float deltaTime);

    // Get race by ID
    RaceSession* GetRace(int raceId);

    // Get statistics
    int GetActiveRaceCount() const;
    int GetTotalPlayerCount() const;

    // Shutdown
    void Shutdown();

private:
    std::map<int, RaceSession*> mRaces;
    int mNextRaceId;
    int mMaxConcurrentRaces;

    // Helper
    void CleanupEmptyRaces();
};
