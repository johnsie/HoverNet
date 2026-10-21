#include <cctype>
#include "LobbyServer.h"

#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>

namespace Authority
{
namespace
{
// A community track name is used as a file name: no path separators, no ".." and nothing a file system treats specially, at most what a message can carry.
bool SafeCommunityName(const std::string& name)
{
    if (name.empty() || name.size() > 63 || name == "." || name == "..") return false;
    for (unsigned char c : name) if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    return name.find("..") == std::string::npos;
}

bool FileExists(const std::string& path)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(path.c_str(), &st) == 0 && (st.st_mode & _S_IFREG) != 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}
}

struct LobbyServer::Entry
{
    std::uint32_t id = 0;
    std::unique_ptr<RaceHost> host;
    HostConfig cfg;
    std::int64_t finishedAt = -1;
    std::int64_t emptySince = -1;   // since when a started race has had nobody in it (-1: somebody is, or it has not started)
    int badAttempts = 0;            // wrong codes tried on this race; after 30 it refuses everyone
    std::string code;               // non-empty: a private race, unlisted, joined or watched only with this code
};

struct LobbyServer::Conn
{
    int id = 0;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> out;
    bool closed = false;
    bool helloDone = false;
    std::string name;
    std::uint32_t caps = 0;   // Hello.capabilities, passed on to the race the connection joins
    bool hasDevice = false;   // the device id of the Hello, passed on the same way (the league)
    std::uint64_t deviceHigh = 0, deviceLow = 0;
    std::int64_t connectedMs = 0;
    double chatTokens = 3;          // chat in the lobby: a burst, then one message a second
    std::int64_t lastChatMs = 0;
    std::vector<std::int64_t> invalidTimes, commandTimes;
    std::uint64_t replayBytesSent = 0;   // shared replays sent on this connection
    std::int64_t lastReplayRequestMs = -100000;
    std::uint32_t raceId = 0;       // 0 until attached
    int hostConn = 0;
};

LobbyServer::LobbyServer(const LobbyConfig& config) : mConfig(config), mNextConn(1), mNextRace(1), mSeed(config.raceTemplate.tokenSeed) {}
LobbyServer::~LobbyServer() {}

LobbyServer::Conn* LobbyServer::Find(int id)
{
    for (auto& c : mConns) if (c->id == id) return c.get();
    return nullptr;
}
const LobbyServer::Conn* LobbyServer::Find(int id) const
{
    for (const auto& c : mConns) if (c->id == id) return c.get();
    return nullptr;
}

int LobbyServer::RunningRaces() const
{
    int n = 0;
    for (const auto& r : mRaces) if (r.second->host->Started() && !r.second->host->RaceFinished()) ++n;
    return n;
}

int LobbyServer::ActivePlayers() const
{
    int n = 0;
    for (const auto& r : mRaces) if (!r.second->host->RaceFinished()) n += r.second->host->ConnectedPlayers();
    return n;
}

const RaceHost* LobbyServer::Race(std::uint32_t id) const
{
    auto it = mRaces.find(id);
    return it == mRaces.end() ? nullptr : it->second->host.get();
}

std::vector<std::uint32_t> LobbyServer::RaceIds() const
{
    std::vector<std::uint32_t> ids;
    for (const auto& r : mRaces) ids.push_back(r.first);
    return ids;
}

static void AddMetrics(RaceHost::Metrics& a, const RaceHost::Metrics& b)
{
    a.connectionsAccepted += b.connectionsAccepted; a.connectionsClosed += b.connectionsClosed; a.bytesIn += b.bytesIn; a.bytesOut += b.bytesOut;
    a.framesIn += b.framesIn; a.framesOut += b.framesOut; a.invalidMessages += b.invalidMessages; a.rejectedConnections += b.rejectedConnections;
    a.ticksSimulated += b.ticksSimulated; a.inputsAccepted += b.inputsAccepted; a.inputsLate += b.inputsLate; a.inputsTooFar += b.inputsTooFar;
    a.snapshotsServed += b.snapshotsServed; a.tickBytesOut += b.tickBytesOut;
}

RaceHost::Metrics LobbyServer::TotalMetrics() const
{
    RaceHost::Metrics total = mRemoved;
    for (const auto& r : mRaces) AddMetrics(total, r.second->host->GetMetrics());
    return total;
}

int LobbyServer::OpenConnections() const
{
    int n = 0;
    for (const auto& c : mConns) if (!IsClosed(c->id)) ++n;
    return n;
}

