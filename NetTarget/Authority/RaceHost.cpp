#include "RaceHost.h"

#include <algorithm>

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
    std::int64_t connectedMs = 0;
    std::vector<std::int64_t> invalidTimes;
    // token bucket for Input frames: refills continuously
    double inputTokens = 0;
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
    c->inputTokens = 8;
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
    mStarted = true;
    mStartMs = nowMs + mConfig.startDelayMs;
    Protocol3::Start s;
    s.countdownMs = static_cast<std::uint32_t>(mConfig.startDelayMs);
    Broadcast(Protocol3::EncodeFrame(Protocol3::kStart, Protocol3::Encode(s)));
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
        std::vector<bool> used(static_cast<std::size_t>(mConfig.race.players), false);
        for (const auto& o : mConnections) if (!o->closed && o->player >= 0) used[static_cast<std::size_t>(o->player)] = true;
        for (int slot = 0; slot < mConfig.race.players; ++slot) if (!used[static_cast<std::size_t>(slot)]) { c.player = slot; break; }
        c.parser.SetMaxFrame(std::min<std::uint16_t>(h.maxFrame, kDefaultMaxFrame));
        mTokens[static_cast<std::size_t>(c.player)] = NextToken();
        SendHelloAck(c);
        BeginRaceIfFull(nowMs);
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

    switch (type) {
    case kInput: {
        Input in;
        if (!Decode(payload, in)) { Invalid(c, nowMs, "input"); return; }
        // Token bucket: refill at maxInputFramesPerSecond, burst of 16.
        c.inputTokens = std::min(16.0, c.inputTokens + (nowMs - c.lastRefillMs) * mConfig.maxInputFramesPerSecond / 1000.0);
        c.lastRefillMs = nowMs;
        if (c.inputTokens < 1) { Invalid(c, nowMs, "rate"); return; }
        c.inputTokens -= 1;
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
    Broadcast(Protocol3::EncodeFrame(Protocol3::kResult, Protocol3::Encode(r)));
}

void RaceHost::Advance(std::int64_t nowMs)
{
    // A connection that never completes Hello is closed.
    for (auto& c : mConnections)
        if (!c->closed && !c->helloDone && nowMs - c->connectedMs > mConfig.helloTimeoutMs) Close(*c, Protocol3::kBadMessage, "hello timeout");

    if (!mStarted || mRace.Finished()) { if (mStarted && mRace.Finished()) FinishRace(); return; }
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
        Broadcast(Protocol3::EncodeFrame(Protocol3::kTick, Protocol3::Encode(t)));
    }
    if (mRace.Finished()) FinishRace();
}
}
