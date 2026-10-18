// Unit test of ReplayLibrary: saving, listing newest first with the details of each replay, keeping only the newest kKeep, damaged and foreign
// files, and names that cannot reach another folder.
#include "ReplayLibrary.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(d) mkdir(d, 0755)
#endif

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

static std::vector<std::uint8_t> MakeReplay(const std::string& track, int players, std::uint32_t ticks)
{
    ReplayFormat::Replay r;
    r.track = track;
    r.trackSha256 = std::string(64, 'a');
    r.players = players;
    r.laps = 3;
    r.ticks = ticks;
    r.inputs.assign(static_cast<std::size_t>(ticks) * players, 1);
    r.checkpoints.assign(ticks / ReplayFormat::kCheckpointTicks, 7);
    r.finalHash = 99;
    return ReplayFormat::Encode(r);
}

int main()
{
    const std::string dir = "hovernet-replay-library-test.tmp";
    MAKE_DIR(dir.c_str());
    ReplayLibrary library(dir);
    for (const ReplayLibrary::Entry& e : library.List()) std::remove(e.path.c_str());   // a clean start even after an aborted run
    Check(library.List().empty(), "starts empty");

    std::tm t{};
    t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 18; t.tm_hour = 14; t.tm_min = 23; t.tm_sec = 55; t.tm_isdst = -1;
    const std::time_t base = std::mktime(&t);
    Check(ReplayLibrary::FileName("Metro Spiral", base) == "Metro_Spiral_20261018-142355.hnr", "the file name from the track and the time");
    const std::string hostile = ReplayLibrary::FileName("../../etc/x", base);
    Check(hostile.find('/') == std::string::npos && hostile.find('\\') == std::string::npos && hostile[0] != '.', "a track name cannot name another folder");

    const std::string first = library.Save(MakeReplay("ClassicH", 2, 600), "ClassicH", base);
    const std::string second = library.Save(MakeReplay("The River", 4, 1250), "The River", base + 60);
    Check(!first.empty() && !second.empty(), "saves");
    const std::string repeat = library.Save(MakeReplay("ClassicH", 2, 600), "ClassicH", base);   // the same second again
    Check(!repeat.empty() && repeat != first, "two races in one second keep both");
    Check(library.Save(std::vector<std::uint8_t>(), "ClassicH", base).empty(), "nothing to save is not saved");

    std::vector<ReplayLibrary::Entry> list = library.List();
    Check(list.size() == 3, "all three are listed");
    Check(list.size() == 3 && list[0].track == "The River" && list[0].players == 4 && list[0].laps == 3 && list[0].ticks == 1250 && list[0].valid, "the newest is first, with its details");
    Check(list.size() == 3 && list[0].durationMs() == 20000 && list[0].when == "2026-10-18 14:24", "its length and time");
    Check(list.size() == 3 && list[2].track == "ClassicH", "older ones follow");

    { std::FILE* f = std::fopen((dir + "/Broken_20261018-150000.hnr").c_str(), "wb"); std::fputs("not a replay", f); std::fclose(f); }
    { std::FILE* f = std::fopen((dir + "/notes.txt").c_str(), "wb"); std::fputs("x", f); std::fclose(f); }
    list = library.List();
    Check(list.size() == 4 && list[0].name == "Broken_20261018-150000.hnr" && !list[0].valid, "a damaged file is listed as damaged");
    Check(library.Remove("Broken_20261018-150000.hnr") && library.List().size() == 3, "a replay can be removed");
    Check(!library.Remove("../notes.txt") && !library.Remove("notes.txt"), "only replay files in the folder can be removed");

    for (int i = 0; i < 30; ++i) library.Save(MakeReplay("Steeplechase", 2, 300), "Steeplechase", base + 3600 + i);
    list = library.List();
    Check(list.size() == ReplayLibrary::kKeep, "only the newest are kept");
    Check(!list.empty() && list[0].track == "Steeplechase" && list.back().track == "Steeplechase", "the older replays were the ones removed");

    for (const ReplayLibrary::Entry& e : list) std::remove(e.path.c_str());
    std::remove((dir + "/notes.txt").c_str());
#ifdef _WIN32
    _rmdir(dir.c_str());
#else
    rmdir(dir.c_str());
#endif
    if (gFailures == 0) std::printf("replay library tests passed\n");
    return gFailures == 0 ? 0 : 1;
}
