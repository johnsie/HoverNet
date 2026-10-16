// Reference implementation of the HoverNet replay format v1 (docs/specs/replay-format.md).
//
//   HoverNetReplay record <track> <ticks> <out.hnr> [--players=1|2]   write a scripted race
//   HoverNetReplay verify <file.hnr>                                  re-simulate and check every checkpoint
//   HoverNetReplay info   <file.hnr>                                  print the header
//
// A replay is the track identity, the rules version and each player's input for every tick, plus a state hash
// every kCheckpointTicks ticks. Because the simulation is deterministic, that is enough to reproduce the race on any
// platform; the checkpoints say where a replay stops reproducing, and a CRC32 trailer detects a damaged file.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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
#include "../Model/StateHash.h"
#include "../ObjFac1/SimElements.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "Sha256.h"
#include "../Authority/ReplayFormat.h"
#include "SimHarness.h"


namespace
{
using namespace ReplayFormat;

using SimHarness::World;
using SimHarness::ScriptedInput;
using SimHarness::TrackPath;

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

int Record(const std::string& track, std::uint32_t ticks, const std::string& outPath, int players)
{
    Replay r;
    r.track = track;
    r.trackSha256 = Sha256::HashFile(TrackPath(track));
    r.ticks = ticks;
    r.players = players;
    World w;
    if (r.trackSha256.empty() || !w.Build(TrackPath(track), track, players)) {
        std::fprintf(stderr, "Could not load %s with %d players\n", track.c_str(), players);
        return 1;
    }
    for (std::uint32_t t = 0; t < ticks; ++t) {
        std::vector<std::uint16_t> frame(players);
        for (int p = 0; p < players; ++p) { frame[p] = ScriptedInput(p, t); r.inputs.push_back(frame[p]); }
        w.Step(frame.data());
        if ((t + 1) % kCheckpointTicks == 0) r.checkpoints.push_back(w.Hash());
    }
    r.finalHash = w.Hash();
    const std::vector<std::uint8_t> bytes = Encode(r);
    std::ofstream f(outPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) { std::fprintf(stderr, "Could not write %s\n", outPath.c_str()); return 1; }
    std::printf("recorded %s: %u ticks, %d players, %zu bytes, final hash %016llx\n", track.c_str(), ticks, players, bytes.size(),
                static_cast<unsigned long long>(r.finalHash));
    return 0;
}

int Verify(const std::string& path)
{
    std::vector<std::uint8_t> file;
    Replay r;
    std::string error;
    if (!ReadFile(path, file)) { std::fprintf(stderr, "Could not read %s\n", path.c_str()); return 1; }
    if (!Decode(file, r, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return 1; }
    if (r.rules != kSimulationRules) {
        std::fprintf(stderr, "%s: recorded under simulation rules %u, this build is %u; it cannot be verified\n", path.c_str(), r.rules, kSimulationRules);
        return 1;
    }
    const std::string sha = Sha256::HashFile(TrackPath(r.track));
    if (sha != r.trackSha256) { std::fprintf(stderr, "%s: the track file differs from the one recorded\n", path.c_str()); return 1; }
    World w;
    if (!w.Build(TrackPath(r.track), r.track, r.players, r.laps, r.weapons)) { std::fprintf(stderr, "Could not load %s\n", r.track.c_str()); return 1; }
    for (std::uint32_t t = 0; t < r.ticks; ++t) {
        w.Step(&r.inputs[static_cast<std::size_t>(t) * r.players]);
        if ((t + 1) % kCheckpointTicks == 0 && w.Hash() != r.checkpoints[(t + 1) / kCheckpointTicks - 1]) {
            std::printf("DIVERGED in the %u ticks ending at tick %u\n", kCheckpointTicks, t + 1);
            return 1;
        }
    }
    if (w.Hash() != r.finalHash) { std::printf("DIVERGED at the end\n"); return 1; }
    std::printf("verified: %s, %u ticks, %d players, final hash %016llx\n", r.track.c_str(), r.ticks, r.players,
                static_cast<unsigned long long>(r.finalHash));
    return 0;
}


// Mutation fuzzing of the decoder: flips, overwrites, truncations and insertions in a valid file, with the CRC repaired
// afterwards so the parser itself (not just the checksum) is exercised. Passes if nothing crashes, hangs or reads out of
// bounds (run it under ASan/UBSan) and every accepted file is internally consistent.
int Fuzz(const std::string& path, int iterations)
{
    std::vector<std::uint8_t> original;
    if (!ReadFile(path, original)) { std::fprintf(stderr, "Could not read %s\n", path.c_str()); return 1; }
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state]() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 2685821657736338717ULL; };
    int accepted = 0, rejected = 0;
    for (int i = 0; i < iterations; ++i) {
        std::vector<std::uint8_t> bytes = original;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits && bytes.size() > 8; ++e) {
            const std::size_t at = static_cast<std::size_t>(next() % (bytes.size() - 4));
            switch (next() % 5) {
            case 0: bytes[at] ^= static_cast<std::uint8_t>(1u << (next() % 8)); break;
            case 1: bytes[at] = static_cast<std::uint8_t>(next()); break;
            case 2: bytes.resize(at + 4); break;                                   // truncate (keeps room for a CRC)
            case 3: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::uint8_t>(next())); break;
            default: for (int k = 0; k < 4 && at + k < bytes.size() - 4; ++k) bytes[at + k] = 0xFF; break;   // huge count
            }
        }
        if (bytes.size() >= 4) {                                                   // repair the CRC
            const std::uint32_t crc = Crc32(bytes.data(), bytes.size() - 4);
            for (int b = 0; b < 4; ++b) bytes[bytes.size() - 4 + b] = static_cast<std::uint8_t>(crc >> (b * 8));
        }
        Replay r;
        std::string error;
        if (Decode(bytes, r, error)) {
            ++accepted;
            if (r.inputs.size() != static_cast<std::size_t>(r.ticks) * r.players || r.checkpoints.size() != r.ticks / kCheckpointTicks) {
                std::fprintf(stderr, "accepted an inconsistent replay (iteration %d)\n", i);
                return 1;
            }
        }
        else {
            ++rejected;
        }
    }
    std::printf("fuzz: %d mutations, %d accepted, %d rejected, no crashes\n", iterations, accepted, rejected);
    return 0;
}

