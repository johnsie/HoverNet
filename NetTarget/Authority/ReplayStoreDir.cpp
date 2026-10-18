#include "ReplayStoreDir.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "CraftNames.h"
#include "ReplayFormat.h"
#include "../LinuxClient/Sha256.h"

namespace Authority
{
namespace
{
constexpr std::int64_t kMaxReplayFile = 8 * 1024 * 1024;

// The seventh column of a results line, the display name of that player (absent in tables written before names were recorded). Anything that is not valid
// text for the protocol is dropped, so a hand-edited table cannot put control characters or oversized names into the lists.
std::string TextField(const char* line, int index)   // the field after `index` tabs, without the end of line
{
    int tabs = 0;
    const char* p = line;
    for (; *p != '\0' && tabs < index; ++p) if (*p == '\t') ++tabs;
    if (tabs < index) return std::string();
    const char* end = p;
    while (*end != '\0' && *end != '\t' && *end != '\n' && *end != '\r') ++end;
    return std::string(p, end);
}
std::string NameField(const char* line)
{
    const std::string name = TextField(line, 6);
    return Protocol3::IsValidText(name, 32) ? name : std::string();
}
int CraftField(const char* line)   // the eighth column: the craft model (0 to 4), -1 when absent or out of range (a table written before craft were recorded)
{
    const std::string text = TextField(line, 7);
    if (text.empty() || text.size() > 2) return -1;
    const int craft = std::atoi(text.c_str());
    return (craft >= 0 && craft < Authority::kCraftCount && (craft != 0 || text == "0")) ? craft : -1;
}

std::vector<std::string> ReplayNames(const std::string& dir)
{
    std::vector<std::string> names;
    auto good = [](const std::string& n) { return n.size() > 4 && n.size() <= 160 && n.compare(n.size() - 4, 4, ".hnr") == 0 && n.find_first_of("/\\") == std::string::npos && n[0] != '.'; };
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA((dir + "\\*.hnr").c_str(), &data);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (good(data.cFileName)) names.push_back(data.cFileName); } while (FindNextFileA(h, &data));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) if (good(e->d_name)) names.push_back(e->d_name);
        closedir(d);
    }
#endif
    return names;
}

bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::uint8_t chunk[8192];
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        bytes.insert(bytes.end(), chunk, chunk + got);
        if (static_cast<std::int64_t>(bytes.size()) > kMaxReplayFile) { std::fclose(f); bytes.clear(); return false; }
    }
    std::fclose(f);
    return true;
}

// The fastest finish in the results table beside a replay (rank, player, laps, finished, finish_ms, best_lap_ms), 0 if nobody finished; `slot` is that player.
std::int32_t WinnerMs(const std::string& replayPath, std::uint8_t& slot, std::string& name, std::uint8_t& craft)
{
    slot = 255;
    name.clear();
    craft = 255;
    const std::string path = replayPath.substr(0, replayPath.size() - 4) + ".results.tsv";
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return 0;
    char line[256];
    long long best = 0;
    while (std::fgets(line, sizeof line, f) != nullptr) {
        int rank, player, laps, finished;
        long long finishMs, bestLap;
        if (std::sscanf(line, "%d\t%d\t%d\t%d\t%lld\t%lld", &rank, &player, &laps, &finished, &finishMs, &bestLap) == 6 && finished == 1 && finishMs > 0 && player >= 0 && player < 8)
            if (best == 0 || finishMs < best) { best = finishMs; slot = static_cast<std::uint8_t>(player); name = NameField(line); craft = CraftField(line) >= 0 ? static_cast<std::uint8_t>(CraftField(line)) : 255; }
    }
    std::fclose(f);
    return best > 0 && best < 2000000000LL ? static_cast<std::int32_t>(best) : 0;
}

// Every player's line of the results table beside a replay (rank, player, laps, finished, finish_ms, best_lap_ms).
void ReadRuns(const std::string& replayPath, std::vector<ReplayRun>& runs)
{
    runs.clear();
    const std::string path = replayPath.substr(0, replayPath.size() - 4) + ".results.tsv";
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return;
    char line[256];
    while (std::fgets(line, sizeof line, f) != nullptr && runs.size() < 8) {
        int rank, player, laps, finished;
        long long finishMs, bestLap;
        if (std::sscanf(line, "%d\t%d\t%d\t%d\t%lld\t%lld", &rank, &player, &laps, &finished, &finishMs, &bestLap) != 6) continue;
        if (player < 0 || player > 7 || finishMs < 0 || finishMs > 2000000000LL || bestLap < 0 || bestLap > 2000000000LL) continue;
        runs.push_back({static_cast<std::uint8_t>(player), static_cast<std::int32_t>(finishMs), static_cast<std::int32_t>(bestLap), finished == 1, NameField(line), CraftField(line)});
    }
    std::fclose(f);
}
}