int LobbyServer::Connect(std::int64_t nowMs)
{
    std::unique_ptr<Conn> c(new Conn);
    c->id = mNextConn++;
    c->connectedMs = nowMs;
    const int id = c->id;
    // Too many connections: accept the socket's bytes but refuse at once with a message.
    const bool full = OpenConnections() >= mConfig.maxConnections;
    mConns.push_back(std::move(c));
    if (full) Close(*mConns.back(), Protocol3::kServerFull, "the server is full");
    return id;
}

void LobbyServer::Disconnect(int connection)
{
    Conn* c = Find(connection);
    if (c == nullptr || c->closed) return;
    c->closed = true;
    if (c->raceId != 0) {
        auto it = mRaces.find(c->raceId);
        if (it != mRaces.end()) it->second->host->Disconnect(c->hostConn);
    }
}

bool LobbyServer::IsClosed(int connection) const
{
    const Conn* c = Find(connection);
    if (c == nullptr) return true;
    if (c->raceId != 0) {
        auto it = mRaces.find(c->raceId);
        if (it == mRaces.end()) return true;
        return it->second->host->IsClosed(c->hostConn);
    }
    return c->closed;
}

std::vector<std::uint8_t> LobbyServer::TakeOutput(int connection)
{
    Conn* c = Find(connection);
    std::vector<std::uint8_t> out;
    if (c == nullptr) return out;
    out.swap(c->out);
    if (c->raceId != 0) {
        auto it = mRaces.find(c->raceId);
        if (it != mRaces.end()) {
            const std::vector<std::uint8_t> more = it->second->host->TakeOutput(c->hostConn);
            out.insert(out.end(), more.begin(), more.end());
        }
    }
    return out;
}

void LobbyServer::Close(Conn& c, std::uint8_t reason, const std::string& message)
{
    if (c.closed) return;
    Protocol3::Reject r;
    r.reason = reason;
    r.message = message;
    const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kReject, Protocol3::Encode(r));
    c.out.insert(c.out.end(), f.begin(), f.end());
    c.closed = true;
}

void LobbyServer::Invalid(Conn& c, std::int64_t nowMs)
{
    c.invalidTimes.push_back(nowMs);
    c.invalidTimes.erase(std::remove_if(c.invalidTimes.begin(), c.invalidTimes.end(),
                                        [&](std::int64_t t) { return nowMs - t > mConfig.invalidWindowMs; }), c.invalidTimes.end());
    if (static_cast<int>(c.invalidTimes.size()) > mConfig.maxInvalidPerWindow) Close(c, Protocol3::kBadMessage, "too many invalid messages");
}

// A public race accepts anyone. A private one needs its code; a wrong code is answered exactly like a race that does not exist, and a
// wrong guess closes the connection, and after 30 wrong guesses the race refuses everyone, so codes cannot be guessed.
bool LobbyServer::CodeAccepted(Conn&, Entry& e, const std::string& code)
{
    if (e.code.empty()) return true;
    if (e.badAttempts >= 30) return false;
    unsigned diff = static_cast<unsigned>(e.code.size() ^ code.size());
    for (std::size_t i = 0; i < e.code.size() && i < code.size(); ++i) diff |= static_cast<unsigned>(e.code[i] ^ code[i]);
    if (diff == 0) return true;
    ++e.badAttempts;
    return false;
}

void LobbyServer::SendList(Conn& c)
{
    Protocol3::RaceList list;
    for (const auto& r : mRaces) {
        if (list.races.size() >= 32) break;
        if (!r.second->code.empty()) continue;   // private races are not listed
        const RaceHost& h = *r.second->host;
        Protocol3::RaceInfo info;
        info.id = r.first;
        info.track = h.Config().race.trackName;
        info.players = static_cast<std::uint8_t>(h.Config().race.players);
        info.joined = static_cast<std::uint8_t>(h.ConnectedPlayers());
        info.laps = static_cast<std::uint8_t>(h.Config().race.laps);
        info.weapons = h.Config().race.weapons;
        info.started = h.Started();
        list.races.push_back(info);
    }
    const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kRaceList, Protocol3::Encode(list));
    c.out.insert(c.out.end(), f.begin(), f.end());
}

void LobbyServer::Attach(Conn& c, Entry& e, const std::vector<std::uint8_t>& firstFrame, std::int64_t nowMs)
{
    c.raceId = e.id;
    c.hostConn = e.host->Connect(nowMs);
    e.host->Receive(c.hostConn, firstFrame.data(), firstFrame.size(), nowMs);
}

