#include "RaceServerClient.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
pid_t gServerPid = -1;

void CleanupServer()
{
    if (gServerPid > 0) {
        kill(gServerPid, SIGKILL);
        waitpid(gServerPid, nullptr, 0);
        gServerPid = -1;
    }
}

pid_t StartServer(const std::string& pPath, unsigned pPort)
{
    const pid_t lPid = fork();
    if (lPid != 0) return lPid;
    const std::string lPort = std::to_string(pPort);
    execl(pPath.c_str(), pPath.c_str(), lPort.c_str(),
          "RaceServerLifecycleSmoke.log", "--max-races", "1",
          "--require-protocol-2", static_cast<char*>(nullptr));
    _exit(127);
}

bool WaitForServer(unsigned pPort)
{
    for (int lTry = 0; lTry < 50; ++lTry) {
        RaceServerClient lProbe;
        if (lProbe.Connect("127.0.0.1", pPort)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool WaitForAck(RaceServerClient& pClient, RaceServerJoinAck& pAck)
{
    for (int lTry = 0; lTry < 20; ++lTry) {
        RaceServerMessage lMessage;
        if (pClient.PollMessage(lMessage, 200) &&
            RaceServerClient::ParseJoinedRace(lMessage, pAck)) return true;
    }
    return false;
}

bool StopServer(pid_t pPid)
{
    kill(pPid, SIGTERM);
    int lStatus = 0;
    for (int lTry = 0; lTry < 50; ++lTry) {
        if (waitpid(pPid, &lStatus, WNOHANG) == pPid) {
            return WIFEXITED(lStatus) && WEXITSTATUS(lStatus) == 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(pPid, SIGKILL);
    waitpid(pPid, &lStatus, 0);
    return false;
}
}

int main(int argc, char** argv)
{
    std::atexit(CleanupServer);
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <RaceServer>\n", argv[0]);
        return 2;
    }
    const std::string lServerPath = argv[1];
    const unsigned lPort = 19876;
    pid_t lPid = StartServer(lServerPath, lPort);
    gServerPid = lPid;
    if (lPid < 0 || !WaitForServer(lPort)) {
        std::fprintf(stderr, "Lifecycle server did not start\n");
        return 1;
    }

    RaceServerClient lFirst;
    RaceServerClient lSecond;
    if (!lFirst.Connect("127.0.0.1", lPort) || !lSecond.Connect("127.0.0.1", lPort) ||
        !lFirst.HostRace("capacity-one", "ClassicH", 3, true)) return 1;
    RaceServerJoinAck lFirstAck;
    if (!WaitForAck(lFirst, lFirstAck) || lFirstAck.mRaceId < 0) return 1;

    if (!lSecond.HostRace("capacity-two", "ClassicH", 3, true)) return 1;
    RaceServerJoinAck lRejectedAck;
    if (!WaitForAck(lSecond, lRejectedAck) || lRejectedAck.mRaceId != -1) {
        std::fprintf(stderr, "Maximum-race capacity was not enforced\n");
        return 1;
    }

    lFirst.Disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!lSecond.HostRace("capacity-recovered", "ClassicH", 3, true)) return 1;
    RaceServerJoinAck lRecoveredAck;
    if (!WaitForAck(lSecond, lRecoveredAck) || lRecoveredAck.mRaceId < 0) {
        std::fprintf(stderr, "Race capacity did not recover after the sole host left\n");
        return 1;
    }
    lSecond.Disconnect();

    if (!StopServer(lPid)) {
        std::fprintf(stderr, "RaceServer did not exit cleanly on SIGTERM\n");
        return 1;
    }
    gServerPid = -1;

    lPid = StartServer(lServerPath, lPort);
    gServerPid = lPid;
    if (lPid < 0 || !WaitForServer(lPort)) {
        std::fprintf(stderr, "RaceServer could not restart on the same port\n");
        return 1;
    }
    RaceServerClient lAfterRestart;
    std::vector<RaceServerGameInfo> lRaces;
    if (!lAfterRestart.Connect("127.0.0.1", lPort) ||
        !lAfterRestart.ListGames(lRaces) || !lRaces.empty()) {
        std::fprintf(stderr, "Restarted server did not have clean race state\n");
        return 1;
    }
    if (!StopServer(lPid)) {
        std::fprintf(stderr, "Restarted RaceServer did not stop cleanly\n");
        return 1;
    }
    gServerPid = -1;

    std::puts("RaceServer capacity, graceful shutdown, and restart lifecycle passed");
    return 0;
}
