#include "LobbyServer.h"

#include <algorithm>

namespace Authority
{
struct LobbyServer::Entry
{
    std::uint32_t id = 0;
    std::unique_ptr<RaceHost> host;
    HostConfig cfg;
    std::int64_t finishedAt = -1;
};

struct LobbyServer::Conn
{
    int id = 0;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> out;
    bool closed = false;
    bool helloDone = false;
    std::string name;
    std::int64_t connectedMs = 0;
    std::vector<std::int64_t> invalidTimes, commandTimes;
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

void LobbyServer::SendList(Conn& c)
{
    Protocol3::RaceList list;
    for (const auto& r : mRaces) {
        if (list.races.size() >= 32) break;
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

void LobbyServer::Handle(Conn& c, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t nowMs)
{
    using namespace Protocol3;
    if (type == kHello && !c.helloDone) {
        Hello h;
        if (!Decode(payload, h)) { Close(c, kBadMessage, "malformed hello"); return; }
        if (h.major != kMajor) { Close(c, kUnsupportedVersion, "this server speaks protocol 3"); return; }
        c.helloDone = true;
        c.name = h.name;
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

    if (type == kListRaces) {
        if (!payload.empty()) { Invalid(c, nowMs); return; }
        SendList(c);
        return;
    }
    if (type == kHostRace) {
        HostRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        const bool allowed = std::find(mConfig.tracks.begin(), mConfig.tracks.end(), m.track) != mConfig.tracks.end();
        if (!allowed || m.players < 2 || m.players > 8 || m.laps < 1 || m.laps > 10) { Close(c, kBadSettings, "that race cannot be hosted here"); return; }
        if (static_cast<int>(mRaces.size()) >= mConfig.maxRaces) { Close(c, kTooManyRaces, "the server is hosting as many races as it can"); return; }
        std::unique_ptr<Entry> e(new Entry);
        e->id = mNextRace++;
        e->cfg = mConfig.raceTemplate;
        e->cfg.race.trackName = m.track;
        e->cfg.race.trackPath = mConfig.trackDirectory + "/" + m.track + ".trk";
        e->cfg.race.players = m.players;
        e->cfg.race.laps = m.laps;
        e->cfg.race.weapons = m.weapons;
        e->cfg.tokenSeed = mSeed + e->id * 7919;
        e->host.reset(new RaceHost(e->cfg));
        std::string error;
        if (!e->host->Init(error)) { Close(c, kBadSettings, "that race could not be created"); return; }
        Hello hello; hello.name = c.name;
        Entry& ref = *e;
        mRaces[ref.id] = std::move(e);
        ++mRacesHosted;
        Attach(c, ref, EncodeFrame(kHello, Encode(hello)), nowMs);
        return;
    }
    if (type == kJoinRace) {
        JoinRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        auto it = mRaces.find(m.id);
        if (it == mRaces.end()) { Close(c, kNoSuchRace, "no such race"); return; }
        RaceHost& h = *it->second->host;
        if (h.Started() || h.Full()) { Close(c, kServerFull, "that race is full or already running"); return; }
        Hello hello; hello.name = c.name;
        Attach(c, *it->second, EncodeFrame(kHello, Encode(hello)), nowMs);
        return;
    }
    if (type == kSpectateRace) {
        SpectateRace m;
        if (!Decode(payload, m)) { Invalid(c, nowMs); return; }
        auto it = mRaces.find(m.id);
        if (it == mRaces.end()) { Close(c, kNoSuchRace, "no such race"); return; }
        Hello hello; hello.name = c.name;
        Attach(c, *it->second, EncodeFrame(kSpectate, Encode(hello)), nowMs);
        return;
    }
    Invalid(c, nowMs);   // anything else is not valid in the lobby
}

void LobbyServer::Advance(std::int64_t nowMs)
{
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
        if ((e.finishedAt >= 0 && nowMs - e.finishedAt > mConfig.finishedRaceLingerMs) || (empty && emptyOfLiveConns && !e.host->Started() && nowMs > 0 && e.host->GetMetrics().connectionsAccepted > 0)) {
            for (auto& c : mConns) if (c->raceId == e.id) { c->closed = true; c->raceId = 0; }
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