void LobbyServer::Receive(int connection, const std::uint8_t* data, std::size_t size, std::int64_t nowMs)
{
    Conn* c = Find(connection);
    if (c == nullptr || c->closed) return;
    c->parser.Feed(data, size);
    std::uint8_t type = 0;
    std::vector<std::uint8_t> payload;
    while (!c->closed && c->parser.Next(type, payload)) {
        if (c->raceId != 0) {
            // In a race: the frame goes on to the race's host exactly as it came.
            auto it = mRaces.find(c->raceId);
            if (it == mRaces.end()) { c->closed = true; break; }
            const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(type, payload);
            it->second->host->Receive(c->hostConn, frame.data(), frame.size(), nowMs);
        }
        else {
            Handle(*c, type, payload, nowMs);
        }
    }
    if (!c->closed && c->raceId == 0 && c->parser.Failed()) Close(*c, Protocol3::kBadMessage, "bad frame");
    if (c->raceId != 0 && c->parser.Failed()) Disconnect(connection);
}

namespace
{
constexpr std::size_t kMaxListPayload = 1100;   // frames are at most 1200 bytes (kDefaultMaxFrame), with a header
}

void LobbyServer::Handle(Conn& c, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t nowMs)
{
    using namespace Protocol3;
    if (type == kHello && !c.helloDone) {
        Hello h;
        if (!Decode(payload, h)) { Close(c, kBadMessage, "malformed hello"); return; }
        if (h.major != kMajor) { Close(c, kUnsupportedVersion, "this server speaks protocol 3"); return; }
        c.helloDone = true;
        c.name = h.name;
        c.caps = h.capabilities;
        c.hasDevice = h.hasDevice; c.deviceHigh = h.deviceHigh; c.deviceLow = h.deviceLow;
        c.parser.SetMaxFrame(std::min<std::uint16_t>(h.maxFrame, kDefaultMaxFrame));
        SendList(c);
        return;
    }
    if (type == kResume && !c.helloDone) {
        Resume m;
        if (!Decode(payload, m)) { Close(c, kBadMessage, "malformed resume"); return; }
        for (auto& r : mRaces) {
            if (r.second->host->OwnsResumeToken(m.token)) {
                c.helloDone = true;
                Attach(c, *r.second, EncodeFrame(kResume, payload), nowMs);
                return;
            }
        }
        Close(c, kNotInRace, "unknown resume token");
        return;
    }
    if (!c.helloDone) { Close(c, kBadMessage, "hello first"); return; }

    // Lobby commands are rate limited.
    c.commandTimes.push_back(nowMs);
    c.commandTimes.erase(std::remove_if(c.commandTimes.begin(), c.commandTimes.end(), [&](std::int64_t t) { return nowMs - t > mConfig.invalidWindowMs; }), c.commandTimes.end());
    if (static_cast<int>(c.commandTimes.size()) > mConfig.maxLobbyCommandsPerWindow) { Close(c, kRateLimited, "too many lobby requests"); return; }

    if (type == kListReplays) {
        ListReplays m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        ReplayList list;
        if (mReplays != nullptr) list.replays = mReplays->List(m.track, std::min<int>(m.max, kMaxReplayList));
        while (list.replays.size() > 1 && Encode(list).size() > kMaxListPayload) list.replays.pop_back();   // a message must fit in one frame: the oldest go first
        const std::vector<std::uint8_t> f = EncodeFrame(kReplayList, Encode(list));
        c.out.insert(c.out.end(), f.begin(), f.end());
        return;
    }
    if (type == kListWinners) {
        ListWinners m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        WinnerList list;
        if (mReplays != nullptr) list = mReplays->Winners(std::min<int>(m.max, kMaxWinnerList));
        while (list.winners.size() > 1 && Encode(list).size() > kMaxListPayload) list.winners.pop_back();   // the oldest go first
        const std::vector<std::uint8_t> f = EncodeFrame(kWinnerList, Encode(list));
        c.out.insert(c.out.end(), f.begin(), f.end());
        return;
    }
    if (type == kListRecordBreaks) {
        ListRecordBreaks m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        RecordBreakList list;
        if (mReplays != nullptr) list = mReplays->RecordBreaks(std::min<int>(m.max, kMaxRecordBreakList));
        while (list.breaks.size() > 1 && Encode(list).size() > kMaxListPayload) list.breaks.pop_back();   // the oldest go first
        const std::vector<std::uint8_t> f = EncodeFrame(kRecordBreakList, Encode(list));
        c.out.insert(c.out.end(), f.begin(), f.end());
        return;
    }
    if (type == kListLeague) {
        ListLeague m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        LeagueData data;
        if (mLeague) data = mLeague(m.month);
        else { data.currentMonth = data.month = m.month.empty() ? "1970-01" : m.month; }
        // A message must fit in one frame: shorten the longest list until it does (the standings of the month last).
        while (Encode(data).size() > kMaxListPayload) {
            if (data.pastWinners.size() > 3 && data.pastWinners.size() >= data.yearStandings.size()) data.pastWinners.pop_back();
            else if (data.yearStandings.size() > 3) data.yearStandings.pop_back();
            else if (data.standings.size() > 3) data.standings.pop_back();
            else if (!data.pastWinners.empty()) data.pastWinners.pop_back();
            else if (!data.yearStandings.empty()) data.yearStandings.pop_back();
            else if (!data.standings.empty()) data.standings.pop_back();
            else break;
        }
        const std::vector<std::uint8_t> f = EncodeFrame(kLeagueData, Encode(data));
        c.out.insert(c.out.end(), f.begin(), f.end());
        return;
    }
    if (type == kListRecords) {
        ListRecords m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        RecordList list;
        list.track = m.track;
        list.laps = m.laps;
        if (mReplays != nullptr) list = mReplays->Records(m.track, m.laps);
        list.track = m.track;
        list.laps = m.laps;
        if (list.boards.size() > static_cast<std::size_t>(kMaxRecordBoards)) list.boards.resize(kMaxRecordBoards);
        if (list.availableLaps.size() > static_cast<std::size_t>(kMaxRecordLaps)) list.availableLaps.resize(kMaxRecordLaps);
        // A message must fit in one frame: take the last entry of the longest board until it does (every craft keeps its best entries).
        while (Encode(list).size() > kMaxListPayload) {
            RecordBoard* longest = nullptr;
            for (RecordBoard& b : list.boards) if (b.entries.size() > 1 && (longest == nullptr || b.entries.size() > longest->entries.size())) longest = &b;
            if (longest == nullptr) { if (!list.boards.empty()) { list.boards.pop_back(); continue; } break; }
            longest->entries.pop_back();
        }
        const std::vector<std::uint8_t> f = EncodeFrame(kRecordList, Encode(list));
        c.out.insert(c.out.end(), f.begin(), f.end());
        return;
    }
    if (type == kGetReplay) {
        GetReplay m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        if (nowMs - c.lastReplayRequestMs < 500) { Close(c, kRateLimited, "too many replay requests"); return; }
        c.lastReplayRequestMs = nowMs;
        std::vector<std::uint8_t> bytes;
        if (mReplays == nullptr || !mReplays->Read(m.id, bytes) || bytes.empty() || bytes.size() > mConfig.maxReplayServeBytes) {
            Reject r; r.reason = kNoSuchReplay; r.message = "that replay is no longer available";
            const std::vector<std::uint8_t> f = EncodeFrame(kReject, Encode(r));
            c.out.insert(c.out.end(), f.begin(), f.end());
            return;
        }
        c.replayBytesSent += bytes.size();
        if (c.replayBytesSent > mConfig.maxReplayBytesPerConnection) { Close(c, kRateLimited, "replay download limit reached"); return; }
        const std::size_t count = (bytes.size() + kReplayChunkBytes - 1) / kReplayChunkBytes;
        for (std::size_t i = 0; i < count; ++i) {
            ReplayChunk chunk;
            chunk.id = m.id; chunk.index = static_cast<std::uint16_t>(i); chunk.count = static_cast<std::uint16_t>(count);
            const std::size_t from = i * kReplayChunkBytes, to = std::min(bytes.size(), from + kReplayChunkBytes);
            chunk.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(from), bytes.begin() + static_cast<std::ptrdiff_t>(to));
            const std::vector<std::uint8_t> f = EncodeFrame(kReplayChunk, Encode(chunk));
            c.out.insert(c.out.end(), f.begin(), f.end());
        }
        return;
    }
    if (type == kListRaces) {
        if (!payload.empty()) { Invalid(c, nowMs); return; }
        SendList(c);
        return;
    }
    if (type == kHostRace) {
        HostRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        if (mDraining) { Close(c, kServerDraining, "the server is restarting; try again in a minute"); return; }
        const bool official = std::find(mConfig.tracks.begin(), mConfig.tracks.end(), m.track) != mConfig.tracks.end();
        const std::string communityPath = mConfig.communityDirectory.empty() || !SafeCommunityName(m.track) ? std::string() : mConfig.communityDirectory + "/" + m.track + ".trk";
        const bool community = !official && !communityPath.empty() && FileExists(communityPath);   // a community track this server has
        const bool allowed = official || community;
        if (!allowed || m.players < 1 || m.players > 8 || m.laps < 1 || m.laps > 10) { Close(c, kBadSettings, "that race cannot be hosted here"); return; }
        if (static_cast<int>(mRaces.size()) >= mConfig.maxRaces) { Close(c, kTooManyRaces, "the server is hosting as many races as it can"); return; }
        std::unique_ptr<Entry> e(new Entry);
        e->id = mNextRace++;
        e->cfg = mConfig.raceTemplate;
        e->cfg.race.trackName = m.track;
        e->cfg.race.trackPath = community ? communityPath : mConfig.trackDirectory + "/" + m.track + ".trk";
        e->cfg.race.players = m.players;
        e->cfg.race.laps = m.laps;
        e->cfg.race.weapons = m.weapons;
        e->cfg.raceId = static_cast<std::uint32_t>(e->id);
        e->code = m.code;
        e->cfg.tokenSeed = mSeed + e->id * 7919;
        e->host.reset(new RaceHost(e->cfg));
        std::string error;
        if (!e->host->Init(error)) { Close(c, kBadSettings, "that race could not be created"); return; }
        Hello hello; hello.name = c.name; hello.capabilities = c.caps; hello.hasDevice = c.hasDevice; hello.deviceHigh = c.deviceHigh; hello.deviceLow = c.deviceLow;
        Entry& ref = *e;
        mRaces[ref.id] = std::move(e);
        ++mRacesHosted;
        Attach(c, ref, EncodeFrame(kHello, Encode(hello)), nowMs);
        return;
    }
    if (type == kJoinRace) {
        JoinRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        if (mDraining) { Close(c, kServerDraining, "the server is restarting; try again in a minute"); return; }
        auto it = mRaces.find(m.id);
        if (it == mRaces.end() || !CodeAccepted(c, *it->second, m.code)) { Close(c, kNoSuchRace, "no such race"); return; }
        RaceHost& h = *it->second->host;
        if (h.Started() || h.Full()) { Close(c, kServerFull, "that race is full or already running"); return; }
        Hello hello; hello.name = c.name; hello.capabilities = c.caps; hello.hasDevice = c.hasDevice; hello.deviceHigh = c.deviceHigh; hello.deviceLow = c.deviceLow;
        Attach(c, *it->second, EncodeFrame(kHello, Encode(hello)), nowMs);
        return;
    }
    if (type == kSpectateRace) {
        SpectateRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        auto it = mRaces.find(m.id);
        if (it == mRaces.end() || !CodeAccepted(c, *it->second, m.code)) { Close(c, kNoSuchRace, "no such race"); return; }
        Hello hello; hello.name = c.name; hello.capabilities = c.caps; hello.hasDevice = c.hasDevice; hello.deviceHigh = c.deviceHigh; hello.deviceLow = c.deviceLow;
        Attach(c, *it->second, EncodeFrame(kSpectate, Encode(hello)), nowMs);
        return;
    }
    if (type == kChat && (c.caps & kCapLobbyChat) != 0) {
        Chat m;
        if (!Decode(payload, m) || !IsValidText(m.text, 120)) { Invalid(c, nowMs); return; }
        const HostConfig& rules = mConfig.raceTemplate;
        c.chatTokens = std::min<double>(rules.chatBurst, c.chatTokens + (nowMs - c.lastChatMs) / 1000.0);
        c.lastChatMs = nowMs;
        ChatMessage notice;
        notice.fromServer = true;
        notice.name = "server";
        if (c.chatTokens < 1) { notice.text = "Slow down: too many messages."; const auto f = EncodeFrame(kChatMessage, Encode(notice)); c.out.insert(c.out.end(), f.begin(), f.end()); return; }
        c.chatTokens -= 1;
        if (rules.moderation != nullptr && rules.moderation->IsMuted(c.name, nowMs)) {
            notice.text = "You are muted.";
            const auto f = EncodeFrame(kChatMessage, Encode(notice));
            c.out.insert(c.out.end(), f.begin(), f.end());
            return;
        }
        ChatMessage out;
        out.name = c.name;
        out.text = m.text;
        if (rules.moderation != nullptr) rules.moderation->RememberChat(nowMs, "[lobby] " + c.name + ": " + m.text);
        const std::vector<std::uint8_t> frame = EncodeFrame(kChatMessage, Encode(out));
        for (auto& o : mConns) {
            if (o->closed || !o->helloDone || o->raceId != 0 || (o->caps & kCapLobbyChat) == 0) continue;
            o->out.insert(o->out.end(), frame.begin(), frame.end());
        }
        return;
    }
    Invalid(c, nowMs);   // anything else is not valid in the lobby
}