// Divergence reports. "trace" prints the state hash of every tick; run it on two platforms or builds and diff the output
// to find the first tick that differs. "dump" prints the full state at one tick; diff two dumps to see which element and
// which field differ. Both read a replay file and re-simulate it.
int Replay_(const std::string& path, long dumpTick, bool trace)
{
    std::vector<std::uint8_t> file;
    Replay r;
    std::string error;
    if (!ReadFile(path, file) || !Decode(file, r, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "unreadable" : error.c_str()); return 1; }
    World w;
    if (!w.Build(TrackPath(r.track), r.track, r.players, r.laps, r.weapons)) { std::fprintf(stderr, "Could not load %s\n", r.track.c_str()); return 1; }
    if (dumpTick == 0) MR_DumpLevelState(w.session->GetCurrentLevel(), w.session->GetSimulationTime(), stdout);
    for (std::uint32_t t = 0; t < r.ticks; ++t) {
        w.Step(&r.inputs[static_cast<std::size_t>(t) * r.players]);
        if (trace) std::printf("%u %016llx\n", t + 1, static_cast<unsigned long long>(w.Hash()));
        if (dumpTick == static_cast<long>(t) + 1) { std::printf("== state after tick %u ==\n", t + 1); MR_DumpLevelState(w.session->GetCurrentLevel(), w.session->GetSimulationTime(), stdout); }
    }
    return 0;
}

int Info(const std::string& path)
{
    std::vector<std::uint8_t> file;
    Replay r;
    std::string error;
    if (!ReadFile(path, file) || !Decode(file, r, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "unreadable" : error.c_str()); return 1; }
    std::printf("HoverNet replay v%u\ntrack %s (sha256 %s)\nrules %u, tick %u ms, %u ticks (%.1f s), %d players, %zu checkpoints\n", kVersion,
                r.track.c_str(), r.trackSha256.c_str(), r.rules, r.tickMs, r.ticks, r.ticks * r.tickMs / 1000.0, r.players, r.checkpoints.size());
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s record <track> <ticks> <out> [--players=N] | verify <file> | info <file>\n", argv[0]);
        return 2;
    }
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();   // no ObjFac1 plug-in or resource file needed
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const std::string cmd = argv[1];
    if (cmd == "record" && argc >= 5) {
        int players = 2;
        for (int i = 5; i < argc; ++i) if (std::strncmp(argv[i], "--players=", 10) == 0) players = std::atoi(argv[i] + 10);
        const long ticks = std::atol(argv[3]);
        if (ticks < 1 || ticks > kMaxTicks || players < 1 || players > kMaxPlayers) { std::fprintf(stderr, "bad arguments\n"); return 2; }
        return Record(argv[2], static_cast<std::uint32_t>(ticks), argv[4], players);
    }
    if (cmd == "verify") return Verify(argv[2]);
    if (cmd == "info") return Info(argv[2]);
    if (cmd == "trace") return Replay_(argv[2], -1, true);
    if (cmd == "dump" && argc >= 4) return Replay_(argv[2], std::atol(argv[3]), false);
    if (cmd == "fuzz" && argc >= 4) return Fuzz(argv[2], std::atoi(argv[3]));
    std::fprintf(stderr, "unknown command\n");
    return 2;
}
