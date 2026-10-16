// Snapshot property test (docs/specs/simulation-state.md): saving the state of a race and restoring it must be
// indistinguishable from never having stopped.
//
//   HoverNetSnapshotTest <track> [snapshot-tick] [end-tick]
//
// For one track the test runs two craft with scripted inputs to the snapshot tick, saves a snapshot, runs on to the end
// tick recording the state hash every tick, and then checks that
//   1. restoring the snapshot into the same session reproduces the saved hash at once, and replaying the remaining
//      ticks reproduces the recorded hash chain exactly;
//   2. restoring it into a brand new session (new level, new craft) does the same;
//   3. saving again after a restore gives byte-identical bytes;
//   4. damaged, truncated, wrong-version and wrong-rules snapshots are refused.
// The last line is "<track> <tick> <snapshot-bytes> <digest>", the digest being a hash of the snapshot bytes, which must
// be identical on every platform (docs/snapshot-golden.txt).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Model/StateSnapshot.h"
#ifdef __EMSCRIPTEN__
void HoverNetInitializeWebFactory();
#else
#include "../ObjFac1/SimElements.h"
#endif

using SimHarness::World;

namespace
{
constexpr std::uint32_t kRules = 4;

std::vector<MR_FreeElement*> Externals(const World& world)
{
    return std::vector<MR_FreeElement*>(world.craft.begin(), world.craft.end());
}

void StepScripted(World& world, int tick)
{
    std::uint16_t inputs[2] = {SimHarness::ScriptedInput(0, tick), SimHarness::ScriptedInput(1, tick)};
    world.Step(inputs);
}

std::uint64_t Digest(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (std::uint8_t b : bytes) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

int TransientCount(World& world)
{
    int n = 0;
    const MR_Level* level = world.session->GetCurrentLevel();
    for (int room = -1; room < level->GetRoomCount(); ++room) {
        for (MR_FreeElementHandle e = level->GetFirstFreeElement(room); e != nullptr; e = MR_Level::GetNextFreeElement(e)) {
            const MR_FreeElement* el = MR_Level::GetFreeElement(e);
            if (el->GetTypeId().mClassId == 150 || el->GetTypeId().mClassId == 151) ++n;
        }
    }
    return n;
}

bool Restore(World& world, const std::vector<std::uint8_t>& bytes, std::uint32_t rules, std::string& error)
{
    std::int64_t time = 0;
    if (!MR_RestoreLevelState(world.session->GetCurrentLevel(), bytes.data(), bytes.size(), Externals(world), rules, time, error)) return false;
    world.session->SetSimulationTime(static_cast<MR_SimulationTime>(time));
    return true;
}

int Fail(const char* what)
{
    std::fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <track> [snapshot-tick] [end-tick]\n", argv[0]);
        return 2;
    }
    const std::string track = argv[1];
    const int snapshotTick = argc > 2 ? std::atoi(argv[2]) : 700;
    const int endTick = argc > 3 ? std::atoi(argv[3]) : 1500;
    if (snapshotTick < 1 || endTick <= snapshotTick) return 2;

    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
#ifdef __EMSCRIPTEN__
    HoverNetInitializeWebFactory();
#else
    MR_RegisterSimElements();
#endif
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const std::string path = SimHarness::TrackPath(track);

    World world;
    if (!world.Build(path, track, 2)) return Fail("could not build the race (needs two start slots)");
    for (int t = 0; t < snapshotTick; ++t) StepScripted(world, t);

    const std::uint64_t savedHash = world.Hash();
    const std::vector<std::uint8_t> snapshot = MR_SaveLevelState(world.session->GetCurrentLevel(), world.session->GetSimulationTime(), Externals(world), kRules);
    const int transient = TransientCount(world);

    std::vector<std::uint64_t> chain;
    for (int t = snapshotTick; t < endTick; ++t) {
        StepScripted(world, t);
        chain.push_back(world.Hash());
    }

    // 1. Restore into the same session (the race is far ahead of the snapshot by now).
    std::string error;
    if (!Restore(world, snapshot, kRules, error)) { std::fprintf(stderr, "restore failed: %s\n", error.c_str()); return Fail("restore into the same session"); }
    if (world.Hash() != savedHash) return Fail("hash after restore differs from the hash at save time");
    {
        const std::vector<std::uint8_t> again = MR_SaveLevelState(world.session->GetCurrentLevel(), world.session->GetSimulationTime(), Externals(world), kRules);
        if (again != snapshot) return Fail("saving after a restore did not give identical bytes");   // 3.
    }
    for (int t = snapshotTick; t < endTick; ++t) {
        StepScripted(world, t);
        if (world.Hash() != chain[t - snapshotTick]) { std::fprintf(stderr, "diverged at tick %d after restoring in place\n", t); return Fail("replay after in-place restore"); }
    }

    // 2. Restore into a brand new session.
    World fresh;
    if (!fresh.Build(path, track, 2)) return Fail("could not build a second race");
    if (!Restore(fresh, snapshot, kRules, error)) { std::fprintf(stderr, "restore failed: %s\n", error.c_str()); return Fail("restore into a new session"); }
    if (fresh.Hash() != savedHash) return Fail("hash in the new session differs from the hash at save time");
    for (int t = snapshotTick; t < endTick; ++t) {
        StepScripted(fresh, t);
        if (fresh.Hash() != chain[t - snapshotTick]) { std::fprintf(stderr, "diverged at tick %d in the new session\n", t); return Fail("replay in a new session"); }
    }

    // 4. Refusals.
    World victim;
    if (!victim.Build(path, track, 2)) return Fail("could not build a third race");
    std::vector<std::uint8_t> damaged = snapshot;
    damaged[damaged.size() / 2] ^= 0x55;
    if (Restore(victim, damaged, kRules, error)) return Fail("a damaged snapshot was accepted");
    for (std::size_t cut : {std::size_t(0), std::size_t(7), std::size_t(60), snapshot.size() - 1}) {
        std::vector<std::uint8_t> truncated(snapshot.begin(), snapshot.begin() + static_cast<std::ptrdiff_t>(cut));
        if (Restore(victim, truncated, kRules, error)) return Fail("a truncated snapshot was accepted");
    }
    if (Restore(victim, snapshot, kRules + 1, error)) return Fail("a snapshot from another rules version was accepted");

    std::printf("%s %d %zu %016llx\n", track.c_str(), snapshotTick, snapshot.size(), static_cast<unsigned long long>(Digest(snapshot)));
    std::fprintf(stderr, "  %s: %d missiles/mines in flight at the snapshot, %d ticks verified in place and in a new session\n", track.c_str(),
                 transient, endTick - snapshotTick);
    return 0;
}
