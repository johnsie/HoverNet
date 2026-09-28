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
    // XML parsing isn't implemented yet (see Phase 2 of docs/roadmap-2.0.md:
    // "Complete configuration loading rather than shipping placeholder XML
    // handling"). Returning TRUE unconditionally here used to make the caller
    // (RaceServer.cpp's --config handling) log a false "Loaded configuration
    // from: X" for a file whose contents were never actually read -- silently
    // misleading an operator into believing their settings took effect.
    // Returning FALSE instead makes that caller correctly log "Failed to load
    // config file: X (using defaults)", which is the truth: every setting
    // still comes from the hardcoded defaults in the constructor above.
    return FALSE;
}

BOOL MR_ServerConfig::SaveToFile(const char* filename)
{
    // See LoadFromFile: not implemented yet, so don't claim success.
    return FALSE;
}
