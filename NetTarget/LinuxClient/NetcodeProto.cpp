// Stage 0 netcode prototype: two clients race through a simulated relay with latency, jitter, loss and
// reordering, using input prediction and rollback on the real, deterministic simulation.
//
//   HoverNetNetcodeProto <track> <ticks> <rtt-ms> <loss-percent> [--jitter=<ms>] [--input-delay=<ticks>] [--seed=<n>]
//
// Each client simulates both craft in its own MR_GameSession. It applies its own input at once, predicts the
// other player's input as "same as last known", and when the real input arrives and differs from the prediction
// it rolls back and re-simulates. There is no snapshot facility in the simulation yet (see
// docs/adr/0001-simulation-core.md), so a rollback rebuilds the session and replays from tick 0; the program
// measures what that costs, which is the answer to the ADR's open question.
//
// The network is the model the real topology has: client -> RaceServer -> client, so the delay between two
// players is one full round trip. Every packet carries the last kWindow inputs, so isolated loss costs nothing.
// The run passes when both clients, after the network drains, reach exactly the same state as an ideal run that
// had every input on time, and it prints the prediction error a player would have seen along the way.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "../MainCharacter/StdAfx.h"
#define protected public
#define private public
#include "../MainCharacter/MainCharacter.h"
#undef private
#undef protected
#include "../Model/GameSession.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"

#ifdef __EMSCRIPTEN__
void HoverNetInitializeWebFactory();
#endif

namespace
{
constexpr int kFrameMs = 16;
constexpr int kWindow = 16;  // inputs carried by every packet

struct Rng
{
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ULL + 1442695040888963407ULL) {}
    std::uint32_t Next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return static_cast<std::uint32_t>((s * 2685821657736338717ULL) >> 32);
    }
    double Unit() { return Next() / 4294967296.0; }
};

// Each player's input is a pure function of the tick, with changes every few dozen ticks so that
// prediction ("same as last time") is wrong often enough to matter.
int ScriptedInput(int player, int tick)
{
    int c = MR_MainCharacter::eMotorOn;
    const int period = player == 0 ? 37 : 53;
    const int phase = (tick / period) % 3;
    if (phase == 0) c |= MR_MainCharacter::eLeft;
    else if (phase == 1) c |= MR_MainCharacter::eRight;
    if (tick % (player == 0 ? 190 : 230) >= 150 && tick % (player == 0 ? 190 : 230) < 160) c |= MR_MainCharacter::eJump;
    if (tick % (player == 0 ? 70 : 90) == 30) c |= MR_MainCharacter::eFire;
    return c;
}

// One simulated world with two craft.
struct World
{
    std::unique_ptr<MR_GameSession> session;
    MR_MainCharacter* craft[2] = {nullptr, nullptr};
    int tick = 0;

    bool Build(const std::string& path, const std::string& name)
    {
        MR_RecordFile* track = new MR_RecordFile;
        if (!track->OpenForRead(path.c_str())) { delete track; return false; }
        session.reset(new MR_GameSession(FALSE));
        if (!session->LoadNew(name.c_str(), track) || session->GetCurrentLevel() == nullptr) return false;
        MR_Level* level = session->GetCurrentLevel();
        if (level->GetPlayerCount() < 2) return false;
        for (int i = 0; i < 2; ++i) {
            const int room = level->GetStartingRoom(i);
            craft[i] = MR_MainCharacter::New(5, TRUE);
            if (craft[i] == nullptr || room < 0 || room >= level->GetRoomCount()) return false;
            craft[i]->mPosition = level->GetStartingPos(i);
            craft[i]->SetOrientation(level->GetStartingOrientation(i));
            craft[i]->mRoom = room;
            if (level->InsertElement(craft[i], room, FALSE) == nullptr) return false;
        }
        session->SetSimulationTime(0);
        tick = 0;
        return true;
    }

    void Step(int input0, int input1)
    {
        craft[0]->SetControlState(input0, session->GetSimulationTime());
        craft[1]->SetControlState(input1, session->GetSimulationTime());
        session->SimulateStep(kFrameMs);
        ++tick;
    }

    std::uint64_t Hash() const
    {
        std::uint64_t h = 1469598103934665603ULL;
        auto mix = [&h](std::uint64_t v) {
            for (int b = 0; b < 8; ++b) { h ^= (v >> (b * 8)) & 0xFF; h *= 1099511628211ULL; }
        };
        for (const MR_MainCharacter* c : craft) {
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(c->mPosition.mX)));
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(c->mPosition.mY)));
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(c->mPosition.mZ)));
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(c->mRoom)));
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(c->mOrientation)));
            std::uint64_t bits;
            std::memcpy(&bits, &c->mXSpeed, 8); mix(bits);
            std::memcpy(&bits, &c->mYSpeed, 8); mix(bits);
            std::memcpy(&bits, &c->mZSpeed, 8); mix(bits);
        }
        return h;
    }
};

