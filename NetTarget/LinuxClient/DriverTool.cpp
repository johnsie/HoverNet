// HoverNetDriver: finds, by search, a sequence of controls that drives one craft around a track and across the finish line, and writes
// it as a verifiable replay (docs/replays.md). It exists so the replay tools can be tested with real finishers: the scripted inputs of
// the other tests never complete a lap. The search is a go-explore style beam over the real simulation (state snapshots, so every
// candidate is the true physics): states are ranked by progress (laps, then the two checkpoints) and, within a stage, only craft in
// cells of the track not seen before at that stage are kept, so the beam spreads out until it finds the next checkpoint.
//
//   HoverNetDriver <track> <out.replay> [--laps=N] [--seed=N] [--hold=N] [--width=N] [--max-ticks=N]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../MainCharacter/StdAfx.h"
#define protected public
#define private public
#include "../MainCharacter/MainCharacter.h"
#undef private
#undef protected
#include "SimHarness.h"
#include "../Authority/AuthoritativeRace.h"
#include "../Authority/RaceWorld.h"
#include "../ObjFac1/SimElements.h"

using Authority::AuthoritativeRace;
using Authority::RaceConfig;
using Authority::RaceWorld;

namespace
{
struct Rng
{
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 2685821657736338717ULL + 1442695040888963407ULL) {}
    std::uint32_t Next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return static_cast<std::uint32_t>((s * 2685821657736338717ULL) >> 32); }
};

struct Node
{
    std::vector<std::uint8_t> snapshot;
    std::vector<std::uint16_t> inputs;   // one per tick since the start
    int stage = 0;
    long cell = 0;
};

int Stage(const MR_MainCharacter* c)
{
    return c->mLapCount * 3 + (c->mCheckPoint1 ? 1 : 0) + (c->mCheckPoint2 ? 1 : 0);
}

long Cell(const MR_MainCharacter* c)
{
    const long cx = c->mPosition.mX / 2500, cy = c->mPosition.mY / 2500;
    return ((cx + 100000) * 200000L + (cy + 100000)) * 64 + (c->mRoom & 63);
}
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <track> <out.replay> [--laps=N] [--seed=N] [--hold=N] [--width=N] [--max-ticks=N]\n", argv[0]); return 2; }
    const std::string track = argv[1], out = argv[2];
    int laps = 1;
    std::uint64_t seed = 1;
    long maxTicks = 40000;
    int kHold = 12, kWidth = 150;
    for (int i = 3; i < argc; ++i) {
        if (!std::strncmp(argv[i], "--laps=", 7)) laps = std::atoi(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--seed=", 7)) seed = std::strtoull(argv[i] + 7, nullptr, 10);
        else if (!std::strncmp(argv[i], "--hold=", 7)) kHold = std::atoi(argv[i] + 7);
        else if (!std::strncmp(argv[i], "--width=", 8)) kWidth = std::atoi(argv[i] + 8);
        else if (!std::strncmp(argv[i], "--max-ticks=", 12)) maxTicks = std::atol(argv[i] + 12);
    }
    if (laps < 1 || laps > 5 || kHold < 1 || kHold > 60 || kWidth < 1 || kWidth > 1000) { std::fprintf(stderr, "bad arguments\n"); return 2; }
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();

        const std::uint16_t options[] = {
        MR_MainCharacter::eMotorOn,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eLeft,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eRight,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eStraffleLeft,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eStraffleRight,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eBreakDirection | MR_MainCharacter::eLeft,
        MR_MainCharacter::eMotorOn | MR_MainCharacter::eBreakDirection | MR_MainCharacter::eRight,
    };
    const int nOptions = static_cast<int>(sizeof(options) / sizeof(options[0]));

    std::string error;
    RaceWorld world;
    if (!world.Build(SimHarness::TrackPath(track), track, 1, laps, false, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }

    Node root;
    root.snapshot = world.Snapshot();
    std::vector<Node> beam(1, root);
    std::set<long> visited;
    int best = 0;
    Rng rng(seed);
    std::vector<std::uint16_t> solution;

    for (long tick = 0; tick < maxTicks && solution.empty(); tick += kHold) {
        std::vector<Node> next;
        for (const Node& n : beam) {
            for (int o = 0; o < nOptions && solution.empty(); ++o) {
                if (!world.Restore(n.snapshot, error)) { std::fprintf(stderr, "restore: %s\n", error.c_str()); return 1; }
                Node child;
                child.inputs = n.inputs;
                for (int k = 0; k < kHold; ++k) {
                    const std::uint16_t c = options[o];
                    world.Step(&c);
                    child.inputs.push_back(c);
                }
                const MR_MainCharacter* craft = world.Craft(0);
                child.stage = Stage(craft);
                child.cell = Cell(craft);
                if (craft->mLapCount >= laps) { solution = child.inputs; break; }
                if (child.stage < best) continue;
                child.snapshot = world.Snapshot();
                next.push_back(std::move(child));
            }
            if (!solution.empty()) break;
        }
        if (!solution.empty() || next.empty()) break;
        int top = best;
        for (const Node& n : next) top = std::max(top, n.stage);
        if (top > best) { best = top; visited.clear(); std::fprintf(stderr, "tick %ld: stage %d\n", tick + kHold, best); }
        for (std::size_t i = next.size(); i > 1; --i) std::swap(next[i - 1], next[rng.Next() % i]);
        std::vector<Node> keep;
        for (Node& n : next) {
            if (n.stage != best) continue;
            if (!visited.insert(n.cell).second) continue;
            keep.push_back(std::move(n));
            if (static_cast<int>(keep.size()) >= kWidth) break;
        }
        if (keep.empty()) {
            // Everything this step lands in known cells: keep a few at random so the search does not die in a dead end.
            for (Node& n : next) { if (n.stage == best) keep.push_back(std::move(n)); if (keep.size() >= 8) break; }
        }
        if (keep.empty()) break;
        beam.swap(keep);
    }
    if (solution.empty()) { std::fprintf(stderr, "no finish found (best stage %d)\n", best); return 1; }

    // Re-run the controls on the authoritative race and write its replay: the file is what the server would have recorded.
    AuthoritativeRace race;
    RaceConfig config;
    config.trackName = track;
    config.trackPath = SimHarness::TrackPath(track);
    config.players = 1;
    config.laps = laps;
    config.weapons = false;
    if (!race.Start(config, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    for (std::uint32_t t = 0; t < solution.size() && !race.Finished(); ++t) {
        race.SubmitInput(0, t, solution[t]);
        race.AdvanceTick();
    }
    const auto results = race.Results();
    if (!race.Finished() || !results[0].finished) { std::fprintf(stderr, "the found controls did not finish on the authoritative race\n"); return 1; }
    const std::vector<std::uint8_t> bytes = race.ReplayBytes();
    FILE* f = std::fopen(out.c_str(), "wb");
    if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    std::fclose(f);
    std::printf("%s: finished %d lap(s) in %lld ms (%zu ticks), replay %s\n", track.c_str(), laps, static_cast<long long>(results[0].finishTimeMs), solution.size(), out.c_str());
    return 0;
}
