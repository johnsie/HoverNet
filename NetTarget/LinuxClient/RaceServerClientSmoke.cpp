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
        std::printf("Client B discovered peer '%s' (client id %d)\n", lPeer.mName.c_str(), lPeer.mClientId);

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
                int lSenderId = -1;
                std::string lReceived;
                lSawChat = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                           lReceived == lChatText;
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
            int lSenderId = -1;
            std::string lReceived;
            if (!RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived))
            {
                std::fprintf(stderr, "Burst message %d failed to parse\n", lIndex);
                return false;
            }
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

        // A fourth client naming a *different* race, still just a waiting room (not
        // started) -- chat is scoped by mRaceStarted, not mRaceId, so a waiting
        // room is still part of the wider lobby conversation. Distinct game names
        // mean genuinely separate races once they start, but not before.
        RaceServerClient lClientD;
        if (!lClientD.Connect("127.0.0.1", pPort) || !lClientD.JoinGame("smoke-test-race-2"))
        {
            std::fprintf(stderr, "Could not join the second, independent race\n");
            return false;
        }
        // Drain D's own eRSMsgJoinedRace ack before checking what it does/doesn't
        // see next -- that's expected traffic addressed to D, not lobby chat.
        RaceServerMessage lMessage;
        while (lClientD.PollMessage(lMessage, 200)) { }

        const std::string lWaitingRoomText = "still just a waiting room";
        if (!lClientA.SendMessage(eRSMsgChatMessage, lWaitingRoomText.data(), lWaitingRoomText.size()))
        {
            std::fprintf(stderr, "Failed to send waiting-room chat message\n");
            return false;
        }
        bool lSawWaitingRoomChat = false;
        for (int lTries = 0; lTries < 20 && !lSawWaitingRoomChat; ++lTries)
        {
            if (lClientD.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lReceived;
                lSawWaitingRoomChat = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                                      lReceived == lWaitingRoomText;
            }
        }
        if (!lSawWaitingRoomChat)
        {
            std::fprintf(stderr, "A different not-yet-started race should still share lobby chat\n");
            return false;
        }
        std::printf("Two separate waiting rooms still share one lobby-wide chat, as expected\n");

        // Lobby-wide chat: two clients that haven't joined any race yet (mRaceId
        // stays -1 on the server) should be able to chat with each other, and with
        // anyone else not yet racing (browsers and every waiting room alike).
        RaceServerClient lLobbyClientE;
        RaceServerClient lLobbyClientF;
        if (!lLobbyClientE.Connect("127.0.0.1", pPort) || !lLobbyClientF.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect two lobby-only (unjoined) clients\n");
            return false;
        }
        // A TCP connect() succeeding only means the OS accepted it into the listen
        // backlog; the server's own poll loop still needs a turn to accept() it at
        // the application level (add it to mConnections) before it'll relay anything
        // to/from it. Give that a moment rather than racing it.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const std::string lLobbyChatText = "hello from the lobby";
        if (!lLobbyClientE.SendMessage(eRSMsgChatMessage, lLobbyChatText.data(), lLobbyChatText.size()))
        {
            std::fprintf(stderr, "Failed to send lobby-wide chat message\n");
            return false;
        }

        bool lLobbyChatSeen = false;
        for (int lTries = 0; lTries < 20 && !lLobbyChatSeen; ++lTries)
        {
            if (lLobbyClientF.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lReceived;
                lLobbyChatSeen = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                                 lReceived == lLobbyChatText;
            }
        }
        if (!lLobbyChatSeen)
        {
            std::fprintf(stderr, "Lobby-only client never received lobby-wide chat\n");
            return false;
        }
        std::printf("Lobby-wide chat between unjoined clients works\n");

        // A client sitting in a waiting room (not yet started) must ALSO see that
        // same lobby-wide chat -- it hasn't narrowed to race-only yet.
        bool lWaitingRoomSawLobbyChat = false;
        for (int lTries = 0; lTries < 20 && !lWaitingRoomSawLobbyChat; ++lTries)
        {
            if (lClientA.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lReceived;
                lWaitingRoomSawLobbyChat = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                                          lReceived == lLobbyChatText;
            }
        }
        if (!lWaitingRoomSawLobbyChat)
        {
            std::fprintf(stderr, "A client in a not-yet-started waiting room should still see lobby-wide chat\n");
            return false;
        }
        // ...and a lobby-only browser must likewise see a waiting room's chat.
        const std::string lWaitingRoomText2 = "still in the waiting room";
        if (!lClientB.SendMessage(eRSMsgChatMessage, lWaitingRoomText2.data(), lWaitingRoomText2.size()))
        {
            std::fprintf(stderr, "Failed to send waiting-room chat for the reverse check\n");
            return false;
        }
        bool lLobbySawWaitingRoomChat = false;
        for (int lTries = 0; lTries < 20 && !lLobbySawWaitingRoomChat; ++lTries)
        {
            if (lLobbyClientE.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lReceived;
                lLobbySawWaitingRoomChat = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                                          lReceived == lWaitingRoomText2;
            }
        }
        if (!lLobbySawWaitingRoomChat)
        {
            std::fprintf(stderr, "A lobby-only browser should still see a waiting room's chat\n");
            return false;
        }
        std::printf("Lobby browsers and not-yet-started waiting rooms share one chat, as expected\n");

        // Duplicate display names: the second, third, ... client to request an
        // already-taken name must be told back (via eRSMsgPlayerNameAssigned) a
        // disambiguated one, numbered from 1, not silently collide with whoever
        // has it already.
        RaceServerClient lNameClientG;
        RaceServerClient lNameClientH;
        RaceServerClient lNameClientI;
        if (!lNameClientG.Connect("127.0.0.1", pPort) || !lNameClientH.Connect("127.0.0.1", pPort) ||
            !lNameClientI.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect three clients for the duplicate-name check\n");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // G, H and I are all still lobby-only (mRaceId == -1), same as E and F
        // above, so each one's SetPlayerName broadcasts eRSMsgLobbyUserPresent to
        // the *other* two -- those can arrive interleaved with the
        // eRSMsgPlayerNameAssigned reply a client is waiting for on its own
        // request, so this has to skip past anything of the wrong type rather
        // than assume the very next message is the one it wants.
        auto lWaitForNameAssigned = [](RaceServerClient& pClient, std::string& pOutName) -> bool {
            RaceServerMessage lMsg;
            for (int lTries = 0; lTries < 20; ++lTries)
            {
                if (pClient.PollMessage(lMsg, 200) && RaceServerClient::ParsePlayerNameAssigned(lMsg, pOutName))
                {
                    return true;
                }
            }
            return false;
        };

        if (!lNameClientG.SetPlayerName("Racer"))
        {
            std::fprintf(stderr, "Failed to set the first client's name to 'Racer'\n");
            return false;
        }
        std::string lAssignedG;
        if (!lWaitForNameAssigned(lNameClientG, lAssignedG) || lAssignedG != "Racer")
        {
            std::fprintf(stderr, "First client claiming 'Racer' should keep it unchanged, got '%s'\n",
                         lAssignedG.c_str());
            return false;
        }

        if (!lNameClientH.SetPlayerName("Racer"))
        {
            std::fprintf(stderr, "Failed to set the second client's name to 'Racer'\n");
            return false;
        }
        std::string lAssignedH;
        if (!lWaitForNameAssigned(lNameClientH, lAssignedH) || lAssignedH != "Racer1")
        {
            std::fprintf(stderr, "Second client claiming 'Racer' should become 'Racer1', got '%s'\n",
                         lAssignedH.c_str());
            return false;
        }

        if (!lNameClientI.SetPlayerName("Racer"))
        {
            std::fprintf(stderr, "Failed to set the third client's name to 'Racer'\n");
            return false;
        }
        std::string lAssignedI;
        if (!lWaitForNameAssigned(lNameClientI, lAssignedI) || lAssignedI != "Racer2")
        {
            std::fprintf(stderr, "Third client claiming 'Racer' should become 'Racer2', got '%s'\n",
                         lAssignedI.c_str());
            return false;
        }
        std::printf("Duplicate display names are disambiguated as 'Racer', 'Racer1', 'Racer2'\n");

        // Host-controlled start: the creator of a race gets told it's the host via
        // eRSMsgJoinedRace, a non-creator does not, a non-host's start request is
        // ignored, and the host's start request broadcasts eRSMsgRaceStarted to
        // every player in that race (including the host itself) at once.
        RaceServerClient lHost;
        RaceServerClient lJoiner;
        if (!lHost.Connect("127.0.0.1", pPort) || !lJoiner.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect host/joiner clients\n");
            return false;
        }

        if (!lHost.JoinGame("start-test-race"))
        {
            std::fprintf(stderr, "Host could not create start-test-race\n");
            return false;
        }
        RaceServerJoinAck lHostAck;
        bool lGotHostAck = false;
        for (int lTries = 0; lTries < 20 && !lGotHostAck; ++lTries)
        {
            if (lHost.PollMessage(lMessage, 200) && RaceServerClient::ParseJoinedRace(lMessage, lHostAck))
            {
                lGotHostAck = true;
            }
        }
        if (!lGotHostAck || !lHostAck.mIsHost)
        {
            std::fprintf(stderr, "Race creator was not acknowledged as host\n");
            return false;
        }

        if (!lJoiner.JoinGame("start-test-race"))
        {
            std::fprintf(stderr, "Joiner could not join start-test-race\n");
            return false;
        }
        RaceServerJoinAck lJoinerAck;
        bool lGotJoinerAck = false;
        for (int lTries = 0; lTries < 20 && !lGotJoinerAck; ++lTries)
        {
            if (lJoiner.PollMessage(lMessage, 200) && RaceServerClient::ParseJoinedRace(lMessage, lJoinerAck))
            {
                lGotJoinerAck = true;
            }
        }
        if (!lGotJoinerAck || lJoinerAck.mIsHost)
        {
            std::fprintf(stderr, "Non-creator was incorrectly acknowledged as host\n");
            return false;
        }
        std::printf("Host/non-host join acknowledgement is correct\n");

        // A non-host's start request must be ignored. Drain whatever arrives for a
        // beat (there may be an unrelated queued eRSMsgConnNameSet from the join
        // above) and confirm none of it is eRSMsgRaceStarted.
        if (!lJoiner.StartRace())
        {
            std::fprintf(stderr, "Failed to send non-host start request\n");
            return false;
        }
        while (lHost.PollMessage(lMessage, 500))
        {
            if (lMessage.mType == eRSMsgRaceStarted)
            {
                std::fprintf(stderr, "Race started from a non-host request (no host authority check)\n");
                return false;
            }
        }

        // The host's start request must reach both the host and the joiner.
        if (!lHost.StartRace())
        {
            std::fprintf(stderr, "Failed to send host start request\n");
            return false;
        }
        bool lHostSawStart = false;
        bool lJoinerSawStart = false;
        for (int lTries = 0; lTries < 20 && !(lHostSawStart && lJoinerSawStart); ++lTries)
        {
            if (!lHostSawStart && lHost.PollMessage(lMessage, 100) && lMessage.mType == eRSMsgRaceStarted)
            {
                lHostSawStart = true;
            }
            if (!lJoinerSawStart && lJoiner.PollMessage(lMessage, 100) && lMessage.mType == eRSMsgRaceStarted)
            {
                lJoinerSawStart = true;
            }
        }
        if (!lHostSawStart || !lJoinerSawStart)
        {
            std::fprintf(stderr, "Host's start request did not reach every player in the race\n");
            return false;
        }
        std::printf("Host-controlled race start works and reaches all players together\n");

        // Chat must keep working once the race is actually in progress, not just
        // while it was still a waiting room, and -- unlike the waiting room -- it
        // must now have actually narrowed to just this race: StartRace flips
        // mRaceStarted for lHost and lJoiner (see ServerSocket.cpp's
        // MRNM_START_RACE case), which is what takes their chat out of the wider
        // lobby pool from here on.
        const std::string lInRaceText = "gg, race is on";
        if (!lJoiner.SendMessage(eRSMsgChatMessage, lInRaceText.data(), lInRaceText.size()))
        {
            std::fprintf(stderr, "Failed to send chat after the race started\n");
            return false;
        }
        bool lSawInRaceChat = false;
        for (int lTries = 0; lTries < 20 && !lSawInRaceChat; ++lTries)
        {
            if (lHost.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lReceived;
                lSawInRaceChat = RaceServerClient::ParseChatMessage(lMessage, lSenderId, lReceived) &&
                                 lReceived == lInRaceText;
            }
        }
        if (!lSawInRaceChat)
        {
            std::fprintf(stderr, "Host never received the joiner's chat after the race started\n");
            return false;
        }
        std::printf("Chat still works once the race has actually started\n");

        // D, E and F all legitimately received earlier waiting-room/lobby chat
        // (that's what the checks above just proved) and some of those messages
        // were never drained -- clear all of that out first so the isolation check
        // below can't mistake old, already-correct traffic for a fresh leak.
        while (lClientD.PollMessage(lMessage, 100)) { }
        while (lLobbyClientE.PollMessage(lMessage, 100)) { }
        while (lLobbyClientF.PollMessage(lMessage, 100)) { }

        // Now that lHost/lJoiner's race has actually started, it must be isolated
        // from everyone else: a lobby-only browser, and a client still sitting in
        // an unrelated, not-yet-started waiting room (lClientD) -- both of whom
        // could see waiting-room chat a moment ago -- must NOT see this.
        if ((lClientD.PollMessage(lMessage, 300) && lMessage.mType == eRSMsgChatMessage) ||
            (lLobbyClientE.PollMessage(lMessage, 300) && lMessage.mType == eRSMsgChatMessage) ||
            (lLobbyClientF.PollMessage(lMessage, 300) && lMessage.mType == eRSMsgChatMessage))
        {
            std::fprintf(stderr, "A started race's chat leaked to the lobby or another waiting room\n");
            return false;
        }
        // ...and the reverse: further lobby-wide chat must not reach the now-racing
        // host or joiner either. Drain whatever's already queued for them first
        // (e.g. leftover position-sync traffic from the race itself) so the check
        // below can't mistake old traffic for this specific message leaking in.
        while (lHost.PollMessage(lMessage, 100)) { }
        while (lJoiner.PollMessage(lMessage, 100)) { }
        const std::string lStillInLobbyText = "still just browsing";
        if (!lLobbyClientE.SendMessage(eRSMsgChatMessage, lStillInLobbyText.data(), lStillInLobbyText.size()))
        {
            std::fprintf(stderr, "Failed to send post-start lobby chat for the reverse isolation check\n");
            return false;
        }
        if ((lHost.PollMessage(lMessage, 300) && lMessage.mType == eRSMsgChatMessage) ||
            (lJoiner.PollMessage(lMessage, 300) && lMessage.mType == eRSMsgChatMessage))
        {
            std::fprintf(stderr, "Lobby-wide chat leaked into a race that's already started\n");
            return false;
        }
        std::printf("A started race is isolated from the lobby and other waiting rooms, as expected\n");

        // Hosting with explicit track/laps/weapons: settings must round-trip into the
        // lobby listing, and an unknown track must be rejected (eRSMsgJoinedRace with
        // raceId == -1); a duplicate race name is checked separately below.
        RaceServerClient lConfiguredHost;
        if (!lConfiguredHost.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect configured-host client\n");
            return false;
        }
        if (!lConfiguredHost.HostRace("configured-race", "Steeplechase", 7, true))
        {
            std::fprintf(stderr, "Failed to send HostRace request\n");
            return false;
        }
        RaceServerJoinAck lConfiguredAck;
        bool lGotConfiguredAck = false;
        for (int lTries = 0; lTries < 20 && !lGotConfiguredAck; ++lTries)
        {
            if (lConfiguredHost.PollMessage(lMessage, 200) &&
                RaceServerClient::ParseJoinedRace(lMessage, lConfiguredAck))
            {
                lGotConfiguredAck = true;
            }
        }
        if (!lGotConfiguredAck || lConfiguredAck.mRaceId < 0 || !lConfiguredAck.mIsHost)
        {
            std::fprintf(stderr, "HostRace with valid settings was not accepted\n");
            return false;
        }

        if (!lLister.ListGames(lGames))
        {
            std::fprintf(stderr, "ListGames failed after HostRace\n");
            return false;
        }
        const RaceServerGameInfo* lConfigured = nullptr;
        for (const RaceServerGameInfo& lGame : lGames)
        {
            if (lGame.mName == "configured-race") { lConfigured = &lGame; }
        }
        if (lConfigured == nullptr || lConfigured->mTrack != "Steeplechase" || lConfigured->mNumLaps != 7)
        {
            std::fprintf(stderr, "Configured race's track/laps didn't reach the lobby listing\n");
            return false;
        }
        std::printf("HostRace with explicit track/laps/weapons works\n");

        RaceServerClient lBadTrackHost;
        if (!lBadTrackHost.Connect("127.0.0.1", pPort) ||
            !lBadTrackHost.HostRace("bad-track-race", "NotARealTrack", 3, false))
        {
            std::fprintf(stderr, "Could not send an unknown-track HostRace request\n");
            return false;
        }
        RaceServerJoinAck lBadTrackAck;
        bool lGotBadTrackAck = false;
        for (int lTries = 0; lTries < 20 && !lGotBadTrackAck; ++lTries)
        {
            if (lBadTrackHost.PollMessage(lMessage, 200) &&
                RaceServerClient::ParseJoinedRace(lMessage, lBadTrackAck))
            {
                lGotBadTrackAck = true;
            }
        }
        if (!lGotBadTrackAck || lBadTrackAck.mRaceId != -1)
        {
            std::fprintf(stderr, "HostRace with an unknown track was not rejected\n");
            return false;
        }

        // Races are joined by id (JoinGameById), not by name, so a duplicate name is
        // no longer rejected -- the lobby can show two "configured-race" entries and
        // each must still be joinable as the specific race it is.
        RaceServerClient lDupeNameHost;
        if (!lDupeNameHost.Connect("127.0.0.1", pPort) ||
            !lDupeNameHost.HostRace("configured-race", "ClassicH", 3, false))
        {
            std::fprintf(stderr, "Could not send a duplicate-name HostRace request\n");
            return false;
        }
        RaceServerJoinAck lDupeNameAck;
        bool lGotDupeNameAck = false;
        for (int lTries = 0; lTries < 20 && !lGotDupeNameAck; ++lTries)
        {
            if (lDupeNameHost.PollMessage(lMessage, 200) &&
                RaceServerClient::ParseJoinedRace(lMessage, lDupeNameAck))
            {
                lGotDupeNameAck = true;
            }
        }
        if (!lGotDupeNameAck || lDupeNameAck.mRaceId < 0 || !lDupeNameAck.mIsHost ||
            lDupeNameAck.mRaceId == lConfiguredAck.mRaceId)
        {
            std::fprintf(stderr, "HostRace with a duplicate race name was not accepted as a distinct race\n");
            return false;
        }
        std::printf("HostRace correctly rejects an unknown track and accepts a duplicate race name as a distinct race\n");

        // With two same-named races now open, JoinGameById must land on the specific
        // one asked for, not whichever one a name lookup happens to match first.
        RaceServerClient lByIdJoiner;
        if (!lByIdJoiner.Connect("127.0.0.1", pPort) ||
            !lByIdJoiner.JoinGameById(lDupeNameAck.mRaceId))
        {
            std::fprintf(stderr, "Could not send JoinGameById\n");
            return false;
        }
        RaceServerJoinAck lByIdAck;
        bool lGotByIdAck = false;
        for (int lTries = 0; lTries < 20 && !lGotByIdAck; ++lTries)
        {
            if (lByIdJoiner.PollMessage(lMessage, 200) &&
                RaceServerClient::ParseJoinedRace(lMessage, lByIdAck))
            {
                lGotByIdAck = true;
            }
        }
        if (!lGotByIdAck || lByIdAck.mRaceId != lDupeNameAck.mRaceId || lByIdAck.mIsHost)
        {
            std::fprintf(stderr, "JoinGameById did not join the specific race requested\n");
            return false;
        }
        std::printf("JoinGameById disambiguates between two same-named races\n");

        // Player position sync: SendPlayerState's [senderClientId][state bytes]
        // envelope must round-trip through the server's opaque relay so a receiver
        // can tell ParsePlayerState whose update it just got, with a third player
        // in the same race present to prove it's not just "the only other client".
        RaceServerClient lThirdRacer;
        if (!lThirdRacer.Connect("127.0.0.1", pPort) || !lThirdRacer.JoinGame("start-test-race"))
        {
            std::fprintf(stderr, "Third racer could not join start-test-race\n");
            return false;
        }
        // Drain lThirdRacer's own join ack and the CONN_NAME_SET backlog for the
        // two already-in-race players before sending anything state-related.
        for (int lDrain = 0; lDrain < 5; ++lDrain)
        {
            if (!lThirdRacer.PollMessage(lMessage, 200))
            {
                break;
            }
        }
        // lHost and lJoiner likewise get a CONN_NAME_SET about the new arrival.
        lHost.PollMessage(lMessage, 200);
        lJoiner.PollMessage(lMessage, 200);

        // Deliberately claim a bogus sender id. The server must replace it with
        // the connection's real id before either recipient sees the update.
        const std::uint8_t lFakeState[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const int lForgedClientId = -999;
        if (!lHost.SendPlayerState(lForgedClientId, lFakeState, sizeof(lFakeState)))
        {
            std::fprintf(stderr, "Failed to send player state\n");
            return false;
        }

        bool lJoinerGotState = false;
        bool lThirdGotState = false;
        for (int lTries = 0; lTries < 20 && !(lJoinerGotState && lThirdGotState); ++lTries)
        {
            if (!lJoinerGotState && lJoiner.PollMessage(lMessage, 100))
            {
                int lSenderId = -1;
                const std::uint8_t* lStateData = nullptr;
                std::size_t lStateLen = 0;
                if (RaceServerClient::ParsePlayerState(lMessage, lSenderId, lStateData, lStateLen) &&
                    lSenderId == lHostAck.mClientId && lStateLen == sizeof(lFakeState) &&
                    std::memcmp(lStateData, lFakeState, lStateLen) == 0)
                {
                    lJoinerGotState = true;
                }
            }
            if (!lThirdGotState && lThirdRacer.PollMessage(lMessage, 100))
            {
                int lSenderId = -1;
                const std::uint8_t* lStateData = nullptr;
                std::size_t lStateLen = 0;
                if (RaceServerClient::ParsePlayerState(lMessage, lSenderId, lStateData, lStateLen) &&
                    lSenderId == lHostAck.mClientId && lStateLen == sizeof(lFakeState) &&
                    std::memcmp(lStateData, lFakeState, lStateLen) == 0)
                {
                    lThirdGotState = true;
                }
            }
        }
        if (!lJoinerGotState || !lThirdGotState)
        {
            std::fprintf(stderr, "Player state update did not reach every other player with the right sender id\n");
            return false;
        }
        std::printf("Player state sync correctly attributes updates to the sending player\n");

        const std::uint8_t lMissileState[16] = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        };
        if (!lHost.SendAutoElement(1, 150, 2, lMissileState, sizeof(lMissileState)))
        {
            std::fprintf(stderr, "Failed to send automatic missile element\n");
            return false;
        }

        auto lReceivesMissile = [&](RaceServerClient& pClient) {
            for (int lTries = 0; lTries < 20; ++lTries)
            {
                if (!pClient.PollMessage(lMessage, 100))
                {
                    continue;
                }
                int lDllId = 0;
                int lClassId = 0;
                int lRoom = -1;
                const std::uint8_t* lStateData = nullptr;
                std::size_t lStateLen = 0;
                if (RaceServerClient::ParseAutoElement(lMessage, lDllId, lClassId, lRoom,
                                                       lStateData, lStateLen) &&
                    lDllId == 1 && lClassId == 150 && lRoom == 2 &&
                    lStateLen == sizeof(lMissileState) &&
                    std::memcmp(lStateData, lMissileState, lStateLen) == 0)
                {
                    return true;
                }
            }
            return false;
        };
        if (!lReceivesMissile(lJoiner) || !lReceivesMissile(lThirdRacer))
        {
            std::fprintf(stderr, "Missile creation did not reach every other racer intact\n");
            return false;
        }
        std::printf("Missile creation sync reaches every other racer intact\n");

        if (!lHost.SendHit(lJoinerAck.mClientId))
        {
            std::fprintf(stderr, "Failed to send targeted missile impact\n");
            return false;
        }
        auto lReceivesHit = [&](RaceServerClient& pClient) {
            for (int lTries = 0; lTries < 20; ++lTries)
            {
                if (!pClient.PollMessage(lMessage, 100))
                {
                    continue;
                }
                int lTargetClientId = -1;
                if (RaceServerClient::ParseHit(lMessage, lTargetClientId) &&
                    lTargetClientId == lJoinerAck.mClientId)
                {
                    return true;
                }
            }
            return false;
        };
        if (!lReceivesHit(lJoiner) || !lReceivesHit(lThirdRacer))
        {
            std::fprintf(stderr, "Targeted missile impact did not reach every other racer\n");
            return false;
        }
        std::printf("Targeted missile impact reaches every other racer\n");

        return true;
    }
}

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // Flush progress even if something hangs afterward

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

    // Give the server a few seconds to exit on SIGTERM; if it doesn't (this test
    // helped find a real bug where it sometimes wouldn't -- see RaceServer.cpp's
    // ConsoleCtrlHandler comment), fall back to SIGKILL rather than hanging forever.
    kill(lServerPid, SIGTERM);
    int lStatus = 0;
    bool lExited = false;
    for (int lWait = 0; lWait < 50 && !lExited; ++lWait)
    {
        if (waitpid(lServerPid, &lStatus, WNOHANG) == lServerPid)
        {
            lExited = true;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    if (!lExited)
    {
        kill(lServerPid, SIGKILL);
        waitpid(lServerPid, &lStatus, 0);
    }

    return lResult;
}