struct Packet
{
    int arriveMs;
    int fromTick;                 // tick of the newest input in the packet
    int inputs[kWindow];          // inputs for fromTick-kWindow+1 .. fromTick
};

struct Client
{
    int me = 0;                   // which craft is this player's
    std::vector<int> local;       // actual local inputs by tick
    std::vector<int> known;       // remote inputs received, -1 if unknown
    std::vector<int> used;        // remote inputs the simulation used (known or predicted)
    std::deque<Packet> inbox;
    World world;
    int lastKnownTick = -1;
    int rollbacks = 0;
    long long resimulatedTicks = 0;
    int maxDepth = 0;
    double rebuildMs = 0;
};

double NowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <track> <ticks> <rtt-ms> <loss-percent> [--jitter=ms] [--input-delay=ticks] [--seed=n]\n", argv[0]);
        return 2;
    }
    const std::string trackName = argv[1];
    const int ticks = std::atoi(argv[2]);
    const int rtt = std::atoi(argv[3]);
    const double loss = std::atof(argv[4]) / 100.0;
    int jitter = 0, inputDelay = 0;
    std::uint64_t seed = 1;
    for (int i = 5; i < argc; ++i) {
        if (std::strncmp(argv[i], "--jitter=", 9) == 0) jitter = std::atoi(argv[i] + 9);
        else if (std::strncmp(argv[i], "--input-delay=", 14) == 0) inputDelay = std::atoi(argv[i] + 14);
        else if (std::strncmp(argv[i], "--seed=", 7) == 0) seed = std::strtoull(argv[i] + 7, nullptr, 10);
    }

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
#ifdef __EMSCRIPTEN__
    HoverNetInitializeWebFactory();
