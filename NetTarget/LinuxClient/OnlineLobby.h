// OnlineLobby.h -- the game client's side of an authoritative server's lobby (protocol 3.0): connect, list the open races, host
// one (a time trial when it has one player, private when it has a code), join one or watch one, and hand the live connection over
// once the race is set up. No window or drawing code, so it is tested against a real server (scripts/test-online-lobby.sh) and the
// menu screen only draws its state. Native clients only; the browser build has no transport for it yet.
#ifndef ONLINE_LOBBY_H
#define ONLINE_LOBBY_H

#include <cstdint>
#include <string>
#include <vector>

#include "AuthConnection.h"
#include "../Authority/Protocol3.h"

class OnlineLobby
{
public:
    enum class State
    {
        eIdle,        // not connected
        eBrowsing,    // connected; the race list is current
        eWaiting,     // asked to host/join/watch; waiting for the race to be set up and to start
        eReady,       // HelloAck and Start received: the race is about to run (see Take)
        eFailed,      // the connection failed or the server refused; Message() says why
    };

    // serverKey: 32 bytes to pin the server's identity (an encrypted session), or empty for a plain connection.
    bool Connect(const std::string& host, int port, const std::string& name, const std::vector<std::uint8_t>& serverKey);
    void Close();
    // Reads what the server sent. Call every frame.
    void Pump();

    void Refresh();                                                            // ask for the race list again
    void Host(const std::string& track, int players, int laps, bool weapons, const std::string& code);
    void Join(std::uint32_t id, const std::string& code);
    void Spectate(std::uint32_t id, const std::string& code);
    void Cancel();                                                             // leave a race that has not started: back to browsing (reconnects)

    State GetState() const { return mState; }
    const std::string& Message() const { return mMessage; }                   // the last error or status, for display
    const std::vector<Protocol3::RaceInfo>& Races() const { return mRaces; }
    const std::string& Notice() const { return mNotice; }                    // why the last host/join/watch was refused (cleared by the next request)
    bool Spectating() const { return mSpectating; }
    const Protocol3::HelloAck& Ack() const { return mAck; }

    // Once eReady: the open connection, and the frames that arrived between HelloAck and now (ticks of the first moments of the race).
    AuthConnection& Connection() { return mConn; }
    std::vector<AuthFrame> TakeBacklog() { std::vector<AuthFrame> b; b.swap(mBacklog); return b; }

    // Where this lobby connected, so the race can reconnect to the same server with its resume token.
    void AddressOf(std::string& host, int& port, std::vector<std::uint8_t>& key) const { host = mHost; port = mPort; key = mKey; }

    // Plain-language text for a server refusal.
    static std::string DescribeReject(const Protocol3::Reject& r);

private:
    void Fail(const std::string& message);
    bool Send(std::uint8_t type, const std::vector<std::uint8_t>& payload);

    AuthConnection mConn;
    State mState = State::eIdle;
    std::string mMessage, mNotice;
    std::string mHost, mName;
    int mPort = 0;
    std::vector<std::uint8_t> mKey;
    std::vector<Protocol3::RaceInfo> mRaces;
    Protocol3::HelloAck mAck;
    std::vector<AuthFrame> mBacklog;
    bool mGotAck = false, mGotStart = false, mSpectating = false;
};

#endif
