// HoverNetAuthBot -- a scripted player for HoverNetAuthServer. It connects, says Hello, waits for Start, then sends its input a
// few ticks ahead every 16 ms while rebuilding the race from the server's Tick messages and checking the state hash of every
// tick. Exits 0 only if every tick matched and the Result arrived.
//
//   HoverNetAuthBot --port 9610 [--host 127.0.0.1] [--name bot] [--track ClassicH] [--lead 12]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "AuthSocket.h"
#include "SimHarness.h"
#include "../Authority/Protocol3.h"
#include "../ObjFac1/SimElements.h"

namespace
{
std::int64_t NowMs()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}
}

int main(int argc, char** argv)
{
    std::string host = "127.0.0.1", name = "bot", track = "ClassicH";
    int port = 9610, lead = 12;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") port = std::atoi(value());
        else if (a == "--host") host = value();
        else if (a == "--name") name = value();
        else if (a == "--track") track = value();
        else if (a == "--lead") lead = std::atoi(value());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    if (!AuthNet::Startup()) return 1;

    AuthSocket s = AuthNet::Connect(host, port);
    if (s == kInvalidSocket) { std::fprintf(stderr, "cannot connect to %s:%d\n", host.c_str(), port); return 1; }

    Protocol3::FrameParser parser;
    auto sendFrame = [&](std::uint8_t type, const std::vector<std::uint8_t>& payload) {
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(type, payload);
        std::size_t off = 0;
        while (off < f.size()) {
            const int n = static_cast<int>(send(s, reinterpret_cast<const char*>(f.data()) + off, static_cast<int>(f.size() - off), 0));
            if (n > 0) off += static_cast<std::size_t>(n);
            else if (n < 0 && AuthNet::WouldBlock()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else return false;
        }
        return true;
    };
    Protocol3::Hello hello;
    hello.name = name;
    if (!sendFrame(Protocol3::kHello, Protocol3::Encode(hello))) return 1;

    SimHarness::World world;
    int player = -1, players = 0;
    bool started = false, gotResult = false, diverged = false, built = false;
    std::uint32_t verified = 0;
    std::int64_t lastSend = -1000, deadline = NowMs() + 120000;
    std::uint8_t buffer[4096];
    while (NowMs() < deadline && !gotResult) {
        const int n = static_cast<int>(recv(s, reinterpret_cast<char*>(buffer), sizeof(buffer), 0));
        if (n > 0) parser.Feed(buffer, static_cast<std::size_t>(n));
        else if (n == 0) break;
        else if (!AuthNet::WouldBlock()) break;
        std::uint8_t type;
        std::vector<std::uint8_t> payload;
        while (parser.Next(type, payload)) {
            using namespace Protocol3;
            if (type == kHelloAck) {
                HelloAck a;
                if (!Decode(payload, a)) { std::fprintf(stderr, "bad HelloAck\n"); return 1; }
                player = a.player; players = a.players;
                built = world.Build(SimHarness::TrackPath(a.track), a.track, players, a.laps, a.weapons);
                if (!built) { std::fprintf(stderr, "cannot build the race\n"); return 1; }
            }
            else if (type == kStart) started = true;
            else if (type == kTick && built) {
                Tick t;
                if (!Decode(payload, t) || t.tick != verified || static_cast<int>(t.controls.size()) != players) { diverged = true; continue; }
                world.Step(t.controls.data());
                if (world.Hash() != t.hash) diverged = true;
                ++verified;
            }
            else if (type == kResult) gotResult = true;
            else if (type == kReject) { Reject r; Decode(payload, r); std::fprintf(stderr, "rejected: %s\n", r.message.c_str()); return 1; }
        }
        if (started && player >= 0 && NowMs() - lastSend >= 16) {
            lastSend = NowMs();
            Protocol3::Input in;
            const std::uint32_t target = verified + static_cast<std::uint32_t>(lead);
            in.firstTick = target >= 15 ? target - 15 : 0;
            for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(SimHarness::ScriptedInput(player, in.firstTick + k));
            if (!sendFrame(Protocol3::kInput, Protocol3::Encode(in))) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AuthNet::CloseSocket(s);
    std::printf("%s (player %d): %u ticks verified, result %s, %s\n", name.c_str(), player, verified, gotResult ? "received" : "MISSING", diverged ? "DIVERGED" : "no divergence");
    return (!diverged && gotResult && verified > 0) ? 0 : 1;
}