#endif
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    const bool isPath = trackName.find('/') != std::string::npos;
    const std::string path = isPath ? trackName : "NetTarget/Tracks/" + trackName + ".trk";

    // Inputs by tick: what each player does, shifted by the local input delay.
    std::vector<int> truth[2];
    for (int p = 0; p < 2; ++p) {
        truth[p].resize(ticks);
        for (int t = 0; t < ticks; ++t) truth[p][t] = t < inputDelay ? MR_MainCharacter::eMotorOn : ScriptedInput(p, t - inputDelay);
    }

    // The ideal run: every input on time. Keep the remote craft's position at every tick to measure error.
    std::vector<std::int64_t> idealX[2], idealY[2];
    std::uint64_t idealHash = 0;
    {
        World ideal;
        if (!ideal.Build(path, trackName)) { std::fprintf(stderr, "Could not build %s (needs two start slots)\n", path.c_str()); return 1; }
        for (int t = 0; t < ticks; ++t) {
            ideal.Step(truth[0][t], truth[1][t]);
            for (int p = 0; p < 2; ++p) {
                idealX[p].push_back(ideal.craft[p]->mPosition.mX);
                idealY[p].push_back(ideal.craft[p]->mPosition.mY);
            }
        }
        idealHash = ideal.Hash();
    }

    Rng rng(seed);
    Client clients[2];
    for (int p = 0; p < 2; ++p) {
        clients[p].me = p;
        clients[p].local = truth[p];
        clients[p].known.assign(ticks, -1);
        clients[p].used.assign(ticks, 0);
        if (!clients[p].world.Build(path, trackName)) return 1;
    }

    const int oneWayMs = rtt;  // client -> RaceServer -> client is one full client round trip
    long long packetsSent = 0, packetsLost = 0;
    double errSum = 0, errMax = 0;
    long long errSamples = 0;

    auto predictRemote = [&](Client& c, int t) {
        // The newest known input at or before t, else the player's neutral "motor on".
        for (int s = std::min(t, ticks - 1); s >= 0 && s >= t - 64; --s) if (c.known[s] >= 0) return c.known[s];
        return static_cast<int>(MR_MainCharacter::eMotorOn);
    };
    auto rebuildAndReplay = [&](Client& c, int upto) {
        const double start = NowMs();
        c.world = World();
        c.world.Build(path, trackName);
        for (int t = 0; t < upto; ++t) {
            const int remote = c.known[t] >= 0 ? c.known[t] : predictRemote(c, t);
            c.used[t] = remote;
            if (c.me == 0) c.world.Step(c.local[t], remote); else c.world.Step(remote, c.local[t]);
        }
        c.rebuildMs += NowMs() - start;
        c.resimulatedTicks += upto;
    };

    for (int t = 0; t < ticks; ++t) {
        const int nowMs = t * kFrameMs;
        for (int p = 0; p < 2; ++p) {
            Client& c = clients[p];
            // 1. Packets that have arrived.
            int earliestWrong = -1;
            for (auto it = c.inbox.begin(); it != c.inbox.end();) {
                if (it->arriveMs > nowMs) { ++it; continue; }
                for (int k = 0; k < kWindow; ++k) {
                    const int s = it->fromTick - kWindow + 1 + k;
                    if (s < 0 || s >= ticks) continue;
                    if (c.known[s] < 0) {
                        c.known[s] = it->inputs[k];
                        if (s < c.world.tick && c.used[s] != c.known[s] && (earliestWrong < 0 || s < earliestWrong)) earliestWrong = s;
                    }
                }
                it = c.inbox.erase(it);
            }
            // 2. Rollback if a prediction already simulated was wrong.
            if (earliestWrong >= 0) {
                ++c.rollbacks;
                c.maxDepth = std::max(c.maxDepth, c.world.tick - earliestWrong);
                rebuildAndReplay(c, c.world.tick);
            }
            // 3. Send this tick's local input (with the recent history) to the other player.
            // With an input delay, the input applied at tick t + delay is already sent now.
            Packet pkt;
            pkt.fromTick = std::min(t + inputDelay, ticks - 1);
            for (int k = 0; k < kWindow; ++k) {
                const int s = pkt.fromTick - kWindow + 1 + k;
                pkt.inputs[k] = s >= 0 ? c.local[s] : MR_MainCharacter::eMotorOn;
            }
            ++packetsSent;
            // Two hops, each lossy.
            if (rng.Unit() < loss || rng.Unit() < loss) {
                ++packetsLost;
            }
            else {
                const int j = jitter > 0 ? static_cast<int>(rng.Unit() * 2 * jitter) - jitter : 0;
                pkt.arriveMs = nowMs + std::max(0, oneWayMs + j);
                clients[1 - p].inbox.push_back(pkt);
            }
            // 4. Simulate this tick with local input and predicted remote input.
            const int remote = c.known[t] >= 0 ? c.known[t] : predictRemote(c, t);
            c.used[t] = remote;
            if (c.me == 0) c.world.Step(c.local[t], remote); else c.world.Step(remote, c.local[t]);
            // 5. What the player sees of the other craft vs where it really is at this tick.
            const int other = 1 - p;
            const double dx = static_cast<double>(c.world.craft[other]->mPosition.mX) - idealX[other][t];
            const double dy = static_cast<double>(c.world.craft[other]->mPosition.mY) - idealY[other][t];
            const double err = std::sqrt(dx * dx + dy * dy);
            errSum += err; errMax = std::max(errMax, err); ++errSamples;
        }
    }

    // Drain the network (everything still in flight arrives), then correct and compare with the ideal run.
    bool allMatch = true;
    for (int p = 0; p < 2; ++p) {
        Client& c = clients[p];
        bool changed = false;
        for (const Packet& pkt : c.inbox) {
            for (int k = 0; k < kWindow; ++k) {
                const int s = pkt.fromTick - kWindow + 1 + k;
                if (s >= 0 && s < ticks && c.known[s] < 0) { c.known[s] = pkt.inputs[k]; changed = true; }
            }
        }
        // Inputs lost beyond the window are the sender's reliable-resend case; model as delivered at the end.
        for (int s = 0; s < ticks; ++s) if (c.known[s] < 0) { c.known[s] = truth[1 - p][s]; changed = true; }
        bool differs = false;
        for (int s = 0; s < ticks; ++s) if (c.used[s] != c.known[s]) differs = true;
        if (changed && differs) { ++c.rollbacks; rebuildAndReplay(c, ticks); }
        allMatch = allMatch && c.world.Hash() == idealHash;
    }

    std::printf("%s ticks=%d rtt=%dms loss=%.1f%% jitter=%dms input-delay=%d seed=%llu\n", trackName.c_str(), ticks, rtt,
                loss * 100, jitter, inputDelay, static_cast<unsigned long long>(seed));
    std::printf("packets sent=%lld lost=%lld\n", packetsSent, packetsLost);
    for (int p = 0; p < 2; ++p) {
        const Client& c = clients[p];
        std::printf("client %d: rollbacks=%d deepest=%d ticks resimulated=%lld rebuild-time=%.1f ms (%.4f ms/resimulated tick)\n", p,
                    c.rollbacks, c.maxDepth, c.resimulatedTicks, c.rebuildMs,
                    c.resimulatedTicks ? c.rebuildMs / static_cast<double>(c.resimulatedTicks) : 0.0);
    }
    std::printf("remote craft error seen by the players: mean %.0f mm, max %.0f mm\n", errSamples ? errSum / errSamples : 0.0, errMax);
    std::printf("converged to the ideal run: %s\n", allMatch ? "yes" : "NO");
    return allMatch ? 0 : 1;
}
