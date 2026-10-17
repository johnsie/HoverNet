// HoverNetTrackPack -- the importer and packager for the versioned track package (docs/specs/track-package.md).
//
//   HoverNetTrackPack pack   IN.trk OUT.hntp [--name N] [--author A] [--licence L] [--description D] [--created YYYY-MM-DD]
//   HoverNetTrackPack verify FILE.hntp [--catalogue]   check the package and re-analyse the track inside it; --catalogue also requires a named
//                                               author and an approved licence (docs/specs/track-package.md, admission rules)
//   HoverNetTrackPack lint   FILE.trk|FILE.hntp [--strict]   authoring warnings (why it is not a race, duplicate finish lines, short grid)
//   HoverNetTrackPack info   FILE.hntp          print the manifest
//   HoverNetTrackPack unpack FILE.hntp OUT.trk  write the original track file back, byte for byte
//   HoverNetTrackPack corpus DIR OUTDIR REPORT.tsv   import every .trk in DIR; REPORT lists each result and each failure's reason
//
// The importer loads the track into the simulation (the same loader the game uses), checks that it can be played, and records
// what it found in the manifest: rooms, starts, element counts and whether it is a race or free play. The original bytes are
// kept unchanged in the package. A track that cannot be imported is reported with the exact reason.
#include <algorithm>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "../MainCharacter/StdAfx.h"
#include "../Authority/TrackPackage.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Model/GameSession.h"
#include "../Model/Level.h"
#include "../ObjFac1/SimElements.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "Sha256.h"
#include "../Authority/ReplayFormat.h"

namespace
{
struct Analysis
{
    bool ok = false;
    std::string reason;          // why it cannot be imported
    int rooms = 0, starts = 0, elements = 0;
    int finish = 0, check1 = 0, check2 = 0, powerUps = 0, gates = 0, fuel = 0, speed = 0, balls = 0;
    std::string mode;            // race or freeplay
};

Analysis Analyze(const std::string& path)
{
    Analysis a;
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(path.c_str())) { delete track; a.reason = "cannot open the file as a track archive"; return a; }
    MR_GameSession session(FALSE);
    MR_Level::ResetSerializationFaultCount();
    if (!session.LoadNew("import", track) || session.GetCurrentLevel() == nullptr) { a.reason = "the game cannot load it (damaged or unsupported archive)"; return a; }
    const MR_Level* level = session.GetCurrentLevel();
    if (MR_Level::GetSerializationFaultCount() != 0) {
        a.reason = std::to_string(MR_Level::GetSerializationFaultCount()) + " sections could not be read (damaged data or unsupported objects)";
        return a;
    }
    a.rooms = level->GetRoomCount();
    a.starts = level->GetPlayerCount();
    if (a.rooms <= 0) { a.reason = "the track has no rooms"; return a; }
    if (a.starts < 1) { a.reason = "the track has no start position"; return a; }
    if (a.starts > MR_NB_MAX_PLAYER) { a.reason = "the track has more start positions than the game supports"; return a; }
    for (int p = 0; p < a.starts; ++p) {
        const int room = level->GetStartingRoom(p);
        if (room < 0 || room >= a.rooms) { a.reason = "start position " + std::to_string(p) + " is not inside the track"; return a; }
    }
    for (int room = -1; room < a.rooms; ++room) {
        for (MR_FreeElementHandle h = level->GetFirstFreeElement(room); h != nullptr; h = MR_Level::GetNextFreeElement(h)) {
            const MR_FreeElement* e = MR_Level::GetFreeElement(h);
            ++a.elements;
            if (e->GetTypeId().mDllId != 1) continue;
            switch (e->GetTypeId().mClassId) {
            case 202: ++a.finish; break;
            case 203: ++a.check1; break;
            case 204: ++a.check2; break;
            case 152: ++a.powerUps; break;
            case 170: ++a.gates; break;
            case 200: ++a.fuel; break;
            case 201: ++a.speed; break;
            case 10: case 13: ++a.balls; break;
            default: break;
            }
        }
    }
    a.mode = (a.finish > 0 && a.check1 > 0 && a.check2 > 0) ? "race" : "freeplay";
    a.ok = true;
    return a;
}