void DirReplayStore::Refresh()
{
    std::set<std::string> present;
    for (const std::string& name : ReplayNames(mDir)) {
        present.insert(name);
        const std::string path = mDir + "/" + name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || st.st_size <= 0 || st.st_size > kMaxReplayFile) { mItems.erase(name); continue; }
        auto it = mItems.find(name);
        if (it != mItems.end() && it->second.size == st.st_size && it->second.mtime == static_cast<std::int64_t>(st.st_mtime)) continue;
        std::vector<std::uint8_t> bytes;
        ReplayFormat::Replay r;
        std::string error;
        if (!ReadFileBytes(path, bytes) || !ReplayFormat::Decode(bytes, r, error)) { mItems.erase(name); continue; }
        Item item;
        item.name = name;
        item.size = st.st_size;
        item.mtime = static_cast<std::int64_t>(st.st_mtime);
        item.info.id = std::strtoull(Sha256::HashBytes(bytes.data(), bytes.size()).substr(0, 16).c_str(), nullptr, 16);
        item.info.track = r.track;
        item.info.players = static_cast<std::uint8_t>(r.players);
        item.info.laps = static_cast<std::uint8_t>(r.laps);
        item.info.ticks = r.ticks;
        item.info.savedAt = static_cast<std::uint32_t>(item.mtime);
        item.info.bytes = static_cast<std::uint32_t>(bytes.size());
        item.info.winnerMs = WinnerMs(path, item.info.winnerSlot, item.info.winnerName, item.info.winnerCraft);
        ReadRuns(path, item.runs);
        for (const ReplayRun& run : item.runs) {   // the table is in finishing order
            Protocol3::ReplayFinisher f;
            f.slot = run.slot; f.ms = run.finished ? run.finishMs : 0; f.craft = run.craft >= 0 ? static_cast<std::uint8_t>(run.craft) : 255; f.name = run.name;
            item.info.finishers.push_back(f);
        }
        mItems[name] = item;
    }
    for (auto it = mItems.begin(); it != mItems.end();) it = present.count(it->first) ? std::next(it) : mItems.erase(it);
}

std::vector<const DirReplayStore::Item*> DirReplayStore::Sorted() const
{
    std::vector<const Item*> all;
    for (const auto& kv : mItems) all.push_back(&kv.second);
    std::sort(all.begin(), all.end(), [](const Item* a, const Item* b) { return a->mtime != b->mtime ? a->mtime > b->mtime : a->name > b->name; });
    return all;
}

std::vector<Protocol3::ReplayInfo> DirReplayStore::List(const std::string& track, int max)
{
    Refresh();
    std::vector<Protocol3::ReplayInfo> out;
    for (const Item* item : Sorted()) {
        if (static_cast<int>(out.size()) >= max) break;
        if (!track.empty() && item->info.track != track) continue;
        out.push_back(item->info);
    }
    return out;
}

bool DirReplayStore::Read(std::uint64_t id, std::vector<std::uint8_t>& bytes)
{
    Refresh();
    for (const auto& kv : mItems) {
        if (kv.second.info.id != id) continue;
        std::vector<std::uint8_t> file;
        if (!ReadFileBytes(mDir + "/" + kv.second.name, file)) return false;
        if (std::strtoull(Sha256::HashBytes(file.data(), file.size()).substr(0, 16).c_str(), nullptr, 16) != id) return false;   // changed since it was indexed
        bytes.swap(file);
        return true;
    }
    return false;
}

Protocol3::RecordList DirReplayStore::Records(const std::string& track, int laps)
{
    Refresh();
    Protocol3::RecordList out;
    out.track = track;
    out.laps = static_cast<std::uint8_t>(std::max(0, std::min(99, laps)));
    std::set<int> lapCounts;
    std::vector<std::vector<Protocol3::RecordEntry>> perCraft(static_cast<std::size_t>(Authority::kCraftCount));
    for (const Item* item : Sorted()) {
        if (item->info.track != track) continue;
        lapCounts.insert(item->info.laps);
        for (const ReplayRun& run : item->runs) {
            if (run.craft < 0) continue;   // a table that does not say which craft: it cannot be placed on a craft's board
            Protocol3::RecordEntry e;
            e.replay = item->info.id; e.slot = run.slot; e.savedAt = item->info.savedAt; e.name = run.name;
            if (laps == 0) { if (run.bestLapMs <= 0) continue; e.ms = run.bestLapMs; }
            else { if (item->info.laps != laps || !run.finished || run.finishMs <= 0) continue; e.ms = run.finishMs; }
            perCraft[static_cast<std::size_t>(run.craft)].push_back(e);
        }
    }
    for (int l : lapCounts) if (out.availableLaps.size() < static_cast<std::size_t>(Protocol3::kMaxRecordLaps)) out.availableLaps.push_back(static_cast<std::uint8_t>(l));
    auto faster = [](const Protocol3::RecordEntry& a, const Protocol3::RecordEntry& b) { return a.ms != b.ms ? a.ms < b.ms : a.savedAt < b.savedAt; };   // a tie goes to the older run
    for (int craft = 0; craft < Authority::kCraftCount; ++craft) {
        std::vector<Protocol3::RecordEntry>& list = perCraft[static_cast<std::size_t>(craft)];
        if (list.empty()) continue;
        std::stable_sort(list.begin(), list.end(), faster);
        if (list.size() > static_cast<std::size_t>(Protocol3::kMaxRecordsPerBoard)) list.resize(Protocol3::kMaxRecordsPerBoard);
        Protocol3::RecordBoard board;
        board.craft = static_cast<std::uint8_t>(craft);
        board.entries = list;
        out.boards.push_back(board);
    }
    return out;
}

int DirReplayStore::Prune()
{
    Refresh();
    const std::vector<const Item*> all = Sorted();
    int removed = 0;
    std::vector<std::string> doomed;
    for (std::size_t i = mKeep; i < all.size(); ++i) doomed.push_back(all[i]->name);
    for (const std::string& name : doomed) {
        const std::string path = mDir + "/" + name;
        if (std::remove(path.c_str()) == 0) ++removed;
        std::remove((path.substr(0, path.size() - 4) + ".results.tsv").c_str());
        mItems.erase(name);
    }
    return removed;
}

std::size_t DirReplayStore::Count() { Refresh(); return mItems.size(); }
}
