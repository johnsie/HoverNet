// Integration smoke test: launches the real RaceServer binary and drives it the
// way a Linux "lobby -> join a game" flow would:
//   1. List races before anything exists (must be empty).
//   2. Two clients name the same race ("smoke-test-race"): the first call creates
//      it, the second joins it. They must discover each other (eRSMsgConnNameSet)
//      and be able to chat (eRSMsgChatMessage), including under a message burst
//      that exercises TCP coalescing/fragmentation handling.
//   3. A third client lists races and must see "smoke-test-race" with 2 players.
//   4. A fourth client names a *different* race ("smoke-test-race-2") and must be
//      isolated from the first race's chat traffic (proves races aren't all just
//      dumped into a single shared room, which used to be the case).
#include "RaceServerClient.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
    bool WaitForServer(const std::string& pHost, unsigned pPort)
    {
        for (int lAttempt = 0; lAttempt < 50; ++lAttempt)
        {
            RaceServerClient lProbe;
            if (lProbe.Connect(pHost, pPort))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    bool CheckPeerDiscoveryAndChat(RaceServerClient& pClientA, RaceServerClient& pClientB)
    {
        // B joined after A, so B should be told about A via eRSMsgConnNameSet.
        RaceServerMessage lMessage;
        RaceServerPeer lPeer;
        bool lSawPeer = false;
        for (int lTries = 0; lTries < 20 && !lSawPeer; ++lTries)
        {
            if (pClientB.PollMessage(lMessage, 200) && RaceServerClient::ParsePeer(lMessage, lPeer))
            {
                lSawPeer = true;
            }
        }
        if (!lSawPeer)
        {
            std::fprintf(stderr, "Client B never discovered client A via eRSMsgConnNameSet\n");
            return false;
        }
        std::printf("Client B discovered peer '%s' on UDP port %u\n", lPeer.mName.c_str(), lPeer.mUdpPort);

        const std::string lChatText = "hello from A";
        if (!pClientA.SendMessage(eRSMsgChatMessage, lChatText.data(), lChatText.size()))
        {
            std::fprintf(stderr, "Failed to send chat message\n");
            return false;
        }

        bool lSawChat = false;
        for (int lTries = 0; lTries < 20 && !lSawChat; ++lTries)
        {
            if (pClientB.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                const std::string lReceived(lMessage.mData.begin(), lMessage.mData.end());
                lSawChat = (lReceived == lChatText);
            }
        }
        if (!lSawChat)
        {
            std::fprintf(stderr, "Client B never received the relayed chat message\n");
            return false;
        }

        // Regression check for message coalescing/fragmentation: fire a burst of small
        // messages back-to-back with no waiting in between, so the kernel (and the
        // server's TCP framing) is likely to deliver several in a single recv(). Every
        // one must still arrive intact and in order.
        const int lBurstCount = 25;
        for (int lIndex = 0; lIndex < lBurstCount; ++lIndex)
        {
            const std::string lText = "burst-" + std::to_string(lIndex);
            if (!pClientA.SendMessage(eRSMsgChatMessage, lText.data(), lText.size()))
            {
                std::fprintf(stderr, "Failed to send burst message %d\n", lIndex);
                return false;
            }
        }
        for (int lIndex = 0; lIndex < lBurstCount; ++lIndex)
        {
            const std::string lExpected = "burst-" + std::to_string(lIndex);
            if (!pClientB.PollMessage(lMessage, 1000) || lMessage.mType != eRSMsgChatMessage)
            {
                std::fprintf(stderr, "Burst message %d missing or wrong type\n", lIndex);
                return false;
            }
            const std::string lReceived(lMessage.mData.begin(), lMessage.mData.end());
            if (lReceived != lExpected)
            {
                std::fprintf(stderr, "Burst message %d corrupted: expected '%s', got '%s'\n", lIndex,
                             lExpected.c_str(), lReceived.c_str());
                return false;
            }
        }
        return true;
    }

    bool RunChecks(unsigned pPort)
    {
        RaceServerClient lLister;
        if (!lLister.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect lobby lister\n");
            return false;
        }

        std::vector<RaceServerGameInfo> lGames;
        if (!lLister.ListGames(lGames) || !lGames.empty())
        {
            std::fprintf(stderr, "Expected an empty lobby before any race is created (got %zu)\n",
                         lGames.size());
            return false;
        }
        std::printf("Lobby listing is empty before any race exists, as expected\n");

        RaceServerClient lClientA;
        RaceServerClient lClientB;
        if (!lClientA.Connect("127.0.0.1", pPort) || !lClientB.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect both clients to RaceServer\n");
            return false;
        }
        if (!lClientA.JoinGame("smoke-test-race") || !lClientB.JoinGame("smoke-test-race"))
        {
            std::fprintf(stderr, "JoinGame failed\n");
            return false;
        }

        if (!CheckPeerDiscoveryAndChat(lClientA, lClientB))
        {
            return false;
        }

        // A third client should now see the race we just created, with 2 players.
        if (!lLister.ListGames(lGames))
        {
            std::fprintf(stderr, "ListGames failed after joining a race\n");
            return false;
        }
        const RaceServerGameInfo* lFound = nullptr;
        for (const RaceServerGameInfo& lGame : lGames)
        {
            if (lGame.mName == "smoke-test-race")
            {
                lFound = &lGame;
            }
        }
        if (lFound == nullptr || lFound->mNumPlayers != 2 || lFound->mTrack != "ClassicH")
        {
            std::fprintf(stderr, "Lobby listing didn't reflect the joined race correctly\n");
            return false;
        }
        std::printf("Lobby listing shows '%s' on track '%s' with %d players\n", lFound->mName.c_str(),
                    lFound->mTrack.c_str(), lFound->mNumPlayers);

        // A fourth client naming a *different* race must be isolated from the first
        // race's traffic -- distinct game names must mean genuinely separate races.
        RaceServerClient lClientD;
        if (!lClientD.Connect("127.0.0.1", pPort) || !lClientD.JoinGame("smoke-test-race-2"))
        {
            std::fprintf(stderr, "Could not join the second, independent race\n");
            return false;
        }

        const std::string lLeakText = "should not leak";
        if (!lClientA.SendMessage(eRSMsgChatMessage, lLeakText.data(), lLeakText.size()))
        {
            std::fprintf(stderr, "Failed to send isolation-check chat message\n");
            return false;
        }
        RaceServerMessage lMessage;
        if (lClientD.PollMessage(lMessage, 500))
        {
            std::fprintf(stderr, "Client D received traffic from an unrelated race (no isolation)\n");
            return false;
        }
        std::printf("Second race is isolated from the first, as expected\n");

        return true;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: %s <path-to-RaceServer>\n", argv[0]);
        return 1;
    }

    const std::string lServerPath = argv[1];
    const unsigned lPort = 19870;
    const std::string lPortStr = std::to_string(lPort);
    const std::string lLogPath = "RaceServerClientSmoke.raceserver.log";

    const pid_t lServerPid = fork();
    if (lServerPid < 0)
    {
        std::fprintf(stderr, "fork() failed\n");
        return 1;
    }

    if (lServerPid == 0)
    {
        execl(lServerPath.c_str(), lServerPath.c_str(), lPortStr.c_str(), lLogPath.c_str(),
              static_cast<char*>(nullptr));
        std::fprintf(stderr, "execl(%s) failed: %s\n", lServerPath.c_str(), std::strerror(errno));
        _exit(127);
    }

    int lResult = 1;
    if (WaitForServer("127.0.0.1", lPort))
    {
        if (RunChecks(lPort))
        {
            std::printf("RaceServerClient smoke test passed\n");
            lResult = 0;
        }
    }
    else
    {
        std::fprintf(stderr, "RaceServer never started listening on port %u\n", lPort);
    }

    kill(lServerPid, SIGTERM);
    int lStatus = 0;
    waitpid(lServerPid, &lStatus, 0);

    return lResult;
}
