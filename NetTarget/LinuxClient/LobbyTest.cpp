// Tests of the lobby that hosts many authoritative races (NetTarget/Authority/LobbyServer.*).
//
//   HoverNetLobbyTest
//
// Races are hosted and joined through the protocol, run side by side without affecting each other (each ends in exactly the
// state a standalone race with the same inputs ends in), a returning player is routed to its race by its resume token, and
// the abuse cases are refused: unknown tracks, bad settings, unknown or full or running races, request floods, silent
// connections, too many races.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/LobbyServer.h"
#include "../Authority/Protocol3.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; }
}

struct Client
{
    LobbyServer* lobby = nullptr;
    int conn = 0;
    Protocol3::FrameParser parser;
    int player = -1;
    std::uint64_t token = 0;
    bool started = false, gotList = false, rejected = false, gotResult = false;
    Protocol3::RaceList list;
    std::uint8_t rejectReason = 0;
    unsigned ticks = 0;
    std::uint32_t ackRaceId = 0;
    std::size_t ackBytes = 0;     // the size of the HelloAck payload (an old client must get the old size)

    void Open(LobbyServer& l, std::int64_t now) { lobby = &l; conn = l.Connect(now); }
    void Send(std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now)
    {
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(type, payload);
        lobby->Receive(conn, f.data(), f.size(), now);
    }
    void Hello(const std::string& name, std::int64_t now, std::uint32_t capabilities = 0)
    {
        Protocol3::Hello h;
        h.name = name;
        h.capabilities = capabilities;
        Send(Protocol3::kHello, Protocol3::Encode(h), now);
    }
    void Pump()
    {
        const std::vector<std::uint8_t> bytes = lobby->TakeOutput(conn);
        parser.Feed(bytes.data(), bytes.size());
        std::uint8_t type;
        std::vector<std::uint8_t> payload;
        while (parser.Next(type, payload)) {
            using namespace Protocol3;
            if (type == kRaceList) gotList = Decode(payload, list);
            else if (type == kHelloAck) { HelloAck a; if (Decode(payload, a)) { player = a.player; token = a.resumeToken; ackRaceId = a.raceId; ackBytes = payload.size(); } }
            else if (type == kStart) started = true;
            else if (type == kTick) ++ticks;
            else if (type == kResult) gotResult = true;
            else if (type == kReject) { Reject r; Decode(payload, r); rejected = true; rejectReason = r.reason; }
        }
    }
};

LobbyConfig MakeConfig()
{
    LobbyConfig c;
    c.tracks = {"ClassicH", "The River", "Switchback"};
    c.trackDirectory = "NetTarget/Tracks";
    c.raceTemplate.startDelayMs = 0;
    return c;
}

Protocol3::HostRace Host(const char* track, int players, int laps = 3)
{
    Protocol3::HostRace h;
    h.track = track;
    h.players = static_cast<std::uint8_t>(players);
    h.laps = static_cast<std::uint8_t>(laps);
    return h;
}

std::uint64_t Standalone(const std::string& track, int players, int ticks)
{
    HostConfig cfg;
    cfg.race.trackName = track;
    cfg.race.trackPath = SimHarness::TrackPath(track);
    cfg.race.players = players;
    cfg.race.laps = 3;
    cfg.startDelayMs = 0;
    RaceHost host(cfg);
    std::string error;
    host.Init(error);
    std::vector<int> conns;
    for (int i = 0; i < players; ++i) {
        conns.push_back(host.Connect(0));
        Protocol3::Hello h;
        h.name = "p" + std::to_string(i);
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kHello, Protocol3::Encode(h));
        host.Receive(conns[i], f.data(), f.size(), 0);
    }
    for (int t = 0; t < ticks; ++t) {
        const std::int64_t now = static_cast<std::int64_t>(t) * 16;
        for (int i = 0; i < players; ++i) {
            Protocol3::Input in;
            in.firstTick = static_cast<std::uint32_t>(t);
            in.controls = {SimHarness::ScriptedInput(i, static_cast<std::uint32_t>(t))};
            const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kInput, Protocol3::Encode(in));
            host.Receive(conns[i], f.data(), f.size(), now);
        }
        host.Advance(now);
    }
    return host.Race().StateHash();
}
}

