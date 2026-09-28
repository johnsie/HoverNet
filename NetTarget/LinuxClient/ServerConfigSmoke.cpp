#include "../RaceServer/stdafx.h"
#include "../RaceServer/Config/ServerConfig.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <config.xml> <round-trip-output.xml>\n", argv[0]);
        return 2;
    }

    MR_ServerConfig lConfig;
    if (!lConfig.LoadFromFile(argv[1]) || lConfig.GetPort() != 9600 ||
        lConfig.GetMaxConnections() != 100 || lConfig.GetMaxConcurrentRaces() != 50 ||
        lConfig.GetMaxPlayersPerRace() != 8 || lConfig.GetIdleRaceTimeoutSec() != 300 ||
        lConfig.GetPlayerDisconnectTimeoutSec() != 30 || !lConfig.GetTcpNoDelay() ||
        lConfig.GetSendBufferSize() != 8192 || lConfig.GetRecvBufferSize() != 8192 ||
        lConfig.GetLogLevel() != MR_LOG_INFO)
    {
        std::fprintf(stderr, "Bundled RaceServer configuration was not parsed correctly\n");
        return 1;
    }

    if (!lConfig.SaveToFile(argv[2])) {
        std::fprintf(stderr, "Could not save RaceServer configuration\n");
        return 1;
    }
    MR_ServerConfig lRoundTrip;
    if (!lRoundTrip.LoadFromFile(argv[2]) ||
        lRoundTrip.GetMaxConnections() != lConfig.GetMaxConnections() ||
        lRoundTrip.GetMaxPlayersPerRace() != lConfig.GetMaxPlayersPerRace() ||
        lRoundTrip.GetLogLevel() != lConfig.GetLogLevel())
    {
        std::fprintf(stderr, "RaceServer configuration did not survive a save/load round trip\n");
        return 1;
    }

    MR_ServerConfig lMissing;
    if (lMissing.LoadFromFile("this-config-does-not-exist.xml")) {
        std::fprintf(stderr, "Missing RaceServer configuration was accepted\n");
        return 1;
    }

    std::puts("RaceServer XML configuration load/save validation passed");
    return 0;
}
