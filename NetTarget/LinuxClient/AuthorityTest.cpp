// Tests of the server's authoritative race (NetTarget/Authority/AuthoritativeRace.*, protocol 3.0 draft).
//
//   HoverNetAuthorityTest <track> [players]
//
// 1. The race the server runs is exactly the race its own replay reproduces (replay bytes decode, re-simulate on the
//    plain harness, and every checkpoint and the final hash agree).
// 2. Input policy: late, too-far-ahead, wrong-player, replaced and masked inputs are handled as specified; a client
//    cannot change anything but its own control word for a tick that has not been simulated yet.
// 3. A missing input repeats the player's previous one.
// 4. A network of clients with latency, jitter and loss drives the server: the server's race is the race its replay says
//    (so it is the same race on every platform), and the number of inputs that arrived too late shrinks to zero as the
//    clients send further ahead.
// 5. A spectator joining mid-race from a server snapshot follows the race exactly.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/AuthoritativeRace.h"
#include "../Authority/ReplayFormat.h"
#include "../Model/StateSnapshot.h"
#include "../ObjFac1/SimElements.h"

using Authority::AuthoritativeRace;
using Authority::InputResult;
using Authority::RaceConfig;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

struct Rng
{
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ULL + 1442695040888963407ULL) {}
    std::uint32_t Next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return static_cast<std::uint32_t>((s * 2685821657736338717ULL) >> 32); }
    double Unit() { return Next() / 4294967296.0; }
};

RaceConfig Config(const std::string& track, int players)
{
    RaceConfig c;
    c.trackName = track;
    c.trackPath = SimHarness::TrackPath(track);
    c.players = players;
    c.laps = 3;
    c.weapons = true;
    return c;
}

// Re-simulates a replay on the plain harness and compares every checkpoint and the end.
bool ReplayReproduces(const std::vector<std::uint8_t>& bytes, const std::string& track)
{
    ReplayFormat::Replay r;
    std::string error;
    if (!ReplayFormat::Decode(bytes, r, error)) { std::fprintf(stderr, "replay decode: %s\n", error.c_str()); return false; }
    SimHarness::World w;
    if (!w.Build(SimHarness::TrackPath(track), track, r.players, r.laps, r.weapons)) return false;
    for (std::uint32_t t = 0; t < r.ticks; ++t) {
        w.Step(&r.inputs[static_cast<std::size_t>(t) * r.players]);
        if ((t + 1) % ReplayFormat::kCheckpointTicks == 0 && w.Hash() != r.checkpoints[(t + 1) / ReplayFormat::kCheckpointTicks - 1]) return false;
    }
    return w.Hash() == r.finalHash;
}

// A client's intended input for a tick (the same scripts the other tools use).
std::uint16_t Intended(int player, std::uint32_t tick) { return SimHarness::ScriptedInput(player, tick); }

struct NetResult { int accepted = 0, late = 0, lost = 0; std::uint64_t hash = 0; std::vector<std::uint8_t> replay; };

