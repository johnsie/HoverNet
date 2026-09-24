// RaceSession.cpp : Race session implementation

#include "stdafx.h"
#include "RaceSession.h"
#include "ServerLogger.h"

extern MR_ServerLogger g_Logger;

RaceSession::RaceSession()
    : mRaceId(-1),
      mCreatedTime(0),
      mRaceStarted(FALSE),
      mCreatorClientId(-1)
{
    mRaceName[0] = '\0';
}

RaceSession::~RaceSession()
{
}

BOOL RaceSession::Initialize(
    int raceId,
    const char* raceName,
    const char* trackName,
    int numLaps,
    BOOL allowWeapons,
    int creatorClientId)
{
    mRaceId = raceId;
    snprintf(mRaceName, sizeof(mRaceName), "%s", raceName);
    mCreatedTime = time(NULL);
    mRaceStarted = FALSE;
    mCreatorClientId = creatorClientId;

    if (!mSimulation.Initialize(trackName, numLaps, allowWeapons, 8)) {
        g_Logger.Log(MR_LOG_ERROR, "Failed to initialize game simulation for race %d", raceId);
        return FALSE;
    }

    g_Logger.Log(MR_LOG_INFO, "Race session initialized: id=%d, name=%s, track=%s, laps=%d",
                raceId, raceName, trackName, numLaps);
    return TRUE;
}

int RaceSession::AddPlayer(int clientId, const char* playerName)
{
    int playerIndex = mSimulation.AddPlayer(clientId, playerName);
    if (playerIndex >= 0) {
        mPlayerClientIds.push_back(clientId);
        g_Logger.Log(MR_LOG_INFO, "Player joined race %d: clientId=%d, name=%s",
                    mRaceId, clientId, playerName);
    }
    return playerIndex;
}

void RaceSession::RemovePlayer(int clientId)
{
    mSimulation.RemovePlayer(clientId);
    auto it = std::find(mPlayerClientIds.begin(), mPlayerClientIds.end(), clientId);
    if (it != mPlayerClientIds.end()) {
        mPlayerClientIds.erase(it);
        g_Logger.Log(MR_LOG_INFO, "Player left race %d: clientId=%d",
                    mRaceId, clientId);
    }
}

void RaceSession::Update(float deltaTime)
{
    mSimulation.Update(deltaTime);
}

void RaceSession::StartRace()
{
    if (!mRaceStarted) {
        mRaceStarted = TRUE;
        mSimulation.StartRace();
        g_Logger.Log(MR_LOG_INFO, "Race %d started with %d players",
                    mRaceId, (int)mPlayerClientIds.size());
    }
}

BOOL RaceSession::ShouldCleanup(time_t currentTime, int idleTimeoutSec) const
{
    // Clean up if race is empty
    if (mPlayerClientIds.empty()) {
        return TRUE;
    }

    // Clean up if race is complete
    if (mSimulation.IsRaceComplete()) {
        // Wait 5 minutes before cleaning up completed race
        return (currentTime - mCreatedTime) > 300;
    }

    return FALSE;
}
