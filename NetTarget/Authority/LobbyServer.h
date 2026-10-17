// LobbyServer.h -- many authoritative races behind one door (protocol 3.0 lobby), still with no sockets and no clock.
//
// A connection says Hello and gets the list of open races; it then hosts a race (track, players, laps, weapons, from the tracks
// the operator allows) or joins one by id, and from then on its frames go to that race's RaceHost unchanged. A returning player
// sends Resume with the token instead of Hello and is routed to the race that issued it. Races are independent: each has its own
// simulation, its own rate limits and its own replay, and a finished race is removed after a short while.
#ifndef LOBBY_SERVER_H
#define LOBBY_SERVER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Protocol3.h"
#include "RaceHost.h"

namespace Authority
{
struct LobbyConfig
{
    HostConfig raceTemplate;                    // rate limits, countdown, ... for every race (the race itself is replaced)
    std::vector<std::string> tracks;            // tracks players may host, by name
    std::string trackDirectory = "NetTarget/Tracks";
    int maxRaces = 16;
    int maxConnections = 256;
    int finishedRaceLingerMs = 15000;           // a finished race stays this long (for late Result and reconnect), then is removed
    int helloTimeoutMs = 5000;
    int maxInvalidPerWindow = 10;
    int invalidWindowMs = 10000;
    int maxLobbyCommandsPerWindow = 40;         // list/host/join requests per window per connection
};

class LobbyServer
{
public:
    explicit LobbyServer(const LobbyConfig& config);
    ~LobbyServer();

    int Connect(std::int64_t nowMs);
    void Receive(int connection, const std::uint8_t* data, std::size_t size, std::int64_t nowMs);
    void Disconnect(int connection);
    std::vector<std::uint8_t> TakeOutput(int connection);
    bool IsClosed(int connection) const;
    void Advance(std::int64_t nowMs);

    // Called once when a race ends (to save its replay, log it, ...).
    void SetRaceFinishedCallback(std::function<void(std::uint32_t, const RaceHost&)> callback) { mOnFinished = callback; }

    std::uint64_t RacesHosted() const { return mRacesHosted; }
    int RaceCount() const { return static_cast<int>(mRaces.size()); }
    const RaceHost* Race(std::uint32_t id) const;
    std::vector<std::uint32_t> RaceIds() const;
    int OpenConnections() const;
    // Draining (a rolling restart): races under way run to their end, but nobody can host or join a race any more. The process
    // exits when RunningRaces() reaches 0.
    void SetDraining(bool draining) { mDraining = draining; }
    bool Draining() const { return mDraining; }
    int RunningRaces() const;
    // Counters of every race the server has hosted (running races plus those already removed).
    RaceHost::Metrics TotalMetrics() const;

private:
    struct Conn;
    struct Entry;
    Conn* Find(int id);
    const Conn* Find(int id) const;
    void Close(Conn& c, std::uint8_t reason, const std::string& message);
    void Invalid(Conn& c, std::int64_t nowMs);
    void Handle(Conn& c, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t nowMs);
    void SendList(Conn& c);
    void BroadcastUsersIfChanged();
    bool CodeAccepted(Conn& c, Entry& e, const std::string& code);
    void Attach(Conn& c, Entry& e, const std::vector<std::uint8_t>& firstFrame, std::int64_t nowMs);

    LobbyConfig mConfig;
    std::vector<std::unique_ptr<Conn>> mConns;
    std::map<std::uint32_t, std::unique_ptr<Entry>> mRaces;
    int mNextConn;
    std::uint32_t mNextRace;
    std::uint64_t mSeed;
    RaceHost::Metrics mRemoved;      // counters of races already removed
    std::uint64_t mRacesHosted = 0;
    bool mDraining = false;
    std::string mLastUsers;          // the lobby's user list as last sent (a change is sent to those that asked for it)
    std::function<void(std::uint32_t, const RaceHost&)> mOnFinished;
};
}

#endif
