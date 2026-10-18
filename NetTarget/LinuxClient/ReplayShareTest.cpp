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
    WriteBytes(dir + "/ClassicH-1-100.hnr", a);  WriteFile(dir + "/ClassicH-1-100.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n1\t2\t3\t1\t95000\t30000\tAnna\t2\n2\t0\t3\t1\t99000\t31000\tBen\t0\n3\t1\t1\t0\t0\t40000\tCara\t0\n");
    WriteBytes(dir + "/ClassicH-2-200.hnr", b);  WriteFile(dir + "/ClassicH-2-200.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n1\t1\t3\t1\t93000\t29000\t\t0\n2\t0\t3\t1\t97000\t33000\t\t2\n");
    WriteBytes(dir + "/ClassicH-3-300.hnr", c);  WriteFile(dir + "/ClassicH-3-300.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n1\t0\t2\t1\t61000\t30000\tDee\t1\n");
    WriteBytes(dir + "/The_River-4-400.hnr", d);
    WriteFile(dir + "/Broken-5-500.hnr", "not a replay");
    WriteFile(dir + "/notes.txt", "x");
    SetAge(dir + "/ClassicH-1-100.hnr", 4000); SetAge(dir + "/ClassicH-2-200.hnr", 3000); SetAge(dir + "/ClassicH-3-300.hnr", 2000); SetAge(dir + "/The_River-4-400.hnr", 1000);

    DirReplayStore store(dir, 3);
    std::vector<Protocol3::ReplayInfo> list = store.List("", 20);
    Check(list.size() == 4, "four valid replays are listed (a damaged file and a text file are not)");
    Check(!list.empty() && list[0].track == "The River" && list[0].players == 4 && list[0].laps == 5 && list[0].ticks == 400 && list[0].bytes == d.size(), "the newest first, with its details from the file's own header");
    Check(list.size() == 4 && list[3].track == "ClassicH" && list[3].winnerMs == 95000 && list[3].winnerSlot == 2 && list[3].winnerName == "Anna", "the oldest last; the winner's time, slot and name come from the results table");
    Check(list.size() == 4 && list[0].winnerSlot == 255 && list[0].finishers.empty(), "a replay with no results table has no winner and no finisher list");
    Check(list.size() == 4 && list[3].finishers.size() == 3 && list[3].finishers[0].name == "Anna" && list[3].finishers[0].slot == 2 && list[3].finishers[0].craft == 2 && list[3].finishers[0].ms == 95000 && list[3].finishers[1].name == "Ben" && list[3].finishers[1].ms == 99000 && list[3].finishers[2].name == "Cara" && list[3].finishers[2].ms == 0, "everyone in the race is listed in finishing order with name, craft and time (0 for a player who did not finish)");
    Check(store.List("ClassicH", 20).size() == 3 && store.List("Nowhere", 20).empty(), "listing by track");
    Check(store.List("", 2).size() == 2, "a limit");
    const std::string hex = Sha256::HashBytes(a.data(), a.size()).substr(0, 16);
    Check(list.size() == 4 && list[3].id == std::strtoull(hex.c_str(), nullptr, 16), "the number of a replay is the first 8 bytes of its SHA-256 (what HoverNetReplay id prints)");
    std::vector<std::uint8_t> got;
    Check(list.size() == 4 && store.Read(list[3].id, got) && got == a, "a replay is read by its number, byte for byte");
    Check(!store.Read(12345, got), "an unknown number is no replay");

    // Records from the results tables: a board per craft, the best five of each, so a time in one craft never replaces a time in another.
    Protocol3::RecordList threeLaps = store.Records("ClassicH", 3);
    Check(threeLaps.availableLaps == std::vector<std::uint8_t>({2, 3}), "the lap counts that have records are offered");
    Check(threeLaps.boards.size() == 2 && threeLaps.boards[0].craft == 0 && threeLaps.boards[1].craft == 2, "a board per craft that has a result, in craft order (Basic, then Bi-Turbo)");
    Check(threeLaps.boards.size() == 2 && threeLaps.boards[0].entries.size() == 2 && threeLaps.boards[0].entries[0].ms == 93000 && threeLaps.boards[0].entries[0].slot == 1 && threeLaps.boards[0].entries[1].ms == 99000 && threeLaps.boards[0].entries[1].name == "Ben", "the Basic board of three-lap races: best first, with the slot and the name");
    Check(threeLaps.boards.size() == 2 && threeLaps.boards[1].entries.size() == 2 && threeLaps.boards[1].entries[0].ms == 95000 && threeLaps.boards[1].entries[0].name == "Anna", "the Bi-Turbo board is separate: its 95 s does not touch the Basic times");
    Check(threeLaps.boards.size() == 2 && threeLaps.boards[0].entries[0].replay == list[2].id, "each record names the replay it came from");
    Protocol3::RecordList bestLaps = store.Records("ClassicH", 0);
    Check(bestLaps.boards.size() == 3 && bestLaps.boards[0].craft == 0 && bestLaps.boards[1].craft == 1 && bestLaps.boards[2].craft == 2, "the fastest laps have a board per craft too, whatever the length of the race");
    Check(bestLaps.boards.size() == 3 && bestLaps.boards[0].entries.size() == 3 && bestLaps.boards[0].entries[0].ms == 29000 && bestLaps.boards[0].entries[1].ms == 31000 && bestLaps.boards[0].entries[2].ms == 40000, "an unfinished run's best lap counts for its craft");
    Check(bestLaps.boards.size() == 3 && bestLaps.boards[1].entries.size() == 1 && bestLaps.boards[1].entries[0].ms == 30000 && bestLaps.boards[1].entries[0].name == "Dee", "a craft with one result has a board of one");
    Check(store.Records("ClassicH", 2).boards.size() == 1 && store.Records("ClassicH", 2).boards[0].craft == 1, "races of another lap count have their own boards");
    Check(store.Records("The River", 0).boards.empty(), "a track with no results table has no records");
    Check(store.Records("ClassicH", 7).boards.empty(), "a lap count nobody raced has none");

    // Recent winners: only races with two or more players that passed the league's checks (valid = 1), newest first, with the winner.
    {
        const std::string wdir = "hovernet-replay-winners.tmp";
        MAKE_DIR(wdir.c_str());
        WriteBytes(wdir + "/ClassicH-1-1.hnr", MakeReplay("ClassicH", 2, 1, 300, 201));
        WriteFile(wdir + "/ClassicH-1-1.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\tvalid\n1\t1\t1\t1\t90000\t90000\tAnna\t2\t1\n2\t0\t1\t1\t95000\t95000\tBen\t0\t1\n");
        WriteBytes(wdir + "/ClassicH-2-2.hnr", MakeReplay("ClassicH", 2, 1, 310, 202));
        WriteFile(wdir + "/ClassicH-2-2.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\tvalid\n1\t0\t1\t1\t80000\t80000\tSelf\t0\t0\n2\t1\t1\t1\t85000\t85000\tSelf2\t0\t0\n");   // not valid: one person twice
        WriteBytes(wdir + "/ClassicH-3-3.hnr", MakeReplay("ClassicH", 1, 1, 320, 203));
        WriteFile(wdir + "/ClassicH-3-3.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\tvalid\n1\t0\t1\t1\t70000\t70000\tAlone\t0\t1\n");   // one player
        WriteBytes(wdir + "/The River-4-4.hnr", MakeReplay("The River", 3, 3, 330, 204));
        WriteFile(wdir + "/The River-4-4.results.tsv", "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\tvalid\n1\t2\t3\t1\t200000\t60000\tCara\t1\t1\n2\t0\t3\t1\t210000\t62000\tDee\t0\t1\n3\t1\t1\t0\t0\t70000\tEli\t4\t1\n");
        WriteBytes(wdir + "/ClassicH-5-5.hnr", MakeReplay("ClassicH", 2, 1, 340, 205));   // an older race: no results table at all
        SetAge(wdir + "/ClassicH-1-1.hnr", 500); SetAge(wdir + "/ClassicH-2-2.hnr", 400); SetAge(wdir + "/ClassicH-3-3.hnr", 300); SetAge(wdir + "/The River-4-4.hnr", 100); SetAge(wdir + "/ClassicH-5-5.hnr", 50);
        DirReplayStore winners(wdir, 50);
        const Protocol3::WinnerList list2 = winners.Winners(15);
        Check(list2.winners.size() == 2, "only races with two or more players that passed the league's checks are winners (not a race against oneself, not a race of one, not an old race with no table)");
        Check(list2.winners.size() == 2 && list2.winners[0].track == "The River" && list2.winners[0].name == "Cara" && list2.winners[0].slot == 2 && list2.winners[0].craft == 1 && list2.winners[0].ms == 200000 && list2.winners[0].players == 3 && list2.winners[0].laps == 3, "the newest first, with the winner's name, slot, craft, time, laps and the number of players");
        Check(list2.winners.size() == 2 && list2.winners[1].name == "Anna" && list2.winners[1].craft == 2, "and the earlier one");
        LobbyConfig config2;
        config2.tracks = {"ClassicH"};
        LobbyServer lobby2(config2);
        lobby2.SetReplayStore(&winners);
        lobby2.SetLeagueSource([](const std::string& month) { Protocol3::LeagueData d; d.currentMonth = "2026-10"; d.month = month.empty() ? "2026-10" : month; Protocol3::LeagueRow r; r.name = "Anna"; r.points = 9; r.wins = 3; r.races = 4; d.standings.push_back(r); return d; });
        Client viewer2(lobby2, 100);
        Protocol3::Hello hello2; hello2.name = "viewer";
        viewer2.Send(Protocol3::kHello, Protocol3::Encode(hello2), 100);
        viewer2.Pump(); viewer2.Clear();
        viewer2.Send(Protocol3::kListWinners, Protocol3::Encode(Protocol3::ListWinners()), 100);
        viewer2.Pump();
        Protocol3::WinnerList got3;
        Check(viewer2.frames.size() == 1 && viewer2.frames[0].first == Protocol3::kWinnerList && Protocol3::Decode(viewer2.frames[0].second, got3) && got3.winners.size() == 2, "the lobby answers the recent winners");
        viewer2.Clear();
        Protocol3::ListLeague askLeague; askLeague.month = "2026-09";
        viewer2.Send(Protocol3::kListLeague, Protocol3::Encode(askLeague), 100);
        viewer2.Pump();
        Protocol3::LeagueData league2;
        Check(viewer2.frames.size() == 1 && viewer2.frames[0].first == Protocol3::kLeagueData && Protocol3::Decode(viewer2.frames[0].second, league2) && league2.month == "2026-09" && league2.standings.size() == 1 && league2.standings[0].points == 9, "the lobby answers the league for the month asked");
        viewer2.Clear();
        viewer2.Send(Protocol3::kListLeague, std::vector<std::uint8_t>{0x07, 'b', 'a', 'd', 'm', 'o', 'n', 't'}, 101);
        viewer2.Pump();
        Check(viewer2.frames.empty() && !lobby2.IsClosed(viewer2.conn), "a month that is not a month is refused without an answer, and without closing the connection");
        for (const char* name : {"ClassicH-1-1.hnr", "ClassicH-1-1.results.tsv", "ClassicH-2-2.hnr", "ClassicH-2-2.results.tsv", "ClassicH-3-3.hnr", "ClassicH-3-3.results.tsv", "The River-4-4.hnr", "The River-4-4.results.tsv", "ClassicH-5-5.hnr"}) std::remove((wdir + "/" + name).c_str());
#ifdef _WIN32
        _rmdir(wdir.c_str());
#else
        rmdir(wdir.c_str());
#endif
    }

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

    Protocol3::ListRecords records; records.track = "ClassicH"; records.laps = 3;
    client.Send(Protocol3::kListRecords, Protocol3::Encode(records), now);
    client.Pump();
    Protocol3::RecordList board;
    Check(client.frames.size() == 1 && client.frames[0].first == Protocol3::kRecordList && Protocol3::Decode(client.frames[0].second, board) && board.track == "ClassicH" && board.laps == 3 && board.boards.size() == 2 && board.boards[0].entries[0].ms == 93000 && board.availableLaps.size() == 2, "the lobby answers the track records, per craft");
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

    // Many lap counts with long names: the records message is trimmed to fit one frame, and still decodes, best entries first.
    {
        const std::string crowded = "hovernet-replay-share-crowded.tmp";
        MAKE_DIR(crowded.c_str());
        std::vector<std::string> made;
        for (int laps = 1; laps <= 9; ++laps) {
            const std::string base = crowded + "/ClassicH-" + std::to_string(laps) + "-" + std::to_string(500 + laps);
            WriteBytes(base + ".hnr", MakeReplay("ClassicH", 8, laps, 300, 100 + laps));
            std::string table = "rank\tplayer\tlaps\tfinished\tfinish_ms\tbest_lap_ms\tname\tcraft\n";
            for (int p = 0; p < 8; ++p) table += std::to_string(p + 1) + "\t" + std::to_string(p) + "\t" + std::to_string(laps) + "\t1\t" + std::to_string(60000 + p * 1000 + laps) + "\t" + std::to_string(30000 + p * 500) + "\t" + std::string("AVeryLongDisplayNameOfThirtyTwoChars").substr(0, 32) + "\t" + std::to_string(p % 5) + "\n";
            WriteFile(base + ".results.tsv", table);
            made.push_back(base);
        }
        DirReplayStore big(crowded, 50);
        LobbyServer crowdedLobby(config);
        crowdedLobby.SetReplayStore(&big);
        Client viewer(crowdedLobby, 5000);
        viewer.Send(Protocol3::kHello, Protocol3::Encode(hello), 5000);
        viewer.Pump(); viewer.Clear();
        Protocol3::ListRecords ask2; ask2.track = "ClassicH"; ask2.laps = 0;
        viewer.Send(Protocol3::kListRecords, Protocol3::Encode(ask2), 5000);
        viewer.Pump();
        Protocol3::RecordList got2;
        Check(viewer.frames.size() == 1 && viewer.frames[0].second.size() <= 1150 && Protocol3::Decode(viewer.frames[0].second, got2), "a records answer with many boards and long names fits one frame and decodes");
        Check(got2.boards.size() == 5 && got2.boards[0].craft == 0 && !got2.boards[0].entries.empty() && got2.boards[0].entries[0].ms == 30000 && got2.boards[0].entries[0].name.size() == 32, "and keeps a board for every craft and its best entries, with their names");
        viewer.Clear();
        viewer.Send(Protocol3::kListReplays, Protocol3::Encode(Protocol3::ListReplays()), 5000);
        viewer.Pump();
        Protocol3::ReplayList crowdedList;
        Check(viewer.frames.size() == 1 && viewer.frames[0].second.size() <= 1150 && Protocol3::Decode(viewer.frames[0].second, crowdedList) && !crowdedList.replays.empty() && crowdedList.replays[0].winnerName.size() == 32 && crowdedList.replays[0].winnerCraft == 0, "a replay list with long winner names fits one frame too, with the winner's craft");
        for (const std::string& b : made) { std::remove((b + ".hnr").c_str()); std::remove((b + ".results.tsv").c_str()); }
#ifdef _WIN32
        _rmdir(crowded.c_str());
#else
        rmdir(crowded.c_str());
#endif
    }

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
