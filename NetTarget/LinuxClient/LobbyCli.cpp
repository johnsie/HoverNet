// HoverNetLobby: interactive command-line lobby for the Linux RaceServer.
// Connects to a RaceServer, lists open races, lets the user join one (or name a
// new one to host it), and then shows the players and chat in that race live.
// This proves out the network side of "list the lobby, join a game" on Linux; it
// does not yet drop the joined race into the 3D game view (HoverNetGame2Player).
#include "RaceServerClient.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    void PrintGames(const std::vector<RaceServerGameInfo>& pGames)
    {
        if (pGames.empty())
        {
            std::printf("  (no open races -- type a name to host one)\n");
            return;
        }
        for (const RaceServerGameInfo& lGame : pGames)
        {
            std::printf("  [%d] %-24s track=%-16s laps=%d players=%d %s\n", lGame.mRaceId,
                        lGame.mName.c_str(), lGame.mTrack.c_str(), lGame.mNumLaps, lGame.mNumPlayers,
                        lGame.mStarted ? "(in progress)" : "");
        }
    }

    void RunRaceLoop(RaceServerClient& pClient, const std::string& pGameName)
    {
        std::printf("Joined '%s'. Watching for players and chat (Ctrl+C to quit)...\n", pGameName.c_str());
        RaceServerMessage lMessage;
        while (true)
        {
            if (!pClient.PollMessage(lMessage, 5000))
            {
                continue;  // Just a quiet moment, or a transient timeout -- keep watching
            }
            if (!pClient.IsConnected())
            {
                std::printf("Disconnected from RaceServer.\n");
                return;
            }

            RaceServerPeer lPeer;
            if (RaceServerClient::ParsePeer(lMessage, lPeer))
            {
                std::printf("  * %s joined (UDP port %u)\n", lPeer.mName.c_str(), lPeer.mUdpPort);
            }
            else if (lMessage.mType == eRSMsgChatMessage)
            {
                const std::string lText(lMessage.mData.begin(), lMessage.mData.end());
                std::printf("  chat: %s\n", lText.c_str());
            }
        }
    }
}

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // Line-buffer so output isn't lost/delayed when piped

    if (argc < 3)
    {
        std::fprintf(stderr, "Usage: %s <raceserver-host> <raceserver-port>\n", argv[0]);
        return 1;
    }

    const std::string lHost = argv[1];
    const unsigned lPort = static_cast<unsigned>(std::atoi(argv[2]));

    RaceServerClient lClient;
    if (!lClient.Connect(lHost, lPort))
    {
        std::fprintf(stderr, "Could not connect to RaceServer at %s:%u\n", lHost.c_str(), lPort);
        return 1;
    }
    std::printf("Connected to RaceServer at %s:%u\n", lHost.c_str(), lPort);

    while (true)
    {
        std::vector<RaceServerGameInfo> lGames;
        if (!lClient.ListGames(lGames))
        {
            std::fprintf(stderr, "Lost connection to RaceServer while listing races\n");
            return 1;
        }

        std::printf("\nLobby:\n");
        PrintGames(lGames);
        std::printf("\nEnter a race name to join or host (or 'r' to refresh, 'q' to quit): ");
        std::fflush(stdout);

        std::string lLine;
        if (!std::getline(std::cin, lLine) || lLine.empty())
        {
            continue;
        }
        if (lLine == "q")
        {
            break;
        }
        if (lLine == "r")
        {
            continue;
        }

        if (!lClient.JoinGame(lLine))
        {
            std::fprintf(stderr, "Could not join or host '%s'\n", lLine.c_str());
            return 1;
        }
        RunRaceLoop(lClient, lLine);
        break;
    }

    return 0;
}
