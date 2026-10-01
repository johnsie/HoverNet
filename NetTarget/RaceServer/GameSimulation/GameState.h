// GameState.h : Snapshot of complete game state at a moment in time

#pragma once

#define MR_MAX_PLAYERS_PER_RACE 8
#define MR_MAX_ELEMENTS_PER_RACE 64

struct GameState
{
    // Race metadata
    char mTrackName[64]; // protocol maximum is 63 bytes (kMaxTrackNameBytes)
    int mNumLaps;
    BOOL mWeaponsAllowed;
    time_t mStartTime;
    BOOL mRaceStarted;
    BOOL mRaceComplete;

    // Player states
    struct PlayerState
    {
        int mClientId;
        char mName[21];
        MR_Vector3 mPosition;
        MR_Vector3 mVelocity;
        MR_Quaternion mRotation;
        int mLapNumber;
        int mCheckpointIndex;
        int mBestLapTimeSec;
        int mLastLapTimeSec;
        BOOL mFinished;
        int mHealthPoints;
    } mPlayers[MR_MAX_PLAYERS_PER_RACE];
    int mNumPlayers;

    // Element states (mines, power-ups, missiles, etc.)
    struct ElementState
    {
        int mElementId;
        int mElementType;           // MINE, ROCKET, PICKUP, etc.
        MR_Vector3 mPosition;
        BOOL mActive;
        int mOwnerId;
    } mElements[MR_MAX_ELEMENTS_PER_RACE];
    int mNumElements;

    // Game clock
    unsigned int mCurrentTickMs;     // Server time in milliseconds since race start
};
