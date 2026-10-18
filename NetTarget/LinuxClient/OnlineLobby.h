// OnlineLobby.h -- the game client's side of an authoritative server's lobby (protocol 3.0): connect, list the open races, host
// one (a time trial when it has one player, private when it has a code), join one or watch one, and hand the live connection over
// once the race is set up. No window or drawing code, so it is tested against a real server (scripts/test-online-lobby.sh) and the
// menu screen only draws its state. Native clients only; the browser build has no transport for it yet.
#ifndef ONLINE_LOBBY_H
#define ONLINE_LOBBY_H

#include <cstdint>
#include <deque>
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
    // Watch a race that may not have started: watches at once if it has, otherwise waits in the list and starts watching when it starts
    // (the server refuses a spectator before the start). Gives up with a Notice() if the race disappears first.
    void WatchWhenStarted(std::uint32_t id, const std::string& code);
    void CancelQueuedWatch() { mQueuedWatchId = 0; }
    std::uint32_t QueuedWatchId() const { return mQueuedWatchId; }
    void Cancel();                                                             // leave a race that has not started: back to browsing (reconnects)

    State GetState() const { return mState; }
    const std::string& Message() const { return mMessage; }                   // the last error or status, for display
    const std::vector<Protocol3::RaceInfo>& Races() const { return mRaces; }
    // Who is in the lobby (the first few names and how many in all) and what has been said: lobby chat before a race, the race's chat while
    // waiting in one. Empty against a server that does not offer lobby chat.
    const std::vector<std::string>& Users() const { return mUsers; }
    int UsersTotal() const { return mUsersTotal; }
    const std::deque<std::string>& ChatLines() const { return mChat; }
    void SendChat(const std::string& text);
    // The host of a race that has not started: who has joined (slot, name; slot 0 is the host) and how many seats, and the button that starts it.
    bool IsHost() const { return mIsHost; }
    const Protocol3::Roster& GetRoster() const { return mRoster; }
    void StartRace();
    const std::string& Notice() const { return mNotice; }                    // why the last host/join/watch was refused (cleared by the next request)
    bool Spectating() const { return mSpectating; }
    const Protocol3::HelloAck& Ack() const { return mAck; }

    // Once eReady: the open connection, and the frames that arrived between HelloAck and now (ticks of the first moments of the race).
    AuthConnection& Connection() { return mConn; }
    std::vector<AuthFrame> TakeBacklog() { std::vector<AuthFrame> b; b.swap(mBacklog); return b; }

    // Where this lobby connected, so the race can reconnect to the same server with its resume token.
    void AddressOf(std::string& host, int& port, std::vector<std::uint8_t>& key) const { host = mHost; port = mPort; key = mKey; }

    // Shared replays and track records (any time while browsing). Ask, call Pump() every frame, read the answer when Loaded() says it came.
    void RefreshReplays(const std::string& track = std::string());             // the server's newest replays, optionally of one track
    const std::vector<Protocol3::ReplayInfo>& Replays() const { return mReplays; }
    bool ReplaysLoaded() const { return mReplaysLoaded; }
    void RefreshRecords(const std::string& track);                              // the track records of one track
    const Protocol3::RecordList& Records() const { return mRecords; }
    bool RecordsLoaded() const { return mRecordsLoaded; }
    // Downloads one replay (in pieces). Downloading() until it is complete; then TakeReplay() gives the checked file once. A refusal, a damaged
    // or incomplete download is reported in ReplayNotice() and nothing is handed over.
    void FetchReplay(std::uint64_t id);
    bool Downloading() const { return mFetchId != 0; }
    int DownloadPercent() const { return mFetchCount == 0 ? 0 : static_cast<int>(100 * mFetchNext / mFetchCount); }
    bool TakeReplay(std::vector<std::uint8_t>& bytes) { if (!mReplayReady) return false; bytes.swap(mReplayBytes); mReplayReady = false; return true; }
    const std::string& ReplayNotice() const { return mReplayNotice; }
    void ClearReplayNotice() { mReplayNotice.clear(); }

    // Plain-language text for a server refusal.
    static std::string DescribeReject(const Protocol3::Reject& r);

private:
    void Fail(const std::string& message);
    void SendPendingFetch();
    bool Send(std::uint8_t type, const std::vector<std::uint8_t>& payload);

    AuthConnection mConn;
    State mState = State::eIdle;
    std::string mMessage, mNotice;
    std::vector<std::string> mUsers;
    int mUsersTotal = 0;
    std::deque<std::string> mChat;
    Protocol3::Roster mRoster;
    bool mIsHost = false;
    std::string mHost, mName;
    int mPort = 0;
    std::vector<std::uint8_t> mKey;
    std::vector<Protocol3::RaceInfo> mRaces;
    Protocol3::HelloAck mAck;
    std::vector<AuthFrame> mBacklog;
    std::vector<Protocol3::ReplayInfo> mReplays;
    Protocol3::RecordList mRecords;
    bool mReplaysLoaded = false, mRecordsLoaded = false, mReplayReady = false;
    std::uint64_t mFetchId = 0;
    long long mLastFetchMs = -100000;   // the server allows one download every half second; a quicker request waits (see FetchReplay)
    bool mFetchSent = false;
    std::uint16_t mFetchNext = 0, mFetchCount = 0;
    std::vector<std::uint8_t> mReplayBytes;
    std::string mReplayNotice;
    std::uint32_t mQueuedWatchId = 0;
    std::string mQueuedWatchCode;
    bool mGotAck = false, mGotStart = false, mSpectating = false;
};

#endif
