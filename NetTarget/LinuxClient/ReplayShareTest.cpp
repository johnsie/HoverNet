// Tests of the replays a server shares with every player: the replay folder store (listing newest first, by track, the number of a replay, pruning,
// files that do not decode), the track records built from the results tables, and the lobby messages (list, download in pieces, a missing replay,
// request limits, malformed requests).
//
//   HoverNetReplayShareTest
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#define MAKE_DIR(d) mkdir(d, 0755)
#endif

#include "../Authority/LobbyServer.h"
#include "../Authority/Protocol3.h"
#include "../Authority/ReplayFormat.h"
#include "../Authority/ReplayStoreDir.h"
#include "Sha256.h"

using namespace Authority;

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

static std::vector<std::uint8_t> MakeReplay(const std::string& track, int players, int laps, std::uint32_t ticks, std::uint64_t salt)
{
    ReplayFormat::Replay r;
    r.track = track; r.trackSha256 = std::string(64, 'b'); r.players = players; r.laps = laps; r.ticks = ticks;
    r.inputs.assign(static_cast<std::size_t>(ticks) * players, static_cast<std::uint16_t>(salt & 0xff));
    r.checkpoints.assign(ticks / ReplayFormat::kCheckpointTicks, salt);
    r.finalHash = salt;
    return ReplayFormat::Encode(r);
}
static void WriteFile(const std::string& path, const std::string& text) { std::ofstream out(path, std::ios::binary); out << text; }
static void WriteBytes(const std::string& path, const std::vector<std::uint8_t>& b) { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size())); }
static void SetAge(const std::string& path, long secondsAgo)
{
#ifndef _WIN32
    timeval times[2]; const long t = static_cast<long>(time(nullptr)) - secondsAgo;
    times[0].tv_sec = times[1].tv_sec = t; times[0].tv_usec = times[1].tv_usec = 0;
    utimes(path.c_str(), times);
#else
    (void)path; (void)secondsAgo;
#endif
}

struct Client
{
    LobbyServer& lobby; int conn; Protocol3::FrameParser parser;
    std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> frames;
    Client(LobbyServer& l, std::int64_t now) : lobby(l), conn(l.Connect(now)) {}
    void Send(std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t now) { const auto f = Protocol3::EncodeFrame(type, payload); lobby.Receive(conn, f.data(), f.size(), now); }
    void Pump()
    {
        const auto bytes = lobby.TakeOutput(conn);
        parser.Feed(bytes.data(), bytes.size());
        std::uint8_t type; std::vector<std::uint8_t> payload;
        while (parser.Next(type, payload)) frames.push_back(std::make_pair(type, payload));
    }
    void Clear() { frames.clear(); }
};