// Authoring checks that do not stop a track from importing but that a creator wants to know about: why a track with a finish line
// is not a race, duplicate finish lines, too few start positions for a full grid. Each message is one line, no trailing newline.
std::vector<std::string> Lint(const Analysis& a)
{
    std::vector<std::string> w;
    if (a.finish > 0 && (a.check1 == 0 || a.check2 == 0)) {
        w.push_back(std::string("has a finish line but no ") + (a.check1 == 0 && a.check2 == 0 ? "checkpoints" : a.check1 == 0 ? "first checkpoint" : "second checkpoint") +
                    ", so no lap can be completed and it plays as free play");
    }
    if (a.finish == 0 && (a.check1 > 0 || a.check2 > 0)) w.push_back("has checkpoints but no finish line, so it plays as free play");
    if (a.finish > 1) w.push_back(std::to_string(a.finish) + " finish lines (check that both are meant to be on the lap route)");
    if (a.mode == "race" && a.starts < 8) w.push_back("only " + std::to_string(a.starts) + " start positions: with more players some share a start");
    if (a.mode == "race" && a.powerUps == 0 && a.fuel == 0) w.push_back("no power-ups or fuel zones: weapons and refuelling are unavailable");
    return w;
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFile(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream f(path.c_str(), std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::string BaseName(const std::string& path)
{
    std::string name = path;
    const std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".trk") == 0) name.resize(name.size() - 4);
    return name;
}

// Characters a manifest value may not contain are replaced; the value is cut to the length a manifest line allows.
std::string Clean(std::string text)
{
    for (char& c : text) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
    while (!text.empty() && !TrackPackage::IsValidManifestText(text)) {
        // Invalid UTF-8 (the classic file names are often in a legacy code page): keep only ASCII.
        std::string ascii;
        for (char c : text) if (static_cast<unsigned char>(c) < 0x80) ascii += c; else ascii += '?';
        text = ascii;
        if (!TrackPackage::IsValidManifestText(text)) text.resize(text.size() > 200 ? 200 : text.size() - 1);
    }
    if (text.size() > 200) text.resize(200);
    return text;
}

bool Pack(const std::string& in, const std::string& out, std::map<std::string, std::string> meta, std::string& reason, Analysis* analysisOut = nullptr)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(in, bytes) || bytes.empty()) { reason = "cannot read the file"; return false; }
    if (bytes.size() > TrackPackage::kMaxPayloadBytes) { reason = "the file is larger than a package may hold"; return false; }
    const Analysis a = Analyze(in);
    if (analysisOut != nullptr) *analysisOut = a;
    if (!a.ok) { reason = a.reason; return false; }
    TrackPackage::Package p;
    p.payload = bytes;
    p.manifest["format"] = "1";
    p.manifest["name"] = Clean(meta.count("name") ? meta["name"] : BaseName(in));
    p.manifest["author"] = Clean(meta.count("author") ? meta["author"] : "unknown");
    p.manifest["licence"] = Clean(meta.count("licence") ? meta["licence"] : "unspecified");
    if (meta.count("description")) p.manifest["description"] = Clean(meta["description"]);
    if (meta.count("created")) p.manifest["created"] = Clean(meta["created"]);
    p.manifest["importer"] = "HoverNetTrackPack 1";
    p.manifest["payload-sha256"] = Sha256::HashBytes(bytes.data(), bytes.size());
    p.manifest["payload-bytes"] = std::to_string(bytes.size());
    p.manifest["mode"] = a.mode;
    p.manifest["rooms"] = std::to_string(a.rooms);
    p.manifest["starts"] = std::to_string(a.starts);
    p.manifest["elements"] = std::to_string(a.elements);
    p.manifest["finish-lines"] = std::to_string(a.finish);
    p.manifest["checkpoints"] = std::to_string(a.check1 + a.check2);
    p.manifest["power-ups"] = std::to_string(a.powerUps);
    p.manifest["bumper-gates"] = std::to_string(a.gates);
    p.manifest["fuel-zones"] = std::to_string(a.fuel);
    p.manifest["speed-zones"] = std::to_string(a.speed);
    const std::vector<std::uint8_t> file = TrackPackage::Encode(p);
    if (!WriteFile(out, file)) { reason = "cannot write the package"; return false; }
    return true;
}

