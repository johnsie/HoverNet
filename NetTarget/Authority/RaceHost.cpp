#include "RaceHost.h"

#include <algorithm>
#include <set>

#include "ReplayFormat.h"
#include "../LinuxClient/Sha256.h"

namespace Authority
{
struct RaceHost::Connection
{
    int id = 0;
    Protocol3::FrameParser parser;
    std::vector<std::uint8_t> out;
    bool closed = false;
    bool helloDone = false;
    int player = -1;
    bool spectator = false;
    std::uint32_t caps = 0;             // Hello.capabilities
    std::string name;
    std::set<std::string> blocked;
    double chatTokens = 3;
    std::int64_t lastChatMs = 0;
    std::vector<std::int64_t> reportTimes;
    std::uint32_t specCursor = 0;       // the next Tick a spectator is owed
    bool specResultSent = false;
    std::int64_t connectedMs = 0;
    std::vector<std::int64_t> invalidTimes;
    // token bucket for Input frames: refills continuously
    double inputTokens = 0;
    int inputOver = 0;          // consecutive Input frames dropped for being over the rate limit
    std::int64_t lastRefillMs = 0;
    std::vector<std::int64_t> snapshotTimes;
};

RaceHost::RaceHost(const HostConfig& config)
    : mConfig(config), mNextId(1), mRaceLoaded(false), mStarted(false), mResultSent(false), mStartMs(0), mSimulated(0), mInvalidTotal(0), mTokenState(config.tokenSeed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) {}
RaceHost::~RaceHost() {}

std::uint64_t RaceHost::NextToken()
{
    mTokenState ^= mTokenState >> 12; mTokenState ^= mTokenState << 25; mTokenState ^= mTokenState >> 27;
    const std::uint64_t t = mTokenState * 2685821657736338717ULL;
    return t == 0 ? 1 : t;
}

bool RaceHost::Init(std::string& error)
{
    mTokens.assign(static_cast<std::size_t>(mConfig.race.players), 0);
    mTrackSha256 = Sha256::HashFile(mConfig.race.trackPath);
    if (mTrackSha256.empty()) { error = "cannot read the track"; return false; }
    // Loading the race here makes configuration errors visible at start-up rather than when the last player arrives.
    if (!mRace.Start(mConfig.race, error)) return false;
    mRaceLoaded = true;
    KeepKeyframe();   // tick 0
    return true;
}

RaceHost::Connection* RaceHost::Find(int id)
{
    for (auto& c : mConnections) if (c->id == id) return c.get();
    return nullptr;
}
const RaceHost::Connection* RaceHost::Find(int id) const
{
    for (const auto& c : mConnections) if (c->id == id) return c.get();
    return nullptr;
}

int RaceHost::Connect(std::int64_t nowMs)
{
    std::unique_ptr<Connection> c(new Connection);
    c->id = mNextId++;
    c->connectedMs = nowMs;
    c->lastRefillMs = nowMs;
    c->inputTokens = 16;
    ++mMetrics.connectionsAccepted;
    const int id = c->id;
    mConnections.push_back(std::move(c));
    return id;
}

void RaceHost::Disconnect(int connection)
{
    Connection* c = Find(connection);
    if (c != nullptr && !c->closed) { c->closed = true; ++mMetrics.connectionsClosed; }
}

bool RaceHost::IsClosed(int connection) const
{
    const Connection* c = Find(connection);
    return c == nullptr || c->closed;
}

std::vector<std::uint8_t> RaceHost::TakeOutput(int connection)
{
    Connection* c = Find(connection);
    std::vector<std::uint8_t> out;
    if (c != nullptr) out.swap(c->out);
    return out;
}

bool RaceHost::OwnsResumeToken(std::uint64_t token) const
{
    if (token == 0) return false;
    for (std::uint64_t t : mTokens) if (t == token) return true;
    return false;
}

void RaceHost::KeepKeyframe()
{
    if (!mKeyframes.empty() && mKeyframes.back().first == mRace.CurrentTick()) return;
    mKeyframes.push_back(std::make_pair(mRace.CurrentTick(), mRace.Snapshot()));
}

int RaceHost::Spectators() const
{
    int n = 0;
    for (const auto& c : mConnections) if (!c->closed && c->spectator) ++n;
    return n;
}

int RaceHost::ConnectedPlayers() const
{
    int n = 0;
    for (const auto& c : mConnections) if (!c->closed && c->player >= 0) ++n;
    return n;
}

void RaceHost::SendHelloAck(Connection& c)
{
    Protocol3::HelloAck a;
    a.maxFrame = std::min<std::uint16_t>(static_cast<std::uint16_t>(c.parser.MaxFrame()), Protocol3::kDefaultMaxFrame);
    a.player = static_cast<std::uint8_t>(c.player);
    a.players = static_cast<std::uint8_t>(mConfig.race.players);
    a.tickMs = ReplayFormat::kFrameMs;
    a.rules = ReplayFormat::kSimulationRules;
    a.track = mConfig.race.trackName;
    a.trackSha256 = mTrackSha256;
    a.laps = static_cast<std::uint8_t>(mConfig.race.laps);
    a.weapons = mConfig.race.weapons;
    a.resumeToken = mTokens[static_cast<std::size_t>(c.player)];
    a.raceId = mConfig.raceId;
    a.includeRaceId = (c.caps & Protocol3::kCapRaceId) != 0;
    Send(c, Protocol3::EncodeFrame(Protocol3::kHelloAck, Protocol3::Encode(a)));
}

void RaceHost::Send(Connection& c, const std::vector<std::uint8_t>& frame)
{
    if (!c.closed) {
        c.out.insert(c.out.end(), frame.begin(), frame.end());
        mMetrics.bytesOut += frame.size();
        ++mMetrics.framesOut;
        if (frame.size() > 2 && frame[2] == Protocol3::kTick) mMetrics.tickBytesOut += frame.size();
    }
}

void RaceHost::Broadcast(const std::vector<std::uint8_t>& frame)
{
    for (auto& c : mConnections) if (!c->closed && c->player >= 0) Send(*c, frame);
}

void RaceHost::Close(Connection& c, std::uint8_t reason, const std::string& message)
{
    if (c.closed) return;
    Protocol3::Reject r;
    r.reason = reason;
    r.message = message;
    Send(c, Protocol3::EncodeFrame(Protocol3::kReject, Protocol3::Encode(r)));
    c.closed = true;
    ++mMetrics.rejectedConnections;
    ++mMetrics.connectionsClosed;
}

void RaceHost::Invalid(Connection& c, std::int64_t nowMs, const char* why)
{
    ++mInvalidTotal;
    ++mMetrics.invalidMessages;
    c.invalidTimes.push_back(nowMs);
    c.invalidTimes.erase(std::remove_if(c.invalidTimes.begin(), c.invalidTimes.end(),
                                        [&](std::int64_t t) { return nowMs - t > mConfig.invalidWindowMs; }), c.invalidTimes.end());
    if (static_cast<int>(c.invalidTimes.size()) > mConfig.maxInvalidPerWindow) Close(c, Protocol3::kBadMessage, "too many invalid messages");
    (void)why;
}

void RaceHost::Receive(int connection, const std::uint8_t* data, std::size_t size, std::int64_t nowMs)
{
    Connection* c = Find(connection);
    if (c == nullptr || c->closed) return;
    c->parser.Feed(data, size);
    mMetrics.bytesIn += size;
    std::uint8_t type = 0;
    std::vector<std::uint8_t> payload;
    while (!c->closed && c->parser.Next(type, payload)) { ++mMetrics.framesIn; Handle(*c, type, payload, nowMs); }
    if (!c->closed && c->parser.Failed()) Close(*c, Protocol3::kBadMessage, "bad frame");
}

void RaceHost::BeginRaceIfFull(std::int64_t nowMs)
{
    if (mStarted || ConnectedPlayers() < mConfig.race.players) return;
    // A race whose host wants to start it himself or herself waits for that, however full it is (a one-player race is a time trial: it starts at once).
    if (mHostStarts && mConfig.race.players > 1) return;
    StartNow(nowMs);
}

void RaceHost::StartNow(std::int64_t nowMs)
{
    mStarted = true;
    mSlotNames.assign(static_cast<std::size_t>(mConfig.race.players), std::string());
    for (const auto& o : mConnections) if (!o->closed && !o->spectator && o->player >= 0 && o->player < mConfig.race.players) mSlotNames[static_cast<std::size_t>(o->player)] = o->name;
    mStartMs = nowMs + mConfig.startDelayMs;
    Protocol3::Start s;
    s.countdownMs = static_cast<std::uint32_t>(mConfig.startDelayMs);
    Broadcast(Protocol3::EncodeFrame(Protocol3::kStart, Protocol3::Encode(s)));
}

// The host pressed Start Race: the race has exactly the players who are here. If fewer than the seats, the race is set up again for that many
// (slots numbered from 0 in order, fresh reconnect tokens) and each player is sent a new HelloAck before Start.
void RaceHost::HostStart(Connection& c, std::int64_t nowMs)
{
    if (!mHostStarts || mStarted || c.player != 0) return;
    std::vector<Connection*> here;
    for (auto& o : mConnections) if (!o->closed && o->player >= 0) here.push_back(o.get());
    std::sort(here.begin(), here.end(), [](const Connection* a, const Connection* b) { return a->player < b->player; });
    const int n = static_cast<int>(here.size());
    if (n < 1) return;
    if (n != mConfig.race.players) {
        mConfig.race.players = n;
        std::string error;
        if (!mRace.Start(mConfig.race, error)) { for (auto* o : here) Close(*o, Protocol3::kBadSettings, "that race could not be started"); return; }
        mKeyframes.clear();
        KeepKeyframe();
        mTokens.assign(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) { here[static_cast<std::size_t>(i)]->player = i; mTokens[static_cast<std::size_t>(i)] = NextToken(); }
        for (int i = 0; i < n; ++i) SendHelloAck(*here[static_cast<std::size_t>(i)]);
    }
    StartNow(nowMs);
}

void RaceHost::SendRosterIfChanged()
{
    if (!mHostStarts || mStarted) return;
    Protocol3::Roster r;
    r.seats = static_cast<std::uint8_t>(mConfig.race.players);
    std::vector<const Connection*> here;
    for (const auto& o : mConnections) if (!o->closed && o->player >= 0) here.push_back(o.get());
    std::sort(here.begin(), here.end(), [](const Connection* a, const Connection* b) { return a->player < b->player; });
    std::string signature = std::to_string(r.seats) + "|";
    for (const Connection* o : here) { r.members.push_back(std::make_pair(static_cast<std::uint8_t>(o->player), o->name)); signature += std::to_string(o->player) + ":" + o->name + "\n"; }
    if (signature == mLastRoster) return;
    mLastRoster = signature;
    const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(Protocol3::kRoster, Protocol3::Encode(r));
    for (auto& o : mConnections) if (!o->closed && o->helloDone && o->player >= 0 && (o->caps & Protocol3::kCapHostStart) != 0) Send(*o, frame);
}

void RaceHost::Handle(Connection& c, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t nowMs)
{
    using namespace Protocol3;
    if (type == kHello) {
        Hello h;
        if (c.helloDone) { Invalid(c, nowMs, "second hello"); return; }
        if (!Decode(payload, h)) { Close(c, kBadMessage, "malformed hello"); return; }
        if (h.major != kMajor) { Close(c, kUnsupportedVersion, "this server speaks protocol 3"); return; }
        if (mStarted || ConnectedPlayers() >= mConfig.race.players) { Close(c, kServerFull, "the race is full or already running"); return; }
        c.helloDone = true;
        c.name = h.name;
        c.caps = h.capabilities;
        if (ConnectedPlayers() == 0 && (h.capabilities & kCapHostStart) != 0) { mHostStarts = true; mHostConn = c.id; }
        std::vector<bool> used(static_cast<std::size_t>(mConfig.race.players), false);
        for (const auto& o : mConnections) if (!o->closed && o->player >= 0) used[static_cast<std::size_t>(o->player)] = true;
        for (int slot = 0; slot < mConfig.race.players; ++slot) if (!used[static_cast<std::size_t>(slot)]) { c.player = slot; break; }
        c.parser.SetMaxFrame(std::min<std::uint16_t>(h.maxFrame, kDefaultMaxFrame));
        mTokens[static_cast<std::size_t>(c.player)] = NextToken();
        SendHelloAck(c);
        BeginRaceIfFull(nowMs);
        return;
    }

    if (type == kSpectate) {
        Hello h;
        if (c.helloDone) { Invalid(c, nowMs, "second hello"); return; }
        if (!Decode(payload, h)) { Close(c, kBadMessage, "malformed spectate request"); return; }
        if (h.major != kMajor) { Close(c, kUnsupportedVersion, "this server speaks protocol 3"); return; }
        if (!mStarted) { Close(c, kNotInRace, "the race has not started"); return; }
        if (Spectators() >= mConfig.maxSpectators) { Close(c, kServerFull, "too many spectators"); return; }
        c.helloDone = true;
        c.name = h.name;
        c.caps = h.capabilities;
        c.spectator = true;
        c.parser.SetMaxFrame(std::min<std::uint16_t>(h.maxFrame, kDefaultMaxFrame));
        StartSpectator(c, nowMs);
        return;
    }

    if (type == kResume) {
        Resume m;
        if (c.helloDone) { Invalid(c, nowMs, "resume after hello"); return; }
        if (!Decode(payload, m)) { Close(c, kBadMessage, "malformed resume"); return; }
        int slot = -1;
        for (std::size_t i = 0; i < mTokens.size(); ++i) if (mTokens[i] == m.token) slot = static_cast<int>(i);
        if (slot < 0 || !mStarted) { Close(c, kNotInRace, "unknown resume token"); return; }
        // Whoever holds the token is the player: a previous connection for the slot, if still open, is replaced.
        for (auto& o : mConnections) if (o.get() != &c && !o->closed && o->player == slot) Close(*o, kNotInRace, "replaced by a reconnection");
        c.helloDone = true;
        c.player = slot;
        c.parser.SetMaxFrame(kDefaultMaxFrame);
        SendHelloAck(c);
        return;
    }

    if (!c.helloDone) { Close(c, kBadMessage, "hello first"); return; }

    if (c.spectator && type != kPing && type != kSnapshotRequest) { Invalid(c, nowMs, "spectators only watch"); return; }

    switch (type) {
    case kInput: {
        Input in;
        if (!Decode(payload, in)) { Invalid(c, nowMs, "input"); return; }
        // Token bucket: refill at maxInputFramesPerSecond with room for a second's worth, so a network freeze that delivers a burst
        // of queued inputs is not a flood. Over the limit, frames are dropped (inputs repeat each other, so nothing is lost); only a
        // sustained flood, far over, counts as invalid and ends the connection.
        c.inputTokens = std::min(64.0, c.inputTokens + (nowMs - c.lastRefillMs) * mConfig.maxInputFramesPerSecond / 1000.0);
        c.lastRefillMs = nowMs;
        if (c.inputTokens < 1) {
            // Over the limit: drop the frame. Only a long unbroken run of them, a flood, counts as invalid.
            if (++c.inputOver > 64) Invalid(c, nowMs, "rate");
            return;
        }
        c.inputTokens -= 1;
        c.inputOver = 0;
        if (!mStarted) return;   // inputs before the start are ignored, not errors
        for (std::size_t i = 0; i < in.controls.size(); ++i) {
            const InputResult r = mRace.SubmitInput(c.player, in.firstTick + static_cast<std::uint32_t>(i), in.controls[i]);
            if (r == InputResult::Accepted || r == InputResult::Replaced) ++mMetrics.inputsAccepted;
            // Only a message's newest input says whether the client is sending far enough ahead; the older ones repeat
            // earlier inputs on purpose (loss cover) and are expected to be late.
            else if (r == InputResult::Late && i + 1 == in.controls.size()) ++mMetrics.inputsLate;
            else if (r == InputResult::TooFarAhead) ++mMetrics.inputsTooFar;
        }
        return;
    }
    case kStartRace: {
        if (!payload.empty()) { Invalid(c, nowMs, "start race"); return; }
        HostStart(c, nowMs);
        return;
    }
    case kChat: {
        Chat m;
        if (!Decode(payload, m) || !IsValidText(m.text, 120)) { Invalid(c, nowMs, "chat"); return; }
        c.chatTokens = std::min<double>(mConfig.chatBurst, c.chatTokens + (nowMs - c.lastChatMs) / 1000.0);
        c.lastChatMs = nowMs;
        ChatMessage notice;
        notice.fromServer = true;
        notice.name = "server";
        if (c.chatTokens < 1) { notice.text = "Slow down: too many messages."; Send(c, EncodeFrame(kChatMessage, Encode(notice))); return; }
        c.chatTokens -= 1;
        if (mConfig.moderation != nullptr && mConfig.moderation->IsMuted(c.name, nowMs)) {
            notice.text = "You are muted.";
            Send(c, EncodeFrame(kChatMessage, Encode(notice)));
            return;
        }
        ChatMessage out;
        out.name = c.name;
        out.text = m.text;
        if (mConfig.moderation != nullptr) mConfig.moderation->RememberChat(nowMs, c.name + ": " + m.text);
        const std::vector<std::uint8_t> frame = EncodeFrame(kChatMessage, Encode(out));
        for (auto& o : mConnections) {
            if (o->closed || !o->helloDone) continue;
            if (o.get() != &c && o->blocked.count(c.name)) continue;      // they blocked the sender
            Send(*o, frame);
        }
        return;
    }
    case kBlock: {
        Block m;
        if (!Decode(payload, m) || !IsValidText(m.name, 32)) { Invalid(c, nowMs, "block"); return; }
        if (c.blocked.size() < 50) c.blocked.insert(m.name);
        return;
    }
    case kReport: {
        Report m;
        if (!Decode(payload, m) || !IsValidText(m.name, 32) || !IsValidText(m.reason, 120)) { Invalid(c, nowMs, "report"); return; }
        ChatMessage notice;
        notice.fromServer = true;
        notice.name = "server";
        c.reportTimes.erase(std::remove_if(c.reportTimes.begin(), c.reportTimes.end(), [&](std::int64_t t) { return nowMs - t > 60000; }), c.reportTimes.end());
        if (static_cast<int>(c.reportTimes.size()) >= mConfig.reportsPerMinute || m.name == c.name || mConfig.moderation == nullptr) {
            notice.text = "That report was not accepted.";
        }
        else {
            c.reportTimes.push_back(nowMs);
            mConfig.moderation->AddReport(nowMs, c.name, m.name, m.reason);
            notice.text = "Report received. Thank you.";
        }
        Send(c, EncodeFrame(kChatMessage, Encode(notice)));
        return;
    }
    case kPing: {
        Ping p;
        if (!Decode(payload, p)) { Invalid(c, nowMs, "ping"); return; }
        Pong q;
        q.clientTick = p.clientTick; q.token = p.token; q.serverTick = mRace.CurrentTick();
        Send(c, EncodeFrame(kPong, Encode(q)));
        return;
    }
    case kSnapshotRequest: {
        SnapshotRequest r;
        if (!Decode(payload, r)) { Invalid(c, nowMs, "snapshot request"); return; }
        c.snapshotTimes.erase(std::remove_if(c.snapshotTimes.begin(), c.snapshotTimes.end(), [&](std::int64_t t) { return nowMs - t > 60000; }), c.snapshotTimes.end());
        if (static_cast<int>(c.snapshotTimes.size()) >= mConfig.snapshotsPerMinute) { Close(c, kRateLimited, "too many snapshot requests"); return; }
        if (!mStarted) { Invalid(c, nowMs, "snapshot before start"); return; }
        c.snapshotTimes.push_back(nowMs);
        ++mMetrics.snapshotsServed;
        if (c.spectator) { StartSpectator(c, nowMs); return; }
        SendSnapshot(c, r.id);
        return;
    }
    default:
        // Server-to-client types and unknown types are errors (a client may not send them).
        Invalid(c, nowMs, "unexpected type");
        return;
    }
}

void RaceHost::SendSnapshot(Connection& c, std::uint32_t id)
{
    using namespace Protocol3;
    const std::vector<std::uint8_t> bytes = mRace.Snapshot();
    const std::size_t per = 900;   // fits one frame at the smallest maximum frame a client may negotiate down to
    const std::size_t count = (bytes.size() + per - 1) / per;
    for (std::size_t i = 0; i < count; ++i) {
        SnapshotChunk chunk;
        chunk.id = id;
        chunk.index = static_cast<std::uint16_t>(i);
        chunk.count = static_cast<std::uint16_t>(count);
        chunk.tick = mRace.CurrentTick();
        chunk.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(i * per), bytes.begin() + static_cast<std::ptrdiff_t>(std::min(bytes.size(), (i + 1) * per)));
        Send(c, EncodeFrame(kSnapshotChunk, Encode(chunk)));
    }
}

