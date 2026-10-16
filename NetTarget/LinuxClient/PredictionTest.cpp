// The predicting client against a live authoritative host over a delayed in-memory network (docs/authoritative-race.md).
//
//   HoverNetPredictionTest <track> [players] [one-way-ms]
//
// Player 0 is a PredictingClient: exact state from the server's Tick stream (every hash checked), and a prediction run ahead
// to the tick on screen from the newest confirmed snapshot. Every frame its predicted positions are recorded and, once the
// server reaches that tick, compared with the truth; the same is done for the alternative of drawing the newest confirmed state
// as it is (which lags by the one-way delay plus the input lead).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/PredictingClient.h"
#include "../Authority/RaceHost.h"
#include "../MainCharacter/MainCharacter.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
struct Pos { double x, y; };
double Dist(const Pos& a, const Pos& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

struct Stat
{
    double sum = 0, max = 0;
    long n = 0;
    void Add(double v) { sum += v; if (v > max) max = v; ++n; }
    double Mean() const { return n ? sum / static_cast<double>(n) : 0; }
};

struct Timed { std::int64_t at; std::vector<std::uint8_t> bytes; };
struct Frame { std::uint32_t tick; std::vector<Pos> predicted, stale; };
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <track> [players] [one-way-ms]\n", argv[0]); return 2; }
    const std::string track = argv[1];
    const int players = argc > 2 ? std::atoi(argv[2]) : 8;
    const int oneWay = argc > 3 ? std::atoi(argv[3]) : 60;
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    HostConfig cfg;
    cfg.race.trackName = track;
    cfg.race.trackPath = SimHarness::TrackPath(track);
    cfg.race.players = players;
    cfg.startDelayMs = 0;
    RaceHost host(cfg);
    std::string error;
    if (!host.Init(error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }

    std::vector<int> conns;
    for (int i = 0; i < players; ++i) {
        conns.push_back(host.Connect(0));
        Protocol3::Hello h;
        h.name = "p" + std::to_string(i);
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kHello, Protocol3::Encode(h));
        host.Receive(conns[i], f.data(), f.size(), 0);
    }

    PredictingClient client;
    Protocol3::FrameParser parser;
    std::deque<Timed> toClient;
    std::vector<std::deque<Timed>> toServer(static_cast<std::size_t>(players));
    std::vector<std::int64_t> lastSend(static_cast<std::size_t>(players), -1000);
    std::vector<std::vector<Pos>> truth;
    std::vector<Frame> frames;
    Stat ownPred, otherPred, ownStale, otherStale;
    bool inited = false, started = false;
    std::uint32_t confirmed = 0, failedTicks = 0;
    std::int64_t lastFrame = -1000;
    const int inputLead = 2 * oneWay / 16 + 3;   // ticks ahead of the confirmed tick that an input is aimed at
    auto positions = [&](const RaceWorld& w) {
        std::vector<Pos> p;
        for (int i = 0; i < players; ++i) p.push_back({static_cast<double>(w.Craft(i)->mPosition.mX), static_cast<double>(w.Craft(i)->mPosition.mY)});
        return p;
    };

    const std::int64_t endAt = 16 * 1500 + 2000;
    truth.push_back(std::vector<Pos>(static_cast<std::size_t>(players), Pos{0, 0}));   // truth[t] = positions after t ticks
    for (std::int64_t now = 0; now < endAt; now += 4) {
        // 1. deliver to the server, advance it, record the truth
        for (int i = 0; i < players; ++i) {
            while (!toServer[i].empty() && toServer[i].front().at <= now) {
                host.Receive(conns[i], toServer[i].front().bytes.data(), toServer[i].front().bytes.size(), now);
                toServer[i].pop_front();
            }
        }
        host.Advance(now);
        while (truth.size() <= host.Race().CurrentTick()) {
            std::vector<Pos> p;
            for (int i = 0; i < players; ++i) p.push_back({static_cast<double>(host.Race().Craft(i)->mPosition.mX), static_cast<double>(host.Race().Craft(i)->mPosition.mY)});
            truth.push_back(p);
        }
        // 2. server output: to the predicting client with delay; the others' output is not needed
        for (int i = 0; i < players; ++i) {
            const std::vector<std::uint8_t> out = host.TakeOutput(conns[i]);
            if (i == 0 && !out.empty()) toClient.push_back({now + oneWay, out});
        }
        // 3. the client receives
        while (!toClient.empty() && toClient.front().at <= now) {
            parser.Feed(toClient.front().bytes.data(), toClient.front().bytes.size());
            toClient.pop_front();
            std::uint8_t type;
            std::vector<std::uint8_t> payload;
            while (parser.Next(type, payload)) {
                if (type == Protocol3::kHelloAck) {
                    Protocol3::HelloAck ack;
                    if (Protocol3::Decode(payload, ack)) inited = client.Init(cfg.race.trackPath, ack, error);
                }
                else if (type == Protocol3::kStart) started = true;
                else if (type == Protocol3::kTick && inited) {
                    Protocol3::Tick t;
                    if (!Protocol3::Decode(payload, t) || !client.OnTick(t)) ++failedTicks;
                    else ++confirmed;
                }
            }
        }
        // 4. every 16 ms the client picks its input, sends it, and draws a frame
        if (inited && started && now - lastFrame >= 16) {
            lastFrame = now;
            const std::uint32_t renderTick = client.ConfirmedTick() + static_cast<std::uint32_t>(inputLead);
            const std::uint16_t input = SimHarness::ScriptedInput(client.Player(), renderTick);
            client.SetLocalInput(renderTick, input);
            Protocol3::Input in;
            in.firstTick = renderTick >= 15 ? renderTick - 15 : 0;
            for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(SimHarness::ScriptedInput(client.Player(), in.firstTick + k));
            toServer[0].push_back({now + oneWay, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in))});
            Frame f;
            f.tick = renderTick;
            f.predicted = positions(client.Predict(renderTick));
            f.stale = positions(client.Confirmed());
            frames.push_back(f);
        }
        // The other players are plain scripted clients.
        for (int i = 1; i < players; ++i) {
            if (!started || now - lastSend[i] < 16) continue;
            lastSend[i] = now;
            const std::uint32_t target = host.Race().CurrentTick() + static_cast<std::uint32_t>(inputLead);
            Protocol3::Input in;
            in.firstTick = target >= 15 ? target - 15 : 0;
            for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(SimHarness::ScriptedInput(i, in.firstTick + k));
            toServer[i].push_back({now + oneWay, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in))});
        }
    }

    // Compare each frame's prediction with where the server's craft really were at that tick.
    for (const Frame& f : frames) {
        if (f.tick >= truth.size() || f.tick < 20) continue;
        for (int i = 0; i < players; ++i) {
            const double p = Dist(f.predicted[i], truth[f.tick][i]);
            const double s = Dist(f.stale[i], truth[f.tick][i]);
            if (i == 0) { ownPred.Add(p); ownStale.Add(s); } else { otherPred.Add(p); otherStale.Add(s); }
        }
    }
    std::printf("%s, %d players, %d ms each way: confirmed %u ticks, %u hash failures\n", track.c_str(), players, oneWay, confirmed, failedTicks);
    std::printf("  own craft:    predicted error mean %.0f mm (max %.0f); drawing the confirmed state instead: mean %.0f mm (max %.0f)\n", ownPred.Mean(), ownPred.max, ownStale.Mean(), ownStale.max);
    std::printf("  other craft:  predicted error mean %.0f mm (max %.0f); drawing the confirmed state instead: mean %.0f mm (max %.0f)\n", otherPred.Mean(), otherPred.max, otherStale.Mean(), otherStale.max);
    std::printf("  predictions rebuilt: %llu\n", static_cast<unsigned long long>(client.PredictionsRebuilt()));

    bool ok = failedTicks == 0 && confirmed > 1000;
    ok = ok && ownPred.Mean() < ownStale.Mean() && otherPred.Mean() < otherStale.Mean();
    std::printf("%s\n", ok ? "prediction test passed" : "prediction test FAILED");
    return ok ? 0 : 1;
}
