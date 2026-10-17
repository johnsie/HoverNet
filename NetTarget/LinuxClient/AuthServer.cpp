// HoverNetAuthServer -- a dedicated server for authoritative races over TCP (protocol 3.0 framing, no encryption yet).
//
//   HoverNetAuthServer --track ClassicH --players 8 [--laps 3] [--port 9610] [--bind 127.0.0.1] [--once]
//                      [--limit-ticks N] [--start-delay-ms 3000] [--max-connections 64] [--replay-dir DIR]
//   HoverNetAuthServer --lobby [--tracks "ClassicH,The River"] [--port 9610]     many races: players host and join through the lobby
//
// It hosts one race at a time (RaceHost) and starts a fresh one when the race ends. It binds to the loopback address unless
// told otherwise: exposing it is a deliberate act, and until the encrypted session exists it should sit behind TLS
// termination or on a trusted network. One thread, non-blocking sockets, no allocation per tick beyond the host's own.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AuthSocket.h"
#include "SimHarness.h"
#include "../Authority/LobbyServer.h"
#include "../Authority/RaceHost.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
struct Peer
{
    AuthSocket socket = kInvalidSocket;
    int connection = 0;
    std::vector<std::uint8_t> pending;   // bytes not yet accepted by the socket
    std::string address;
};

std::int64_t NowMs()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

bool FlushPeer(Peer& p)
{
    while (!p.pending.empty()) {
        const int sent = static_cast<int>(send(p.socket, reinterpret_cast<const char*>(p.pending.data()), static_cast<int>(p.pending.size()), 0));
        if (sent > 0) { p.pending.erase(p.pending.begin(), p.pending.begin() + sent); continue; }
        if (sent < 0 && AuthNet::WouldBlock()) return true;
        return false;
    }
    return true;
}
}


// One pass over the sockets for any server core (RaceHost or LobbyServer): accept, read, advance time, write, reap.
template <class Core>
void PumpSockets(Core& core, AuthSocket listener, std::vector<std::unique_ptr<Peer>>& peers, int maxConnections)
{
    // Accept.
        for (;;) {
            sockaddr_in from;
            socklen_t len = sizeof(from);
            AuthSocket s = accept(listener, reinterpret_cast<sockaddr*>(&from), &len);
            if (s == kInvalidSocket) break;
            char text[64] = "?";
            inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
            int sameAddress = 0;
            for (const auto& p : peers) if (p->address == text) ++sameAddress;
            if (static_cast<int>(peers.size()) >= maxConnections || sameAddress >= 16) { AuthNet::CloseSocket(s); continue; }   // a cap per address and overall
            AuthNet::SetNonBlocking(s);
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            std::unique_ptr<Peer> p(new Peer);
            p->socket = s;
            p->address = text;
            p->connection = core.Connect(NowMs());
            peers.push_back(std::move(p));
        }
        // Read.
        std::uint8_t buffer[4096];
        for (auto& p : peers) {
            if (p->socket == kInvalidSocket) continue;
            for (int rounds = 0; rounds < 8; ++rounds) {
                const int n = static_cast<int>(recv(p->socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0));
                if (n > 0) { core.Receive(p->connection, buffer, static_cast<std::size_t>(n), NowMs()); continue; }
                if (n == 0 || !AuthNet::WouldBlock()) core.Disconnect(p->connection);
                break;
            }
        }
        core.Advance(NowMs());
        // Write and reap.
        for (auto& p : peers) {
            if (p->socket == kInvalidSocket) continue;
            const std::vector<std::uint8_t> out = core.TakeOutput(p->connection);
            p->pending.insert(p->pending.end(), out.begin(), out.end());
            const bool ok = FlushPeer(*p);
            if (!ok || (core.IsClosed(p->connection) && p->pending.empty())) { AuthNet::CloseSocket(p->socket); p->socket = kInvalidSocket; }
            else if (p->pending.size() > (1u << 20)) { core.Disconnect(p->connection); }     // a client that does not read is dropped
        }
        peers.erase(std::remove_if(peers.begin(), peers.end(), [](const std::unique_ptr<Peer>& p) { return p->socket == kInvalidSocket; }), peers.end());

}