// Gives a spectator the newest keyframe that is not newer than what it may see, then (from Advance) the Ticks after it.
void RaceHost::StartSpectator(Connection& c, std::int64_t)
{
    using namespace Protocol3;
    const std::int64_t delayTicks = mConfig.spectatorDelayMs / ReplayFormat::kFrameMs;
    const std::int64_t target = static_cast<std::int64_t>(mRace.CurrentTick()) - delayTicks;
    const std::pair<std::uint32_t, std::vector<std::uint8_t>>* key = &mKeyframes.front();
    for (const auto& k : mKeyframes) if (static_cast<std::int64_t>(k.first) <= target || &k == &mKeyframes.front()) key = &k;
    HelloAck a;
    a.maxFrame = static_cast<std::uint16_t>(c.parser.MaxFrame());
    a.player = 0;
    a.players = static_cast<std::uint8_t>(mConfig.race.players);
    a.tickMs = ReplayFormat::kFrameMs;
    a.rules = ReplayFormat::kSimulationRules;
    a.track = mConfig.race.trackName;
    a.trackSha256 = mTrackSha256;
    a.laps = static_cast<std::uint8_t>(mConfig.race.laps);
    a.weapons = mConfig.race.weapons;
    a.resumeToken = 0;
    a.raceId = mConfig.raceId;
    a.includeRaceId = (c.caps & kCapRaceId) != 0;
    if (c.specCursor == 0 && !c.specResultSent) Send(c, EncodeFrame(kHelloAck, Encode(a)));
    const std::size_t per = 900;
    const std::size_t count = (key->second.size() + per - 1) / per;
    for (std::size_t i = 0; i < count; ++i) {
        SnapshotChunk chunk;
        chunk.id = 0;
        chunk.index = static_cast<std::uint16_t>(i);
        chunk.count = static_cast<std::uint16_t>(count);
        chunk.tick = key->first;
        chunk.data.assign(key->second.begin() + static_cast<std::ptrdiff_t>(i * per), key->second.begin() + static_cast<std::ptrdiff_t>(std::min(key->second.size(), (i + 1) * per)));
        Send(c, EncodeFrame(kSnapshotChunk, Encode(chunk)));
    }
    c.specCursor = key->first;
    c.specResultSent = false;
}

