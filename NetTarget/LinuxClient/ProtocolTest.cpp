// Tests of protocol 3.0 (NetTarget/Authority/Protocol3.*) and the race host that speaks it (RaceHost.*).
//
//   HoverNetProtocolTest <track> [players]
//
// Framing and message codecs; a complete race with eight clients over a delayed in-memory network, where each client
// rebuilds the race from the server's Tick messages alone and checks every state hash; a snapshot fetched through the
// protocol; and the abuse cases (bad version, input before hello, oversize and malformed frames, floods, a full server,
// slow connections, random bytes).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/Protocol3.h"
#include "../Authority/RaceHost.h"
#include "../Authority/RaceWorld.h"
#include "../Authority/ReplayFormat.h"
#include "../Model/StateSnapshot.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

HostConfig MakeConfig(const std::string& track, int players)
{
    HostConfig c;
    c.race.trackName = track;
    c.race.trackPath = SimHarness::TrackPath(track);
    c.race.players = players;
    c.race.laps = 3;
    c.race.weapons = true;
    return c;
}

std::vector<std::uint8_t> HelloFrame(const char* name = "tester", std::uint16_t major = Protocol3::kMajor)
{
    Protocol3::Hello h;
    h.major = major;
    h.name = name;
    return Protocol3::EncodeFrame(Protocol3::kHello, Protocol3::Encode(h));
}

// Pulls every whole frame out of a host's output for one connection.
struct Inbox
{
    Protocol3::FrameParser parser;
    struct Frame { std::uint8_t type; std::vector<std::uint8_t> payload; };
    std::vector<Frame> frames;
    void Take(RaceHost& host, int conn)
    {
        const std::vector<std::uint8_t> bytes = host.TakeOutput(conn);
        parser.Feed(bytes.data(), bytes.size());
        Frame f;
        while (parser.Next(f.type, f.payload)) frames.push_back(f);
    }
    bool Has(std::uint8_t type) const { for (const Frame& f : frames) if (f.type == type) return true; return false; }
};

void Send(RaceHost& host, int conn, const std::vector<std::uint8_t>& bytes, std::int64_t now)
{
    host.Receive(conn, bytes.data(), bytes.size(), now);
}