int main()
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    // ---- two races at once, hosted and joined through the lobby ------------------------------------------------
    {
        LobbyServer lobby(MakeConfig());
        std::int64_t now = 1000;
        Client a, b, c, d;
        a.Open(lobby, now); b.Open(lobby, now); c.Open(lobby, now); d.Open(lobby, now);
        a.Hello("alice", now); a.Pump();
        Check(a.gotList && a.list.races.empty(), "an empty server lists no races");
        a.Send(Protocol3::kHostRace, Protocol3::Encode(Host("ClassicH", 2)), now);
        a.Pump();
        Check(a.player == 0 && a.token != 0, "the host gets slot 0 and a resume token");
        Check(lobby.RaceCount() == 1, "hosting creates a race");

        b.Hello("bob", now); b.Pump();
        Check(b.gotList && b.list.races.size() == 1 && b.list.races[0].track == "ClassicH" && b.list.races[0].joined == 1 && b.list.races[0].players == 2 && !b.list.races[0].started,
              "a second player sees the open race with one of two joined");
        const std::uint32_t firstRace = b.list.races.empty() ? 0 : b.list.races[0].id;
        b.Send(Protocol3::kJoinRace, Protocol3::Encode(Protocol3::JoinRace{firstRace}), now);
        b.Pump(); a.Pump();
        Check(b.player == 1, "the joiner gets slot 1");
        Check(a.started && b.started, "the race starts when it is full");

        c.Hello("carol", now); c.Pump();
        c.Send(Protocol3::kHostRace, Protocol3::Encode(Host("The River", 2)), now);
        c.Pump();
        d.Hello("dave", now); d.Pump();
        Check(d.gotList && d.list.races.size() == 2, "the lobby lists both races");
        std::uint32_t secondRace = 0;
        for (const auto& r : d.list.races) if (r.track == "The River") secondRace = r.id;
        Check(d.list.races.size() == 2 && [&]{ for (const auto& r : d.list.races) if (r.id == firstRace) return r.started; return false; }(), "the full race is listed as started");
        d.Send(Protocol3::kJoinRace, Protocol3::Encode(Protocol3::JoinRace{secondRace}), now);
        d.Pump(); c.Pump();
        Check(c.started && d.started && lobby.RaceCount() == 2, "the second race is running beside the first");

        // Both races are driven for 600 ticks by the same scripted inputs as standalone runs.
        {
            std::int64_t t0 = now;
            std::int64_t end = t0 + 600 * 16;
            std::uint32_t tick = 0;
            for (; t0 < end; t0 += 16, ++tick) {
                Client* all[4] = {&a, &b, &c, &d};
                for (Client* x : all) {
                    Protocol3::Input in;
                    in.firstTick = tick;
                    in.controls = {SimHarness::ScriptedInput(x->player, tick)};
                    x->Send(Protocol3::kInput, Protocol3::Encode(in), t0);
                }
                lobby.Advance(t0);
                for (Client* x : all) x->Pump();
            }
            now = t0;
        }
        const RaceHost* r1 = lobby.Race(firstRace);
        const RaceHost* r2 = lobby.Race(secondRace);
        Check(r1 != nullptr && r2 != nullptr, "both races exist");
        if (r1 && r2) {
            // The lobby's clock started at `1100`; the race clock starts at its own start, so compare after the same tick count.
            Check(r1->Race().CurrentTick() > 500 && r2->Race().CurrentTick() > 500, "both races ran");
            const std::uint32_t t1 = r1->Race().CurrentTick(), t2 = r2->Race().CurrentTick();
            Check(r1->Race().StateHash() == Standalone("ClassicH", 2, static_cast<int>(t1)), "race 1 ends in exactly the state of a standalone race with the same inputs");
            Check(r2->Race().StateHash() == Standalone("The River", 2, static_cast<int>(t2)), "race 2 ends in exactly the state of a standalone race with the same inputs");
        }
        Check(a.ticks > 400 && c.ticks > 400, "players receive their race's Tick stream");

        // ---- a returning player is routed to its race by the token -----------------------------------------------
        const std::uint64_t token = b.token;
        lobby.Disconnect(b.conn);
        Client b2;
        b2.Open(lobby, now);
        b2.Send(Protocol3::kResume, Protocol3::Encode(Protocol3::Resume{token}), now);
        b2.Pump();
        Check(b2.player == 1 && !b2.rejected, "a resume with the token returns to the same race and slot");
        Client b3;
        b3.Open(lobby, now);
        b3.Send(Protocol3::kResume, Protocol3::Encode(Protocol3::Resume{token ^ 0x5555}), now);
        b3.Pump();
        Check(b3.rejected && lobby.IsClosed(b3.conn), "a resume with an unknown token is refused");

        // ---- cannot join a running race; unknown race ------------------------------------------------------------
        Client e;
        e.Open(lobby, now);
        e.Hello("eve", now); e.Pump();
        e.Send(Protocol3::kJoinRace, Protocol3::Encode(Protocol3::JoinRace{firstRace}), now);
        e.Pump();
        Check(e.rejected && e.rejectReason == Protocol3::kServerFull, "a running race refuses a joiner");
        Client f;
        f.Open(lobby, now);
        f.Hello("frank", now); f.Pump();
        f.Send(Protocol3::kJoinRace, Protocol3::Encode(Protocol3::JoinRace{9999}), now);
        f.Pump();
        Check(f.rejected && f.rejectReason == Protocol3::kNoSuchRace, "an unknown race is refused");
    }

    // ---- settings and limits --------------------------------------------------------------------------------------
    {
        LobbyServer lobby(MakeConfig());
        std::int64_t now = 0;
        struct Case { const char* what; Protocol3::HostRace race; };
        const Case cases[] = {
            {"a track the operator does not allow", Host("Tidal Causeway", 2)},
            {"a track name that climbs out of the directory", Host("../../etc/passwd", 2)},
            {"no players", Host("ClassicH", 0)},
            {"nine players", Host("ClassicH", 9)},
            {"zero laps", Host("ClassicH", 2, 0)},
            {"eleven laps", Host("ClassicH", 2, 11)},
        };
        for (const Case& k : cases) {
            Client x;
            x.Open(lobby, now);
            x.Hello("x", now); x.Pump();
            x.Send(Protocol3::kHostRace, Protocol3::Encode(k.race), now);
            x.Pump();
            const std::string what = std::string("hosting ") + k.what + " is refused";
            Check(x.rejected && x.rejectReason == Protocol3::kBadSettings && lobby.RaceCount() == 0, what.c_str());
        }
        LobbyConfig cfg = MakeConfig();
        cfg.maxRaces = 2;
        LobbyServer limited(cfg);
        for (int i = 0; i < 3; ++i) {
            Client x;
            x.Open(limited, now);
            x.Hello("x", now); x.Pump();
            x.Send(Protocol3::kHostRace, Protocol3::Encode(Host("ClassicH", 2)), now);
            x.Pump();
            if (i < 2) Check(!x.rejected, "a race within the limit is created");
            else Check(x.rejected && x.rejectReason == Protocol3::kTooManyRaces, "a race over the limit is refused");
        }
        // Abandoned races that never started are removed.
        limited.Advance(100);
        limited.Advance(200);
        // Commands before hello, floods, silent connections.
        Client early;
        early.Open(lobby, now);
        early.Send(Protocol3::kListRaces, {}, now);
        early.Pump();
        Check(early.rejected, "a request before hello is refused");
        Client flood;
        flood.Open(lobby, now);
        flood.Hello("flood", now);
        for (int i = 0; i < 100 && !lobby.IsClosed(flood.conn); ++i) flood.Send(Protocol3::kListRaces, {}, now);
        Check(lobby.IsClosed(flood.conn), "a flood of list requests closes the connection");
        Client silent;
        silent.Open(lobby, now);
        lobby.Advance(now + 6000);
        Check(lobby.IsClosed(silent.conn), "a connection that never says hello is closed");
        Client wrong;
        wrong.Open(lobby, now);
        Protocol3::Hello h;
        h.name = "old";
        h.major = 2;
        wrong.Send(Protocol3::kHello, Protocol3::Encode(h), now);
        wrong.Pump();
        Check(wrong.rejected && wrong.rejectReason == Protocol3::kUnsupportedVersion, "a protocol 2 client is told to update");
    }

    // ---- private races: unlisted, joined only with the code -------------------------------------------------------
    {
        LobbyServer lobby(MakeConfig());
        std::int64_t now = 1000;
        Client host, friendly, stranger, wrong, watcher, observer;
        host.Open(lobby, now); friendly.Open(lobby, now); stranger.Open(lobby, now); wrong.Open(lobby, now); watcher.Open(lobby, now); observer.Open(lobby, now);
        host.Hello("hostess", now); host.Pump();
        Protocol3::HostRace priv = Host("ClassicH", 2);
        priv.code = "kestrel7";
        host.Send(Protocol3::kHostRace, Protocol3::Encode(priv), now);
        host.Pump();
        Check(host.player == 0 && !host.rejected, "a private race can be hosted");
        stranger.Hello("stranger", now); stranger.Pump();
        Check(stranger.gotList && stranger.list.races.empty(), "a private race is not in the race list");
        Protocol3::JoinRace join; join.id = 1;
        stranger.Send(Protocol3::kJoinRace, Protocol3::Encode(join), now); stranger.Pump();
        Check(stranger.rejected && stranger.rejectReason == Protocol3::kNoSuchRace, "joining a private race without a code looks like a race that does not exist");
        wrong.Hello("wrong", now); wrong.Pump();
        join.code = "kestrel8";
        wrong.Send(Protocol3::kJoinRace, Protocol3::Encode(join), now); wrong.Pump();
        Check(wrong.rejected && wrong.rejectReason == Protocol3::kNoSuchRace, "a wrong code is refused the same way");
        friendly.Hello("friend", now); friendly.Pump();
        join.code = "kestrel7";
        friendly.Send(Protocol3::kJoinRace, Protocol3::Encode(join), now); friendly.Pump();
        Check(friendly.player == 1 && !friendly.rejected, "the right code joins the private race");
        watcher.Hello("watcher", now); watcher.Pump();
        Protocol3::SpectateRace watch; watch.id = 1;
        watcher.Send(Protocol3::kSpectateRace, Protocol3::Encode(watch), now); watcher.Pump();
        Check(watcher.rejected, "watching a private race needs the code too");
        observer.Hello("observer", now); observer.Pump();
        watch.code = "kestrel7";
        observer.Send(Protocol3::kSpectateRace, Protocol3::Encode(watch), now); observer.Pump();
        Check(!observer.rejected, "watching with the code is allowed");
        // Codes are 6 to 16 characters when present; shorter ones are an invalid message.
        Protocol3::HostRace shortCode = Host("ClassicH", 2);
        shortCode.code = "abc";
        const std::vector<std::uint8_t> enc = Protocol3::Encode(shortCode);
        Protocol3::HostRace back;
        Check(!Protocol3::Decode(enc, back), "a code shorter than 6 characters does not decode");
        Check(Protocol3::Decode(Protocol3::Encode(Host("ClassicH", 2)), back) && back.code.empty(), "a host request without a code is an ordinary public one");
        // Guessing: after 30 wrong guesses the race refuses even the right code.
        for (int i = 0; i < 30; ++i) {
            Client g; g.Open(lobby, now); g.Hello("g", now); g.Pump();
            Protocol3::JoinRace j; j.id = 1; j.code = "guess" + std::to_string(100 + i);
            g.Send(Protocol3::kJoinRace, Protocol3::Encode(j), now);
            now += 300;   // stay under the per-connection limits; each guess is its own connection
        }
        Client late; late.Open(lobby, now); late.Hello("late", now); late.Pump();
        join.code = "kestrel7";
        late.Send(Protocol3::kJoinRace, Protocol3::Encode(join), now); late.Pump();
        Check(late.rejected, "a race that has been guessed at 30 times refuses everyone");
    }

    // ---- the race number reaches a host that asks for it (kCapRaceId), and only that host -------------------------------------
    {
        LobbyServer lobby(MakeConfig());
        std::int64_t now = 1000;
        Client modern, old, joiner;
        modern.Open(lobby, now); old.Open(lobby, now); joiner.Open(lobby, now);
        modern.Hello("modern", now, Protocol3::kCapRaceId); modern.Pump();
        Protocol3::HostRace priv = Host("ClassicH", 2);
        priv.code = "kestrel7";
        modern.Send(Protocol3::kHostRace, Protocol3::Encode(priv), now); modern.Pump();
        Check(modern.ackRaceId == 1, "a host that asks for it is told the race number (so it can tell friends)");
        old.Hello("plain", now); old.Pump();
        Protocol3::JoinRace join; join.id = 1; join.code = "kestrel7";
        old.Send(Protocol3::kJoinRace, Protocol3::Encode(join), now); old.Pump();
        Check(old.player == 1 && old.ackRaceId == 0, "a client that did not ask gets the original acknowledgement, without the race number");
        Check(old.ackBytes + 4 == modern.ackBytes, "the acknowledgement is exactly four bytes longer for a client that asked");
    }

    // ---- random bytes ----------------------------------------------------------------------------------------------
    {
        LobbyServer lobby(MakeConfig());
        std::uint64_t s = 4242;
        auto next = [&s]() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ULL; };
        for (int round = 0; round < 2000; ++round) {
            const int conn = lobby.Connect(round);
            std::vector<std::uint8_t> junk(1 + next() % 120);
            for (auto& b : junk) b = static_cast<std::uint8_t>(next());
            if (round % 3 == 0) {
                Protocol3::Hello h; h.name = "fuzz";
                const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(Protocol3::kHello, Protocol3::Encode(h));
                junk.insert(junk.begin(), f.begin(), f.end());
            }
            lobby.Receive(conn, junk.data(), junk.size(), round);
            lobby.TakeOutput(conn);
            lobby.Disconnect(conn);
            if (round % 50 == 0) lobby.Advance(round * 10);
        }
        Check(true, "2000 connections of random bytes");
    }

    std::printf("lobby tests %s\n", gFailures == 0 ? "passed" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
