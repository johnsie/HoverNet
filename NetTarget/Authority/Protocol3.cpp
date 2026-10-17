#include "Protocol3.h"

namespace Protocol3
{
namespace
{
struct Out
{
    std::vector<std::uint8_t> b;
    void U8(std::uint32_t v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void U16(std::uint32_t v) { U8(v); U8(v >> 8); }
    void U32(std::uint32_t v) { U16(v); U16(v >> 16); }
    void U64(std::uint64_t v) { U32(static_cast<std::uint32_t>(v)); U32(static_cast<std::uint32_t>(v >> 32)); }
    void Str(const std::string& s) { U8(static_cast<std::uint32_t>(s.size())); b.insert(b.end(), s.begin(), s.end()); }
};

struct In
{
    const std::vector<std::uint8_t>& b;
    std::size_t pos = 0;
    bool ok = true;
    explicit In(const std::vector<std::uint8_t>& v) : b(v) {}
    bool Need(std::size_t n) { if (!ok || b.size() - pos < n) ok = false; return ok; }
    std::uint32_t U8() { if (!Need(1)) return 0; return b[pos++]; }
    std::uint32_t U16() { const std::uint32_t lo = U8(); return lo | (U8() << 8); }
    std::uint32_t U32() { const std::uint32_t lo = U16(); return lo | (U16() << 16); }
    std::uint64_t U64() { const std::uint64_t lo = U32(); return lo | (static_cast<std::uint64_t>(U32()) << 32); }
    std::string Str(std::size_t maxBytes)
    {
        const std::size_t n = U8();
        if (n > maxBytes || !Need(n)) { ok = false; return std::string(); }
        std::string s(b.begin() + static_cast<std::ptrdiff_t>(pos), b.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        if (!IsValidText(s, maxBytes)) ok = false;
        return s;
    }
    bool Done() const { return ok && pos == b.size(); }
};
}

bool IsValidText(const std::string& s, std::size_t maxBytes)
{
    if (s.size() > maxBytes) return false;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::uint8_t c = static_cast<std::uint8_t>(s[i]);
        if (c < 0x20 || c == 0x7F) return false;                       // control bytes, including NUL, tab and newline
        if (c < 0x80) { ++i; continue; }
        int extra = 0;
        std::uint32_t cp = 0, minimum = 0;
        if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; minimum = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; minimum = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; minimum = 0x10000; }
        else return false;
        if (i + extra >= s.size()) return false;                          // truncated sequence
        for (int k = 1; k <= extra; ++k) {
            const std::uint8_t d = static_cast<std::uint8_t>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x80 && cp < 0xA0) return false;                      // C1 controls
        i += extra + 1;
    }
    return true;
}

std::vector<std::uint8_t> EncodeFrame(std::uint8_t type, const std::vector<std::uint8_t>& payload)
{
    Out o;
    o.U16(static_cast<std::uint32_t>(payload.size() + 1));
    o.U8(type);
    o.b.insert(o.b.end(), payload.begin(), payload.end());
    return o.b;
}

void FrameParser::Feed(const std::uint8_t* data, std::size_t size)
{
    if (mFailed) return;
    mBuffer.insert(mBuffer.end(), data, data + size);
}

bool FrameParser::Next(std::uint8_t& type, std::vector<std::uint8_t>& payload)
{
    if (mFailed || mBuffer.size() < 2) return false;
    const std::size_t length = mBuffer[0] | (static_cast<std::size_t>(mBuffer[1]) << 8);
    if (length == 0 || length > mMaxFrame) { mFailed = true; mBuffer.clear(); return false; }
    if (mBuffer.size() < 2 + length) return false;
    type = mBuffer[2];
    payload.assign(mBuffer.begin() + 3, mBuffer.begin() + static_cast<std::ptrdiff_t>(2 + length));
    mBuffer.erase(mBuffer.begin(), mBuffer.begin() + static_cast<std::ptrdiff_t>(2 + length));
    return true;
}

// ---- messages ----------------------------------------------------------------------------------------------
static const std::uint8_t kMagic[4] = {'H', 'N', '3', 0};

std::vector<std::uint8_t> Encode(const Hello& m)
{
    Out o;
    for (int i = 0; i < 4; ++i) o.U8(kMagic[i]);
    o.U16(m.major); o.U16(m.minor); o.U32(m.capabilities); o.U16(m.maxFrame); o.Str(m.name);
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Hello& m)
{
    In in(p);
    for (int i = 0; i < 4; ++i) if (in.U8() != kMagic[i]) return false;
    m.major = static_cast<std::uint16_t>(in.U16()); m.minor = static_cast<std::uint16_t>(in.U16()); m.capabilities = in.U32();
    m.maxFrame = static_cast<std::uint16_t>(in.U16()); m.name = in.Str(32);
    return in.Done() && !m.name.empty() && m.maxFrame >= kMinMaxFrame;
}

std::vector<std::uint8_t> Encode(const HelloAck& m)
{
    Out o;
    o.U16(m.major); o.U16(m.minor); o.U32(m.capabilities); o.U16(m.maxFrame);
    o.U8(m.player); o.U8(m.players); o.U8(m.tickMs); o.U32(m.rules); o.Str(m.track); o.Str(m.trackSha256); o.U8(m.laps); o.U8(m.weapons ? 1 : 0); o.U64(m.resumeToken);
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, HelloAck& m)
{
    In in(p);
    m.major = static_cast<std::uint16_t>(in.U16()); m.minor = static_cast<std::uint16_t>(in.U16()); m.capabilities = in.U32();
    m.maxFrame = static_cast<std::uint16_t>(in.U16()); m.player = static_cast<std::uint8_t>(in.U8()); m.players = static_cast<std::uint8_t>(in.U8());
    m.tickMs = static_cast<std::uint8_t>(in.U8()); m.rules = in.U32(); m.track = in.Str(64); m.trackSha256 = in.Str(64); m.laps = static_cast<std::uint8_t>(in.U8());
    const std::uint32_t w = in.U8(); m.weapons = w != 0;
    m.resumeToken = in.U64();
    return in.Done() && w <= 1 && m.players >= 1 && m.players <= 8 && m.player < m.players && m.tickMs >= 5;
}

std::vector<std::uint8_t> Encode(const Reject& m) { Out o; o.U8(m.reason); o.Str(m.message); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Reject& m) { In in(p); m.reason = static_cast<std::uint8_t>(in.U8()); m.message = in.Str(120); return in.Done(); }

std::vector<std::uint8_t> Encode(const Resume& m) { Out o; o.U64(m.token); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Resume& m) { In in(p); m.token = in.U64(); return in.Done() && m.token != 0; }

std::vector<std::uint8_t> Encode(const Start& m) { Out o; o.U32(m.countdownMs); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Start& m) { In in(p); m.countdownMs = in.U32(); return in.Done(); }

std::vector<std::uint8_t> Encode(const Input& m)
{
    Out o;
    o.U32(m.firstTick); o.U8(static_cast<std::uint32_t>(m.controls.size()));
    for (std::uint16_t c : m.controls) o.U16(c);
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Input& m)
{
    In in(p);
    m.firstTick = in.U32();
    const std::uint32_t count = in.U8();
    if (!in.ok || count == 0 || count > static_cast<std::uint32_t>(kMaxInputsPerMessage) || !in.Need(count * 2)) return false;
    m.controls.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) m.controls[i] = static_cast<std::uint16_t>(in.U16());
    return in.Done();
}

