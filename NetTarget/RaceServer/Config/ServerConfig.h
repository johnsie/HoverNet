// ServerConfig.h : Server configuration management

#pragma once

enum MR_LogLevel {
    MR_LOG_DEBUG = 0,
    MR_LOG_INFO = 1,
    MR_LOG_WARN = 2,
    MR_LOG_ERROR = 3
};

class MR_ServerConfig
{
public:
    MR_ServerConfig();

    // Load configuration from XML file
    BOOL LoadFromFile(const char* filename);

    // Save configuration to XML file
    BOOL SaveToFile(const char* filename);

    // Getters
    unsigned GetPort() const { return mPort; }
    int GetMaxConnections() const { return mMaxConnections; }
    int GetMaxConcurrentRaces() const { return mMaxConcurrentRaces; }
    int GetMaxPlayersPerRace() const { return mMaxPlayersPerRace; }
    int GetIdleRaceTimeoutSec() const { return mIdleRaceTimeoutSec; }
    int GetPlayerDisconnectTimeoutSec() const { return mPlayerDisconnectTimeoutSec; }
    BOOL GetTcpNoDelay() const { return mTcpNoDelay; }
    int GetSendBufferSize() const { return mSendBufferSize; }
    int GetRecvBufferSize() const { return mRecvBufferSize; }
    const char* GetLogFile() const { return mLogFile; }
    int GetLogLevel() const { return mLogLevel; }

    // Setters (for command-line overrides)
    void SetPort(unsigned port) { mPort = port; }
    void SetMaxConcurrentRaces(int max) { mMaxConcurrentRaces = max; }
    void SetLogLevel(int level) { mLogLevel = level; }

private:
    // Network settings
    unsigned mPort;
    int mMaxConnections;
    BOOL mTcpNoDelay;
    int mSendBufferSize;
    int mRecvBufferSize;

    // Race limits
    int mMaxConcurrentRaces;
    int mMaxPlayersPerRace;
    int mIdleRaceTimeoutSec;
    int mPlayerDisconnectTimeoutSec;

    // Logging
    char mLogFile[256];
    int mLogLevel;

    // InternetRoom server connection
    char mInternetRoomHost[64];
    unsigned mInternetRoomPort;
};
