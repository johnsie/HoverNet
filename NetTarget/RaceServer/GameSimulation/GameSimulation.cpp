// GameSimulation.cpp : Game simulation implementation

#include "stdafx.h"
#include "GameSimulation.h"
#include "ServerLogger.h"

extern MR_ServerLogger g_Logger;

GameSimulation::GameSimulation()
    : mMaxPlayers(8),
      mElapsedTime(0.0f),
      mRaceStarted(FALSE),
      mRaceComplete(FALSE)
{
    memset(&mState, 0, sizeof(mState));
    mState.mNumPlayers = 0;
    mState.mNumElements = 0;
}

GameSimulation::~GameSimulation()
{
}

BOOL GameSimulation::Initialize(
    const char* trackName,
    int numLaps,
    BOOL allowWeapons,
    int maxPlayers)
{
    snprintf(mState.mTrackName, sizeof(mState.mTrackName), "%s", trackName);
    mState.mNumLaps = numLaps;
    mState.mWeaponsAllowed = allowWeapons;
    mMaxPlayers = maxPlayers;
    mState.mStartTime = time(NULL);
    mState.mRaceStarted = FALSE;
    mState.mRaceComplete = FALSE;

    g_Logger.Log(MR_LOG_INFO, "Game simulation initialized: track=%s, laps=%d, weapons=%s",
                trackName, numLaps, allowWeapons ? "yes" : "no");
    return TRUE;
}

int GameSimulation::AddPlayer(int clientId, const char* playerName)
{
    if (mState.mNumPlayers >= mMaxPlayers) {
        g_Logger.Log(MR_LOG_WARN, "Cannot add player %d (race full)", clientId);
        return -1;
    }

    int playerIndex = mState.mNumPlayers;
    GameState::PlayerState& player = mState.mPlayers[playerIndex];

    player.mClientId = clientId;
    snprintf(player.mName, sizeof(player.mName), "%s", playerName);
    player.mPosition.x = 0;
    player.mPosition.y = 0;
    player.mPosition.z = 0;
    player.mVelocity.x = 0;
    player.mVelocity.y = 0;
    player.mVelocity.z = 0;
    player.mLapNumber = 1;
    player.mCheckpointIndex = 0;
    player.mBestLapTimeSec = 0;
    player.mLastLapTimeSec = 0;
    player.mFinished = FALSE;
    player.mHealthPoints = 100;

    mState.mNumPlayers++;

    g_Logger.Log(MR_LOG_INFO, "Player added: clientId=%d, name=%s (total=%d)",
                clientId, playerName, mState.mNumPlayers);
    return playerIndex;
}

void GameSimulation::RemovePlayer(int clientId)
{
    int playerIndex = GetPlayerIndex(clientId);
    if (playerIndex >= 0) {
        // Remove player by swapping with last player
        if (playerIndex < mState.mNumPlayers - 1) {
            mState.mPlayers[playerIndex] = mState.mPlayers[mState.mNumPlayers - 1];
        }
        mState.mNumPlayers--;
        g_Logger.Log(MR_LOG_INFO, "Player removed: clientId=%d (remaining=%d)",
                    clientId, mState.mNumPlayers);
    }
}

void GameSimulation::Update(float deltaTime)
{
    if (!mRaceStarted) {
        return;
    }

    mElapsedTime += deltaTime;
    mState.mCurrentTickMs = (unsigned int)(mElapsedTime * 1000);

    // Update physics
    UpdatePlayerPositions(deltaTime);

    // Check for lap completions
    CheckLapCompletion();

    // Check if race is complete
    CheckRaceCompletion();
}

void GameSimulation::UpdatePlayerPositions(float deltaTime)
{
    // TODO: Integrate with existing physics simulation
    // For now, just apply velocity to position
    for (int i = 0; i < mState.mNumPlayers; i++) {
        GameState::PlayerState& player = mState.mPlayers[i];
        if (!player.mFinished) {
            player.mPosition.x += player.mVelocity.x * deltaTime;
            player.mPosition.y += player.mVelocity.y * deltaTime;
            player.mPosition.z += player.mVelocity.z * deltaTime;
        }
    }
}

void GameSimulation::CheckLapCompletion()
{
    // TODO: Implement lap detection based on checkpoint crossings
    // This requires integration with track geometry
}

void GameSimulation::CheckRaceCompletion()
{
    // Check if all players finished
    int finishedCount = 0;
    for (int i = 0; i < mState.mNumPlayers; i++) {
        if (mState.mPlayers[i].mLapNumber > mState.mNumLaps) {
            mState.mPlayers[i].mFinished = TRUE;
            finishedCount++;
        }
    }

    // Race complete when all players finished
    if (finishedCount == mState.mNumPlayers && mState.mNumPlayers > 0) {
        mRaceComplete = TRUE;
        mState.mRaceComplete = TRUE;
        g_Logger.Log(MR_LOG_INFO, "Race completed!");
    }
}

BOOL GameSimulation::IsRaceComplete() const
{
    return mRaceComplete;
}

void GameSimulation::StartRace()
{
    if (!mRaceStarted) {
        mRaceStarted = TRUE;
        mState.mRaceStarted = TRUE;
        mElapsedTime = 0.0f;
        mState.mCurrentTickMs = 0;
        g_Logger.Log(MR_LOG_INFO, "Race started with %d players", mState.mNumPlayers);
    }
}

void GameSimulation::StopRace()
{
    if (mRaceStarted) {
        mRaceStarted = FALSE;
        mState.mRaceStarted = FALSE;
        g_Logger.Log(MR_LOG_INFO, "Race stopped");
    }
}

void GameSimulation::ProcessPlayerUpdate(
    int clientId,
    const MR_Vector3& position,
    const MR_Vector3& velocity,
    const MR_Quaternion& rotation)
{
    int playerIndex = GetPlayerIndex(clientId);
    if (playerIndex >= 0) {
        GameState::PlayerState& player = mState.mPlayers[playerIndex];
        player.mPosition = position;
        player.mVelocity = velocity;
        player.mRotation = rotation;
    }
}

int GameSimulation::GetPlayerIndex(int clientId) const
{
    for (int i = 0; i < mState.mNumPlayers; i++) {
        if (mState.mPlayers[i].mClientId == clientId) {
            return i;
        }
    }
    return -1;
}
