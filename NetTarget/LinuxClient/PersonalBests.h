// Personal bests: the player's fastest finish and fastest lap on each track, kept in a small text file in the per-user settings folder
// (the browser build keeps that folder in IndexedDB), so the results screen can say "new personal best". Local to the player's own machine:
// nothing is sent anywhere and no account is involved. Header only, no SDL, so it is unit tested on its own.
//
// File format, one record per line, fields separated by a tab:  R <track> <laps> <milliseconds>   or   L <track> <milliseconds>
// A race time is only comparable with another over the same number of laps, so it is kept per (track, laps); a lap time per track.
// Unknown or malformed lines are ignored, so a newer file read by an older build loses nothing it understands.
#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <utility>

class PersonalBests
{
public:
    struct Outcome
    {
        bool newRaceBest = false;       // faster than the previous best for this track and lap count (or the first finish)
        bool newLapBest = false;
        bool hadRaceBest = false;       // there was an earlier best to compare with
        bool hadLapBest = false;
        long long previousRace = 0;     // milliseconds, valid when hadRaceBest
        long long previousLap = 0;
        long long raceBest = 0;         // the best now stored (after this finish), 0 when there is none
        long long lapBest = 0;
        bool Any() const { return newRaceBest || newLapBest || raceBest > 0 || lapBest > 0; }
    };

    // Reads the file; a missing or unreadable file is an empty set of records.
    void Load(const std::string& path)
    {
        mRace.clear();
        mLap.clear();
        std::FILE* file = std::fopen(path.c_str(), "r");
        if (file == nullptr) return;
        char line[512];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            std::string text(line);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            ParseLine(text);
        }
        std::fclose(file);
    }

    // Writes the file; returns false when it could not be written (the bests then only last for this run).
    bool Save(const std::string& path) const
    {
        const std::string temp = path + ".tmp";
        std::FILE* file = std::fopen(temp.c_str(), "w");
        if (file == nullptr) return false;
        for (const auto& r : mRace) std::fprintf(file, "R\t%s\t%d\t%lld\n", r.first.first.c_str(), r.first.second, r.second);
        for (const auto& l : mLap) std::fprintf(file, "L\t%s\t%lld\n", l.first.c_str(), l.second);
        const bool ok = std::fclose(file) == 0;
        if (!ok) { std::remove(temp.c_str()); return false; }
        if (std::rename(temp.c_str(), path.c_str()) != 0) {
            // Windows does not rename over an existing file.
            std::remove(path.c_str());
            if (std::rename(temp.c_str(), path.c_str()) != 0) { std::remove(temp.c_str()); return false; }
        }
        return true;
    }

    // Offers a finish. A time of zero or less is not a real time (nothing finished) and is ignored. Returns what changed.
    Outcome Record(const std::string& track, int laps, long long raceMs, long long bestLapMs)
    {
        Outcome out;
        if (!Valid(track)) return out;
        if (raceMs > 0 && laps > 0) {
            auto it = mRace.find(std::make_pair(track, laps));
            if (it != mRace.end()) { out.hadRaceBest = true; out.previousRace = it->second; }
            if (it == mRace.end() || raceMs < it->second) { mRace[std::make_pair(track, laps)] = raceMs; out.newRaceBest = true; }
        }
        if (bestLapMs > 0) {
            auto it = mLap.find(track);
            if (it != mLap.end()) { out.hadLapBest = true; out.previousLap = it->second; }
            if (it == mLap.end() || bestLapMs < it->second) { mLap[track] = bestLapMs; out.newLapBest = true; }
        }
        const auto race = mRace.find(std::make_pair(track, laps));
        if (race != mRace.end()) out.raceBest = race->second;
        const auto lap = mLap.find(track);
        if (lap != mLap.end()) out.lapBest = lap->second;
        return out;
    }

    // The stored bests, 0 when there is none.
    long long RaceBest(const std::string& track, int laps) const
    {
        const auto it = mRace.find(std::make_pair(track, laps));
        return it == mRace.end() ? 0 : it->second;
    }
    long long LapBest(const std::string& track) const
    {
        const auto it = mLap.find(track);
        return it == mLap.end() ? 0 : it->second;
    }
    // Forgets every record (a player who wants a clean slate deletes the file; this is for the same wish from a menu).
    void Clear() { mRace.clear(); mLap.clear(); }
    bool Empty() const { return mRace.empty() && mLap.empty(); }

private:
    // Track names come from files the player may have downloaded: keep out what would break the one-line-per-record format.
    static bool Valid(const std::string& track)
    {
        if (track.empty() || track.size() > 120) return false;
        for (char c : track) if (c == '\t' || c == '\n' || c == '\r') return false;
        return true;
    }

    void ParseLine(const std::string& text)
    {
        std::string field[4];
        std::size_t count = 0, start = 0;
        while (count < 4) {
            const std::size_t tab = text.find('\t', start);
            if (tab == std::string::npos) { field[count++] = text.substr(start); break; }
            field[count++] = text.substr(start, tab - start);
            start = tab + 1;
        }
        auto number = [](const std::string& s, long long& value) {
            if (s.empty() || s.size() > 12) return false;
            long long v = 0;
            for (char c : s) { if (c < '0' || c > '9') return false; v = v * 10 + (c - '0'); }
            value = v;
            return v > 0;
        };
        long long ms = 0, laps = 0;
        if (field[0] == "R" && count == 4 && Valid(field[1]) && number(field[2], laps) && laps <= 1000 && number(field[3], ms)) {
            auto key = std::make_pair(field[1], static_cast<int>(laps));
            auto it = mRace.find(key);
            if (it == mRace.end() || ms < it->second) mRace[key] = ms;
        }
        else if (field[0] == "L" && count == 3 && Valid(field[1]) && number(field[2], ms)) {
            auto it = mLap.find(field[1]);
            if (it == mLap.end() || ms < it->second) mLap[field[1]] = ms;
        }
    }

    std::map<std::pair<std::string, int>, long long> mRace;
    std::map<std::string, long long> mLap;
};