// Everyone who has said Hello and is not yet in a race, in a fixed order; sent to the clients that asked for the lobby's chat whenever it changes.
void LobbyServer::BroadcastUsersIfChanged()
{
    std::vector<std::string> names;
    for (const auto& c : mConns) if (!c->closed && c->helloDone && c->raceId == 0) names.push_back(c->name);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        std::string la = a, lb = b;
        for (char& ch : la) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (char& ch : lb) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return la != lb ? la < lb : a < b;
    });
    std::string signature;
    for (const std::string& n : names) { signature += n; signature += '\n'; }
    if (signature == mLastUsers) return;
    mLastUsers = signature;
    Protocol3::LobbyUsers users;
    users.total = static_cast<std::uint16_t>(names.size());
    for (std::size_t i = 0; i < names.size() && static_cast<int>(i) < Protocol3::kMaxLobbyUserNames; ++i) users.names.push_back(names[i]);
    const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(Protocol3::kLobbyUsers, Protocol3::Encode(users));
    for (auto& c : mConns) {
        if (c->closed || !c->helloDone || c->raceId != 0 || (c->caps & Protocol3::kCapLobbyChat) == 0) continue;
        c->out.insert(c->out.end(), frame.begin(), frame.end());
    }
}

void LobbyServer::Advance(std::int64_t nowMs)
{
    BroadcastUsersIfChanged();
    for (auto& c : mConns)
        if (!c->closed && !c->helloDone && c->raceId == 0 && nowMs - c->connectedMs > mConfig.helloTimeoutMs) Close(*c, Protocol3::kBadMessage, "hello timeout");

    for (auto it = mRaces.begin(); it != mRaces.end();) {
        Entry& e = *it->second;
        e.host->Advance(nowMs);
        if (e.host->RaceFinished() && e.finishedAt < 0) {
            e.finishedAt = nowMs;
            if (mOnFinished) mOnFinished(e.id, *e.host);
        }
        // An abandoned race that never started goes when nobody is in it.
        const bool empty = e.host->ConnectedPlayers() == 0 && !e.host->Started();
        bool emptyOfLiveConns = true;
        for (const auto& c : mConns) if (c->raceId == e.id && !c->closed && !e.host->IsClosed(c->hostConn)) emptyOfLiveConns = false;
        // A started race that everybody has left cannot be finished by anyone: after a grace period (for a dropped connection to resume) it goes.
        if (e.host->Started() && !e.host->RaceFinished() && e.host->ConnectedPlayers() == 0 && emptyOfLiveConns) {
            if (e.emptySince < 0) e.emptySince = nowMs;
        }
        else e.emptySince = -1;
        const bool abandoned = e.emptySince >= 0 && nowMs - e.emptySince > mConfig.abandonedRaceMs;
        if (abandoned || (e.finishedAt >= 0 && nowMs - e.finishedAt > mConfig.finishedRaceLingerMs) || (empty && emptyOfLiveConns && !e.host->Started() && nowMs > 0 && e.host->GetMetrics().connectionsAccepted > 0)) {
            for (auto& c : mConns) {
                if (c->raceId != e.id) continue;
                // What the race still had to say to this connection (a Reject, a last frame) is kept until it is read.
                const std::vector<std::uint8_t> pending = e.host->TakeOutput(c->hostConn);
                c->out.insert(c->out.end(), pending.begin(), pending.end());
                c->closed = true;
                c->raceId = 0;
            }
            AddMetrics(mRemoved, e.host->GetMetrics());
            it = mRaces.erase(it);
        }
        else {
            ++it;
        }
    }
    // Forget connections that are fully closed and have nothing left to read.
    mConns.erase(std::remove_if(mConns.begin(), mConns.end(), [](const std::unique_ptr<Conn>& c) { return c->closed && c->raceId == 0 && c->out.empty(); }), mConns.end());
}
}
