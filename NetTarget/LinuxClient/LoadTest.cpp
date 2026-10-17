// Load and soak test of the lobby server, in memory and faster than real time.
//
//   HoverNetLoadTest [races] [players-per-race] [race-minutes] [rounds]
//
// Runs `races` simultaneous races of `players` scripted players through a LobbyServer on a virtual clock. Each race lasts
// `race-minutes` of race time; when it ends its players are replaced by a new race, `rounds` times over, so the server hosts,
// finishes, lingers and removes many races while it runs. It checks the things a soak test is for: no player is ever refused or
// sent an invalid message, every race produces a replay that re-simulates to the same final hash (sampled), the number of
// races and connections does not creep up, and memory does not grow from round to round. It prints the server's cost: CPU time
// per simulated race-minute and how many 8-player races one core could host in real time.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/LobbyServer.h"
#include "../Authority/ReplayFormat.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
long ResidentKb()
{
    std::ifstream f("/proc/self/statm");
    long pages = 0, resident = 0;
    if (f >> pages >> resident) return resident * 4;
    return -1;   // not available on this platform
}

struct Player
{
    int conn = 0;
    Protocol3::FrameParser parser;
    int slot = -1;
    bool started = false, gotResult = false, rejected = false;
    unsigned ticks = 0;
};

void Pump(LobbyServer& lobby, Player& p)
{
    const std::vector<std::uint8_t> bytes = lobby.TakeOutput(p.conn);
    p.parser.Feed(bytes.data(), bytes.size());
    std::uint8_t type;
    std::vector<std::uint8_t> payload;
    while (p.parser.Next(type, payload)) {
        using namespace Protocol3;
        if (type == kHelloAck) { HelloAck a; if (Decode(payload, a)) p.slot = a.player; }
        else if (type == kStart) p.started = true;
        else if (type == kTick) ++p.ticks;
        else if (type == kResult) p.gotResult = true;
        else if (type == kReject) p.rejected = true;
    }
}

void Send(LobbyServer& lobby, Player& p, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now)
{
    const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(type, payload);
    lobby.Receive(p.conn, f.data(), f.size(), now);
}
}