// ---- framing and codecs --------------------------------------------------------------------------------------
void TestFraming()
{
    using namespace Protocol3;
    // Several frames, fed one byte at a time and all at once, give the same frames.
    std::vector<std::uint8_t> stream;
    for (int i = 0; i < 5; ++i) {
        Ping p; p.clientTick = i; p.token = 1000 + i;
        const std::vector<std::uint8_t> f = EncodeFrame(kPing, Encode(p));
        stream.insert(stream.end(), f.begin(), f.end());
    }
    FrameParser byByte, atOnce;
    int got1 = 0, got2 = 0;
    std::uint8_t type; std::vector<std::uint8_t> payload;
    for (std::uint8_t b : stream) { byByte.Feed(&b, 1); while (byByte.Next(type, payload)) ++got1; }
    atOnce.Feed(stream.data(), stream.size());
    while (atOnce.Next(type, payload)) ++got2;
    Check(got1 == 5 && got2 == 5, "frames arrive intact fed byte by byte and all at once");
    // A zero length and an oversize length are framing errors.
    FrameParser zero;
    const std::uint8_t z[] = {0, 0, 1};
    zero.Feed(z, sizeof(z));
    Check(!zero.Next(type, payload) && zero.Failed(), "a zero length is a framing error");
    FrameParser big(100);
    const std::uint8_t b[] = {200, 0, 1};
    big.Feed(b, sizeof(b));
    Check(!big.Next(type, payload) && big.Failed(), "a length above the maximum is a framing error");
    FrameParser partial;
    const std::uint8_t pr[] = {10, 0, 1, 2};
    partial.Feed(pr, sizeof(pr));
    Check(!partial.Next(type, payload) && !partial.Failed(), "an incomplete frame waits for more bytes");

    // Round trips, then every truncation and a trailing byte must be refused.
    Hello hello; hello.name = "Zoë ✓"; hello.capabilities = 7;
    Hello hello2;
    Check(Decode(Encode(hello), hello2) && hello2.name == hello.name && hello2.capabilities == 7, "hello round trip");
    HelloAck ack; ack.player = 2; ack.players = 4; ack.track = "ClassicH"; ack.trackSha256 = "abc"; ack.laps = 3; ack.rules = 4;
    HelloAck ack2;
    Check(Decode(Encode(ack), ack2) && ack2.track == "ClassicH" && ack2.player == 2 && ack2.laps == 3, "hello ack round trip");
    Input in; in.firstTick = 99; in.controls = {1, 2, 3};
    Input in2;
    Check(Decode(Encode(in), in2) && in2.controls == in.controls && in2.firstTick == 99, "input round trip");
    Tick tk; tk.tick = 7; tk.hash = 0x1122334455667788ULL; tk.controls = {5, 6};
    Tick tk2;
    Check(Decode(Encode(tk), tk2) && tk2.hash == tk.hash && tk2.controls == tk.controls, "tick round trip");
    Result res; res.ticks = 5; res.finalHash = 9; res.players.resize(2); res.players[1].rank = 2; res.players[1].finished = true;
    Result res2;
    Check(Decode(Encode(res), res2) && res2.players.size() == 2 && res2.players[1].rank == 2, "result round trip");
    SnapshotChunk ch; ch.id = 3; ch.index = 1; ch.count = 2; ch.tick = 4; ch.data = {1, 2, 3, 4};
    SnapshotChunk ch2;
    Check(Decode(Encode(ch), ch2) && ch2.data == ch.data && ch2.index == 1, "snapshot chunk round trip");

    struct Case { const char* name; std::vector<std::uint8_t> bytes; bool (*decode)(const std::vector<std::uint8_t>&); };
    auto decHello = [](const std::vector<std::uint8_t>& p) { Hello m; return Decode(p, m); };
    auto decAck = [](const std::vector<std::uint8_t>& p) { HelloAck m; return Decode(p, m); };
    auto decInput = [](const std::vector<std::uint8_t>& p) { Input m; return Decode(p, m); };
    auto decTick = [](const std::vector<std::uint8_t>& p) { Tick m; return Decode(p, m); };
    auto decResult = [](const std::vector<std::uint8_t>& p) { Result m; return Decode(p, m); };
    auto decChunk = [](const std::vector<std::uint8_t>& p) { SnapshotChunk m; return Decode(p, m); };
    const Case cases[] = {{"hello", Encode(hello), decHello}, {"hello ack", Encode(ack), decAck}, {"input", Encode(in), decInput},
                          {"tick", Encode(tk), decTick}, {"result", Encode(res), decResult}, {"snapshot chunk", Encode(ch), decChunk}};
    for (const Case& c : cases) {
        bool allTruncationsRefused = true;
        for (std::size_t n = 0; n < c.bytes.size(); ++n) {
            std::vector<std::uint8_t> cut(c.bytes.begin(), c.bytes.begin() + static_cast<std::ptrdiff_t>(n));
            if (c.decode(cut)) allTruncationsRefused = false;
        }
        std::vector<std::uint8_t> extra = c.bytes;
        extra.push_back(0);
        std::fprintf(stderr, "");
        if (!allTruncationsRefused) { std::fprintf(stderr, "FAIL: a truncated %s was accepted\n", c.name); ++gFailures; }
        if (c.decode(extra)) { std::fprintf(stderr, "FAIL: a %s with a trailing byte was accepted\n", c.name); ++gFailures; }
    }
    Input empty; Input out;
    Check(!Decode(Encode(empty), out), "an input message with no inputs is refused");
    Input many; many.controls.assign(kMaxInputsPerMessage + 1, 1);
    Check(!Decode(Encode(many), out), "an input message with too many inputs is refused");

    // Text rules.
    Check(IsValidText("Alice", 32) && IsValidText("Zoë ✓", 32), "ordinary and non-ASCII names are valid");
    Check(!IsValidText(std::string("a\0b", 3), 32), "an embedded NUL is refused");
    Check(!IsValidText("line\nbreak", 32) && !IsValidText("tab\t", 32), "control bytes are refused");
    Check(!IsValidText("\xC0\xAF", 32), "an overlong encoding is refused");
    Check(!IsValidText("\xED\xA0\x80", 32), "a surrogate is refused");
    Check(!IsValidText("\xF4\x90\x80\x80", 32), "a code point above U+10FFFF is refused");
    Check(!IsValidText("abc\xE2\x82", 32), "a truncated sequence is refused");
    Check(!IsValidText(std::string(33, 'x'), 32), "an overlong string is refused");
}

// ---- a whole race over the protocol ----------------------------------------------------------------------------
struct Client
{
    int conn = 0;
    Inbox inbox;
    int player = -1;
    bool started = false;
    std::deque<std::pair<std::int64_t, std::vector<std::uint8_t>>> toServer;    // (delivery time, bytes)
    std::deque<std::pair<std::int64_t, std::vector<std::uint8_t>>> fromServer;
    // The client's own rebuild of the race from the server's Tick messages.
    SimHarness::World world;
    std::uint32_t verifiedTicks = 0;
    bool diverged = false;
    bool gotResult = false;
    Protocol3::Result result;
    std::vector<std::uint8_t> snapshot;
    std::vector<Protocol3::SnapshotChunk> chunks;
};

