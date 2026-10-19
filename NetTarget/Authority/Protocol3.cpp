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
    bool AtEnd() const { return pos == b.size(); }
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
    if (m.hasDevice) { o.U64(m.deviceHigh); o.U64(m.deviceLow); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Hello& m)
{
    In in(p);
    for (int i = 0; i < 4; ++i) if (in.U8() != kMagic[i]) return false;
    m.major = static_cast<std::uint16_t>(in.U16()); m.minor = static_cast<std::uint16_t>(in.U16()); m.capabilities = in.U32();
    m.maxFrame = static_cast<std::uint16_t>(in.U16()); m.name = in.Str(32);
    m.hasDevice = false;
    if (in.ok && !in.AtEnd()) { m.deviceHigh = in.U64(); m.deviceLow = in.U64(); m.hasDevice = true; }
    return in.Done() && !m.name.empty() && m.maxFrame >= kMinMaxFrame && (!m.hasDevice || (m.capabilities & kCapDeviceId) != 0);
}

std::vector<std::uint8_t> Encode(const HelloAck& m)
{
    Out o;
    o.U16(m.major); o.U16(m.minor); o.U32(m.capabilities); o.U16(m.maxFrame);
    o.U8(m.player); o.U8(m.players); o.U8(m.tickMs); o.U32(m.rules); o.Str(m.track); o.Str(m.trackSha256); o.U8(m.laps); o.U8(m.weapons ? 1 : 0); o.U64(m.resumeToken);
    if (m.includeRaceId) o.U32(m.raceId);
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
    if (!in.AtEnd()) m.raceId = in.U32();
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

std::vector<std::uint8_t> Encode(const Roster& m)
{
    Out o;
    o.U8(m.seats);
    o.U8(static_cast<std::uint32_t>(m.members.size()));
    for (const auto& member : m.members) { o.U8(member.first); o.Str(member.second); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, Roster& m)
{
    In in(p);
    m.seats = static_cast<std::uint8_t>(in.U8());
    const std::uint32_t count = in.U8();
    if (!in.ok || count > 8 || m.seats < 1 || m.seats > 8) return false;
    m.members.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        const std::uint8_t slot = static_cast<std::uint8_t>(in.U8());
        m.members.push_back(std::make_pair(slot, in.Str(32)));
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const LobbyUsers& m)
{
    Out o;
    o.U16(m.total);
    o.U8(static_cast<std::uint32_t>(m.names.size()));
    for (const std::string& n : m.names) o.Str(n);
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, LobbyUsers& m)
{
    In in(p);
    m.total = static_cast<std::uint16_t>(in.U16());
    const std::uint32_t count = in.U8();
    if (!in.ok || count > static_cast<std::uint32_t>(kMaxLobbyUserNames)) return false;
    m.names.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) m.names.push_back(in.Str(32));
    return in.Done() && m.total >= m.names.size();
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

std::vector<std::uint8_t> Encode(const ListReplays& m) { Out o; o.U8(m.max); o.Str(m.track); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ListReplays& m)
{
    In in(p);
    m.max = static_cast<std::uint8_t>(in.U8());
    m.track = in.Str(64);
    return in.Done() && m.max >= 1 && m.max <= kMaxReplayList;
}

std::vector<std::uint8_t> Encode(const ReplayList& m)
{
    Out o;
    o.U8(static_cast<std::uint32_t>(m.replays.size()));
    for (const ReplayInfo& r : m.replays) {
        o.U64(r.id); o.Str(r.track); o.U8(r.players); o.U8(r.laps); o.U32(r.ticks); o.U32(r.savedAt); o.U32(r.bytes); o.U32(static_cast<std::uint32_t>(r.winnerMs)); o.U8(r.winnerSlot); o.Str(r.winnerName); o.U8(r.winnerCraft);
        o.U8(static_cast<std::uint32_t>(r.finishers.size()));
        for (const ReplayFinisher& f : r.finishers) { o.U8(f.slot); o.U32(static_cast<std::uint32_t>(f.ms)); o.U8(f.craft); o.Str(f.name); }
    }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, ReplayList& m)
{
    In in(p);
    const std::uint32_t count = in.U8();
    if (!in.ok || count > static_cast<std::uint32_t>(kMaxReplayList)) return false;
    m.replays.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        ReplayInfo r;
        r.id = in.U64(); r.track = in.Str(64); r.players = static_cast<std::uint8_t>(in.U8()); r.laps = static_cast<std::uint8_t>(in.U8());
        r.ticks = in.U32(); r.savedAt = in.U32(); r.bytes = in.U32(); r.winnerMs = static_cast<std::int32_t>(in.U32()); r.winnerSlot = static_cast<std::uint8_t>(in.U8()); r.winnerName = in.Str(32); r.winnerCraft = static_cast<std::uint8_t>(in.U8());
        const std::uint32_t finisherCount = in.U8();
        if (finisherCount > 8) return false;
        for (std::uint32_t k = 0; k < finisherCount && in.ok; ++k) {
            ReplayFinisher f;
            f.slot = static_cast<std::uint8_t>(in.U8()); f.ms = static_cast<std::int32_t>(in.U32()); f.craft = static_cast<std::uint8_t>(in.U8()); f.name = in.Str(32);
            if (f.slot >= 8 || f.ms < 0 || (f.craft >= 8 && f.craft != 255)) return false;
            r.finishers.push_back(f);
        }
        if (r.players < 1 || r.players > 8 || r.track.empty() || (r.winnerSlot >= 8 && r.winnerSlot != 255) || (r.winnerCraft >= 8 && r.winnerCraft != 255)) return false;
        m.replays.push_back(r);
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const ListRecords& m) { Out o; o.Str(m.track); o.U8(m.laps); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ListRecords& m)
{
    In in(p);
    m.track = in.Str(64);
    m.laps = static_cast<std::uint8_t>(in.U8());
    return in.Done() && !m.track.empty() && m.laps <= 99;
}

std::vector<std::uint8_t> Encode(const RecordList& m)
{
    Out o;
    o.Str(m.track);
    o.U8(m.laps);
    o.U8(static_cast<std::uint32_t>(m.availableLaps.size()));
    for (std::uint8_t l : m.availableLaps) o.U8(l);
    o.U8(static_cast<std::uint32_t>(m.boards.size()));
    for (const RecordBoard& b : m.boards) {
        o.U8(b.craft);
        o.U8(static_cast<std::uint32_t>(b.entries.size()));
        for (const RecordEntry& e : b.entries) { o.U64(e.replay); o.U8(e.slot); o.U32(static_cast<std::uint32_t>(e.ms)); o.U32(e.savedAt); o.Str(e.name); }
    }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, RecordList& m)
{
    In in(p);
    m.track = in.Str(64);
    m.laps = static_cast<std::uint8_t>(in.U8());
    const std::uint32_t lapCount = in.U8();
    if (!in.ok || m.track.empty() || lapCount > static_cast<std::uint32_t>(kMaxRecordLaps) || m.laps > 99) return false;
    m.availableLaps.clear();
    for (std::uint32_t i = 0; i < lapCount && in.ok; ++i) m.availableLaps.push_back(static_cast<std::uint8_t>(in.U8()));
    const std::uint32_t count = in.U8();
    if (!in.ok || count > static_cast<std::uint32_t>(kMaxRecordBoards)) return false;
    m.boards.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        RecordBoard b;
        b.craft = static_cast<std::uint8_t>(in.U8());
        const std::uint32_t entries = in.U8();
        if (!in.ok || b.craft >= 8 || entries > static_cast<std::uint32_t>(kMaxRecordsPerBoard)) return false;
        for (std::uint32_t k = 0; k < entries && in.ok; ++k) {
            RecordEntry e;
            e.replay = in.U64(); e.slot = static_cast<std::uint8_t>(in.U8()); e.ms = static_cast<std::int32_t>(in.U32()); e.savedAt = in.U32(); e.name = in.Str(32);
            if (e.slot >= 8 || e.ms <= 0) return false;
            b.entries.push_back(e);
        }
        m.boards.push_back(b);
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const ListRecordBreaks& m) { Out o; o.U8(m.max); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ListRecordBreaks& m) { In in(p); m.max = static_cast<std::uint8_t>(in.U8()); return in.Done() && m.max >= 1 && m.max <= kMaxRecordBreakList; }

std::vector<std::uint8_t> Encode(const RecordBreakList& m)
{
    Out o;
    o.U8(static_cast<std::uint32_t>(m.breaks.size()));
    for (const RecordBreakEntry& b : m.breaks) {
        o.U64(b.replay); o.Str(b.track); o.U8(b.slot); o.U8(b.raceLaps); o.U8(b.craft); o.U8(b.lap ? 1 : 0); o.U32(b.savedAt);
        o.U32(static_cast<std::uint32_t>(b.ms)); o.U32(static_cast<std::uint32_t>(b.previousMs)); o.Str(b.name); o.Str(b.previousName);
    }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, RecordBreakList& m)
{
    In in(p);
    const std::uint32_t count = in.U8();
    if (!in.ok || count > static_cast<std::uint32_t>(kMaxRecordBreakList)) return false;
    m.breaks.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        RecordBreakEntry b;
        b.replay = in.U64(); b.track = in.Str(64); b.slot = static_cast<std::uint8_t>(in.U8()); b.raceLaps = static_cast<std::uint8_t>(in.U8()); b.craft = static_cast<std::uint8_t>(in.U8());
        const std::uint32_t lap = in.U8();
        b.savedAt = in.U32(); b.ms = static_cast<std::int32_t>(in.U32()); b.previousMs = static_cast<std::int32_t>(in.U32()); b.name = in.Str(32); b.previousName = in.Str(32);
        if (b.track.empty() || b.slot >= 8 || lap > 1 || b.craft >= 8 || b.ms <= 0 || b.previousMs < 0 || (b.previousMs > 0 && b.ms > b.previousMs)) return false;
        b.lap = lap == 1;
        m.breaks.push_back(b);
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const ListWinners& m) { Out o; o.U8(m.max); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ListWinners& m) { In in(p); m.max = static_cast<std::uint8_t>(in.U8()); return in.Done() && m.max >= 1 && m.max <= kMaxWinnerList; }

std::vector<std::uint8_t> Encode(const WinnerList& m)
{
    Out o;
    o.U8(static_cast<std::uint32_t>(m.winners.size()));
    for (const WinnerEntry& w : m.winners) { o.U64(w.replay); o.Str(w.track); o.U8(w.slot); o.U8(w.laps); o.U8(w.players); o.U32(w.savedAt); o.U32(static_cast<std::uint32_t>(w.ms)); o.U8(w.craft); o.Str(w.name); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, WinnerList& m)
{
    In in(p);
    const std::uint32_t count = in.U8();
    if (!in.ok || count > static_cast<std::uint32_t>(kMaxWinnerList)) return false;
    m.winners.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        WinnerEntry w;
        w.replay = in.U64(); w.track = in.Str(64); w.slot = static_cast<std::uint8_t>(in.U8()); w.laps = static_cast<std::uint8_t>(in.U8()); w.players = static_cast<std::uint8_t>(in.U8());
        w.savedAt = in.U32(); w.ms = static_cast<std::int32_t>(in.U32()); w.craft = static_cast<std::uint8_t>(in.U8()); w.name = in.Str(32);
        if (w.track.empty() || w.slot >= 8 || w.players < 2 || w.players > 8 || w.ms <= 0 || (w.craft >= 8 && w.craft != 255)) return false;
        m.winners.push_back(w);
    }
    return in.Done();
}

static bool ValidMonth(const std::string& s)   // "YYYY-MM"
{
    if (s.size() != 7 || s[4] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6}) if (s[static_cast<std::size_t>(i)] < '0' || s[static_cast<std::size_t>(i)] > '9') return false;
    const int month = (s[5] - '0') * 10 + (s[6] - '0');
    return month >= 1 && month <= 12;
}

std::vector<std::uint8_t> Encode(const ListLeague& m) { Out o; o.Str(m.month); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ListLeague& m) { In in(p); m.month = in.Str(7); return in.Done() && (m.month.empty() || ValidMonth(m.month)); }

static void PutRows(Out& o, const std::vector<LeagueRow>& rows)
{
    o.U8(static_cast<std::uint32_t>(rows.size()));
    for (const LeagueRow& r : rows) { o.Str(r.name); o.U16(r.points); o.U16(r.wins); o.U16(r.races); }
}
static bool GetRows(In& in, std::vector<LeagueRow>& rows, std::uint32_t limit)
{
    const std::uint32_t count = in.U8();
    if (!in.ok || count > limit) return false;
    rows.clear();
    for (std::uint32_t i = 0; i < count && in.ok; ++i) {
        LeagueRow r;
        r.name = in.Str(32); r.points = static_cast<std::uint16_t>(in.U16()); r.wins = static_cast<std::uint16_t>(in.U16()); r.races = static_cast<std::uint16_t>(in.U16());
        rows.push_back(r);
    }
    return in.ok;
}
std::vector<std::uint8_t> Encode(const LeagueData& m)
{
    Out o;
    o.Str(m.currentMonth); o.Str(m.month);
    PutRows(o, m.standings);
    o.U8(static_cast<std::uint32_t>(m.pastWinners.size()));
    for (const LeagueMonthWinner& w : m.pastWinners) { o.Str(w.month); o.Str(w.name); o.U16(w.points); }
    o.U16(m.currentYear);
    PutRows(o, m.yearStandings);
    o.U8(static_cast<std::uint32_t>(m.yearWinners.size()));
    for (const LeagueYearWinner& w : m.yearWinners) { o.U16(w.year); o.Str(w.name); o.U16(w.points); }
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, LeagueData& m)
{
    In in(p);
    m.currentMonth = in.Str(7); m.month = in.Str(7);
    if (!in.ok || !ValidMonth(m.currentMonth) || !ValidMonth(m.month)) return false;
    if (!GetRows(in, m.standings, static_cast<std::uint32_t>(kMaxLeagueRows))) return false;
    const std::uint32_t months = in.U8();
    if (!in.ok || months > static_cast<std::uint32_t>(kMaxLeagueMonths)) return false;
    m.pastWinners.clear();
    for (std::uint32_t i = 0; i < months && in.ok; ++i) {
        LeagueMonthWinner w;
        w.month = in.Str(7); w.name = in.Str(32); w.points = static_cast<std::uint16_t>(in.U16());
        if (!ValidMonth(w.month)) return false;
        m.pastWinners.push_back(w);
    }
    m.currentYear = static_cast<std::uint16_t>(in.U16());
    if (!GetRows(in, m.yearStandings, static_cast<std::uint32_t>(kMaxLeagueRows))) return false;
    const std::uint32_t years = in.U8();
    if (!in.ok || years > static_cast<std::uint32_t>(kMaxLeagueYears)) return false;
    m.yearWinners.clear();
    for (std::uint32_t i = 0; i < years && in.ok; ++i) {
        LeagueYearWinner w;
        w.year = static_cast<std::uint16_t>(in.U16()); w.name = in.Str(32); w.points = static_cast<std::uint16_t>(in.U16());
        m.yearWinners.push_back(w);
    }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const GetReplay& m) { Out o; o.U64(m.id); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, GetReplay& m) { In in(p); m.id = in.U64(); return in.Done(); }

std::vector<std::uint8_t> Encode(const ReplayChunk& m)
{
    Out o;
    o.U64(m.id); o.U16(m.index); o.U16(m.count);
    o.b.insert(o.b.end(), m.data.begin(), m.data.end());
    return o.b;
}
bool Decode(const std::vector<std::uint8_t>& p, ReplayChunk& m)
{
    In in(p);
    m.id = in.U64(); m.index = static_cast<std::uint16_t>(in.U16()); m.count = static_cast<std::uint16_t>(in.U16());
    if (!in.ok || m.count == 0 || m.index >= m.count || p.size() < 12 || p.size() - 12 > kReplayChunkBytes) return false;
    m.data.assign(p.begin() + 12, p.end());
    return true;
}

std::vector<std::uint8_t> Encode(const HostRace& m) { Out o; o.Str(m.track); o.U8(m.players); o.U8(m.laps); o.U8(m.weapons ? 1 : 0); if (!m.code.empty()) o.Str(m.code); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, HostRace& m)
{
    In in(p);
    m.track = in.Str(64); m.players = static_cast<std::uint8_t>(in.U8()); m.laps = static_cast<std::uint8_t>(in.U8());
    const std::uint32_t w = in.U8();
    m.weapons = w != 0;
    m.code.clear();
    if (in.ok && !in.AtEnd()) { m.code = in.Str(16); if (m.code.size() < 6) return false; }   // present means 6 to 16 characters
    return in.Done() && w <= 1 && !m.track.empty();
}

std::vector<std::uint8_t> Encode(const JoinRace& m) { Out o; o.U32(m.id); if (!m.code.empty()) o.Str(m.code); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, JoinRace& m)
{
    In in(p); m.id = in.U32(); m.code.clear();
    if (in.ok && !in.AtEnd()) { m.code = in.Str(16); if (m.code.size() < 6) return false; }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const SpectateRace& m) { Out o; o.U32(m.id); if (!m.code.empty()) o.Str(m.code); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, SpectateRace& m)
{
    In in(p); m.id = in.U32(); m.code.clear();
    if (in.ok && !in.AtEnd()) { m.code = in.Str(16); if (m.code.size() < 6) return false; }
    return in.Done();
}

std::vector<std::uint8_t> Encode(const Chat& m) { Out o; o.Str(m.text); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Chat& m) { In in(p); m.text = in.Str(120); return in.Done() && !m.text.empty(); }
std::vector<std::uint8_t> Encode(const ChatMessage& m) { Out o; o.Str(m.name); o.Str(m.text); o.U8(m.fromServer ? 1 : 0); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, ChatMessage& m)
{
    In in(p);
    m.name = in.Str(32); m.text = in.Str(120);
    const std::uint32_t f = in.U8();
    m.fromServer = f != 0;
    return in.Done() && f <= 1;
}
std::vector<std::uint8_t> Encode(const Block& m) { Out o; o.Str(m.name); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Block& m) { In in(p); m.name = in.Str(32); return in.Done() && !m.name.empty(); }
std::vector<std::uint8_t> Encode(const Report& m) { Out o; o.Str(m.name); o.Str(m.reason); return o.b; }
bool Decode(const std::vector<std::uint8_t>& p, Report& m) { In in(p); m.name = in.Str(32); m.reason = in.Str(120); return in.Done() && !m.name.empty(); }
}
