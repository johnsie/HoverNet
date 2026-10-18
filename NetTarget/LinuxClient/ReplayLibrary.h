// The player's saved replays: a "replays" folder inside the per-user settings folder (the browser build keeps that folder in IndexedDB), holding
// the races the player took part in or watched from the first tick on a HoverNet 3.0 server (ReplayRecorder.h). The newest kKeep are kept;
// older ones are removed as new ones are saved, so the folder cannot grow without bound. Header only and free of SDL, so it is unit tested on its
// own (ReplayLibraryTest.cpp). A file that is not a valid replay is listed as damaged, never played.
#pragma once

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "../Authority/ReplayFormat.h"

class ReplayLibrary
{
public:
    static constexpr std::size_t kKeep = 20;
    static constexpr std::size_t kMaxFileBytes = 6 * 1024 * 1024;   // a two-hour race of eight players is about 7 MB; a larger file is not a replay we wrote

    struct Entry
    {
        std::string name;           // the file name, e.g. "ClassicH_20261018-142355.hnr"
        std::string path;
        bool valid = false;         // it decodes (checksum and structure)
        std::string track;
        int players = 0;
        int laps = 0;
        int mySlot = -1;            // the craft the player drove in this race (-1: they only watched, or it is not known)
        std::uint32_t ticks = 0;
        long long durationMs() const { return static_cast<long long>(ticks) * ReplayFormat::kFrameMs; }
        std::string when;           // "2026-10-18 14:23" from the file name, empty when it has none
    };

    explicit ReplayLibrary(const std::string& directory) : mDir(directory) {}

    // "<track>_<YYYYMMDD-HHMMSS>.hnr": the track name keeps letters and digits only, so a name can never reach another folder.
    static std::string FileName(const std::string& track, std::time_t now)
    {
        std::string clean;
        for (char c : track) clean += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') ? c : '_';
        if (clean.empty()) clean = "race";
        if (clean.size() > 40) clean.resize(40);
        std::tm parts{};
#ifdef _WIN32
        localtime_s(&parts, &now);
#else
        localtime_r(&now, &parts);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &parts);
        return clean + "_" + stamp + ".hnr";
    }

    // Writes a replay (bytes as ReplayRecorder::Bytes made them) and prunes. Returns the file name, or empty on failure.
    // mySlot: the player's own slot in the race, kept in a small file beside it (<name>.hnr.slot) so the replay itself stays the server's exact bytes.
    std::string Save(const std::vector<std::uint8_t>& bytes, const std::string& track, std::time_t now, int mySlot = -1)
    {
        if (bytes.empty() || bytes.size() > kMaxFileBytes) return std::string();
        MakeDirectory();
        std::string name = FileName(track, now);
        if (Exists(mDir + "/" + name)) {   // two races in the same second (a test, a very short race): keep both
            const std::string base = name.substr(0, name.size() - 4);
            for (int n = 2; n < 100 && Exists(mDir + "/" + name); ++n) name = base + "-" + std::to_string(n) + ".hnr";
        }
        const std::string path = mDir + "/" + name;
        const std::string temp = path + ".tmp";
        std::FILE* f = std::fopen(temp.c_str(), "wb");
        if (f == nullptr) return std::string();
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        const bool closed = std::fclose(f) == 0;
        if (!written || !closed || std::rename(temp.c_str(), path.c_str()) != 0) { std::remove(temp.c_str()); return std::string(); }
        if (mySlot >= 0 && mySlot < 8) {
            if (std::FILE* sf = std::fopen((path + ".slot").c_str(), "w")) { std::fprintf(sf, "%d\n", mySlot); std::fclose(sf); }
        }
        Prune();
        return name;
    }

    // The saved replays, newest first (the name carries the time). Files that do not decode are included with valid == false.
    std::vector<Entry> List() const
    {
        std::vector<std::string> names = Names();
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Stamp(a) != Stamp(b) ? Stamp(a) > Stamp(b) : a > b; });
        std::vector<Entry> entries;
        for (const std::string& name : names) {
            Entry e;
            e.name = name;
            e.path = mDir + "/" + name;
            const std::string stamp = Stamp(name);
            if (stamp.size() == 15) e.when = stamp.substr(0, 4) + "-" + stamp.substr(4, 2) + "-" + stamp.substr(6, 2) + " " + stamp.substr(9, 2) + ":" + stamp.substr(11, 2);
            std::vector<std::uint8_t> bytes;
            ReplayFormat::Replay r;
            std::string error;
            if (Read(e.path, bytes) && ReplayFormat::Decode(bytes, r, error)) {
                e.valid = true; e.track = r.track; e.players = r.players; e.laps = r.laps; e.ticks = r.ticks;
                if (std::FILE* sf = std::fopen((e.path + ".slot").c_str(), "r")) {
                    int slot = -1;
                    if (std::fscanf(sf, "%d", &slot) == 1 && slot >= 0 && slot < r.players) e.mySlot = slot;
                    std::fclose(sf);
                }
            }
            entries.push_back(e);
        }
        return entries;
    }

    bool Remove(const std::string& name) const
    {
        if (!IsReplayName(name)) return false;
        std::remove((mDir + "/" + name + ".slot").c_str());
        return std::remove((mDir + "/" + name).c_str()) == 0;
    }

    static bool Read(const std::string& path, std::vector<std::uint8_t>& bytes)
    {
        bytes.clear();
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) return false;
        std::uint8_t chunk[4096];
        std::size_t got;
        while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
            bytes.insert(bytes.end(), chunk, chunk + got);
            if (bytes.size() > kMaxFileBytes) { std::fclose(f); bytes.clear(); return false; }
        }
        std::fclose(f);
        return true;
    }

    const std::string& Directory() const { return mDir; }

    void MakeDirectory() const
    {
#ifdef _WIN32
        _mkdir(mDir.c_str());
#else
        mkdir(mDir.c_str(), 0755);
#endif
    }


    static bool IsReplayName(const std::string& name)
    {
        return name.size() > 4 && name.size() <= 120 && name.compare(name.size() - 4, 4, ".hnr") == 0 && name.find_first_of("/\\") == std::string::npos && name[0] != '.';
    }

