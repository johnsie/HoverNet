// ServerLogger.h : Logging utilities for RaceServer

#pragma once

#include "../Config/ServerConfig.h"

// Log levels defined in ServerConfig.h
// enum MR_LogLevel {
//     MR_LOG_DEBUG = 0,
//     MR_LOG_INFO = 1,
//     MR_LOG_WARN = 2,
//     MR_LOG_ERROR = 3
// };

class MR_ServerLogger
{
public:
    MR_ServerLogger();
    ~MR_ServerLogger();

    // Initialize logger with output file
    BOOL Initialize(const char* logfilePath, int minLevel = MR_LOG_DEBUG);

    // Log a message with printf-style formatting
    void Log(int level, const char* format, ...);

    // Flush log to disk
    void Flush();

    // Close log file
    void Close();

private:
    FILE* mpLogFile;
    int mMinLevel;
    std::mutex mLock;

    const char* LevelToString(int level);
};
