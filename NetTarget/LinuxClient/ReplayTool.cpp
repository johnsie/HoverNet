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
#ifndef __EMSCRIPTEN__
#include "../ObjFac1/SimElements.h"
#endif
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "Sha256.h"

#ifdef __EMSCRIPTEN__
void HoverNetInitializeWebFactory();
#endif

namespace
{
constexpr int kFrameMs = 16;
constexpr std::uint32_t kCheckpointTicks = 250;
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kSimulationRules = 3;  // bump when a deliberate change alters the golden hashes
constexpr std::uint32_t kMaxTicks = 2u * 60 * 60 * 1000 / kFrameMs;  // two hours
constexpr int kMaxPlayers = 8;

struct Replay
{
    std::string track;           // track name as the game knows it
    std::string trackSha256;     // lower-case hex of the .trk file
    std::uint32_t rules = kSimulationRules;
    std::uint32_t tickMs = kFrameMs;
    std::uint32_t ticks = 0;
    int players = 0;
    std::vector<std::uint16_t> inputs;                       // [tick * players + player]
    std::vector<std::uint64_t> checkpoints;                  // hash after tick (i+1)*kCheckpointTicks
    std::uint64_t finalHash = 0;
};

// ---- little-endian byte stream ----------------------------------------------------------------------------
struct Writer
{
    std::vector<std::uint8_t> bytes;
    void U8(std::uint32_t v) { bytes.push_back(static_cast<std::uint8_t>(v)); }
    void U16(std::uint32_t v) { U8(v); U8(v >> 8); }
    void U32(std::uint32_t v) { U16(v); U16(v >> 16); }
    void U64(std::uint64_t v) { U32(static_cast<std::uint32_t>(v)); U32(static_cast<std::uint32_t>(v >> 32)); }
    void Str(const std::string& s) { U8(static_cast<std::uint32_t>(s.size())); bytes.insert(bytes.end(), s.begin(), s.end()); }
};

struct Reader
{
    const std::vector<std::uint8_t>& bytes;
    std::size_t pos = 0;
    bool ok = true;
    explicit Reader(const std::vector<std::uint8_t>& b) : bytes(b) {}
    bool Need(std::size_t n) { if (!ok || bytes.size() - pos < n) ok = false; return ok; }
    std::uint32_t U8() { if (!Need(1)) return 0; return bytes[pos++]; }
    std::uint32_t U16() { const std::uint32_t lo = U8(); return lo | (U8() << 8); }
    std::uint32_t U32() { const std::uint32_t lo = U16(); return lo | (U16() << 16); }
    std::uint64_t U64() { const std::uint64_t lo = U32(); return lo | (static_cast<std::uint64_t>(U32()) << 32); }
    std::string Str()
    {
        const std::size_t n = U8();
        if (!Need(n)) return std::string();
        std::string s(bytes.begin() + pos, bytes.begin() + pos + n);
        pos += n;
        return s;
    }
};

std::uint32_t Crc32(const std::uint8_t* data, std::size_t n)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

std::vector<std::uint8_t> Encode(const Replay& r)
{
    Writer w;
    w.U8('H'); w.U8('N'); w.U8('R'); w.U8('P');
    w.U16(kVersion);
    w.U16(0);  // flags, reserved
    w.U32(r.rules);
    w.U32(r.tickMs);
    w.Str(r.track);
    w.Str(r.trackSha256);
    w.U8(static_cast<std::uint32_t>(r.players));
    w.U32(r.ticks);
    w.U32(kCheckpointTicks);
    for (std::uint16_t input : r.inputs) w.U16(input);
    w.U32(static_cast<std::uint32_t>(r.checkpoints.size()));
    for (std::uint64_t h : r.checkpoints) w.U64(h);
    w.U64(r.finalHash);
    w.U32(Crc32(w.bytes.data(), w.bytes.size()));
    return w.bytes;
}

// Strict: any inconsistency is an error, nothing is read past the end, and counts are checked before allocating.
bool Decode(const std::vector<std::uint8_t>& file, Replay& r, std::string& error)
{
    if (file.size() < 8 + 4) { error = "file too short"; return false; }
    const std::size_t bodyLen = file.size() - 4;
    Reader tail(file);
    tail.pos = bodyLen;
    if (tail.U32() != Crc32(file.data(), bodyLen)) { error = "CRC mismatch (damaged file)"; return false; }
    Reader in(file);
    if (in.U8() != 'H' || in.U8() != 'N' || in.U8() != 'R' || in.U8() != 'P') { error = "not a HoverNet replay"; return false; }
    const std::uint32_t version = in.U16();
    if (version != kVersion) { error = "unsupported replay version " + std::to_string(version); return false; }
    in.U16();  // flags
    r.rules = in.U32();
    r.tickMs = in.U32();
    r.track = in.Str();
    r.trackSha256 = in.Str();
    r.players = static_cast<int>(in.U8());
    r.ticks = in.U32();
    const std::uint32_t interval = in.U32();
    if (!in.ok) { error = "truncated header"; return false; }
    if (r.tickMs != kFrameMs) { error = "unsupported tick length"; return false; }
    if (r.players < 1 || r.players > kMaxPlayers) { error = "bad player count"; return false; }
    if (r.ticks > kMaxTicks) { error = "too many ticks"; return false; }
    if (interval != kCheckpointTicks) { error = "unsupported checkpoint interval"; return false; }
    const std::uint64_t inputBytes = static_cast<std::uint64_t>(r.ticks) * r.players * 2;
    if (inputBytes > bodyLen - in.pos) { error = "truncated inputs"; return false; }
    r.inputs.resize(static_cast<std::size_t>(r.ticks) * r.players);
    for (std::uint16_t& v : r.inputs) v = static_cast<std::uint16_t>(in.U16());
    const std::uint32_t checkpointCount = in.U32();
    if (checkpointCount != r.ticks / kCheckpointTicks) { error = "wrong checkpoint count"; return false; }
    if (static_cast<std::uint64_t>(checkpointCount) * 8 + 8 > bodyLen - in.pos) { error = "truncated checkpoints"; return false; }
    r.checkpoints.resize(checkpointCount);
    for (std::uint64_t& h : r.checkpoints) h = in.U64();
    r.finalHash = in.U64();
    if (!in.ok || in.pos != bodyLen) { error = "trailing or missing bytes"; return false; }
    return true;
}

// ---- the simulation ---------------------------------------------------------------------------------------
struct World
{
    std::unique_ptr<MR_GameSession> session;
    std::vector<MR_MainCharacter*> craft;