void TestRace(const std::string& track, int players)
{
    HostConfig cfg = MakeConfig(track, players);
    RaceHost host(cfg);
    std::string error;
    Check(host.Init(error), "host init");
    const int oneWayMs = 60;
    std::vector<Client> clients(static_cast<std::size_t>(players));
    std::int64_t now = 1000;
    for (int i = 0; i < players; ++i) {
        clients[i].conn = host.Connect(now);
        Check(clients[i].world.Build(SimHarness::TrackPath(track), track, players, cfg.race.laps, cfg.race.weapons), "client builds its copy of the race");
        clients[i].toServer.push_back({now + oneWayMs, HelloFrame(("player" + std::to_string(i)).c_str())});
    }

    const std::uint32_t raceTicks = 1500;
    bool snapshotRequested = false;
    std::uint32_t snapshotTick = 0;
    for (; now < 1000 + 3000 + oneWayMs * 3 + static_cast<std::int64_t>(raceTicks) * 16 + 4000; now += 4) {
        // deliver client -> server
        for (Client& c : clients) {
            while (!c.toServer.empty() && c.toServer.front().first <= now) {
                Send(host, c.conn, c.toServer.front().second, now);
                c.toServer.pop_front();
            }
        }
        host.Advance(now);
        // server -> client
        for (Client& c : clients) {
            const std::vector<std::uint8_t> out = host.TakeOutput(c.conn);
            if (!out.empty()) c.fromServer.push_back({now + oneWayMs, out});
            while (!c.fromServer.empty() && c.fromServer.front().first <= now) {
                c.inbox.parser.Feed(c.fromServer.front().second.data(), c.fromServer.front().second.size());
                c.fromServer.pop_front();
                Inbox::Frame f;
                while (c.inbox.parser.Next(f.type, f.payload)) {
                    using namespace Protocol3;
                    if (f.type == kHelloAck) { HelloAck a; if (Decode(f.payload, a)) c.player = a.player; }
                    else if (f.type == kStart) c.started = true;
                    else if (f.type == kTick) {
                        Tick t;
                        if (!Decode(f.payload, t)) { c.diverged = true; continue; }
                        if (t.tick != c.verifiedTicks) { c.diverged = true; continue; }
                        c.world.Step(t.controls.data());
                        if (c.world.Hash() != t.hash) c.diverged = true;
                        ++c.verifiedTicks;
                    }
                    else if (f.type == kSnapshotChunk) { SnapshotChunk ch; if (Decode(f.payload, ch)) c.chunks.push_back(ch); }
                    else if (f.type == kResult) { c.gotResult = Decode(f.payload, c.result); }
                }
            }
            // Each client sends its input every tick, 12 ticks ahead of the tick it believes the server is on.
            if (c.started && host.Started() && now >= 1000) {
                static std::vector<std::int64_t> lastSent(8, -1000);
                if (now - lastSent[c.player] >= 16) {
                    lastSent[c.player] = now;
                    const std::uint32_t estimate = c.verifiedTicks + static_cast<std::uint32_t>(2 * oneWayMs / 16) + 2;
                    Protocol3::Input in;
                    in.firstTick = estimate >= 15 ? estimate - 15 : 0;
                    for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(SimHarness::ScriptedInput(c.player, in.firstTick + k));
                    c.toServer.push_back({now + oneWayMs, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in))});
                }
            }
        }
        if (!snapshotRequested && host.Race().CurrentTick() >= 600) {
            snapshotRequested = true;
            snapshotTick = host.Race().CurrentTick();
            Protocol3::SnapshotRequest r; r.id = 77;
            clients[0].toServer.push_back({now + oneWayMs, Protocol3::EncodeFrame(Protocol3::kSnapshotRequest, Protocol3::Encode(r))});
        }
        if (host.RaceFinished()) { /* keep delivering the result */ }
        if (host.Race().CurrentTick() >= raceTicks) break;
    }
    // Drain the last messages.
    for (int extra = 0; extra < 100; ++extra, now += 4) {
        host.Advance(now);
        for (Client& c : clients) {
            const std::vector<std::uint8_t> out = host.TakeOutput(c.conn);
            c.inbox.parser.Feed(out.data(), out.size());
            Inbox::Frame f;
            while (c.inbox.parser.Next(f.type, f.payload)) {
                if (f.type == Protocol3::kTick) {
                    Protocol3::Tick t;
                    if (Protocol3::Decode(f.payload, t) && t.tick == c.verifiedTicks) { c.world.Step(t.controls.data()); if (c.world.Hash() != t.hash) c.diverged = true; ++c.verifiedTicks; }
                }
            }
        }
    }

    Check(host.Started(), "the race started once the last player joined");
    Check(host.Race().CurrentTick() >= raceTicks, "the race ran");
    bool allPlayers = true, noDivergence = true, enough = true;
    for (int i = 0; i < players; ++i) {
        allPlayers = allPlayers && clients[i].player == i;
        noDivergence = noDivergence && !clients[i].diverged;
        enough = enough && clients[i].verifiedTicks + 40 >= host.Race().CurrentTick();
    }
    Check(allPlayers, "each connection got its own player slot");
    Check(noDivergence, "every client rebuilt the race from Tick messages with matching state hashes");
    Check(enough, "every client followed (almost) every tick");
    // Bandwidth budget: what one player costs the server in each direction, measured over the race above.
    {
        const RaceHost::Metrics& m = host.GetMetrics();
        const double seconds = host.Race().CurrentTick() * 0.016;
        const double downPerPlayer = static_cast<double>(m.tickBytesOut) / players / seconds;
        const double upPerPlayer = static_cast<double>(m.bytesIn) / players / seconds;
        std::fprintf(stderr, "  bandwidth per player: %.0f B/s down (Tick stream), %.0f B/s up (inputs); %llu inputs late, %llu accepted\n", downPerPlayer, upPerPlayer,
                     (unsigned long long)m.inputsLate, (unsigned long long)m.inputsAccepted);
        Check(downPerPlayer < 3000, "the Tick stream costs less than 3 KB/s per player at eight players");
        Check(upPerPlayer < 4000, "a player's inputs cost less than 4 KB/s up");
        Check(m.ticksSimulated == host.Race().CurrentTick(), "the tick counter matches the race");
        Check(m.invalidMessages == 0, "an honest race has no invalid messages");
    }
    // The replay of what the server ran verifies.
    ReplayFormat::Replay r;
    Check(ReplayFormat::Decode(host.Race().ReplayBytes(), r, error), "the host's replay decodes");

    // Snapshot through the protocol.
    {
        const std::vector<Protocol3::SnapshotChunk>& chunks = clients[0].chunks;
        Check(!chunks.empty(), "snapshot chunks arrived");
        std::vector<std::uint8_t> bytes;
        bool ordered = !chunks.empty() && chunks.size() == chunks[0].count;
        for (std::size_t i = 0; i < chunks.size(); ++i) { ordered = ordered && chunks[i].index == i && chunks[i].id == 77; bytes.insert(bytes.end(), chunks[i].data.begin(), chunks[i].data.end()); }
        Check(ordered, "snapshot chunks are complete and in order");
        SimHarness::World spectator;
        Check(spectator.Build(SimHarness::TrackPath(track), track, players, cfg.race.laps, cfg.race.weapons), "build the spectator's race");
        std::vector<MR_FreeElement*> external(spectator.craft.begin(), spectator.craft.end());
        std::int64_t t = 0;
        Check(MR_RestoreLevelState(spectator.session->GetCurrentLevel(), bytes.data(), bytes.size(), external, ReplayFormat::kSimulationRules, t, error),
              "the spectator restores the snapshot received through the protocol");
        // The snapshot was taken at a known tick; the spectator now follows the Tick stream from there.
        spectator.session->SetSimulationTime(static_cast<MR_SimulationTime>(t));
        std::fprintf(stderr, "  snapshot of %zu bytes in %zu chunks taken at tick %u (requested at %u)\n", bytes.size(), chunks.size(), chunks.empty() ? 0 : chunks[0].tick, snapshotTick);
        Check(!chunks.empty() && spectator.session->GetSimulationTime() == static_cast<MR_SimulationTime>(chunks[0].tick) * 16 + static_cast<MR_SimulationTime>(ReplayFormat::StartTimeMs(ReplayFormat::kSimulationRules)), "snapshot time matches its tick");
    }
}

