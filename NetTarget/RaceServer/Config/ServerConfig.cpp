// ServerConfig.cpp : Configuration implementation

#include "stdafx.h"
#include "ServerConfig.h"

MR_ServerConfig::MR_ServerConfig()
    : mPort(9600),
      mMaxConnections(100),
      mTcpNoDelay(TRUE),
      mSendBufferSize(8192),
      mRecvBufferSize(8192),
      mMaxConcurrentRaces(50),
      mMaxPlayersPerRace(8),
      mIdleRaceTimeoutSec(300),
      mPlayerDisconnectTimeoutSec(30),
      mLogLevel(1),  // MR_LOG_INFO
      mInternetRoomPort(80)
{
    snprintf(mLogFile, sizeof(mLogFile), "%s", "raceserver.log");
    snprintf(mInternetRoomHost, sizeof(mInternetRoomHost), "%s", "localhost");
}

BOOL MR_ServerConfig::LoadFromFile(const char* filename)
{
    // Placeholder for XML parsing
    // TODO: Implement XML config file loading (use existing HoverRace XML libraries)
    // For now, return success (use hardcoded defaults)
    return TRUE;
}

BOOL MR_ServerConfig::SaveToFile(const char* filename)
{
    // Placeholder for XML generation
    // TODO: Implement XML config file saving
    return TRUE;
}