void RaceHost::SendSpectatorFrames(Connection& c)
{
    const std::uint32_t delayTicks = static_cast<std::uint32_t>(mConfig.spectatorDelayMs / ReplayFormat::kFrameMs);
    const std::uint32_t simulated = static_cast<std::uint32_t>(mTickFrames.size());
    // A spectator is owed every Tick up to `simulated - delayTicks` (or all of them once the race is over and the delay has elapsed).
    // Time-based, so the delay keeps running after the race ends and the last Ticks and the Result still reach the spectator.
    const std::int64_t dueTicks = mStarted ? (mLastNow - mStartMs) / ReplayFormat::kFrameMs + 1 : 0;
    const std::int64_t releasable = dueTicks - static_cast<std::int64_t>(delayTicks);
    const std::uint32_t allowed = releasable <= 0 ? 0 : static_cast<std::uint32_t>(std::min<std::int64_t>(releasable, simulated));
    while (c.specCursor < allowed && c.specCursor < simulated) {
        Send(c, mTickFrames[c.specCursor]);
        ++c.specCursor;
    }
    if (mRace.Finished() && c.specCursor >= simulated && !c.specResultSent && mResultSent) {
        c.specResultSent = true;
        Send(c, mResultFrame);
    }
}

void RaceHost::FinishRace()
{
    if (mResultSent) return;
    mResultSent = true;
    Protocol3::Result r;
    r.ticks = mRace.CurrentTick();
    r.finalHash = mRace.StateHash();
    for (const PlayerResult& p : mRace.Results()) {
        Protocol3::PlayerResult q;
        q.player = static_cast<std::uint8_t>(p.player);
        q.rank = static_cast<std::uint8_t>(p.rank);
        q.finished = p.finished;
        q.laps = static_cast<std::uint8_t>(p.laps);
        q.finishTimeMs = static_cast<std::int32_t>(p.finishTimeMs);
        q.bestLapMs = static_cast<std::int32_t>(p.bestLapMs);
        r.players.push_back(q);
    }
    mResultFrame = Protocol3::EncodeFrame(Protocol3::kResult, Protocol3::Encode(r));
    Broadcast(mResultFrame);
}