// ---- abuse ---------------------------------------------------------------------------------------------------------
// A player drops out mid-race and comes back with its resume token: the slot is kept, the craft carries on with its last
// input, and the player fetches a snapshot and follows the Tick stream from there with every state hash matching.
void TestReconnect(const std::string& track)
{
    HostConfig cfg = MakeConfig(track, 2);
    cfg.startDelayMs = 0;
    RaceHost host(cfg);
    std::string error;
    host.Init(error);
    const int a = host.Connect(0), b = host.Connect(0);
    Send(host, a, HelloFrame("a"), 0);
    Send(host, b, HelloFrame("b"), 0);
    Inbox ib;
    ib.Take(host, b);
    std::uint64_t token = 0;
    for (const Inbox::Frame& f : ib.frames) if (f.type == Protocol3::kHelloAck) { Protocol3::HelloAck ack; if (Protocol3::Decode(f.payload, ack)) token = ack.resumeToken; }
    Check(token != 0, "the HelloAck carries a resume token");

    // Stage 1: both play to tick 300; b drops; a plays on to tick 450.
    auto play = [&](int upto, std::uint32_t from, bool bPlays) {
        for (std::uint32_t t = from; t < static_cast<std::uint32_t>(upto); ++t) {
            const std::int64_t now = static_cast<std::int64_t>(t) * 16;
            Protocol3::Input ia; ia.firstTick = t; ia.controls = {SimHarness::ScriptedInput(0, t)};
            Send(host, a, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(ia)), now);
            if (bPlays) {
                Protocol3::Input in; in.firstTick = t; in.controls = {SimHarness::ScriptedInput(1, t)};
                Send(host, b, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in)), now);
            }
            host.Advance(now);
        }
    };
    play(300, 0, true);
    host.Disconnect(b);
    Check(host.IsClosed(b), "player b dropped");
    play(450, 300, false);

    // A wrong token and a token before the start are refused.
    const int bad = host.Connect(450 * 16);
    Protocol3::Resume wrong; wrong.token = token ^ 0xFFFF;
    Send(host, bad, Protocol3::EncodeFrame(Protocol3::kResume, Protocol3::Encode(wrong)), 450 * 16);
    Check(host.IsClosed(bad), "a wrong resume token closes the connection");
    {
        RaceHost fresh(cfg); fresh.Init(error);
        const int c = fresh.Connect(0);
        Protocol3::Resume early; early.token = token;
        Send(fresh, c, Protocol3::EncodeFrame(Protocol3::kResume, Protocol3::Encode(early)), 0);
        Check(fresh.IsClosed(c), "a resume before the race starts is refused");
    }

    // Stage 2: b comes back.
    const int b2 = host.Connect(450 * 16);
    Protocol3::Resume resume; resume.token = token;
    Send(host, b2, Protocol3::EncodeFrame(Protocol3::kResume, Protocol3::Encode(resume)), 450 * 16);
    Protocol3::SnapshotRequest request; request.id = 5;
    Send(host, b2, Protocol3::EncodeFrame(Protocol3::kSnapshotRequest, Protocol3::Encode(request)), 450 * 16);
    Check(!host.IsClosed(b2), "the resumed connection stays open");
    Inbox back;
    back.Take(host, b2);
    int slot = -1;
    std::vector<Protocol3::SnapshotChunk> chunks;
    for (const Inbox::Frame& f : back.frames) {
        if (f.type == Protocol3::kHelloAck) { Protocol3::HelloAck ack; if (Protocol3::Decode(f.payload, ack)) slot = ack.player; }
        if (f.type == Protocol3::kSnapshotChunk) { Protocol3::SnapshotChunk ch; if (Protocol3::Decode(f.payload, ch)) chunks.push_back(ch); }
    }
    Check(slot == 1, "the resumed connection gets its old slot back");
    std::vector<std::uint8_t> bytes;
    for (const auto& ch : chunks) bytes.insert(bytes.end(), ch.data.begin(), ch.data.end());
    SimHarness::World world;
    Check(world.Build(SimHarness::TrackPath(track), track, 2, cfg.race.laps, cfg.race.weapons), "the returning player builds a fresh copy of the race");
    std::vector<MR_FreeElement*> external(world.craft.begin(), world.craft.end());
    std::int64_t t0 = 0;
    Check(!chunks.empty() && MR_RestoreLevelState(world.session->GetCurrentLevel(), bytes.data(), bytes.size(), external, ReplayFormat::kSimulationRules, t0, error), "the returning player restores the snapshot");
    world.session->SetSimulationTime(static_cast<MR_SimulationTime>(t0));
    const std::uint32_t snapshotTick = chunks.empty() ? 0 : chunks[0].tick;
    // Follow the Tick stream from the snapshot tick on.
    play(700, 450, true);
    back.Take(host, b2);
    std::uint32_t next = snapshotTick, matched = 0;
    bool ok = true;
    for (const Inbox::Frame& f : back.frames) {
        if (f.type != Protocol3::kTick) continue;
        Protocol3::Tick t;
        if (!Protocol3::Decode(f.payload, t) || t.tick < snapshotTick) continue;
        if (t.tick != next) { ok = false; break; }
        world.Step(t.controls.data());
        if (world.Hash() != t.hash) { ok = false; break; }
        ++next; ++matched;
    }
    Check(ok && matched >= 200, "after the snapshot the returning player matches the server's hash on every tick");
    std::fprintf(stderr, "  reconnected at tick %u from a %zu-byte snapshot; %u ticks matched afterwards\n", snapshotTick, bytes.size(), matched);

    // The old token still works (last connection wins) and replaces the connection that holds the slot.
    const int b3 = host.Connect(700 * 16);
    Send(host, b3, Protocol3::EncodeFrame(Protocol3::kResume, Protocol3::Encode(resume)), 700 * 16);
    Check(host.IsClosed(b2) && !host.IsClosed(b3), "a second reconnection replaces the first");
}