std::vector<std::uint8_t> Encode(const Tick& m)
{
    Out o;
    o.U32(m.tick); o.U64(m.hash); o.U8(static_cast<std::uint32_t>(m.controls.size()));
    for (std::uint16_t c : m.controls) o.U16(c);
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Tick& m)
{
    In in(p);
    m.tick = in.U32(); m.hash = in.U64();
    const std::uint32_t count = in.U8();
    if (!in.ok || count < 1 || count > 8 || !in.Need(count * 2)) return false;
    m.controls.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) m.controls[i] = static_cast<std::uint16_t>(in.U16());
    return in.Done();
}

std::vector<std::uint8_t> Encode(const SnapshotRequest& m) { Out o; o.U32(m.id); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, SnapshotRequest& m) { In in(p); m.id = in.U32(); return in.Done(); }

std::vector<std::uint8_t> Encode(const SnapshotChunk& m)
{
    Out o;
    o.U32(m.id); o.U16(m.index); o.U16(m.count); o.U32(m.tick); o.U16(static_cast<std::uint32_t>(m.data.size()));
    o.b.insert(o.b.end(), m.data.begin(), m.data.end());
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, SnapshotChunk& m)
{
    In in(p);
    m.id = in.U32(); m.index = static_cast<std::uint16_t>(in.U16()); m.count = static_cast<std::uint16_t>(in.U16()); m.tick = in.U32();
    const std::uint32_t size = in.U16();
    if (!in.ok || m.count == 0 || m.index >= m.count || !in.Need(size)) return false;
    m.data.assign(p.begin() + static_cast<std::ptrdiff_t>(in.pos), p.begin() + static_cast<std::ptrdiff_t>(in.pos + size));
    in.pos += size;
    return in.Done();
}