int main(int argc, char** argv)
{
    HostConfig cfg;
    cfg.race.trackName = "ClassicH";
    cfg.race.players = 2;
    cfg.race.laps = 3;
    cfg.startDelayMs = 3000;
    int port = 9610, maxConnections = 64;
    std::string bind = "127.0.0.1";
    bool once = false;
    std::string replayDir;
    bool lobbyMode = false;
    std::string trackList;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--track") cfg.race.trackName = value();
        else if (a == "--players") cfg.race.players = std::atoi(value());
        else if (a == "--laps") cfg.race.laps = std::atoi(value());
        else if (a == "--port") port = std::atoi(value());
        else if (a == "--bind") bind = value();
        else if (a == "--once") once = true;
        else if (a == "--limit-ticks") cfg.race.maxTicks = static_cast<std::uint32_t>(std::atol(value()));
        else if (a == "--start-delay-ms") cfg.startDelayMs = std::atoi(value());
        else if (a == "--max-connections") maxConnections = std::atoi(value());
        else if (a == "--replay-dir") replayDir = value();
        else if (a == "--lobby") lobbyMode = true;
        else if (a == "--tracks") trackList = value();
        else if (a == "--no-weapons") cfg.race.weapons = false;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    cfg.race.trackPath = SimHarness::TrackPath(cfg.race.trackName);

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    if (!AuthNet::Startup()) { std::fprintf(stderr, "network start-up failed\n"); return 1; }

    int boundPort = 0;
    AuthSocket listener = AuthNet::Listen(bind, port, boundPort);
    if (listener == kInvalidSocket) { std::fprintf(stderr, "cannot listen on %s:%d\n", bind.c_str(), port); return 1; }

    {   // Resume tokens must not be guessable: seed them from the operating system.
        std::random_device rd;
        cfg.tokenSeed = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
    }
    if (lobbyMode) {
        LobbyConfig lobbyConfig;
        lobbyConfig.raceTemplate = cfg;
        std::stringstream names(trackList.empty() ? "ClassicH,Metro Spiral,Steeplechase,Switchback,The Alley2,The River,Tidal Causeway" : trackList);
        for (std::string name; std::getline(names, name, ',');) if (!name.empty()) lobbyConfig.tracks.push_back(name);
        lobbyConfig.maxConnections = maxConnections;
        LobbyServer lobby(lobbyConfig);
        lobby.SetRaceFinishedCallback([&](std::uint32_t id, const RaceHost& finished) {
            std::printf("race %u over after %u ticks, final hash %016llx\n", id, finished.Race().CurrentTick(), static_cast<unsigned long long>(finished.Race().StateHash()));
            for (const auto& r : finished.Race().Results()) std::printf("  player %d: rank %d, laps %d%s\n", r.player, r.rank, r.laps, r.finished ? ", finished" : "");
            if (!replayDir.empty()) {
                char name[160];
                std::snprintf(name, sizeof(name), "/%s-%u-%lld.hnr", finished.Config().race.trackName.c_str(), id, static_cast<long long>(std::time(nullptr)));
                const std::string path = replayDir + name;
                const std::vector<std::uint8_t> bytes = finished.Race().ReplayBytes();
                FILE* f = std::fopen(path.c_str(), "wb");
                if (f != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size()) std::printf("replay saved to %s (%zu bytes)\n", path.c_str(), bytes.size());
                else std::fprintf(stderr, "cannot write the replay to %s\n", path.c_str());
                if (f != nullptr) std::fclose(f);
            }
            std::fflush(stdout);
        });
        std::printf("listening on %s:%d, lobby with %zu tracks\n", bind.c_str(), boundPort, lobbyConfig.tracks.size());
        std::fflush(stdout);
        std::vector<std::unique_ptr<Peer>> lobbyPeers;
        for (;;) {
            PumpSockets(lobby, listener, lobbyPeers, maxConnections);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::unique_ptr<RaceHost> host(new RaceHost(cfg));
    std::string error;
    if (!host->Init(error)) { std::fprintf(stderr, "cannot host the race: %s\n", error.c_str()); return 1; }
    std::printf("listening on %s:%d, %s, %d players, %d laps\n", bind.c_str(), boundPort, cfg.race.trackName.c_str(), cfg.race.players, cfg.race.laps);
    std::fflush(stdout);

    std::vector<std::unique_ptr<Peer>> peers;
    std::int64_t raceEndedAt = -1;
    for (;;) {
        PumpSockets(*host, listener, peers, maxConnections);

        if (host->RaceFinished()) {
            if (raceEndedAt < 0) raceEndedAt = NowMs();
            bool flushed = true;
            for (const auto& p : peers) flushed = flushed && p->pending.empty();
            if (flushed && NowMs() - raceEndedAt > 300) {
                const std::vector<Authority::PlayerResult> results = host->Race().Results();
                std::printf("race over after %u ticks, final hash %016llx\n", host->Race().CurrentTick(), static_cast<unsigned long long>(host->Race().StateHash()));
                for (const auto& r : results) std::printf("  player %d: rank %d, laps %d%s\n", r.player, r.rank, r.laps, r.finished ? ", finished" : "");
                std::fflush(stdout);
                const RaceHost::Metrics& m = host->GetMetrics();
                std::printf("metrics: connections %llu accepted / %llu closed, %llu frames in (%llu bytes), %llu frames out (%llu bytes), %llu invalid, %llu ticks, inputs %llu accepted / %llu late / %llu too far, %llu snapshots\n",
                            (unsigned long long)m.connectionsAccepted, (unsigned long long)m.connectionsClosed, (unsigned long long)m.framesIn, (unsigned long long)m.bytesIn,
                            (unsigned long long)m.framesOut, (unsigned long long)m.bytesOut, (unsigned long long)m.invalidMessages, (unsigned long long)m.ticksSimulated,
                            (unsigned long long)m.inputsAccepted, (unsigned long long)m.inputsLate, (unsigned long long)m.inputsTooFar, (unsigned long long)m.snapshotsServed);
                std::fflush(stdout);
                if (!replayDir.empty()) {
                    // The official record of the race: a replay anyone can verify and watch.
                    char name[128];
                    std::snprintf(name, sizeof(name), "/%s-%lld.hnr", cfg.race.trackName.c_str(), static_cast<long long>(std::time(nullptr)));
                    const std::string path = replayDir + name;
                    const std::vector<std::uint8_t> bytes = host->Race().ReplayBytes();
                    FILE* f = std::fopen(path.c_str(), "wb");
                    if (f != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size()) std::printf("replay saved to %s (%zu bytes)\n", path.c_str(), bytes.size());
                    else std::fprintf(stderr, "cannot write the replay to %s\n", path.c_str());
                    if (f != nullptr) std::fclose(f);
                    std::fflush(stdout);
                }
                if (once) break;
                for (auto& p : peers) { host->Disconnect(p->connection); AuthNet::CloseSocket(p->socket); }
                peers.clear();
                host.reset(new RaceHost(cfg));
                if (!host->Init(error)) { std::fprintf(stderr, "cannot host the next race: %s\n", error.c_str()); return 1; }
                raceEndedAt = -1;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (auto& p : peers) AuthNet::CloseSocket(p->socket);
    AuthNet::CloseSocket(listener);
    return 0;
}