// Spectators see the race a fixed delay behind the drivers, starting from a keyframe snapshot, and can only watch.
void TestSpectator(const std::string& track)
{
    HostConfig cfg = MakeConfig(track, 4);
    cfg.startDelayMs = 0;
    cfg.spectatorDelayMs = 2560;      // 160 ticks
    cfg.race.maxTicks = 900;
    RaceHost host(cfg);
    std::string error;
    host.Init(error);
    int conns[4];
    for (int i = 0; i < 4; ++i) { conns[i] = host.Connect(0); Send(host, conns[i], HelloFrame(("p" + std::to_string(i)).c_str()), 0); }

    // A spectator before the start would be refused; here the race is already on, so join after 700 ticks.
    int spec = -1;
    Inbox seen;
    auto spectatorFrame = [&](std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now) { Send(host, spec, Protocol3::EncodeFrame(type, payload), now); };
    for (std::uint32_t t = 0; t < 900; ++t) {
        const std::int64_t now = static_cast<std::int64_t>(t) * 16;
        for (int i = 0; i < 4; ++i) {
            Protocol3::Input in; in.firstTick = t; in.controls = {SimHarness::ScriptedInput(i, t)};
            Send(host, conns[i], Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in)), now);
        }
        host.Advance(now);
        if (t == 700) {
            spec = host.Connect(now);
            Protocol3::Hello h; h.name = "watcher";
            spectatorFrame(Protocol3::kSpectate, Protocol3::Encode(h), now);
            Check(!host.IsClosed(spec), "a spectator is accepted once the race is running");
            Protocol3::Input rogue; rogue.firstTick = t; rogue.controls = {0xFF};
            spectatorFrame(Protocol3::kInput, Protocol3::Encode(rogue), now);   // a spectator cannot drive
        }
        if (spec >= 0) seen.Take(host, spec);
        for (int i = 0; i < 4; ++i) host.TakeOutput(conns[i]);
    }
    for (std::int64_t now = 900 * 16; now < 900 * 16 + 4000; now += 16) { host.Advance(now); seen.Take(host, spec); }

    // Rebuild the race from what the spectator was sent.
    RaceWorld world;
    Check(world.Build(cfg.race.trackPath, track, 4, cfg.race.laps, cfg.race.weapons, error), "the spectator builds its copy of the race");
    std::vector<Protocol3::SnapshotChunk> chunks;
    std::uint32_t keyTick = 0, nextTick = 0, applied = 0, lastTick = 0;
    bool restored = false, ok = true, gotAck = false, gotResult = false;
    for (const Inbox::Frame& f : seen.frames) {
        using namespace Protocol3;
        if (f.type == kHelloAck) gotAck = true;
        else if (f.type == kSnapshotChunk) { SnapshotChunk c; if (Decode(f.payload, c)) chunks.push_back(c); }
        else if (f.type == kTick) {
            if (!restored) {
                std::vector<std::uint8_t> bytes;
                for (const SnapshotChunk& c : chunks) bytes.insert(bytes.end(), c.data.begin(), c.data.end());
                keyTick = chunks.empty() ? 0 : chunks[0].tick;
                restored = !chunks.empty() && world.Restore(bytes, error);
                nextTick = keyTick;
                if (!restored) { ok = false; break; }
            }
            Tick t;
            if (!Decode(f.payload, t) || t.tick != nextTick) { ok = false; break; }
            world.Step(t.controls.data());
            if (world.Hash() != t.hash) { ok = false; break; }
            lastTick = t.tick;
            ++nextTick;
            ++applied;
        }
        else if (f.type == kResult) gotResult = true;
    }
    Check(gotAck, "the spectator gets the race details");
    Check(ok && restored, "the spectator restores the keyframe and follows the Tick stream with every hash matching");
    Check(keyTick == 500, "it starts from the newest keyframe not newer than 'now minus the delay' (tick 500 for 700 - 160 = 540)");
    Check(applied > 700 - 160 - 500, "it received the ticks from the keyframe on");
    std::fprintf(stderr, "  spectator joined at tick 700, started from keyframe %u, followed %u ticks to tick %u\n", keyTick, applied, lastTick);
    Check(gotResult, "the result reaches the spectator after the delay");
    Check(host.InvalidTotal() >= 1, "a spectator trying to send inputs is counted as invalid");

    // A spectator may not join before the race starts.
    {
        RaceHost waiting(cfg);
        std::string e2;
        waiting.Init(e2);
        const int sp = waiting.Connect(0);
        Protocol3::Hello h;
        h.name = "early";
        Send(waiting, sp, Protocol3::EncodeFrame(Protocol3::kSpectate, Protocol3::Encode(h)), 0);
        Check(waiting.IsClosed(sp), "a spectator is refused before the race starts");
    }
}

