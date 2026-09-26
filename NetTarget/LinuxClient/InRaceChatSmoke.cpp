// Integration smoke test for in-race chat: two real MR_ClientSession objects (one
// per "player", exactly as Track3DViewer.cpp's gameplay loop uses) exchanging chat
// over a real RaceServerClient connection to a real RaceServer, after actually
// starting a race -- not just the raw wire mechanics (RaceServerClientSmoke.cpp
// already covers that) and not just a read of the client source, but the whole
// send -> relay -> parse -> MR_ClientSession::AddMessage -> GetMessageStack path
// that a real racing client relies on to both send and display in-race chat.
#include "../Game2/ClientSession.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/Sprite.h"
#include "../VideoServices/VideoBuffer.h"
#include "RaceServerClient.h"

#include <cerrno>
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

    // Constructs a real, race-ready MR_ClientSession, exactly the way main()
    // does before entering the online gameplay loop.
    bool MakeRacingSession(MR_ClientSession& pSession, MR_VideoBuffer& pBuffer)
    {
        MR_RecordFile* lTrack = new MR_RecordFile;
        if (!lTrack->OpenForRead("NetTarget/Tracks/ClassicH.trk"))
        {
            delete lTrack;
            return false;
        }
        if (!pSession.LoadNew("ClassicH", lTrack, 1, TRUE, &pBuffer) || !pSession.CreateMainCharacter())
        {
            return false;
        }
        return true;
    }

    bool RunChecks(unsigned pPort)
    {
        MR_MainCharacter::RegisterFactory();
        MR_DllObjectFactory::MR_DllObjectFactoryCleanup lFactoryCleanup;

        MR_VideoBuffer lBufferA(nullptr, 1.0, 0.5, 0.5);
        MR_VideoBuffer lBufferB(nullptr, 1.0, 0.5, 0.5);
        if (!lBufferA.SetVideoMode(320, 200) || !lBufferA.Lock() ||
            !lBufferB.SetVideoMode(320, 200) || !lBufferB.Lock())
        {
            std::fprintf(stderr, "Could not create framebuffers\n");
            return false;
        }

        MR_ClientSession lSessionA;
        MR_ClientSession lSessionB;
        if (!MakeRacingSession(lSessionA, lBufferA) || !MakeRacingSession(lSessionB, lBufferB))
        {
            std::fprintf(stderr, "Could not build racing sessions for A/B\n");
            return false;
        }

        RaceServerClient lClientA;
        RaceServerClient lClientB;
        if (!lClientA.Connect("127.0.0.1", pPort) || !lClientB.Connect("127.0.0.1", pPort))
        {
            std::fprintf(stderr, "Could not connect both clients to RaceServer\n");
            return false;
        }
        if (!lClientA.SetPlayerName("Linux Racer") || !lClientB.SetPlayerName("Windows Racer"))
        {
            std::fprintf(stderr, "Could not publish player names\n");
            return false;
        }
        if (!lClientA.HostRace("in-race-chat-test", "ClassicH", 3, true))
        {
            std::fprintf(stderr, "HostRace failed\n");
            return false;
        }
        RaceServerMessage lMessage;
        RaceServerJoinAck lHostAck;
        bool lGotHostAck = false;
        for (int lTries = 0; lTries < 20 && !lGotHostAck; ++lTries)
        {
            if (lClientA.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgJoinedRace)
            {
                lGotHostAck = RaceServerClient::ParseJoinedRace(lMessage, lHostAck) && lHostAck.mRaceId >= 0;
            }
        }
        if (!lGotHostAck)
        {
            std::fprintf(stderr, "Host never got a valid eRSMsgJoinedRace ack\n");
            return false;
        }
        if (!lClientB.JoinGameById(lHostAck.mRaceId))
        {
            std::fprintf(stderr, "JoinGameById failed\n");
            return false;
        }
        RaceServerJoinAck lJoinAck;
        bool lGotJoinAck = false;
        for (int lTries = 0; lTries < 20 && !lGotJoinAck; ++lTries)
        {
            if (lClientB.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgJoinedRace)
            {
                lGotJoinAck = RaceServerClient::ParseJoinedRace(lMessage, lJoinAck) && lJoinAck.mRaceId >= 0;
            }
        }
        if (!lGotJoinAck)
        {
            std::fprintf(stderr, "Joiner never got a valid eRSMsgJoinedRace ack\n");
            return false;
        }
        // The server must advertise B's chosen name to A. This is the piece the
        // Windows client previously skipped before joining, leaving every peer to
        // see the generated Player_<id> fallback instead.
        std::string lRemoteName;
        while (lClientA.PollMessage(lMessage, 200))
        {
            RaceServerPeer lPeer;
            if (lMessage.mType == eRSMsgConnNameSet &&
                RaceServerClient::ParsePeer(lMessage, lPeer) &&
                lPeer.mClientId == lJoinAck.mClientId)
            {
                lRemoteName = lPeer.mName;
            }
        }
        if (lRemoteName != "Windows Racer")
        {
            std::fprintf(stderr, "Chosen peer name was not advertised (got '%s')\n", lRemoteName.c_str());
            return false;
        }

        if (!lClientA.StartRace())
        {
            std::fprintf(stderr, "StartRace failed\n");
            return false;
        }
        bool lASawStart = false;
        bool lBSawStart = false;
        for (int lTries = 0; lTries < 20 && !(lASawStart && lBSawStart); ++lTries)
        {
            if (!lASawStart && lClientA.PollMessage(lMessage, 100) && lMessage.mType == eRSMsgRaceStarted)
            {
                lASawStart = true;
            }
            if (!lBSawStart && lClientB.PollMessage(lMessage, 100) && lMessage.mType == eRSMsgRaceStarted)
            {
                lBSawStart = true;
            }
        }
        if (!lASawStart || !lBSawStart)
        {
            std::fprintf(stderr, "Race never actually started for both players\n");
            return false;
        }
        std::printf("Both racing sessions are up and the race has started\n");

        // This is the exact sequence Track3DViewer.cpp's gameplay loop runs on
        // SDLK_RETURN: send over the network, then push the same text onto this
        // client's own MR_ClientSession message stack as a local echo.
        const std::string lChatText = "gg everyone";
        const std::string lWireChatText = RaceServerClient::EncodeInRaceChat(lChatText);
        if (!lClientB.SendMessage(eRSMsgChatMessage, lWireChatText.data(), lWireChatText.size()))
        {
            std::fprintf(stderr, "Failed to send in-race chat\n");
            return false;
        }
        lSessionB.AddMessage(("You: " + lChatText).c_str());

        // This is the exact sequence the gameplay loop runs on receipt: parse,
        // resolve, then push onto the RECEIVING client's own (different!)
        // MR_ClientSession -- proving the message actually crosses process/session
        // boundaries via the network, not just that AddMessage itself works.
        bool lReceived = false;
        std::string lExpectedDisplayLine;
        for (int lTries = 0; lTries < 20 && !lReceived; ++lTries)
        {
            if (lClientA.PollMessage(lMessage, 200) && lMessage.mType == eRSMsgChatMessage)
            {
                int lSenderId = -1;
                std::string lText;
                if (RaceServerClient::ParseChatMessage(lMessage, lSenderId, lText))
                {
                    lText = RaceServerClient::DecodeInRaceChat(lText);
                }
                if (lText == lChatText)
                {
                    lExpectedDisplayLine = lRemoteName + ": " + lText;
                    lSessionA.AddMessage(lExpectedDisplayLine.c_str());
                    lReceived = true;
                }
            }
        }
        if (!lReceived)
        {
            std::fprintf(stderr, "Client A never received B's in-race chat\n");
            return false;
        }

        // The real proof: read it back out of A's message stack via
        // GetMessageStack, exactly like Observer::RenderNormalDisplay does every
        // frame to draw the on-screen chat overlay. What's stored is Ascii2Simple-
        // encoded (see MR_ClientSession::AddMessage) -- that's the game's bitmap-
        // font glyph-index encoding, not plain ASCII, so the comparison has to
        // encode the expected text the same way rather than compare it as text.
        char lStackBuffer[256] = {0};
        const std::string lExpectedEncoded = Ascii2Simple(lExpectedDisplayLine.c_str());
        if (!lSessionA.GetMessageStack(0, lStackBuffer, 20) || lExpectedEncoded != lStackBuffer)
        {
            std::fprintf(stderr, "Received chat never made it into the message stack Observer renders\n");
            return false;
        }
        std::printf("In-race chat crosses the network and lands in the receiver's rendered message stack\n");

        // And B's own local echo must be in its stack too.
        const std::string lExpectedLocalEcho = "You: " + lChatText;
        const std::string lExpectedLocalEchoEncoded = Ascii2Simple(lExpectedLocalEcho.c_str());
        char lLocalEchoBuffer[256] = {0};
        if (!lSessionB.GetMessageStack(0, lLocalEchoBuffer, 20) || lExpectedLocalEchoEncoded != lLocalEchoBuffer)
        {
            std::fprintf(stderr, "Sender's own local echo never made it into its message stack\n");
            return false;
        }
        std::printf("Sender's own local echo is in its message stack too\n");

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
    const unsigned lPort = 19875;  // Distinct from RaceServerClientSmoke's 19870
    const std::string lPortStr = std::to_string(lPort);
    const std::string lLogPath = "InRaceChatSmoke.raceserver.log";

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
            std::printf("InRaceChatSmoke passed\n");
            lResult = 0;
        }
    }
    else
    {
        std::fprintf(stderr, "RaceServer never started listening on port %u\n", lPort);
    }

    // Give the server a few seconds to exit on SIGTERM; fall back to SIGKILL
    // rather than hanging forever if it doesn't.
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
