// The game client's lobby logic (OnlineLobby) against a real HoverNetAuthServer --lobby: browse, host a private race, be refused with a wrong
// code and told why, join with the right one, run a one-player time trial, and watch a running race.
//   HoverNetOnlineLobbyTest <port>        (scripts/test-online-lobby.sh starts the server)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "OnlineLobby.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++gFailures; } }

bool WaitFor(OnlineLobby& lobby, OnlineLobby::State want, int ms)
{
    for (int t = 0; t < ms; t += 20) {
        lobby.Pump();
        if (lobby.GetState() == want) return true;
        if (lobby.GetState() == OnlineLobby::State::eFailed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <port>\n", argv[0]); return 2; }
    const int port = std::atoi(argv[1]);
    const std::vector<std::uint8_t> noKey;

    OnlineLobby host, friendLobby, stranger, solo, watcher;
    Check(host.Connect("127.0.0.1", port, "Hosty", noKey), "connect");
    Check(WaitFor(host, OnlineLobby::State::eBrowsing, 3000), "the lobby lists races after connecting");
    Check(host.Races().empty(), "no races yet");

    host.Host("ClassicH", 2, 1, false, "kestrel7");
    Check(host.GetState() == OnlineLobby::State::eWaiting, "hosting waits for a second player");
    for (int i = 0; i < 300 && host.Ack().raceId == 0; ++i) { host.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Check(host.Ack().raceId == 1, "the host of a private race is told its number, to give to friends");

    // A stranger without the code does not see it, and a wrong code is refused with a plain message, leaving them at the list.
    Check(stranger.Connect("127.0.0.1", port, "Stranger", noKey) && WaitFor(stranger, OnlineLobby::State::eBrowsing, 3000), "a stranger connects");
    Check(stranger.Races().empty(), "a private race is not listed");
    stranger.Join(1, "wrongcode");
    for (int i = 0; i < 200 && stranger.Notice().empty(); ++i) { stranger.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Check(stranger.Notice().find("No such race") != std::string::npos, "a wrong code says so");
    Check(WaitFor(stranger, OnlineLobby::State::eBrowsing, 3000), "after a refusal the player is back at the list");

    // The friend has the code.
    Check(friendLobby.Connect("127.0.0.1", port, "Friend", noKey) && WaitFor(friendLobby, OnlineLobby::State::eBrowsing, 3000), "the friend connects");
    friendLobby.Join(1, "kestrel7");
    for (int i = 0; i < 300 && host.GetRoster().members.size() < 2; ++i) { host.Pump(); friendLobby.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Check(host.GetRoster().members.size() == 2 && host.GetRoster().members[0].second == "Hosty" && host.GetRoster().members[1].second == "Friend", "the host sees who has joined");
    Check(friendLobby.GetRoster().members.size() == 2 && !friendLobby.IsHost() && host.IsHost(), "the friend sees the same roster and knows it is not the host");
    // The race has all its seats taken and still waits: it starts when the host presses Start Race, not when the seats fill.
    for (int i = 0; i < 40; ++i) { host.Pump(); friendLobby.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Check(host.GetState() == OnlineLobby::State::eWaiting && friendLobby.GetState() == OnlineLobby::State::eWaiting, "a full race waits for the host to start it");
    friendLobby.StartRace();   // a player who is not the host cannot start it
    for (int i = 0; i < 20; ++i) { host.Pump(); friendLobby.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    Check(host.GetState() == OnlineLobby::State::eWaiting, "only the host's Start Race starts the race");
    host.StartRace();
    Check(WaitFor(friendLobby, OnlineLobby::State::eReady, 8000), "the friend's race starts when the host presses Start Race");
    Check(WaitFor(host, OnlineLobby::State::eReady, 8000), "the host's race starts when the host presses Start Race");
    Check(host.Ack().track == "ClassicH" && host.Ack().players == 2 && host.Ack().laps == 1, "the host is told the race settings");
    Check(host.Ack().player != friendLobby.Ack().player, "each player has their own slot");

    // A time trial: one player starts at once.
    Check(solo.Connect("127.0.0.1", port, "Solo", noKey) && WaitFor(solo, OnlineLobby::State::eBrowsing, 3000), "the solo player connects");
    solo.Host("ClassicH", 1, 1, false, "");
    Check(WaitFor(solo, OnlineLobby::State::eReady, 8000), "a one-player race starts at once");
    Check(solo.Ack().players == 1, "a time trial has one player");

    // Watch the running public race (the solo one is public).
    Check(watcher.Connect("127.0.0.1", port, "Watcher", noKey) && WaitFor(watcher, OnlineLobby::State::eBrowsing, 3000), "the watcher connects");
    std::uint32_t id = 0;
    for (int i = 0; i < 100 && id == 0; ++i) {
        watcher.Refresh();
        for (int k = 0; k < 10; ++k) { watcher.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        for (const auto& r : watcher.Races()) if (r.started) id = r.id;
    }
    Check(id != 0, "a running public race is listed");
    if (id != 0) { watcher.Spectate(id, ""); Check(WaitFor(watcher, OnlineLobby::State::eReady, 8000) && watcher.Spectating(), "a spectator is let in"); }

    // Choosing to watch a race that has not started: the watcher waits in the list and is let in when the host starts it.
    {
        OnlineLobby host2, queued;
        Check(host2.Connect("127.0.0.1", port, "Host2", noKey) && WaitFor(host2, OnlineLobby::State::eBrowsing, 3000), "a second host connects");
        Check(queued.Connect("127.0.0.1", port, "Queued", noKey) && WaitFor(queued, OnlineLobby::State::eBrowsing, 3000), "the queued watcher connects");
        host2.Host("ClassicH", 8, 1, false, "");
        std::uint32_t waitingId = 0;
        for (int i = 0; i < 100 && waitingId == 0; ++i) {
            queued.Refresh();
            for (int k = 0; k < 10; ++k) { host2.Pump(); queued.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            for (const auto& r : queued.Races()) if (!r.started && r.track == "ClassicH") waitingId = r.id;
        }
        Check(waitingId != 0, "a race that has not started is listed");
        if (waitingId != 0) {
            queued.WatchWhenStarted(waitingId, "");
            for (int i = 0; i < 100; ++i) { host2.Pump(); queued.Pump(); if (i % 20 == 0) queued.Refresh(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            Check(queued.GetState() == OnlineLobby::State::eBrowsing && queued.QueuedWatchId() == waitingId && !queued.Spectating(), "the watcher keeps waiting while the race has not started");
            host2.StartRace();
            bool in = false;
            for (int i = 0; i < 800 && !in; ++i) {
                host2.Pump(); queued.Pump();
                if (i % 20 == 0 && queued.GetState() == OnlineLobby::State::eBrowsing) queued.Refresh();
                in = queued.GetState() == OnlineLobby::State::eReady && queued.Spectating();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check(in && queued.QueuedWatchId() == 0, "the waiting watcher is let in when the race starts");
            Check(WaitFor(host2, OnlineLobby::State::eReady, 8000) && !host2.Spectating(), "the host of that race is in it too");
        }
    }

    // The lobby's chat and who is in it.
    {
        OnlineLobby ann, bob;
        Check(ann.Connect("127.0.0.1", port, "Ann", noKey) && bob.Connect("127.0.0.1", port, "Bob", noKey), "two players connect to chat");
        bool seen = false;
        for (int i = 0; i < 300 && !seen; ++i) {
            ann.Pump(); bob.Pump();
            auto has = [](const OnlineLobby& l, const char* n) { for (const auto& u : l.Users()) if (u == n) return true; return false; };
            seen = has(ann, "Ann") && has(ann, "Bob") && has(bob, "Ann") && has(bob, "Bob");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(seen, "each player sees the other in the lobby's user list");
        ann.SendChat("hello lobby");
        bool heard = false;
        for (int i = 0; i < 300 && !heard; ++i) {
            ann.Pump(); bob.Pump();
            heard = !bob.ChatLines().empty() && bob.ChatLines().back() == "Ann: hello lobby" && !ann.ChatLines().empty();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(heard, "a lobby message reaches the others, and the sender sees it too");
    }

    // Shared replays and track records, when the server was started with a replay folder holding two ClassicH replays (HOVERNET_TEST_SHARED_REPLAYS=1,
    // set by scripts/test-online-lobby.sh): list them, read the records, download one and have it checked, and be told about one that is gone.
    if (std::getenv("HOVERNET_TEST_SHARED_REPLAYS") != nullptr) {
        OnlineLobby viewer;
        Check(viewer.Connect("127.0.0.1", port, "Viewer", noKey) && WaitFor(viewer, OnlineLobby::State::eBrowsing, 3000), "a viewer connects");
        viewer.RefreshReplays();
        for (int i = 0; i < 300 && !viewer.ReplaysLoaded(); ++i) { viewer.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        Check(viewer.ReplaysLoaded() && viewer.Replays().size() == 2 && viewer.Replays()[0].track == "ClassicH", "the server lists its replays to someone who was not in the races");
        viewer.RefreshRecords("ClassicH", 0);
        for (int i = 0; i < 300 && !viewer.RecordsLoaded(); ++i) { viewer.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        Check(viewer.RecordsLoaded() && !viewer.Records().boards.empty() && !viewer.Records().boards[0].entries.empty() && viewer.Records().boards[0].craft == 0 && viewer.Records().boards[0].entries[0].ms == 61000 && viewer.Records().boards[0].entries[0].name == "Anna", "the track records come per craft, with the fastest lap and the name");
        if (viewer.Replays().size() == 2) {
            viewer.FetchReplay(viewer.Replays()[0].id);
            std::vector<std::uint8_t> file;
            bool got = false;
            for (int i = 0; i < 500 && !got; ++i) { viewer.Pump(); got = viewer.TakeReplay(file); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            Check(got && !file.empty() && viewer.ReplayNotice().empty() && !viewer.Downloading(), "a replay downloads and passes its own check (the number is its SHA-256)");
            viewer.FetchReplay(0x1234);
            for (int i = 0; i < 300 && viewer.ReplayNotice().empty(); ++i) { viewer.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            Check(viewer.ReplayNotice().find("no longer") != std::string::npos && viewer.GetState() == OnlineLobby::State::eBrowsing, "a replay that is gone is reported and the lobby stays usable");
        }
    }

    std::printf("%s\n", gFailures == 0 ? "online lobby: browse, private race, refusal message, join, time trial, spectate, lobby chat and users" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
