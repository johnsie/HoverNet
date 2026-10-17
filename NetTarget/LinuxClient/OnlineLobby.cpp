#include "OnlineLobby.h"

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
    hello.capabilities = kCapRaceId | kCapLobbyChat;   // this client shows the race number to a host, and the lobby's chat and users
    mRaces.clear(); mBacklog.clear(); mUsers.clear(); mUsersTotal = 0; mChat.clear();
    mGotAck = mGotStart = mSpectating = false;
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
        else if (f.type == kChatMessage) {
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
    if (mGotAck && (mGotStart || mSpectating) && mState == State::eWaiting) { mState = State::eReady; mMessage = "Starting"; return; }
    if (!open) Fail(mState == State::eWaiting ? "The connection to the server was lost before the race started." : "The connection to the server was lost.");
}

void OnlineLobby::Refresh()
{
    if (mState != State::eBrowsing) return;
    mNotice.clear();
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
    if (Send(kSpectateRace, Encode(s))) { mState = State::eWaiting; mMessage = "Joining as a spectator..."; }
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
