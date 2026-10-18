#include "OnlineLobby.h"

#include <chrono>
#include <cstdlib>

#include "Sha256.h"
#include "../Authority/ReplayFormat.h"

using namespace Protocol3;

std::string OnlineLobby::DescribeReject(const Reject& r)
{
    switch (r.reason) {
    case kUnsupportedVersion: return "This server speaks a different version of the HoverNet protocol.";
    case kServerFull: return "The server is full.";
    case kRateLimited: return "Too many requests: wait a moment and try again.";
    case kNoSuchRace: return "No such race, or the code is wrong.";
    case kBadSettings: return "The server does not allow those race settings (track, players or laps).";
    case kTooManyRaces: return "The server is running as many races as it allows. Try again shortly.";
    case kBanned: return "You are not allowed on this server.";
    case kNoSuchReplay: return "That replay is no longer on the server.";
    case kServerDraining: return "The server is restarting and not starting new races. Try again in a few minutes.";
    default: return r.message.empty() ? "The server refused the request." : r.message;
    }
}

void OnlineLobby::Fail(const std::string& message)
{
    mState = State::eFailed;
    mMessage = message;
    mConn.Close();
}

bool OnlineLobby::Send(std::uint8_t type, const std::vector<std::uint8_t>& payload)
{
    if (!mConn.Send(type, payload)) { Fail("The connection to the server was lost."); return false; }
    return true;
}

bool OnlineLobby::Connect(const std::string& host, int port, const std::string& name, const std::vector<std::uint8_t>& serverKey)
{
    Close();
    mHost = host; mPort = port; mName = name; mKey = serverKey;
    if (!serverKey.empty() && serverKey.size() != 32) { Fail("The server key must be 64 hex characters."); return false; }
    std::string error;
    if (!mConn.Connect(host, port, error, serverKey.empty() ? nullptr : serverKey.data())) { Fail(error.empty() ? "Could not connect to the server." : error); return false; }
    Hello hello;
    hello.name = name.substr(0, 32);
    hello.capabilities = kCapRaceId | kCapLobbyChat | kCapHostStart;   // this client shows the race number to a host, and the lobby's chat and users
    mRaces.clear(); mBacklog.clear(); mUsers.clear(); mUsersTotal = 0; mChat.clear(); mRoster = Roster(); mIsHost = false;
    mGotAck = mGotStart = mSpectating = false;
    mQueuedWatchId = 0;
    mState = State::eIdle;
    mMessage = "Connecting...";
    return Send(kHello, Encode(hello));
}

void OnlineLobby::Close()
{
    mConn.Close();
    mState = State::eIdle;
    mMessage.clear();
}

void OnlineLobby::Pump()
{
    if (mState == State::eIdle && !mConn.IsOpen()) return;
    if (mState == State::eFailed || mState == State::eReady) return;
    std::vector<AuthFrame> frames;
    const bool open = mConn.Pump(frames);
    for (const AuthFrame& f : frames) {
        if (f.type == kRaceList) {
            RaceList list;
            if (Decode(f.payload, list)) { mRaces = list.races; if (mState == State::eIdle || mState == State::eBrowsing) { mState = State::eBrowsing; mMessage.clear(); } }
        }
        else if (f.type == kReplayList) {
            ReplayList l;
            if (Decode(f.payload, l)) { mReplays = l.replays; mReplaysLoaded = true; mListsUnreadable = false; }
            else mListsUnreadable = true;
        }
        else if (f.type == kRecordList) {
            RecordList l;
            if (Decode(f.payload, l)) { mRecords = l; mRecordsLoaded = true; mListsUnreadable = false; }
            else mListsUnreadable = true;
        }
        else if (f.type == kReplayChunk) {
            ReplayChunk ch;
            if (mFetchId == 0 || !Decode(f.payload, ch) || ch.id != mFetchId || ch.index != mFetchNext || (mFetchNext != 0 && ch.count != mFetchCount)) {
                if (mFetchId != 0) { mFetchId = 0; mReplayBytes.clear(); mReplayNotice = "The replay did not arrive in one piece. Try again."; }
                continue;
            }
            mFetchCount = ch.count;
            mReplayBytes.insert(mReplayBytes.end(), ch.data.begin(), ch.data.end());
            ++mFetchNext;
            if (mFetchNext == mFetchCount) {
                const std::uint64_t id = mFetchId;
                mFetchId = 0;
                ReplayFormat::Replay check;
                std::string error;
                const std::uint64_t got = std::strtoull(Sha256::HashBytes(mReplayBytes.data(), mReplayBytes.size()).substr(0, 16).c_str(), nullptr, 16);
                if (got != id || !ReplayFormat::Decode(mReplayBytes, check, error)) { mReplayBytes.clear(); mReplayNotice = "The replay was damaged on the way. Try again."; }
                else mReplayReady = true;
            }
        }
        else if (f.type == kReject && mState == State::eBrowsing && [&] { Reject r; return Decode(f.payload, r) && r.reason == kNoSuchReplay; }()) {
            mFetchId = 0; mReplayBytes.clear();
            mReplayNotice = "That replay is no longer on the server (it keeps only the newest).";
        }
        else if (f.type == kReject) {
            Reject r;
            Decode(f.payload, r);
            const std::string text = DescribeReject(r);
            if (mState == State::eWaiting) {
                // The server closes the connection with its refusal: say why, and reconnect so the player is back at the list.
                const std::string host = mHost, name = mName;
                const int port = mPort;
                const std::vector<std::uint8_t> key = mKey;
                Connect(host, port, name, key);
                mNotice = text;
                return;
            }
            Fail(text);
        }
        else if (f.type == kLobbyUsers) {
            LobbyUsers u;
            if (Decode(f.payload, u)) { mUsers = u.names; mUsersTotal = u.total; }
        }
        else if (f.type == kRoster) {
            Roster r;
            if (Decode(f.payload, r)) mRoster = r;
        }
        else if (f.type == kChatMessage && !mGotStart) {
            // Lobby chat, and the chat of a race that has not started yet. Once the race has started, chat belongs to the race and is
            // handed over with the rest of what arrived (the backlog), so a message sent at the start is not lost.
            ChatMessage m;
            if (Decode(f.payload, m)) {
                mChat.push_back(m.fromServer ? "Server: " + m.text : m.name + ": " + m.text);
                while (mChat.size() > 200) mChat.pop_front();
            }
        }
        else if (f.type == kHelloAck) { mGotAck = Decode(f.payload, mAck); }
        else if (f.type == kStart) { mGotStart = true; }
        else if (mGotAck) mBacklog.push_back(f);   // ticks that arrive while the race is being set up
    }
    SendPendingFetch();
    if (mQueuedWatchId != 0 && mState == State::eBrowsing) {
        bool found = false, started = false;
        for (const auto& r : mRaces) if (r.id == mQueuedWatchId) { found = true; started = r.started; }
        if (!found) { mQueuedWatchId = 0; mNotice = "The race you were waiting to watch is gone."; }
        else if (started) { const std::uint32_t id = mQueuedWatchId; mQueuedWatchId = 0; Spectate(id, mQueuedWatchCode); return; }
    }
    if (mGotAck && (mGotStart || mSpectating) && mState == State::eWaiting) { mState = State::eReady; mMessage = "Starting"; return; }
    if (!open) Fail(mState == State::eWaiting ? "The connection to the server was lost before the race started." : "The connection to the server was lost.");
}