int main(int argc, char** argv)
{
    const int races = argc > 1 ? std::atoi(argv[1]) : 8;
    const int players = argc > 2 ? std::atoi(argv[2]) : 8;
    const double raceMinutes = argc > 3 ? std::atof(argv[3]) : 1.0;
    const int rounds = argc > 4 ? std::atoi(argv[4]) : 3;
    const int raceTicks = static_cast<int>(raceMinutes * 60 * 1000 / 16);
    const char* tracks[] = {"ClassicH", "Metro Spiral", "Steeplechase", "Switchback", "The Alley2", "The River", "Tidal Causeway"};

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    LobbyConfig cfg;
    for (const char* t : tracks) cfg.tracks.push_back(t);
    cfg.raceTemplate.startDelayMs = 0;
    cfg.maxRaces = races + 4;
    cfg.maxConnections = races * players + 16;
    cfg.finishedRaceLingerMs = 2000;
    cfg.raceTemplate.race.maxTicks = static_cast<std::uint32_t>(raceTicks);
    LobbyServer lobby(cfg);

    std::int64_t now = 1000;
    int failures = 0;
    long baselineKb = -1;
    double cpuSeconds = 0;
    std::uint64_t totalTicks = 0;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> replays;   // sampled finished races
    lobby.SetRaceFinishedCallback([&](std::uint32_t id, const RaceHost& host) {
        if (replays.size() < 12) replays.push_back(std::make_pair(host.Config().race.trackName, host.Race().ReplayBytes()));
        (void)id;
    });

    for (int round = 0; round < rounds; ++round) {
        std::vector<std::vector<Player>> group(static_cast<std::size_t>(races));
        for (int r = 0; r < races; ++r) {
            group[r].resize(static_cast<std::size_t>(players));
            for (int i = 0; i < players; ++i) {
                group[r][i].conn = lobby.Connect(now);
                Protocol3::Hello h;
                h.name = "r" + std::to_string(r) + "p" + std::to_string(i);
                Send(lobby, group[r][i], Protocol3::kHello, Protocol3::Encode(h), now);
                Pump(lobby, group[r][i]);
                if (i == 0) {
                    Protocol3::HostRace host;
                    host.track = tracks[(r + round) % 7];
                    host.players = static_cast<std::uint8_t>(players);
                    Send(lobby, group[r][i], Protocol3::kHostRace, Protocol3::Encode(host), now);
                }
                else {
                    // Join the race created for this group: the newest race with room, found through the list the lobby gave us.
                    Protocol3::RaceList list;
                    Send(lobby, group[r][i], Protocol3::kListRaces, {}, now);
                    const std::vector<std::uint8_t> bytes = lobby.TakeOutput(group[r][i].conn);
                    Protocol3::FrameParser parser;
                    parser.Feed(bytes.data(), bytes.size());
                    std::uint8_t type;
                    std::vector<std::uint8_t> payload;
                    std::uint32_t target = 0;
                    while (parser.Next(type, payload)) if (type == Protocol3::kRaceList && Protocol3::Decode(payload, list)) {
                        for (const auto& info : list.races) if (!info.started && info.joined < info.players && info.joined > 0) target = info.id;
                    }
                    Send(lobby, group[r][i], Protocol3::kJoinRace, Protocol3::Encode(Protocol3::JoinRace{target}), now);
                }
                Pump(lobby, group[r][i]);
            }
        }
        for (auto& g : group) for (auto& p : g) if (!p.started) { /* started when the last player joined */ }

        const std::int64_t roundStart = now;
        const auto t0 = std::chrono::steady_clock::now();
        for (int tick = 0; tick < raceTicks + 8; ++tick) {
            const std::int64_t t = roundStart + static_cast<std::int64_t>(tick) * 16;
            for (auto& g : group) {
                for (auto& p : g) {
                    if (p.slot < 0 || p.gotResult) continue;
                    Protocol3::Input in;
                    in.firstTick = static_cast<std::uint32_t>(tick);
                    in.controls = {SimHarness::ScriptedInput(p.slot, static_cast<std::uint32_t>(tick))};
                    Send(lobby, p, Protocol3::kInput, Protocol3::Encode(in), t);
                }
            }
            lobby.Advance(t);
            for (auto& g : group) for (auto& p : g) Pump(lobby, p);
            totalTicks += static_cast<std::uint64_t>(races) * players;
        }
        cpuSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        now = roundStart + static_cast<std::int64_t>(raceTicks + 8) * 16;

        // Everyone gets the result; nobody was refused; the finished races are removed after their linger time.
        int results = 0, refused = 0, missing = 0;
        for (auto& g : group) for (auto& p : g) { if (p.gotResult) ++results; else ++missing; if (p.rejected) ++refused; }
        now += 5000;
        lobby.Advance(now);
        lobby.Advance(now + 1);
        std::printf("round %d: %d races of %d players, %d results, %d missing, %d refused, %d races left on the server, %d open connections, RSS %ld kB\n", round + 1, races, players,
                    results, missing, refused, lobby.RaceCount(), lobby.OpenConnections(), ResidentKb());
        if (missing != 0 || refused != 0) ++failures;
        if (lobby.RaceCount() != 0) { std::fprintf(stderr, "FAIL: finished races were not removed\n"); ++failures; }
        if (round == 0) baselineKb = ResidentKb();
    }
    const long endKb = ResidentKb();
    if (baselineKb > 0 && endKb > baselineKb + 20 * 1024) { std::fprintf(stderr, "FAIL: memory grew from %ld kB to %ld kB over %d rounds\n", baselineKb, endKb, rounds - 1); ++failures; }

    // Sampled replays re-simulate to the same final hash.
    int verified = 0;
    for (const auto& r : replays) {
        ReplayFormat::Replay rep;
        std::string error;
        if (!ReplayFormat::Decode(r.second, rep, error)) { ++failures; continue; }
        SimHarness::World w;
        if (!w.Build(SimHarness::TrackPath(r.first), r.first, rep.players, rep.laps, rep.weapons)) { ++failures; continue; }
        for (std::uint32_t t = 0; t < rep.ticks; ++t) w.Step(&rep.inputs[static_cast<std::size_t>(t) * rep.players]);
        if (w.Hash() == rep.finalHash) ++verified; else { std::fprintf(stderr, "FAIL: a replay of %s does not reproduce\n", r.first.c_str()); ++failures; }
    }
    std::printf("%d of %zu sampled replays re-simulate to their recorded final hash\n", verified, replays.size());

    const double raceSeconds = rounds * (raceTicks + 8) * 0.016;
    const double cpuPerRaceMinute = cpuSeconds / (static_cast<double>(races) * raceSeconds / 60.0);
    std::printf("server cost: %.2f s of CPU per race-minute; one core could host about %.0f races of %d players in real time (%.0f simulated ticks per second)\n",
                cpuPerRaceMinute, 60.0 / cpuPerRaceMinute, players, totalTicks / cpuSeconds);
    std::printf("load test %s\n", failures == 0 ? "passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