int Verify(const std::string& path, bool print)
{
    std::vector<std::uint8_t> bytes;
    TrackPackage::Package p;
    std::string error;
    if (!ReadFile(path, bytes)) { std::fprintf(stderr, "%s: cannot read\n", path.c_str()); return 1; }
    if (!TrackPackage::Decode(bytes, p, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return 1; }
    // Re-analyse the payload with the importer: the manifest must still describe it.
    const std::string tmp = path + ".verify.trk";
    if (!WriteFile(tmp, p.payload)) { std::fprintf(stderr, "cannot write a temporary file\n"); return 1; }
    const Analysis a = Analyze(tmp);
    std::remove(tmp.c_str());
    if (!a.ok) { std::fprintf(stderr, "%s: the track inside no longer imports: %s\n", path.c_str(), a.reason.c_str()); return 1; }
    if (p.manifest["rooms"] != std::to_string(a.rooms) || p.manifest["starts"] != std::to_string(a.starts) || p.manifest["mode"] != a.mode) {
        std::fprintf(stderr, "%s: the manifest does not describe the track inside it\n", path.c_str());
        return 1;
    }
    if (print) std::printf("verified %s: %s by %s (%s), %s, %d rooms, %d starts\n", path.c_str(), p.manifest["name"].c_str(), p.manifest["author"].c_str(),
                           p.manifest["licence"].c_str(), a.mode.c_str(), a.rooms, a.starts);
    return 0;
}

// Admission rule 1 of the catalogue: a named author and a licence that lets HoverNet distribute the track. The approved list is
// deliberately short: public-domain dedication, attribution, or attribution with share-alike.
bool CatalogueRules(const std::map<std::string, std::string>& manifest, std::string& why)
{
    static const char* const kApproved[] = { "CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0" };
    const auto get = [&](const char* key) { const auto it = manifest.find(key); return it == manifest.end() ? std::string() : it->second; };
    const std::string author = get("author"), licence = get("licence");
    if (author.empty() || author == "unknown") { why = "no author is named"; return false; }
    for (const char* ok : kApproved) if (licence == ok) return true;
    why = "the licence \"" + licence + "\" is not one of CC0-1.0, CC-BY-4.0, CC-BY-SA-4.0";
    return false;
}

// lint FILE.trk|FILE.hntp [--strict]: import it as the game would and print the authoring warnings. Exit status 1 when it cannot be
// imported, or (with --strict) when there is any warning.
int LintFile(const std::string& path, bool strict)
{
    std::string trk = path;
    std::string temp;
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".hntp") == 0) {
        std::vector<std::uint8_t> bytes;
        TrackPackage::Package p;
        std::string error;
        if (!ReadFile(path, bytes) || !TrackPackage::Decode(bytes, p, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "unreadable" : error.c_str()); return 1; }
        temp = path + ".lint.trk";
        if (!WriteFile(temp, p.payload)) { std::fprintf(stderr, "cannot write a temporary file\n"); return 1; }
        trk = temp;
    }
    const Analysis a = Analyze(trk);
    if (!temp.empty()) std::remove(temp.c_str());
    if (!a.ok) { std::fprintf(stderr, "%s: cannot import: %s\n", path.c_str(), a.reason.c_str()); return 1; }
    const std::vector<std::string> warnings = Lint(a);
    std::printf("%s: %s, %d rooms, %d starts, %d finish, %d+%d checkpoints, %d power-ups\n", path.c_str(), a.mode.c_str(), a.rooms, a.starts, a.finish, a.check1, a.check2, a.powerUps);
    for (const std::string& m : warnings) std::printf("  warning: %s\n", m.c_str());
    return (strict && !warnings.empty()) ? 1 : 0;
}