// Clients send, every tick t, the inputs for ticks up to t + ahead (the last 16 of them, so loss is covered). Each packet
// takes oneWayMs plus jitter to reach the server and may be lost. The server simulates a tick every 16 ms.
NetResult RunNetwork(const std::string& track, int players, int ticks, int oneWayMs, int jitterMs, double loss, int ahead, std::uint64_t seed)
{
    struct Packet { int arriveMs; int player; std::uint32_t fromTick; };
    std::vector<Packet> inFlight;
    Rng rng(seed);
    AuthoritativeRace race;
    std::string error;
    NetResult result;
    if (!race.Start(Config(track, players), error)) { std::fprintf(stderr, "%s\n", error.c_str()); ++gFailures; return result; }
    for (int t = 0; t < ticks; ++t) {
        const int nowMs = t * 16;
        for (int p = 0; p < players; ++p) {
            if (rng.Unit() < loss) { ++result.lost; continue; }
            const int j = jitterMs > 0 ? static_cast<int>(rng.Unit() * 2 * jitterMs) - jitterMs : 0;
            inFlight.push_back({nowMs + (oneWayMs + j > 0 ? oneWayMs + j : 0), p, static_cast<std::uint32_t>(t + ahead)});
        }
        for (std::size_t i = 0; i < inFlight.size();) {
            if (inFlight[i].arriveMs > nowMs) { ++i; continue; }
            const Packet pk = inFlight[i];
            inFlight.erase(inFlight.begin() + static_cast<std::ptrdiff_t>(i));
            for (int k = 0; k < 16; ++k) {
                const long tick = static_cast<long>(pk.fromTick) - 15 + k;
                if (tick < 0) continue;
                const InputResult r = race.SubmitInput(pk.player, static_cast<std::uint32_t>(tick), Intended(pk.player, static_cast<std::uint32_t>(tick)));
                if (r == InputResult::Accepted) ++result.accepted;
                else if (r == InputResult::Late && k == 15) ++result.late;   // count only a packet's newest input
            }
        }
        race.AdvanceTick();
    }
    result.hash = race.StateHash();
    result.replay = race.ReplayBytes();
    return result;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <track> [players]\n", argv[0]); return 2; }
    const std::string track = argv[1];
    const int players = argc > 2 ? std::atoi(argv[2]) : 2;
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    std::string error;
    // ---- 1. the server's race is its replay -------------------------------------------------------------------
    {
        AuthoritativeRace race;
        Check(race.Start(Config(track, players), error), "start the race");
        for (std::uint32_t t = 0; t < 1200; ++t) {
            for (int p = 0; p < players; ++p) race.SubmitInput(p, t, Intended(p, t));
            race.AdvanceTick();
        }
        Check(race.CurrentTick() == 1200, "ticks advanced");
        Check(ReplayReproduces(race.ReplayBytes(), track), "the replay reproduces the race the server ran");
        const std::vector<Authority::PlayerResult> results = race.Results();
        Check(static_cast<int>(results.size()) == players, "one result per player");
        int rankSum = 0;
        for (const auto& r : results) rankSum += r.rank;
        Check(rankSum == players * (players + 1) / 2, "ranks are 1..n");
    }

    // ---- 2. input policy and tampering -------------------------------------------------------------------------
    {
        AuthoritativeRace race;
        Check(race.Start(Config(track, players), error), "start the race (policy)");
        for (int i = 0; i < 100; ++i) race.AdvanceTick();
        Check(race.SubmitInput(0, 50, 1) == InputResult::Late, "an input for a tick already simulated is late");
        Check(race.SubmitInput(0, 100, 1) == InputResult::Accepted, "an input for the current tick is accepted");
        Check(race.SubmitInput(0, 100, 3) == InputResult::Replaced, "a second input for the same tick replaces the first");
        Check(race.SubmitInput(0, 100 + 65, 1) == InputResult::TooFarAhead, "an input too far ahead is refused");
        Check(race.SubmitInput(players, 100, 1) == InputResult::BadPlayer, "a player that does not exist is refused");
        Check(race.SubmitInput(-1, 100, 1) == InputResult::BadPlayer, "a negative player is refused");
        race.SubmitInput(0, 101, 0xFFFF);   // every bit set, including ones the simulation does not define
        race.AdvanceTick();
        race.AdvanceTick();
        const std::vector<std::uint8_t> bytes = race.ReplayBytes();
        ReplayFormat::Replay r;
        Check(ReplayFormat::Decode(bytes, r, error), "replay decodes (policy)");
        Check(r.inputs[static_cast<std::size_t>(101) * r.players] == (0xFFFF & AuthoritativeRace::kValidControlBits), "undefined control bits are masked off");
        Check(r.inputs[static_cast<std::size_t>(100) * r.players] == 3, "the replacing input is the one simulated");
        Check(ReplayReproduces(bytes, track), "a race with policy edge cases still reproduces");
    }

    // ---- 3. missing input repeats the previous one --------------------------------------------------------------
    {
        AuthoritativeRace race;
        Check(race.Start(Config(track, players), error), "start the race (repeat)");
        race.SubmitInput(0, 0, MR_MainCharacter::eMotorOn | MR_MainCharacter::eLeft);
        for (int i = 0; i < 40; ++i) race.AdvanceTick();
        ReplayFormat::Replay r;
        Check(ReplayFormat::Decode(race.ReplayBytes(), r, error), "replay decodes (repeat)");
        bool repeated = true;
        for (std::uint32_t t = 0; t < 40; ++t) repeated = repeated && r.inputs[static_cast<std::size_t>(t) * r.players] == (MR_MainCharacter::eMotorOn | MR_MainCharacter::eLeft);
        Check(repeated, "a player with no new input keeps their previous one");
    }

    // ---- 4. clients across a lossy, delayed network ---------------------------------------------------------------
    {
        const int ticks = 1000;
        const NetResult a = RunNetwork(track, players, ticks, 60, 10, 0.02, 6, 1);     // 120 ms round trip, 2% loss
        Check(ReplayReproduces(a.replay, track), "120 ms / 2% loss: the server's race is its replay");
        const NetResult b = RunNetwork(track, players, ticks, 125, 25, 0.05, 12, 2);   // 250 ms round trip, 5% loss
        Check(ReplayReproduces(b.replay, track), "250 ms / 5% loss: the server's race is its replay");
        // Sending further ahead removes late inputs.
        const NetResult tight = RunNetwork(track, players, ticks, 125, 25, 0.0, 1, 3);
        const NetResult roomy = RunNetwork(track, players, ticks, 125, 25, 0.0, 14, 3);
        Check(tight.late > 0, "sending only 1 tick ahead over a 250 ms path makes inputs late");
        Check(roomy.late == 0, "sending 14 ticks ahead over a 250 ms path leaves no input late");
        // With enough lead and no loss, the server's race equals the ideal race where every input arrived on time.
        const NetResult ideal = RunNetwork(track, players, ticks, 0, 0, 0.0, 20, 4);
        const NetResult delayed = RunNetwork(track, players, ticks, 60, 0, 0.0, 20, 4);
        Check(ideal.hash == delayed.hash, "with enough lead the server's race does not depend on the network delay");
        std::fprintf(stderr, "  inputs late: 250 ms path sending 1 tick ahead %d, 14 ticks ahead %d; ideal race hash %016llx\n", tight.late, roomy.late,
                     static_cast<unsigned long long>(ideal.hash));
    }

    // ---- 5. a spectator joins from a snapshot ---------------------------------------------------------------------
    {
        AuthoritativeRace race;
        Check(race.Start(Config(track, players), error), "start the race (spectator)");
        for (std::uint32_t t = 0; t < 800; ++t) {
            for (int p = 0; p < players; ++p) race.SubmitInput(p, t, Intended(p, t));
            race.AdvanceTick();
        }
        const std::vector<std::uint8_t> snapshot = race.Snapshot();
        // The spectator has its own copy of the race (craft driven by the server's inputs) and restores the snapshot.
        SimHarness::World spectator;
        Check(spectator.Build(SimHarness::TrackPath(track), track, players, 3, true), "build the spectator's race");
        std::vector<MR_FreeElement*> external(spectator.craft.begin(), spectator.craft.end());
        std::int64_t time = 0;
        Check(MR_RestoreLevelState(spectator.session->GetCurrentLevel(), snapshot.data(), snapshot.size(), external, ReplayFormat::kSimulationRules, time, error), "the spectator restores the snapshot");
        spectator.session->SetSimulationTime(static_cast<MR_SimulationTime>(time));
        Check(spectator.Hash() == race.StateHash(), "the spectator's state equals the server's at the snapshot");
        bool follows = true;
        for (std::uint32_t t = 800; t < 1200 && follows; ++t) {
            std::vector<std::uint16_t> in(players);
            for (int p = 0; p < players; ++p) { in[p] = Intended(p, t); race.SubmitInput(p, t, in[p]); }
            race.AdvanceTick();
            spectator.Step(in.data());
            follows = spectator.Hash() == race.StateHash();
        }
        Check(follows, "the spectator follows the server's race tick for tick");
    }

    std::printf("%s %d players: %s\n", track.c_str(), players, gFailures == 0 ? "authority tests passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
