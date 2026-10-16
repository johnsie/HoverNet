// RaceHost.h -- one authoritative race hosted behind protocol 3.0, with no sockets and no clock.
//
// A transport (TCP, a WebSocket gateway, or the in-memory pipes of the tests) calls Connect(), hands the bytes it receives
// to Receive() and sends whatever TakeOutput() returns. Time is a number the caller passes in. Everything a connection can
// make the server do is decided here: version negotiation, one player slot per connection, input rate limits, the rule
// that ten invalid messages in ten seconds close the connection, and the race loop that broadcasts every tick.
#ifndef RACE_HOST_H
#define RACE_HOST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AuthoritativeRace.h"
#include "Protocol3.h"

namespace Authority
{
struct HostConfig
{
    RaceConfig race;
    int startDelayMs = 3000;          // countdown shown by the clients before tick 0
    int maxInputFramesPerSecond = 125; // a client sends about one Input frame per tick (62.5/s); twice that is the ceiling
    int maxInvalidPerWindow = 10;
    int invalidWindowMs = 10000;
    int helloTimeoutMs = 5000;        // a connection that has not completed Hello by then is closed
    int snapshotsPerMinute = 6;       // per connection
};

class RaceHost
{
public:
    explicit RaceHost(const HostConfig& config);
    ~RaceHost();

    bool Init(std::string& error);                       // loads the track
    int Connect(std::int64_t nowMs);                     // a new connection: returns its id
    void Receive(int connection, const std::uint8_t* data, std::size_t size, std::int64_t nowMs);
    void Disconnect(int connection);
    std::vector<std::uint8_t> TakeOutput(int connection);   // bytes to send to this connection
    bool IsClosed(int connection) const;
    // Runs the race up to the given time (and expires slow connections); broadcasts a Tick for each tick simulated.
    void Advance(std::int64_t nowMs);

    bool Started() const { return mStarted; }
    bool RaceFinished() const { return mRace.Finished(); }
    const AuthoritativeRace& Race() const { return mRace; }
    int ConnectedPlayers() const;
    int InvalidTotal() const { return mInvalidTotal; }

private:
    struct Connection;
    Connection* Find(int id);
    const Connection* Find(int id) const;
    void Send(Connection& c, const std::vector<std::uint8_t>& frame);
    void Close(Connection& c, std::uint8_t reason, const std::string& message);
    void Invalid(Connection& c, std::int64_t nowMs, const char* why);
    void Handle(Connection& c, std::uint8_t type, const std::vector<std::uint8_t>& payload, std::int64_t nowMs);
    void BeginRaceIfFull(std::int64_t nowMs);
    void SendSnapshot(Connection& c, std::uint32_t id);
    void Broadcast(const std::vector<std::uint8_t>& frame);
    void FinishRace();

    HostConfig mConfig;
    AuthoritativeRace mRace;
    std::vector<std::unique_ptr<Connection>> mConnections;
    int mNextId;
    bool mRaceLoaded;
    bool mStarted;
    bool mResultSent;
    std::int64_t mStartMs;       // server time of tick 0
    std::uint32_t mSimulated;
    int mInvalidTotal;
    std::string mTrackSha256;
};
}

#endif
