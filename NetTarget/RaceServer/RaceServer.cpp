// RaceServer.cpp : Centralized Race Server Application
// Hosts multiple concurrent race sessions for HoverRace players

#include "stdafx.h"
#include "ServerLogger.h"
#include "ServerConfig.h"
#include "ServerSocket.h"
#include "RaceManager.h"

// Logging instance
MR_ServerLogger g_Logger;

// Global server socket
static MR_ServerSocket g_ServerSocket;

// Global race manager
static MR_RaceManager g_RaceManager;

// Global configuration
static MR_ServerConfig g_Config;

// Signal handler for graceful shutdown
static volatile bool g_bShutdownRequested = false;

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        g_Logger.Log(MR_LOG_INFO, "Shutdown requested via console signal");
        g_bShutdownRequested = true;
        return TRUE;
    }
    return FALSE;
}
#else
void ConsoleCtrlHandler(int)
{
    g_Logger.Log(MR_LOG_INFO, "Shutdown requested via signal");
    g_bShutdownRequested = true;
}
#endif

void PrintUsage(const char* programName)
{
    printf("HoverRace Centralized Race Server\n");
    printf("Usage: %s <port> <logfile> [--config <config.xml>] [--max-races <n>]\n\n", programName);
    printf("  port          TCP/UDP listening port (e.g., 9600)\n");
    printf("  logfile       Output log file (e.g., raceserver.log)\n");
    printf("  --config      Path to XML configuration file (optional)\n");
    printf("  --max-races   Maximum concurrent races to support (optional)\n\n");
    printf("Example:\n");
    printf("  %s 9600 raceserver.log --config config.xml --max-races 50\n", programName);
}

int main(int argc, char* argv[])
{
    printf("HoverRace Centralized Race Server v1.0\n");
    printf("=======================================\n\n");

    // Validate command line
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }

    unsigned port = atoi(argv[1]);
    const char* logfile = argv[2];

    if (port <= 1024 || port > 65535) {
        fprintf(stderr, "ERROR: Port must be between 1025 and 65535\n");
        return 1;
    }

    // Initialize logger
    if (!g_Logger.Initialize(logfile)) {
        fprintf(stderr, "ERROR: Failed to initialize logging\n");
        return 1;
    }

    g_Logger.Log(MR_LOG_INFO, "=== RaceServer Starting ===");
    g_Logger.Log(MR_LOG_INFO, "Port: %u, Logfile: %s", port, logfile);

    // Parse optional arguments
    int maxRaces = 50;
    const char* configFile = NULL;

    for (int i = 3; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            configFile = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--max-races") == 0) {
            maxRaces = atoi(argv[i + 1]);
            i++;
        }
    }

    // Load configuration
    if (configFile) {
        if (!g_Config.LoadFromFile(configFile)) {
            g_Logger.Log(MR_LOG_WARN, "Failed to load config file: %s (using defaults)", configFile);
        } else {
            g_Logger.Log(MR_LOG_INFO, "Loaded configuration from: %s", configFile);
        }
    }

    // Apply command-line overrides
    g_Config.SetPort(port);
    if (maxRaces > 0) {
        g_Config.SetMaxConcurrentRaces(maxRaces);
    }

    // Initialize race manager
    if (!g_RaceManager.Initialize(g_Config.GetMaxConcurrentRaces())) {
        g_Logger.Log(MR_LOG_ERROR, "Failed to initialize race manager");
        return 1;
    }
    g_Logger.Log(MR_LOG_INFO, "Race manager initialized for %d max concurrent races", 
                 g_Config.GetMaxConcurrentRaces());

    // Initialize server socket
    if (!g_ServerSocket.Initialize(port, g_Config.GetMaxConnections())) {
        g_Logger.Log(MR_LOG_ERROR, "Failed to initialize server socket on port %u", port);
        return 1;
    }
    g_Logger.Log(MR_LOG_INFO, "Server socket listening on port %u", port);

    // Set console control handler for graceful shutdown
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    signal(SIGINT, ConsoleCtrlHandler);
    signal(SIGTERM, ConsoleCtrlHandler);
#endif

    // Main event loop
    g_Logger.Log(MR_LOG_INFO, "=== Server Ready - Accepting Connections ===");
    printf("Server listening on port %u\n", port);
    printf("Press Ctrl+C to shutdown gracefully\n\n");

    const float FRAME_TIME = 0.016f;  // ~60 Hz
    std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastStatsTime = lastUpdateTime;

    while (!g_bShutdownRequested) {
        std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
        lastUpdateTime = currentTime;

        // Update race manager (process all race simulations)
        g_RaceManager.UpdateAllRaces(FRAME_TIME);

        // Process incoming connections and messages
        // (This is handled inside ServerSocket event loop)
        g_ServerSocket.ProcessEvents(&g_RaceManager);

        // Log statistics periodically (every 10 seconds)
        if (std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastStatsTime).count() >= 10) {
            lastStatsTime = currentTime;
            int activeRaces = g_RaceManager.GetActiveRaceCount();
            int totalPlayers = g_RaceManager.GetTotalPlayerCount();
            g_Logger.Log(MR_LOG_DEBUG, "Server stats - Active races: %d, Total players: %d",
                        activeRaces, totalPlayers);
        }

        // Limit frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Graceful shutdown
    g_Logger.Log(MR_LOG_INFO, "=== Server Shutting Down ===");
    g_ServerSocket.Shutdown();
    g_RaceManager.Shutdown();
    g_Logger.Log(MR_LOG_INFO, "Server shutdown complete");

    return 0;
}