    bool Build(const std::string& path, const std::string& name, int players)
    {
        MR_RecordFile* track = new MR_RecordFile;
        if (!track->OpenForRead(path.c_str())) { delete track; return false; }
        session.reset(new MR_GameSession(FALSE));
        if (!session->LoadNew(name.c_str(), track) || session->GetCurrentLevel() == nullptr) return false;
        MR_Level* level = session->GetCurrentLevel();
        if (level->GetPlayerCount() < players) return false;
        for (int i = 0; i < players; ++i) {
            const int room = level->GetStartingRoom(i);
            MR_MainCharacter* c = MR_MainCharacter::New(5, TRUE);
            if (c == nullptr || room < 0 || room >= level->GetRoomCount()) return false;
            c->mPosition = level->GetStartingPos(i);
            c->SetOrientation(level->GetStartingOrientation(i));
            c->mRoom = room;
            if (level->InsertElement(c, room, FALSE) == nullptr) return false;
            craft.push_back(c);
        }
        session->SetSimulationTime(0);
        return true;
    }

    void Step(const std::uint16_t* inputs)
    {
        for (std::size_t i = 0; i < craft.size(); ++i) craft[i]->SetControlState(inputs[i], session->GetSimulationTime());
        session->SimulateStep(kFrameMs);
    }

    // The whole state of the race, hashed by the simulation library (docs/specs/simulation-state.md).
    std::uint64_t Hash() const { return MR_HashLevelState(session->GetCurrentLevel(), session->GetSimulationTime()); }
};

std::string TrackPath(const std::string& name)
{
    return name.find('/') != std::string::npos ? name : "NetTarget/Tracks/" + name + ".trk";
}

std::uint16_t ScriptedInput(int player, std::uint32_t tick)
{
    int c = MR_MainCharacter::eMotorOn;
    const int period = 37 + 16 * player;
    const int phase = static_cast<int>(tick / period) % 3;
    if (phase == 0) c |= MR_MainCharacter::eLeft;
    else if (phase == 1) c |= MR_MainCharacter::eRight;
    const std::uint32_t jumpPeriod = 190 + 40 * player;
    if (tick % jumpPeriod >= 150 && tick % jumpPeriod < 160) c |= MR_MainCharacter::eJump;
    if (tick % (70 + 20 * player) == 30) c |= MR_MainCharacter::eFire;
    if (tick % (410 + 30 * player) == 350) c |= MR_MainCharacter::eSelectWeapon;
    return static_cast<std::uint16_t>(c);
}

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
    if (!w.Build(TrackPath(r.track), r.track, r.players)) { std::fprintf(stderr, "Could not load %s\n", r.track.c_str()); return 1; }
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
#ifdef __EMSCRIPTEN__
    HoverNetInitializeWebFactory();
#else
    MR_RegisterSimElements();   // no ObjFac1 plug-in or resource file needed
#endif
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
    std::fprintf(stderr, "unknown command\n");
    return 2;
}
