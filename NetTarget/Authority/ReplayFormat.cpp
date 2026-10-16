#include "ReplayFormat.h"

namespace ReplayFormat
{
namespace
{
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

}

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
    w.U16(static_cast<std::uint32_t>(r.laps & 0xFF) | (r.weapons ? 0x100u : 0u));   // flags: laps and weapons
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
    const std::uint32_t flags = in.U16();
    r.laps = static_cast<int>(flags & 0xFF);
    r.weapons = (flags & 0x100) != 0;
    if (flags == 0) { r.laps = 5; r.weapons = true; }   // files from before the settings were recorded
    if (flags & ~0x1FFu) { error = "unknown flags"; return false; }
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

}
