// A ghost: the path of the player's best run on a track, recorded as it was driven and shown beside the craft in a later race on the same
// track and lap count, so the player races their own best time. It lives in the per-user settings folder next to the personal bests (the
// browser build keeps that folder in IndexedDB) and never leaves the player's machine. Header only, no SDL and no game headers, so it is
// unit tested on its own (GhostRunTest.cpp).
//
// The ghost is only drawn. It is not part of the simulation, so it cannot touch another craft, change the race or the state hash, and the same
// code serves local races and races on a server.
//
// File: "HNGHOST1", then little-endian: laps (int32), finish time in ms (int32), track name length (uint16) and the name, sample count (uint32),
// then per sample: time ms, x, y, z (int32 each), orientation and room (int16 each). Anything that does not parse, or is larger than the limits
// below, is ignored as if there were no ghost.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class GhostRun
{
public:
    struct Sample
    {
        std::int32_t t = 0;             // race time in milliseconds (0 = the start)
        std::int32_t x = 0, y = 0, z = 0;
        std::int16_t orientation = 0;   // the game's angle units, 0 to angleRange - 1
        std::int16_t room = 0;
    };

    static constexpr int kIntervalMs = 48;            // samples are kept at most this often (three simulation ticks)
    static constexpr std::size_t kMaxSamples = 200000;   // about 2 hours; a larger file is not read
    static constexpr std::size_t kMaxFileBytes = 8 * 1024 * 1024;

    std::string track;
    int laps = 0;
    std::int32_t finishMs = 0;
    std::vector<Sample> samples;

    bool Empty() const { return samples.empty(); }
    void Clear() { track.clear(); laps = 0; finishMs = 0; samples.clear(); }

    // Adds a point of the run being driven. Times before the start, repeats and anything closer than the interval to the last point are
    // skipped; a time that goes backwards (the race was restarted) clears the run so far.
    void Record(std::int64_t timeMs, std::int32_t x, std::int32_t y, std::int32_t z, int orientation, int room)
    {
        if (timeMs < 0 || timeMs > 2000000000LL) return;
        if (!samples.empty()) {
            if (timeMs < samples.back().t) samples.clear();
            else if (timeMs - samples.back().t < kIntervalMs) return;
        }
        if (samples.size() >= kMaxSamples) return;
        Sample s;
        s.t = static_cast<std::int32_t>(timeMs);
        s.x = x; s.y = y; s.z = z;
        s.orientation = static_cast<std::int16_t>(orientation);
        s.room = static_cast<std::int16_t>(room);
        samples.push_back(s);
    }

    // Where the ghost is at a race time, between the two recorded points around it (the angle takes the short way round). False before the
    // recording starts and for a second after it ends, when the ghost has finished and is no longer shown.
    bool At(std::int64_t timeMs, Sample& out, int angleRange = 4096) const
    {
        if (samples.empty() || timeMs < samples.front().t || timeMs > static_cast<std::int64_t>(samples.back().t) + 1000) return false;
        if (timeMs >= samples.back().t) { out = samples.back(); return true; }
        std::size_t low = 0, high = samples.size() - 1;   // samples[low].t <= timeMs < samples[high].t
        while (high - low > 1) {
            const std::size_t middle = (low + high) / 2;
            if (samples[middle].t <= timeMs) low = middle; else high = middle;
        }
        const Sample& a = samples[low];
        const Sample& b = samples[high];
        const std::int64_t span = static_cast<std::int64_t>(b.t) - a.t;
        const std::int64_t into = timeMs - a.t;
        auto lerp = [&](std::int32_t from, std::int32_t to) { return static_cast<std::int32_t>(from + (static_cast<std::int64_t>(to) - from) * into / span); };
        out.t = static_cast<std::int32_t>(timeMs);
        out.x = lerp(a.x, b.x); out.y = lerp(a.y, b.y); out.z = lerp(a.z, b.z);
        int delta = (b.orientation - a.orientation) % angleRange;
        if (delta > angleRange / 2) delta -= angleRange;
        if (delta < -angleRange / 2) delta += angleRange;
        int angle = static_cast<int>(a.orientation + delta * into / span) % angleRange;
        if (angle < 0) angle += angleRange;
        out.orientation = static_cast<std::int16_t>(angle);
        out.room = a.room;
        return true;
    }

    // The file name for a track and lap count: only letters, digits, '.', '-' and '_' are kept, so a track name can never name another path.
    static std::string FileName(const std::string& trackName, int lapCount)
    {
        std::string clean;
        for (char c : trackName) {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            clean += keep ? c : '_';
        }
        if (clean.empty() || clean[0] == '.') clean = "_" + clean;
        if (clean.size() > 80) clean.resize(80);
        return "ghost_" + clean + "_" + std::to_string(lapCount) + ".bin";
    }

    bool Save(const std::string& path) const
    {
        if (samples.empty() || samples.size() > kMaxSamples || track.size() > 0xffff) return false;
        std::vector<std::uint8_t> bytes;
        bytes.reserve(32 + track.size() + samples.size() * 20);
        const char magic[] = "HNGHOST1";
        bytes.insert(bytes.end(), magic, magic + 8);
        Put32(bytes, static_cast<std::uint32_t>(laps));
        Put32(bytes, static_cast<std::uint32_t>(finishMs));
        Put16(bytes, static_cast<std::uint16_t>(track.size()));
        bytes.insert(bytes.end(), track.begin(), track.end());
        Put32(bytes, static_cast<std::uint32_t>(samples.size()));
        for (const Sample& s : samples) {
            Put32(bytes, static_cast<std::uint32_t>(s.t)); Put32(bytes, static_cast<std::uint32_t>(s.x));
            Put32(bytes, static_cast<std::uint32_t>(s.y)); Put32(bytes, static_cast<std::uint32_t>(s.z));
            Put16(bytes, static_cast<std::uint16_t>(s.orientation)); Put16(bytes, static_cast<std::uint16_t>(s.room));
        }
        const std::string temp = path + ".tmp";
        std::FILE* file = std::fopen(temp.c_str(), "wb");
        if (file == nullptr) return false;
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        const bool closed = std::fclose(file) == 0;
        if (!written || !closed) { std::remove(temp.c_str()); return false; }
        if (std::rename(temp.c_str(), path.c_str()) != 0) {
            std::remove(path.c_str());   // Windows does not rename over an existing file
            if (std::rename(temp.c_str(), path.c_str()) != 0) { std::remove(temp.c_str()); return false; }
        }
        return true;
    }

    // Reads a ghost file; false (and an empty ghost) for a missing, truncated, oversized or otherwise unusable file.
    bool Load(const std::string& path)
    {
        Clear();
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) return false;
        std::vector<std::uint8_t> bytes;
        std::uint8_t chunk[4096];
        std::size_t got;
        while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
            bytes.insert(bytes.end(), chunk, chunk + got);
            if (bytes.size() > kMaxFileBytes) { std::fclose(file); return false; }
        }
        std::fclose(file);
        const bool ok = Parse(bytes);
        if (!ok) Clear();
        return ok;
    }