// A race with a time limit ends, broadcasts its result, and the late inputs after the end are refused.
void TestResult(const std::string& track)
{
    HostConfig cfg = MakeConfig(track, 2);
    cfg.race.maxTicks = 400;
    cfg.startDelayMs = 0;
    RaceHost host(cfg);
    std::string error;
    Check(host.Init(error), "host init (time limit)");
    const int a = host.Connect(0), b = host.Connect(0);
    Send(host, a, HelloFrame("a"), 0);
    Send(host, b, HelloFrame("b"), 0);
    for (std::int64_t now = 0; now < 400 * 16 + 200; now += 8) host.Advance(now);
    Inbox in; in.Take(host, a);
    Check(host.RaceFinished(), "a race with a time limit ends at the limit");
    Check(in.Has(Protocol3::kResult), "the result is broadcast when the race ends");
    int results = 0;
    for (const Inbox::Frame& f : in.frames) {
        if (f.type != Protocol3::kResult) continue;
        ++results;
        Protocol3::Result r;
        Check(Protocol3::Decode(f.payload, r) && r.ticks == 400 && r.players.size() == 2, "the result carries both players");
        Check(r.players.size() == 2 && ((r.players[0].rank == 1 && r.players[1].rank == 2) || (r.players[0].rank == 2 && r.players[1].rank == 1)), "ranks are 1 and 2");
        Check(r.finalHash == host.Race().StateHash(), "the result carries the final state hash");
    }
    Check(results == 1, "the result is sent once");
    Protocol3::Input i; i.firstTick = 400; i.controls = {1};
    Send(host, a, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(i)), 9000);
    Check(!host.IsClosed(a), "an input after the end is ignored, not an error");
}

