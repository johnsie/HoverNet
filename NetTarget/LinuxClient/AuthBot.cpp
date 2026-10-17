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

#include "AuthConnection.h"
#include "AuthSocket.h"
#include "../Authority/Crypto.h"
#include "SimHarness.h"
#include "../Authority/Protocol3.h"
#include "../Authority/ReplayFormat.h"
#include "../Model/StateSnapshot.h"
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
    std::string serverKeyHex;            // pin the server's key and talk over the secure session
    std::string say, reportName;         // optional: say something in the race once it has started, report a player
    bool spectate = false;               // watch instead of driving (single-race servers: --spectate; lobby: --spectate-race TRACK)
    std::string hostTrack, joinTrack;   // lobby servers: host a race ('Track:players[:laps]') or join an open race on a track
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") port = std::atoi(value());
        else if (a == "--host") host = value();
        else if (a == "--name") name = value();
        else if (a == "--track") track = value();
        else if (a == "--lead") lead = std::atoi(value());
        else if (a == "--server-key") serverKeyHex = value();
        else if (a == "--say") say = value();
        else if (a == "--report") reportName = value();
        else if (a == "--spectate") spectate = true;
        else if (a == "--spectate-race") { spectate = true; joinTrack = value(); }
        else if (a == "--host-race") hostTrack = value();
        else if (a == "--join-race") joinTrack = value();
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    if (!AuthNet::Startup()) return 1;

    AuthConnection conn;
    {
        std::string error;
        Crypto::Bytes key;
        if (!serverKeyHex.empty() && (!Crypto::FromHex(serverKeyHex, key) || key.size() != 32)) { std::fprintf(stderr, "--server-key must be 64 hex characters\n"); return 2; }
        if (!conn.Connect(host, port, error, key.empty() ? nullptr : key.data())) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    }
    auto sendFrame = [&](std::uint8_t type, const std::vector<std::uint8_t>& payload) { return conn.Send(type, payload); };
    Protocol3::Hello hello;
    hello.name = name;
    // A spectator on a single-race server says Spectate instead of Hello; on a lobby server it says Hello and then picks a race.
    const bool lobbySpectator = spectate && !joinTrack.empty();
    if (!sendFrame(spectate && !lobbySpectator ? Protocol3::kSpectate : Protocol3::kHello, Protocol3::Encode(hello))) return 1;

    SimHarness::World world;
    int player = -1, players = 0;
    bool started = false, gotResult = false, diverged = false, built = false, lobbyDone = false, restored = false;
    std::vector<Protocol3::SnapshotChunk> chunks;
    std::uint32_t firstTick = 0;
    std::uint32_t verified = 0;
    std::int64_t lastSend = -1000, deadline = NowMs() + 120000;
    while (NowMs() < deadline && !gotResult) {
        std::vector<AuthFrame> received;
        const bool open = conn.Pump(received);
        if (!open && received.empty()) break;
        for (const AuthFrame& frame : received) {
            const std::uint8_t type = frame.type;
            const std::vector<std::uint8_t>& payload = frame.payload;
            using namespace Protocol3;
            if (type == kHelloAck) {
                HelloAck a;
                if (!Decode(payload, a)) { std::fprintf(stderr, "bad HelloAck\n"); return 1; }
                player = a.player; players = a.players;
                built = world.Build(SimHarness::TrackPath(a.track), a.track, players, a.laps, a.weapons);
                if (!built) { std::fprintf(stderr, "cannot build the race\n"); return 1; }
            }
            else if (type == kRaceList && !lobbyDone) {
                RaceList list;
                if (!Decode(payload, list)) { std::fprintf(stderr, "bad race list\n"); return 1; }
                lobbyDone = true;
                if (!hostTrack.empty()) {
                    HostRace h;
                    std::vector<std::string> parts;
                    std::string part;
                    for (char ch : hostTrack + ":") { if (ch == ':') { parts.push_back(part); part.clear(); } else part += ch; }
                    h.track = parts[0];
                    h.players = static_cast<std::uint8_t>(parts.size() > 1 && !parts[1].empty() ? std::atoi(parts[1].c_str()) : 2);
                    h.laps = static_cast<std::uint8_t>(parts.size() > 2 && !parts[2].empty() ? std::atoi(parts[2].c_str()) : 3);
                    track = h.track;
                    if (!sendFrame(kHostRace, Encode(h))) return 1;
                }
                else if (lobbySpectator) {
                    bool joined = false;
                    for (const RaceInfo& r : list.races) {
                        if (!r.started || r.track != joinTrack) continue;
                        SpectateRace j;
                        j.id = r.id;
                        if (!sendFrame(kSpectateRace, Encode(j))) return 1;
                        track = r.track;
                        joined = true;
                        break;
                    }
                    if (!joined) { std::fprintf(stderr, "no running race to watch\n"); return 1; }
                }
                else {
                    bool joined = false;
                    for (const RaceInfo& r : list.races) {
                        if (r.started || r.joined >= r.players || (!joinTrack.empty() && r.track != joinTrack)) continue;
                        JoinRace j;
                        j.id = r.id;
                        if (!sendFrame(kJoinRace, Encode(j))) return 1;
                        joined = true;
                        break;
                    }
                    if (!joined) { std::fprintf(stderr, "no open race to join\n"); return 1; }
                }
            }
            else if (type == kStart) {
                started = true;
                if (!say.empty()) { Chat c; c.text = say; sendFrame(kChat, Encode(c)); }
                if (!reportName.empty()) { Report r; r.name = reportName; r.reason = "reported by the test bot"; sendFrame(kReport, Encode(r)); }
            }
            else if (type == kChatMessage) {
                ChatMessage m;
                if (Decode(payload, m)) std::printf("[chat] %s%s: %s\n", m.fromServer ? "(server) " : "", m.name.c_str(), m.text.c_str());
            }
            else if (type == kTick && built && (!spectate || restored)) {
                Tick t;
                if (!Decode(payload, t) || t.tick != verified || static_cast<int>(t.controls.size()) != players) { diverged = true; continue; }
                world.Step(t.controls.data());
                if (world.Hash() != t.hash) diverged = true;
                ++verified;
            }
            else if (type == kSnapshotChunk && built && spectate) {
                SnapshotChunk c;
                if (!Decode(payload, c)) { diverged = true; continue; }
                if (c.index == 0) chunks.clear();
                chunks.push_back(c);
                if (c.index + 1 == c.count && chunks.size() == c.count) {
                    std::vector<std::uint8_t> bytes;
                    for (const SnapshotChunk& part : chunks) bytes.insert(bytes.end(), part.data.begin(), part.data.end());
                    std::vector<MR_FreeElement*> external(world.craft.begin(), world.craft.end());
                    std::int64_t time = 0;
                    std::string error;
                    if (MR_RestoreLevelState(world.session->GetCurrentLevel(), bytes.data(), bytes.size(), external, ReplayFormat::kSimulationRules, time, error)) {
                        world.session->SetSimulationTime(static_cast<MR_SimulationTime>(time));
                        verified = c.tick;
                        restored = true;
                    }
                    else { std::fprintf(stderr, "cannot restore the snapshot: %s\n", error.c_str()); diverged = true; }
                    chunks.clear();
                }
            }
            else if (type == kResult) gotResult = true;
            else if (type == kReject) { Reject r; Decode(payload, r); std::fprintf(stderr, "rejected: %s\n", r.message.c_str()); return 1; }
        }
        if (!spectate && started && player >= 0 && NowMs() - lastSend >= 16) {
            lastSend = NowMs();
            Protocol3::Input in;
            const std::uint32_t target = verified + static_cast<std::uint32_t>(lead);
            in.firstTick = target >= 15 ? target - 15 : 0;
            for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(SimHarness::ScriptedInput(player, in.firstTick + k));
            if (!sendFrame(Protocol3::kInput, Protocol3::Encode(in))) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    conn.Close();
    std::printf("%s (player %d): %u ticks verified, result %s, %s\n", name.c_str(), player, verified, gotResult ? "received" : "MISSING", diverged ? "DIVERGED" : "no divergence");
    return (!diverged && gotResult && verified > 0 && (!spectate || restored)) ? 0 : 1;
}