private:
    static void Put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
    static void Put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }

    bool Parse(const std::vector<std::uint8_t>& b)
    {
        std::size_t at = 0;
        auto need = [&](std::size_t n) { return b.size() - at >= n; };
        auto get16 = [&]() { const std::uint16_t v = static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8)); at += 2; return v; };
        auto get32 = [&]() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b[at + i]) << (8 * i); at += 4; return v; };
        if (!need(8) || std::string(b.begin(), b.begin() + 8) != "HNGHOST1") return false;
        at = 8;
        if (!need(10)) return false;
        laps = static_cast<std::int32_t>(get32());
        finishMs = static_cast<std::int32_t>(get32());
        const std::size_t nameLength = get16();
        if (!need(nameLength + 4)) return false;
        track.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + nameLength));
        at += nameLength;
        const std::size_t count = get32();
        if (count == 0 || count > kMaxSamples || !need(count * 20)) return false;
        samples.resize(count);
        std::int64_t previous = -1;
        for (Sample& s : samples) {
            s.t = static_cast<std::int32_t>(get32()); s.x = static_cast<std::int32_t>(get32());
            s.y = static_cast<std::int32_t>(get32()); s.z = static_cast<std::int32_t>(get32());
            s.orientation = static_cast<std::int16_t>(get16()); s.room = static_cast<std::int16_t>(get16());
            if (s.t < 0 || s.t <= previous) return false;   // times must be increasing, or the lookup would not make sense
            previous = s.t;
        }
        return laps > 0 && finishMs > 0;
    }
};