// Runs a two-player race through a RaceHost with fixed inputs. With `tamper`, player 1 also sends everything it should not:
// server-only messages, a forged Tick and Result, undefined control bits, inputs for ticks far ahead and in the past, and
// garbage. The official outcome (state hash, results, replay) must be exactly that of the honest run with the same inputs.
struct Outcome { std::uint64_t hash = 0; std::vector<Authority::PlayerResult> results; std::vector<std::uint8_t> replay; int invalid = 0; bool closed = false; };

Outcome RunTampering(const std::string& track, bool tamper)
{
    HostConfig cfg = MakeConfig(track, 2);
    cfg.startDelayMs = 0;
    cfg.maxInvalidPerWindow = 100000;   // let the tamperer keep trying; closing it is tested elsewhere
    RaceHost host(cfg);
    std::string error;
    host.Init(error);
    const int a = host.Connect(0), b = host.Connect(0);
    Send(host, a, HelloFrame("honest"), 0);
    Send(host, b, HelloFrame("tamperer"), 0);
    for (std::uint32_t t = 0; t < 600; ++t) {
        const std::int64_t now = static_cast<std::int64_t>(t) * 16;
        Protocol3::Input ia; ia.firstTick = t; ia.controls = {SimHarness::ScriptedInput(0, t)};
        Send(host, a, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(ia)), now);
        Protocol3::Input ib; ib.firstTick = t; ib.controls = {SimHarness::ScriptedInput(1, t)};
        Send(host, b, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(ib)), now);
        if (tamper) {
            using namespace Protocol3;
            Tick forged; forged.tick = t; forged.hash = 0xBAD; forged.controls = {1, 1};
            Send(host, b, EncodeFrame(kTick, Encode(forged)), now);
            Result forgedResult; forgedResult.players.resize(2); forgedResult.players[1].rank = 1; forgedResult.players[1].finished = true;
            Send(host, b, EncodeFrame(kResult, Encode(forgedResult)), now);
            HelloAck ack; ack.players = 2; ack.player = 0;
            Send(host, b, EncodeFrame(kHelloAck, Encode(ack)), now);
            Send(host, b, EncodeFrame(kStart, Encode(Start())), now);
            Input wild; wild.firstTick = t + 1000; wild.controls = {0xFFFF};          // far in the future
            Send(host, b, EncodeFrame(kInput, Encode(wild)), now);
            if (t >= 10) {
                Input past; past.firstTick = t - 5; past.controls = {0xFFFF, 0xFFFF};          // ticks already simulated
                Send(host, b, EncodeFrame(kInput, Encode(past)), now);
            }
            Send(host, b, EncodeFrame(200, {1, 2, 3}), now);                          // unknown type
        }
        host.Advance(now);
    }
    Outcome o;
    o.hash = host.Race().StateHash();
    o.results = host.Race().Results();
    o.replay = host.Race().ReplayBytes();
    o.invalid = host.InvalidTotal();
    o.closed = host.IsClosed(a);
    return o;
}

void TestTampering(const std::string& track)
{
    const Outcome honest = RunTampering(track, false);
    const Outcome tampered = RunTampering(track, true);
    Check(honest.invalid == 0, "an honest run has no invalid messages");
    Check(tampered.invalid > 1000, "the tamperer's forged and out-of-window messages were all counted as invalid");
    Check(!tampered.closed, "the honest player is unaffected");
    Check(honest.hash == tampered.hash, "forged messages do not change the official state hash");
    bool sameResults = honest.results.size() == tampered.results.size();
    for (std::size_t i = 0; sameResults && i < honest.results.size(); ++i)
        sameResults = honest.results[i].rank == tampered.results[i].rank && honest.results[i].laps == tampered.results[i].laps && honest.results[i].finished == tampered.results[i].finished;
    Check(sameResults, "forged messages do not change the official results");
    Check(honest.replay == tampered.replay, "forged messages do not change the official replay, byte for byte");
}