void RaceHost::Advance(std::int64_t nowMs)
{
    mLastNow = nowMs;
    // The host left a race that had not started: it cannot start, so the others are told and sent away.
    if (mHostStarts && !mStarted && mHostConn != 0 && IsClosed(mHostConn)) {
        for (auto& o : mConnections) if (!o->closed && o->player >= 0) Close(*o, Protocol3::kNotInRace, "the host left the race");
        mHostConn = 0;
    }
    SendRosterIfChanged();
    // A moderator's removal order takes effect here.
    if (mConfig.moderation != nullptr)
        for (auto& conn : mConnections)
            if (!conn->closed && !conn->name.empty() && mConfig.moderation->TakeKick(conn->name)) Close(*conn, Protocol3::kBanned, "removed by a moderator");

    // A connection that never completes Hello is closed.
    for (auto& c : mConnections)
        if (!c->closed && !c->helloDone && nowMs - c->connectedMs > mConfig.helloTimeoutMs) Close(*c, Protocol3::kBadMessage, "hello timeout");

    if (!mStarted || mRace.Finished()) {
        if (mStarted && mRace.Finished()) {
            FinishRace();
            for (auto& conn : mConnections) if (!conn->closed && conn->spectator) SendSpectatorFrames(*conn);
        }
        return;
    }
    // Tick n is due at mStartMs + n * tick; run every tick that is due (a very late call catches up, bounded by the race).
    while (!mRace.Finished() && nowMs >= mStartMs + static_cast<std::int64_t>(mSimulated) * ReplayFormat::kFrameMs) {
        const std::uint32_t tick = mRace.CurrentTick();
        mRace.AdvanceTick();
        ++mSimulated;
        ++mMetrics.ticksSimulated;
        Protocol3::Tick t;
        t.tick = tick;
        t.hash = mRace.StateHash();
        // The inputs actually used at this tick, for every player (prediction and spectators reconstruct the race from them).
        t.controls = mRace.InputsAt(tick);
        const std::vector<std::uint8_t> tickFrame = Protocol3::EncodeFrame(Protocol3::kTick, Protocol3::Encode(t));
        mTickFrames.push_back(tickFrame);
        Broadcast(tickFrame);
        if (mConfig.keyframeTicks > 0 && mRace.CurrentTick() % static_cast<std::uint32_t>(mConfig.keyframeTicks) == 0) KeepKeyframe();
    }
    for (auto& conn : mConnections) if (!conn->closed && conn->spectator) SendSpectatorFrames(*conn);
    if (mRace.Finished()) FinishRace();
}
}
