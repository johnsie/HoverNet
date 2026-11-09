// CraftDesignNetTest -- craft designs through the RaceServer: validated, relayed to the players who asked for them, and never part of the race.
//
//   HoverNetCraftDesignNetTest
#include <cstdio>
#include <string>
#include <vector>

#include "SimHarness.h"
#include "../Authority/CraftDesign.h"
#include "../Authority/RaceHost.h"
#include "../ObjFac1/SimElements.h"

using namespace Authority;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFailures; }

struct Player
{
    RaceHost* host = nullptr;
    int conn = 0;
    Protocol3::FrameParser parser;
    std::vector<std::pair<int, std::vector<std::uint8_t>>> designs;   // slot, bytes
    std::vector<std::uint64_t> tickHashes;
    void Send(std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now)
    {
        const std::vector<std::uint8_t> f = Protocol3::EncodeFrame(type, payload);
        host->Receive(conn, f.data(), f.size(), now);
    }
    void Pump()
    {
        const std::vector<std::uint8_t> bytes = host->TakeOutput(conn);
        parser.Feed(bytes.data(), bytes.size());
        std::uint8_t type;
        std::vector<std::uint8_t> payload;
        while (parser.Next(type, payload)) {
            if (type == Protocol3::kCraftDesign && !payload.empty()) designs.emplace_back(payload[0], std::vector<std::uint8_t>(payload.begin() + 1, payload.end()));
            if (type == Protocol3::kTick) { Protocol3::Tick t; if (Protocol3::Decode(payload, t)) tickHashes.push_back(t.hash); }
        }
    }
};

struct Room
{
    HostConfig cfg;
    std::unique_ptr<RaceHost> host;
    Player p[3];
    explicit Room(int players)
    {
        cfg.race.trackName = "ClassicH";
        cfg.race.trackPath = SimHarness::TrackPath("ClassicH");
        cfg.race.players = players;
        cfg.startDelayMs = 0;
        host.reset(new RaceHost(cfg));
        std::string error;
        host->Init(error);
    }
    void Join(int i, bool withCap)
    {
        p[i].host = host.get();
        p[i].conn = host->Connect(0);
        Protocol3::Hello h;
        h.name = std::string("player") + char('a' + i);
        if (withCap) h.capabilities = Protocol3::kCapCraftDesign;
        p[i].Send(Protocol3::kHello, Protocol3::Encode(h), 0);
    }
};
}

int main()
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    MR_RegisterSimElements();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    CraftDesign d;
    d.craft = 2; d.length = 15; d.width = 10; d.height = -5; d.body = {{200, 20, 20}}; d.bodyStrength = 200; d.name = "Racer";
    for (int i = 0; i < 100; ++i) d.paint[static_cast<size_t>(i * 9) % d.paint.size()] = 3;
    const std::vector<std::uint8_t> bytes = EncodeCraftDesign(d);

    // Two of three seats are filled; the first sends a design; then the third joins.
    Room room(3);
    room.Join(0, true);
    room.Join(1, true);
    room.p[0].Send(Protocol3::kSetCraftDesign, bytes, 10);
    room.p[0].Pump(); room.p[1].Pump();
    Check(room.p[0].designs.size() == 1 && room.p[0].designs[0].first == 0 && room.p[0].designs[0].second == bytes, "the sender is sent its own design back with its slot");
    Check(room.p[1].designs.size() == 1 && room.p[1].designs[0].first == 0 && room.p[1].designs[0].second == bytes, "another player who asked for designs is sent it, with the sender's slot, byte for byte");
    room.Join(2, false);
    room.p[2].Pump();
    Check(room.p[2].designs.empty(), "a player who did not ask for designs is sent none");

    // A late player who asked gets the designs already set.
    Room late(3);
    late.Join(0, true);
    late.p[0].Send(Protocol3::kSetCraftDesign, bytes, 10);
    late.Join(1, true);
    late.p[1].Pump();
    Check(late.p[1].designs.size() == 1 && late.p[1].designs[0].first == 0, "a player who joins later is sent the designs already set");

    // Anything that does not decode is refused as invalid and not relayed.
    const int invalidBefore = late.host->InvalidTotal();
    std::vector<std::uint8_t> damaged = bytes; damaged[20] ^= 1;
    late.p[1].Send(Protocol3::kSetCraftDesign, damaged, 20);
    Check(late.host->InvalidTotal() > invalidBefore, "a damaged design is refused as invalid");
    CraftDesign bad = d; bad.length = 90;
    late.p[1].Send(Protocol3::kSetCraftDesign, EncodeCraftDesign(bad), 30);
    late.p[0].Pump(); late.p[1].Pump();
    Check(late.p[0].designs.size() == 1, "a design with a shape out of range is not relayed");

    // A client that did not ask for designs may not send one.
    const int before = room.host->InvalidTotal();
    room.p[2].Send(Protocol3::kSetCraftDesign, bytes, 40);
    Check(room.host->InvalidTotal() > before, "a client that did not set the capability is refused");

    // The race itself is untouched: the same race with and without designs gives the same tick hashes.
    Room with(2), without(2);
    with.Join(0, true); with.Join(1, true);
    without.Join(0, true); without.Join(1, true);
    with.p[0].Send(Protocol3::kSetCraftDesign, bytes, 10);
    with.p[1].Send(Protocol3::kSetCraftDesign, bytes, 10);
    with.host->Advance(3000); without.host->Advance(3000);
    with.p[0].Pump(); without.p[0].Pump();
    Check(!with.p[0].tickHashes.empty() && with.p[0].tickHashes == without.p[0].tickHashes, "the race's tick hashes are identical with and without designs");
    std::printf("%zu tick hashes compared\n", with.p[0].tickHashes.size());

    std::printf(gFailures == 0 ? "craft design net: all checks passed\n" : "craft design net: FAILED\n");
    return gFailures == 0 ? 0 : 1;
}