private:
    static std::string Stamp(const std::string& name)   // "YYYYMMDD-HHMMSS" from "<track>_<stamp>[-n].hnr", or empty
    {
        const std::size_t underscore = name.rfind('_');
        if (underscore == std::string::npos || name.size() < underscore + 1 + 15 + 4) return std::string();
        const std::string stamp = name.substr(underscore + 1, 15);
        for (std::size_t i = 0; i < stamp.size(); ++i) if (i == 8 ? stamp[i] != '-' : (stamp[i] < '0' || stamp[i] > '9')) return std::string();
        return stamp;
    }

    static bool Exists(const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) return false;
        std::fclose(f);
        return true;
    }

    std::vector<std::string> Names() const
    {
        std::vector<std::string> names;
#ifdef _WIN32
        WIN32_FIND_DATAA data;
        HANDLE h = FindFirstFileA((mDir + "\\*.hnr").c_str(), &data);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (IsReplayName(data.cFileName)) names.push_back(data.cFileName); } while (FindNextFileA(h, &data));
            FindClose(h);
        }
#else
        DIR* d = opendir(mDir.c_str());
        if (d != nullptr) {
            while (dirent* e = readdir(d)) { if (IsReplayName(e->d_name)) names.push_back(e->d_name); }
            closedir(d);
        }
#endif
        return names;
    }

    void Prune() const
    {
        std::vector<Entry> all = List();   // newest first
        for (std::size_t i = kKeep; i < all.size(); ++i) { std::remove(all[i].path.c_str()); std::remove((all[i].path + ".slot").c_str()); }
    }

    std::string mDir;
};