int main()
{
    const std::string dir = "hovernet-replay-share-test.tmp";
    MAKE_DIR(dir.c_str());
    {   // a clean start even after an aborted run
        DirReplayStore wipe(dir, 0);
        wipe.Prune();
    }
    // Files: three ClassicH replays (3 laps; 2 laps), one The River, a damaged one, one that is not a replay.
    const std::vector<std::uint8_t> a = MakeReplay("ClassicH", 3, 3, 500, 11), b = MakeReplay("ClassicH", 2, 3, 700, 22), c = MakeReplay("ClassicH", 2, 2, 300, 33), d = MakeReplay("The River", 4, 5, 400, 44);
    WriteBytes(dir + "/ClassicH-1-100.hnr", a);  WriteFile(dir + "/ClassicH-1-100.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n1\t2\t3\t1\t95000\t30000\n2\t0\t3\t1\t99000\t31000\n3\t1\t1\t0\t0\t40000\n");
    WriteBytes(dir + "/ClassicH-2-200.hnr", b);  WriteFile(dir + "/ClassicH-2-200.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n1\t1\t3\t1\t93000\t29000\n2\t0\t3\t1\t97000\t33000\n");
    WriteBytes(dir + "/ClassicH-3-300.hnr", c);  WriteFile(dir + "/ClassicH-3-300.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\n1\t0\t2\t1\t61000\t30000\n");
    WriteBytes(dir + "/The_River-4-400.hnr", d);
    WriteFile(dir + "/Broken-5-500.hnr", "not a replay");
    WriteFile(dir + "/notes.txt", "x");
    SetAge(dir + "/ClassicH-1-100.hnr", 4000); SetAge(dir + "/ClassicH-2-200.hnr", 3000); SetAge(dir + "/ClassicH-3-300.hnr", 2000); SetAge(dir + "/The_River-4-400.hnr", 1000);

    DirReplayStore store(dir, 3);
    std::vector<Protocol3::ReplayInfo> list = store.List("", 20);
    Check(list.size() == 4, "four valid replays are listed (a damaged file and a text file are not)");
    Check(!list.empty() && list[0].track == "The River" && list[0].players == 4 && list[0].laps == 5 && list[0].ticks == 400 && list[0].bytes == d.size(), "the newest first, with its details from the file's own header");
    Check(list.size() == 4 && list[3].track == "ClassicH" && list[3].winnerMs == 95000, "the oldest last; the winner's time comes from the results table");
    Check(store.List("ClassicH", 20).size() == 3 && store.List("Nowhere", 20).empty(), "listing by track");
    Check(store.List("", 2).size() == 2, "a limit");
    const std::string hex = Sha256::HashBytes(a.data(), a.size()).substr(0, 16);
    Check(list.size() == 4 && list[3].id == std::strtoull(hex.c_str(), nullptr, 16), "the number of a replay is the first 8 bytes of its SHA-256 (what HoverNetReplay id prints)");
    std::vector<std::uint8_t> got;
    Check(list.size() == 4 && store.Read(list[3].id, got) && got == a, "a replay is read by its number, byte for byte");
    Check(!store.Read(12345, got), "an unknown number is no replay");

    // Records from the results tables.
    std::vector<Protocol3::RecordBoard> boards = store.Records("ClassicH");
    Check(boards.size() == 2 && boards[0].laps == 2 && boards[1].laps == 3, "a board per lap count, fewest laps first");
    Check(boards.size() == 2 && boards[1].finishes.size() == 4 && boards[1].finishes[0].ms == 93000 && boards[1].finishes[0].slot == 1 && boards[1].finishes[1].ms == 95000 && boards[1].finishes[1].slot == 2, "the fastest finishes of three laps, best first, with the slot");
    Check(boards.size() == 2 && boards[1].finishes[0].replay == list[2].id, "each record names the replay it came from");
    Check(boards.size() == 2 && boards[1].bestLaps.size() == 5 && boards[1].bestLaps[0].ms == 29000 && boards[1].bestLaps[1].ms == 30000, "the fastest laps (an unfinished run's best lap counts)");
    Check(store.Records("The River").empty(), "a track with no results table has no records");

    // The lobby messages.
    LobbyConfig config;
    config.tracks = {"ClassicH"};
    LobbyServer lobby(config);
    lobby.SetReplayStore(&store);
    std::int64_t now = 1000;
    Client client(lobby, now);
    Protocol3::Hello hello; hello.name = "tester";
    client.Send(Protocol3::kHello, Protocol3::Encode(hello), now);
    client.Pump(); client.Clear();
    Protocol3::ListReplays ask; ask.max = 10;
    client.Send(Protocol3::kListReplays, Protocol3::Encode(ask), now);
    client.Pump();
    Protocol3::ReplayList over;
    Check(client.frames.size() == 1 && client.frames[0].first == Protocol3::kReplayList && Protocol3::Decode(client.frames[0].second, over) && over.replays.size() == 4, "the lobby lists the replays");
    client.Clear();
    ask.track = "The River";
    client.Send(Protocol3::kListReplays, Protocol3::Encode(ask), now);
    client.Pump();
    Check(client.frames.size() == 1 && Protocol3::Decode(client.frames[0].second, over) && over.replays.size() == 1 && over.replays[0].track == "The River", "listing one track");
    client.Clear();

    Protocol3::GetReplay get; get.id = list[3].id;
    now += 1000;
    client.Send(Protocol3::kGetReplay, Protocol3::Encode(get), now);
    client.Pump();
    std::vector<std::uint8_t> assembled; bool inOrder = true; std::size_t chunks = 0;
    for (const auto& f : client.frames) {
        Protocol3::ReplayChunk ch;
        if (f.first != Protocol3::kReplayChunk || !Protocol3::Decode(f.second, ch) || ch.id != get.id || ch.index != chunks) { inOrder = false; break; }
        assembled.insert(assembled.end(), ch.data.begin(), ch.data.end());
        ++chunks;
        if (ch.index + 1 == ch.count) break;
    }
    Check(inOrder && assembled == a, "a replay downloads in order, in pieces, and reassembles to the file");
    Check(chunks == (a.size() + Protocol3::kReplayChunkBytes - 1) / Protocol3::kReplayChunkBytes, "in pieces of the documented size");
    client.Clear();

    now += 1000;
    get.id = 777;
    client.Send(Protocol3::kGetReplay, Protocol3::Encode(get), now);
    client.Pump();
    Protocol3::Reject rejected;
    Check(client.frames.size() == 1 && client.frames[0].first == Protocol3::kReject && Protocol3::Decode(client.frames[0].second, rejected) && rejected.reason == Protocol3::kNoSuchReplay, "a replay that is gone is refused with its own reason");
    Check(!lobby.IsClosed(client.conn), "and the connection stays open");
    client.Clear();

    Protocol3::ListRecords records; records.track = "ClassicH";
    client.Send(Protocol3::kListRecords, Protocol3::Encode(records), now);
    client.Pump();
    Protocol3::RecordList board;
    Check(client.frames.size() == 1 && client.frames[0].first == Protocol3::kRecordList && Protocol3::Decode(client.frames[0].second, board) && board.track == "ClassicH" && board.boards.size() == 2 && board.boards[1].finishes[0].ms == 93000, "the lobby answers the track records");
    client.Clear();

    now += 100;   // faster than the download pace
    get.id = list[3].id;
    client.Send(Protocol3::kGetReplay, Protocol3::Encode(get), now);
    client.Send(Protocol3::kGetReplay, Protocol3::Encode(get), now + 10);
    Check(lobby.IsClosed(client.conn), "two downloads in a row, faster than allowed, close the connection");

    // Malformed requests do not crash and are counted as invalid.
    Client bad(lobby, now);
    bad.Send(Protocol3::kHello, Protocol3::Encode(hello), now);
    bad.Send(Protocol3::kListReplays, std::vector<std::uint8_t>{1, 2, 3}, now);
    bad.Send(Protocol3::kGetReplay, std::vector<std::uint8_t>{1}, now);
    bad.Send(Protocol3::kListRecords, std::vector<std::uint8_t>(), now);
    bad.Pump();
    Check(!lobby.IsClosed(bad.conn), "a few malformed requests are tolerated");

    // A server with no replay folder answers with nothing.
    LobbyServer empty(config);
    Client none(empty, now);
    none.Send(Protocol3::kHello, Protocol3::Encode(hello), now);
    none.Pump(); none.Clear();
    ask.track.clear();
    none.Send(Protocol3::kListReplays, Protocol3::Encode(ask), now);
    none.Pump();
    Check(none.frames.size() == 1 && Protocol3::Decode(none.frames[0].second, over) && over.replays.empty(), "no replay folder: an empty list");

    // Pruning keeps the newest three, with their results tables.
    Check(store.Prune() == 1 && store.Count() == 3, "pruning keeps the newest replays");
    std::ifstream gone(dir + "/ClassicH-1-100.hnr");
    Check(!gone.good(), "the oldest replay was removed");
    std::ifstream goneResults(dir + "/ClassicH-1-100.results.tsv");
    Check(!goneResults.good(), "and its results table");

    // Protocol round trips and limits.
    Protocol3::ReplayChunk chunk; chunk.id = 5; chunk.index = 2; chunk.count = 1;
    Protocol3::ReplayChunk back;
    Check(!Protocol3::Decode(Protocol3::Encode(chunk), back), "a chunk with an index past the count is refused");
    chunk.count = 3; chunk.data.assign(Protocol3::kReplayChunkBytes + 1, 7);
    Check(!Protocol3::Decode(Protocol3::Encode(chunk), back), "an oversized chunk is refused");

    // Clean up.
    for (const char* name : {"ClassicH-2-200.hnr", "ClassicH-2-200.results.tsv", "ClassicH-3-300.hnr", "ClassicH-3-300.results.tsv", "The_River-4-400.hnr", "Broken-5-500.hnr", "notes.txt"}) std::remove((dir + "/" + name).c_str());
#ifdef _WIN32
    _rmdir(dir.c_str());
#else
    rmdir(dir.c_str());
#endif
    if (gFailures == 0) std::printf("replay share tests passed\n");
    return gFailures == 0 ? 0 : 1;
}