void OnlineLobby::RefreshReplays(const std::string& track)
{
    if (mState != State::eBrowsing) return;
    ListReplays ask;
    ask.track = track;
    ask.max = static_cast<std::uint8_t>(kMaxReplayList);
    mReplaysLoaded = false;
    Send(kListReplays, Encode(ask));
}

void OnlineLobby::RefreshRecords(const std::string& track)
{
    if (mState != State::eBrowsing || track.empty()) return;
    ListRecords ask;
    ask.track = track;
    mRecordsLoaded = false;
    Send(kListRecords, Encode(ask));
}

void OnlineLobby::FetchReplay(std::uint64_t id)
{
    if (mState != State::eBrowsing || mFetchId != 0) return;
    mFetchId = id; mFetchNext = 0; mFetchCount = 0; mReplayBytes.clear(); mReplayReady = false; mReplayNotice.clear();
    mFetchSent = false;   // sent from Pump() as soon as the server's pace allows
    SendPendingFetch();
}

void OnlineLobby::SendPendingFetch()
{
    if (mFetchId == 0 || mFetchSent || mState != State::eBrowsing) return;
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - mLastFetchMs < 650) return;
    mLastFetchMs = now;
    mFetchSent = true;
    GetReplay get;
    get.id = mFetchId;
    Send(kGetReplay, Encode(get));
}

void OnlineLobby::Refresh()
{
    if (mState != State::eBrowsing) return;
    Send(kListRaces, std::vector<std::uint8_t>());
}

void OnlineLobby::Host(const std::string& track, int players, int laps, bool weapons, const std::string& code)
{
    if (mState != State::eBrowsing) return;
    HostRace h;
    h.track = track;
    h.players = static_cast<std::uint8_t>(players);
    h.laps = static_cast<std::uint8_t>(laps);
    h.weapons = weapons;
    h.code = code;
    mNotice.clear();
    mSpectating = false;
    mIsHost = true;
    mRoster = Roster();
    if (Send(kHostRace, Encode(h))) { mState = State::eWaiting; mMessage = players == 1 ? "Starting your time trial..." : "Race created: waiting for players."; }
}

void OnlineLobby::Join(std::uint32_t id, const std::string& code)
{
    if (mState != State::eBrowsing) return;
    JoinRace j;
    j.id = id;
    j.code = code;
    mNotice.clear();
    mSpectating = false;
    mIsHost = false;
    mRoster = Roster();
    if (Send(kJoinRace, Encode(j))) { mState = State::eWaiting; mMessage = "Joined: waiting for the race to start."; }
}

void OnlineLobby::Spectate(std::uint32_t id, const std::string& code)
{
    if (mState != State::eBrowsing) return;
    SpectateRace s;
    s.id = id;
    s.code = code;
    mNotice.clear();
    mSpectating = true;
    mIsHost = false;
    if (Send(kSpectateRace, Encode(s))) { mState = State::eWaiting; mMessage = "Joining as a spectator..."; }
}

void OnlineLobby::WatchWhenStarted(std::uint32_t id, const std::string& code)
{
    if (mState != State::eBrowsing) return;
    mNotice.clear();
    for (const auto& r : mRaces) if (r.id == id && r.started) { Spectate(id, code); return; }
    mQueuedWatchId = id;
    mQueuedWatchCode = code;
}

void OnlineLobby::Cancel()
{
    if (mState != State::eWaiting) return;
    // The protocol has no "leave" message before a race starts: drop the connection and reconnect to the lobby.
    const std::string host = mHost, name = mName;
    const int port = mPort;
    const std::vector<std::uint8_t> key = mKey;
    Connect(host, port, name, key);
}

void OnlineLobby::SendChat(const std::string& text)
{
    if (mState != State::eBrowsing && mState != State::eWaiting) return;
    Chat c;
    c.text = text.substr(0, 120);
    Send(kChat, Encode(c));
}

void OnlineLobby::StartRace()
{
    if (mState != State::eWaiting || !mIsHost) return;
    Send(kStartRace, std::vector<std::uint8_t>());
    mMessage = "Starting the race...";
}
