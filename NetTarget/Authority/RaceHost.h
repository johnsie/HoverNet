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
#include "Moderation.h"
#include "Protocol3.h"

namespace Authority
{
struct HostConfig
{
    RaceConfig race;
    std::uint32_t raceId = 0;         // the lobby's number for this race (0 when the host is not in a lobby); told to clients that ask
    int startDelayMs = 3000;          // countdown shown by the clients before tick 0
    int maxInputFramesPerSecond = 125; // a client sends about one Input frame per tick (62.5/s); twice that is the ceiling
    int maxInvalidPerWindow = 10;
    int invalidWindowMs = 10000;
    int helloTimeoutMs = 5000;        // a connection that has not completed Hello by then is closed
    int snapshotsPerMinute = 6;       // per connection
    int spectatorDelayMs = 10000;     // spectators see the race this far behind the drivers (protects competitive integrity)
    int maxSpectators = 64;
    int keyframeTicks = 250;          // a state snapshot is kept this often, for spectators joining and for seeking
    Moderation* moderation = nullptr;   // shared by every race on the server; mutes, reports and the audit log (optional)
    int chatBurst = 3;                // chat messages allowed in a burst; then one a second
    int reportsPerMinute = 3;
    std::uint64_t tokenSeed = 1;      // seeds the reconnect tokens; a production server seeds this from the operating system
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

    // Counters for observability (the server prints them; a monitoring endpoint can export them).
    struct Metrics
    {
        std::uint64_t connectionsAccepted = 0, connectionsClosed = 0;
        std::uint64_t bytesIn = 0, bytesOut = 0;
        std::uint64_t framesIn = 0, framesOut = 0;
        std::uint64_t invalidMessages = 0, rejectedConnections = 0;
        std::uint64_t ticksSimulated = 0;
        std::uint64_t inputsAccepted = 0, inputsLate = 0, inputsTooFar = 0;
        std::uint64_t snapshotsServed = 0;
        std::uint64_t tickBytesOut = 0;        // bytes of Tick frames, summed over recipients
    };
    const Metrics& GetMetrics() const { return mMetrics; }

    bool Started() const { return mStarted; }
    int Spectators() const;
    int PlayerSlots() const { return mConfig.race.players; }
    const HostConfig& Config() const { return mConfig; }
    bool OwnsResumeToken(std::uint64_t token) const;
    bool Full() const { return ConnectedPlayers() >= mConfig.race.players; }
    bool RaceFinished() const { return mRace.Finished(); }
    const AuthoritativeRace& Race() const { return mRace; }
    // The display name each player slot had when the race started (empty for a slot nobody held). Names are what players typed; nothing proves they are theirs.
    // The device id the player of a slot sent in its Hello (false when it sent none: an older game). Used only by the league; never sent on to anybody.
    bool SlotDeviceId(int slot, std::uint64_t& high, std::uint64_t& low) const
    {
        if (slot < 0 || slot >= static_cast<int>(mSlotDevices.size()) || !mSlotDevices[static_cast<std::size_t>(slot)].has) return false;
        high = mSlotDevices[static_cast<std::size_t>(slot)].high; low = mSlotDevices[static_cast<std::size_t>(slot)].low;
        return true;
    }
    const std::string& SlotName(int slot) const { static const std::string none; return slot >= 0 && slot < static_cast<int>(mSlotNames.size()) ? mSlotNames[static_cast<std::size_t>(slot)] : none; }
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
    void StartNow(std::int64_t nowMs);                    // sends Start: the race runs from here
    void HostStart(Connection& c, std::int64_t nowMs);    // the host presses Start Race: start with the players who are here
    void SendRosterIfChanged();
    void SendSnapshot(Connection& c, std::uint32_t id);
    void Broadcast(const std::vector<std::uint8_t>& frame);
    void FinishRace();
    void SendSpectatorFrames(Connection& c);
    void StartSpectator(Connection& c, std::int64_t nowMs);
    void KeepKeyframe();

    HostConfig mConfig;
    AuthoritativeRace mRace;
    bool mHostStarts = false;      // the first player asked (kCapHostStart) to start the race himself or herself
    int mHostConn = 0;             // that player's connection
    std::string mLastRoster;       // the roster as last sent
    std::vector<std::unique_ptr<Connection>> mConnections;
    int mNextId;
    bool mRaceLoaded;
    bool mStarted;
    bool mResultSent;
    std::int64_t mLastNow = 0;
    std::int64_t mStartMs;       // server time of tick 0
    std::uint32_t mSimulated;
    int mInvalidTotal;
    Metrics mMetrics;
    std::string mTrackSha256;
    std::vector<std::uint8_t> mResultFrame;
    std::vector<std::vector<std::uint8_t>> mTickFrames;                 // every Tick frame sent, by tick (for spectators)
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> mKeyframes;   // (tick, snapshot) every keyframeTicks
    std::vector<std::uint64_t> mTokens;   // reconnect token per player slot (0 = none yet)
    std::vector<std::string> mSlotNames;  // display name per player slot, taken when the race starts
    struct SlotDevice { bool has = false; std::uint64_t high = 0, low = 0; };
    std::vector<SlotDevice> mSlotDevices; // device id per player slot, taken when the race starts
    std::uint64_t mTokenState;
    std::uint64_t NextToken();
    void SendHelloAck(Connection& c);
};
}

#endif