int Info(const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    TrackPackage::Package p;
    std::string error;
    if (!ReadFile(path, bytes) || !TrackPackage::Decode(bytes, p, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "unreadable" : error.c_str()); return 1; }
    for (const auto& kv : p.manifest) std::printf("%s=%s\n", kv.first.c_str(), kv.second.c_str());
    return 0;
}

int Unpack(const std::string& path, const std::string& out)
{
    std::vector<std::uint8_t> bytes;
    TrackPackage::Package p;
    std::string error;
    if (!ReadFile(path, bytes) || !TrackPackage::Decode(bytes, p, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.empty() ? "unreadable" : error.c_str()); return 1; }
    if (!WriteFile(out, p.payload)) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    return 0;
}


// Mutation fuzzing of the package decoder with the CRC repaired afterwards, so the parser itself is exercised.
int Fuzz(const std::string& path, int iterations)
{
    std::vector<std::uint8_t> original;
    if (!ReadFile(path, original)) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); return 1; }
    // Keep the payload small for speed: fuzz a package whose payload is a few bytes (the manifest and framing are what is parsed).
    TrackPackage::Package base;
    std::string error;
    if (!TrackPackage::Decode(original, base, error)) { std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str()); return 1; }
    base.payload.resize(64);
    base.manifest["payload-bytes"] = "64";
    base.manifest["payload-sha256"] = Sha256::HashBytes(base.payload.data(), base.payload.size());
    const std::vector<std::uint8_t> tiny = TrackPackage::Encode(base);
    std::uint64_t state = 0x2545F4914F6CDD1DULL;
    auto next = [&state]() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 2685821657736338717ULL; };
    int accepted = 0, rejected = 0;
    for (int i = 0; i < iterations; ++i) {
        std::vector<std::uint8_t> bytes = tiny;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits && bytes.size() > 12; ++e) {
            const std::size_t at = static_cast<std::size_t>(next() % (bytes.size() - 4));
            switch (next() % 4) {
            case 0: bytes[at] ^= static_cast<std::uint8_t>(1u << (next() % 8)); break;
            case 1: bytes[at] = static_cast<std::uint8_t>(next()); break;
            case 2: bytes.resize(at + 4); break;
            default: for (int k = 0; k < 4 && at + k < bytes.size() - 4; ++k) bytes[at + k] = 0xFF; break;
            }
        }
        if (bytes.size() >= 4) {
            const std::uint32_t crc = ReplayFormat::Crc32(bytes.data(), bytes.size() - 4);
            for (int b = 0; b < 4; ++b) bytes[bytes.size() - 4 + b] = static_cast<std::uint8_t>(crc >> (b * 8));
        }
        TrackPackage::Package p;
        if (TrackPackage::Decode(bytes, p, error)) ++accepted; else ++rejected;
    }
    std::printf("fuzz: %d mutations, %d accepted, %d rejected, no crashes\n", iterations, accepted, rejected);
    return 0;
}