void TestAbuse(const std::string& track)
{
    std::string error;
    {   // Wrong version.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int c = host.Connect(0);
        Send(host, c, HelloFrame("x", 2), 0);
        Inbox in; in.Take(host, c);
        Check(in.Has(Protocol3::kReject) && host.IsClosed(c), "a protocol 2 hello is rejected and closed");
    }
    {   // Anything before Hello.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int c = host.Connect(0);
        Protocol3::Input i; i.controls = {1};
        Send(host, c, Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(i)), 0);
        Check(host.IsClosed(c), "an input before hello closes the connection");
    }
    {   // Bad framing.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int c = host.Connect(0);
        const std::uint8_t big[] = {0xFF, 0xFF, 1};
        host.Receive(c, big, sizeof(big), 0);
        Check(host.IsClosed(c), "an oversize frame closes the connection");
        const int d = host.Connect(0);
        const std::uint8_t zero[] = {0, 0};
        host.Receive(d, zero, sizeof(zero), 0);
        Check(host.IsClosed(d), "a zero-length frame closes the connection");
    }
    {   // Malformed hello and a hello with bad text.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int c = host.Connect(0);
        Send(host, c, Protocol3::EncodeFrame(Protocol3::kHello, {1, 2, 3}), 0);
        Check(host.IsClosed(c), "a malformed hello closes the connection");
        const int d = host.Connect(0);
        Send(host, d, HelloFrame("bad\nname"), 0);
        Check(host.IsClosed(d), "a hello with a control byte in the name closes the connection");
    }
    {   // Eleven invalid messages in ten seconds.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int c = host.Connect(0);
        Send(host, c, HelloFrame(), 0);
        for (int i = 0; i < 10; ++i) Send(host, c, Protocol3::EncodeFrame(Protocol3::kPong, {0}), 100 * i);   // clients may not send Pong
        Check(!host.IsClosed(c), "ten invalid messages are tolerated");
        Send(host, c, Protocol3::EncodeFrame(Protocol3::kPong, {0}), 1100);
        Check(host.IsClosed(c), "the eleventh invalid message in ten seconds closes the connection");
        // Spread over time they are fine.
        const int d = host.Connect(0);
        Send(host, d, HelloFrame("slow"), 0);
        for (int i = 0; i < 30; ++i) Send(host, d, Protocol3::EncodeFrame(Protocol3::kPong, {0}), 2000 * i);
        Check(!host.IsClosed(d), "invalid messages spread over time do not close the connection");
    }
    {   // A full server and a late joiner.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int a = host.Connect(0), b = host.Connect(0), c = host.Connect(0);
        Send(host, a, HelloFrame("a"), 0); Send(host, b, HelloFrame("b"), 0); Send(host, c, HelloFrame("c"), 0);
        Inbox in; in.Take(host, c);
        Check(host.IsClosed(c) && in.Has(Protocol3::kReject), "a third player is refused with a message when the race is full");
        Check(host.Started(), "the race starts when it is full");
        const int d = host.Connect(10);
        Send(host, d, HelloFrame("late"), 10);
        Check(host.IsClosed(d), "a joiner after the start is refused");
    }
    {   // Input flood.
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        const int a = host.Connect(0), b = host.Connect(0);
        Send(host, a, HelloFrame("a"), 0); Send(host, b, HelloFrame("b"), 0);
        Protocol3::Input i; i.firstTick = 0; i.controls = {1};
        const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(i));
        for (int k = 0; k < 400 && !host.IsClosed(a); ++k) Send(host, a, frame, 5000);
        Check(host.IsClosed(a), "an input flood is rate limited and closes the connection");
        Check(!host.IsClosed(b), "the other player is unaffected by the flood");
    }
    {   // Hello timeout.
        HostConfig cfg = MakeConfig(track, 2);
        RaceHost host(cfg); host.Init(error);
        const int c = host.Connect(0);
        host.Advance(cfg.helloTimeoutMs + 1);
        Check(host.IsClosed(c), "a connection that never says hello is closed");
    }
    {   // Snapshot rate limit.
        HostConfig cfg = MakeConfig(track, 2);
        RaceHost host(cfg); host.Init(error);
        const int a = host.Connect(0), b = host.Connect(0);
        Send(host, a, HelloFrame("a"), 0); Send(host, b, HelloFrame("b"), 0);
        Protocol3::SnapshotRequest r; r.id = 1;
        const std::vector<std::uint8_t> frame = Protocol3::EncodeFrame(Protocol3::kSnapshotRequest, Protocol3::Encode(r));
        for (int k = 0; k < 20 && !host.IsClosed(a); ++k) Send(host, a, frame, 1000 + k);
        Check(host.IsClosed(a), "repeated snapshot requests are rate limited");
    }
    {   // Random bytes never crash the host.
        std::uint64_t s = 99;
        auto next = [&s]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; };
        RaceHost host(MakeConfig(track, 2)); host.Init(error);
        for (int round = 0; round < 3000; ++round) {
            const int c = host.Connect(round);
            std::vector<std::uint8_t> junk(1 + next() % 200);
            for (auto& b : junk) b = static_cast<std::uint8_t>(next());
            if (round % 3 == 0) { const std::vector<std::uint8_t> h = HelloFrame(); junk.insert(junk.begin(), h.begin(), h.end()); }
            if (round % 7 == 0) { junk[0] = static_cast<std::uint8_t>(junk.size() - 2 > 255 ? 255 : junk.size() - 2); if (junk.size() > 1) junk[1] = 0; }
            Send(host, c, junk, round);
            host.TakeOutput(c);
            host.Disconnect(c);
        }
        Check(true, "3000 connections of random bytes");
    }
}
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <track> [players]\n", argv[0]); return 2; }
    const std::string track = argv[1];
    const int players = argc > 2 ? std::atoi(argv[2]) : 8;
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    TestFraming();
    TestRace(track, players);
    TestReconnect(track);
    TestSpectator(track);
    TestResult(track);
    TestAbuse(track);
    TestTampering(track);
    std::printf("%s %d players: %s\n", track.c_str(), players, gFailures == 0 ? "protocol tests passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
