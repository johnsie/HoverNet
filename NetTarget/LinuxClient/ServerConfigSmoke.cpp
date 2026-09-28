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

    // <internetroom> is documented as "for future integration" and nothing reads
    // it yet -- a config missing that section entirely (an operator trimmed it,
    // or upgraded from a template that never had it) must still load every other
    // setting instead of falling back to 100% hardcoded defaults.
    {
        const std::string lNoInternetRoomPath = "no-internetroom-config.xml";
        FILE* lFile = std::fopen(lNoInternetRoomPath.c_str(), "w");
        if (lFile == nullptr) {
            std::fprintf(stderr, "Could not write the no-<internetroom> test config\n");
            return 1;
        }
        std::fputs(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<raceserver>\n"
            "  <network><port>9601</port><max_connections>42</max_connections>"
            "<tcp_nodelay>true</tcp_nodelay><send_buffer_size>8192</send_buffer_size>"
            "<receive_buffer_size>8192</receive_buffer_size></network>\n"
            "  <races><max_concurrent>7</max_concurrent><max_players_per_race>4</max_players_per_race>"
            "<idle_race_timeout_sec>60</idle_race_timeout_sec>"
            "<player_disconnect_timeout_sec>15</player_disconnect_timeout_sec></races>\n"
            "  <logging><level>WARN</level><file>x.log</file></logging>\n"
            "</raceserver>\n",
            lFile);
        std::fclose(lFile);

        MR_ServerConfig lNoInternetRoom;
        if (!lNoInternetRoom.LoadFromFile(lNoInternetRoomPath.c_str()) ||
            lNoInternetRoom.GetPort() != 9601 || lNoInternetRoom.GetMaxConnections() != 42 ||
            lNoInternetRoom.GetMaxConcurrentRaces() != 7 || lNoInternetRoom.GetMaxPlayersPerRace() != 4)
        {
            std::fprintf(stderr, "A config missing <internetroom> entirely was not parsed correctly\n");
            return 1;
        }
    }

    std::puts("RaceServer XML configuration load/save validation passed");
    return 0;
}