// Imports every .trk in a directory. The report has one line per track; the exit status is 0 only if at least 95% import.
int Corpus(const std::string& dir, const std::string& outDir, const std::string& reportPath)
{
    std::vector<std::string> files;
    {
#ifdef _WIN32
        std::string command = "dir /b \"" + dir + "\"";
#else
        std::string command = "ls -1 \"" + dir + "\"";
#endif
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe == nullptr) { std::fprintf(stderr, "cannot list %s\n", dir.c_str()); return 1; }
        char line[4096];
        while (std::fgets(line, sizeof(line), pipe) != nullptr) {
            std::string name = line;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".trk") == 0) files.push_back(name);
        }
        pclose(pipe);
    }
    std::sort(files.begin(), files.end());
    std::FILE* report = std::fopen(reportPath.c_str(), "w");
    if (report == nullptr) { std::fprintf(stderr, "cannot write %s\n", reportPath.c_str()); return 1; }
    std::fprintf(report, "# name\\tresult\\tmode\\trooms\\tstarts\\tpackage bytes\\treason\n");
    int ok = 0, failed = 0, freeplay = 0, verifyFailures = 0;
    std::map<std::string, int> reasons;
    int warned = 0;
    for (const std::string& file : files) {
        const std::string in = dir + "/" + file;
        const std::string out = outDir + "/" + std::to_string(ok + failed) + ".hntp";
        std::string reason;
        Analysis a;
        std::map<std::string, std::string> meta;
        meta["name"] = BaseName(file);
        meta["author"] = "unknown";
        meta["licence"] = "unspecified";
        if (Pack(in, out, meta, reason, &a)) {
            ++ok;
            if (a.mode == "freeplay") ++freeplay;
            if (!Lint(a).empty()) ++warned;
            std::vector<std::uint8_t> packed;
            ReadFile(out, packed);
            if (Verify(out, false) != 0) { ++verifyFailures; }
            std::fprintf(report, "%s\tok\t%s\t%d\t%d\t%zu\t\n", BaseName(file).c_str(), a.mode.c_str(), a.rooms, a.starts, packed.size());
            std::remove(out.c_str());
        }
        else {
            ++failed;
            ++reasons[reason];
            std::fprintf(report, "%s\tFAILED\t\t\t\t\t%s\n", BaseName(file).c_str(), reason.c_str());
        }
    }
    std::fclose(report);
    const int total = ok + failed;
    std::printf("imported %d of %d tracks (%.1f%%): %d races, %d free play; %d failed; %d packages failed to verify; %d with authoring warnings\n", ok, total,
                total ? 100.0 * ok / total : 0.0, ok - freeplay, freeplay, failed, verifyFailures, warned);
    for (const auto& r : reasons) std::printf("  %d x %s\n", r.second, r.first.c_str());
    return (total > 0 && ok * 100 >= total * 95 && verifyFailures == 0) ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s pack|verify|info|unpack|corpus ...\n", argv[0]);
        return 2;
    }
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;
    const std::string cmd = argv[1];
    if (cmd == "pack" && argc >= 4) {
        std::map<std::string, std::string> meta;
        for (int i = 4; i + 1 < argc; i += 2) {
            std::string key = argv[i];
            if (key.size() > 2 && key.compare(0, 2, "--") == 0) meta[key.substr(2)] = argv[i + 1];
        }
        std::string reason;
        if (!Pack(argv[2], argv[3], meta, reason)) { std::fprintf(stderr, "%s: cannot import: %s\n", argv[2], reason.c_str()); return 1; }
        return Verify(argv[3], true);
    }
    if (cmd == "verify") {
        const int rc = Verify(argv[2], true);
        if (rc != 0 || argc < 4 || std::string(argv[3]) != "--catalogue") return rc;
        std::vector<std::uint8_t> bytes;
        TrackPackage::Package p;
        std::string error;
        if (!ReadFile(argv[2], bytes) || !TrackPackage::Decode(bytes, p, error)) return 1;
        if (!CatalogueRules(p.manifest, error)) { std::fprintf(stderr, "%s: not admissible to the catalogue: %s\n", argv[2], error.c_str()); return 1; }
        std::printf("%s: admissible to the catalogue\n", argv[2]);
        return 0;
    }
    if (cmd == "info") return Info(argv[2]);
    if (cmd == "lint") return LintFile(argv[2], argc >= 4 && std::string(argv[3]) == "--strict");
    if (cmd == "unpack" && argc >= 4) return Unpack(argv[2], argv[3]);
    if (cmd == "fuzz" && argc >= 4) return Fuzz(argv[2], std::atoi(argv[3]));
    if (cmd == "corpus" && argc >= 5) return Corpus(argv[2], argv[3], argv[4]);
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