std::vector<std::uint8_t> Encode(const Ping& m) { Out o; o.U32(m.clientTick); o.U32(m.token); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Ping& m) { In in(p); m.clientTick = in.U32(); m.token = in.U32(); return in.Done(); }
std::vector<std::uint8_t> Encode(const Pong& m) { Out o; o.U32(m.clientTick); o.U32(m.token); o.U32(m.serverTick); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Pong& m) { In in(p); m.clientTick = in.U32(); m.token = in.U32(); m.serverTick = in.U32(); return in.Done(); }

std::vector<std::uint8_t> Encode(const Result& m)
{
    Out o;
    o.U32(m.ticks); o.U64(m.finalHash); o.U8(static_cast<std::uint32_t>(m.players.size()));
    for (const PlayerResult& r : m.players) { o.U8(r.player); o.U8(r.rank); o.U8(r.finished ? 1 : 0); o.U8(r.laps); o.U32(static_cast<std::uint32_t>(r.finishTimeMs)); o.U32(static_cast<std::uint32_t>(r.bestLapMs)); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Result& m)
{
    In in(p);
    m.ticks = in.U32(); m.finalHash = in.U64();
    const std::uint32_t count = in.U8();
    if (!in.ok || count < 1 || count > 8 || !in.Need(count * 12)) return false;
    m.players.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PlayerResult& r = m.players[i];
        r.player = static_cast<std::uint8_t>(in.U8()); r.rank = static_cast<std::uint8_t>(in.U8());
        const std::uint32_t fin = in.U8(); r.finished = fin != 0; if (fin > 1) return false;
        r.laps = static_cast<std::uint8_t>(in.U8()); r.finishTimeMs = static_cast<std::int32_t>(in.U32()); r.bestLapMs = static_cast<std::int32_t>(in.U32());
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const RaceList& m)
{
    Out o;
    o.U8(static_cast<std::uint32_t>(m.races.size()));
    for (const RaceInfo& r : m.races) { o.U32(r.id); o.Str(r.track); o.U8(r.players); o.U8(r.joined); o.U8(r.laps); o.U8(r.weapons ? 1 : 0); o.U8(r.started ? 1 : 0); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, RaceList& m)
{
    In in(p);
    const std::uint32_t count = in.U8();
    if (!in.ok || count > 32) return false;
    m.races.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        RaceInfo r;
        r.id = in.U32(); r.track = in.Str(64); r.players = static_cast<std::uint8_t>(in.U8()); r.joined = static_cast<std::uint8_t>(in.U8());
        r.laps = static_cast<std::uint8_t>(in.U8());
        const std::uint32_t w = in.U8(), st = in.U8();
        if (w > 1 || st > 1 || r.players < 1 || r.players > 8 || r.joined > r.players) return false;
        r.weapons = w != 0; r.started = st != 0;
        m.races.push_back(r);
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const HostRace& m) { Out o; o.Str(m.track); o.U8(m.players); o.U8(m.laps); o.U8(m.weapons ? 1 : 0); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, HostRace& m)
{
    In in(p);
    m.track = in.Str(64); m.players = static_cast<std::uint8_t>(in.U8()); m.laps = static_cast<std::uint8_t>(in.U8());
    const std::uint32_t w = in.U8();
    m.weapons = w != 0;
    return in.Done() && w <= 1 && !m.track.empty();
}

std::vector<std::uint8_t> Encode(const JoinRace& m) { Out o; o.U32(m.id); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, JoinRace& m) { In in(p); m.id = in.U32(); return in.Done(); }

std::vector<std::uint8_t> Encode(const SpectateRace& m) { Out o; o.U32(m.id); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, SpectateRace& m) { In in(p); m.id = in.U32(); return in.Done(); }
}
