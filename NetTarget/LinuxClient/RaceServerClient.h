// RaceServerClient.h : Linux-native TCP client for HoverRace's centralized RaceServer.
//
// This intentionally does not reuse Game2's MR_NetworkInterface (NetInterface.h/.cpp):
// that class mixes the wire protocol with Win32 modal dialogs (HWND connect prompts),
// so it isn't something a headless Linux binary can call into. This class instead
// speaks the same on-the-wire message format directly, matching the framing used by
// NetTarget/RaceServer/NetworkInterface/ServerSocket.cpp (see MakeMessageHeader there):
// a little-endian uint16 header (messageType in bits 10-15), a uint8 data length, then
// the data bytes.
#ifndef HOVERNET_RACE_SERVER_CLIENT_H
#define HOVERNET_RACE_SERVER_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>

// Message type values as assigned by NetTarget/RaceServer/NetworkInterface/ServerSocket.cpp
// and MessageDispatcher.cpp. Only the subset the server actually acts on today is listed.
enum MR_RaceServerMessageType
{
    eRSMsgCreateMainElem   = 2,
    eRSMsgSetMainElemState = 3,
    eRSMsgCreateAutoElem   = 4,
    eRSMsgChatMessage      = 6,
    eRSMsgHitMessage       = 10,
    eRSMsgGameName         = 42,  // Client -> Server: join (or create) a race by name
    eRSMsgConnNameGetSet   = 43,
    eRSMsgConnNameSet      = 44,  // Server -> Client: a peer's UDP port + name
    eRSMsgClientAddrReq    = 45,
    eRSMsgSetPlayerName    = 46,  // Client -> Server: set this connection's display name
    eRSMsgLagTest          = 47,
    eRSMsgLobbyUserPresent = 48,  // Server -> Client: a user in the lobby (join announce, or a ListLobbyUsers row)
    eRSMsgLobbyUserLeft    = 49,  // Server -> Client: a user is no longer browsing the lobby
    eRSMsgReady            = 51,
    eRSMsgListLobbyUsers   = 56,  // Client -> Server: request who else is currently browsing
    eRSMsgLobbyUserListEnd = 57,  // Server -> Client: the ListLobbyUsers burst is complete
    eRSMsgListGames        = 60,  // Client -> Server: request the lobby listing
    eRSMsgGameInfo         = 61,  // Server -> Client: one open race (see ParseGameInfo)
    eRSMsgGameListEnd      = 62,  // Server -> Client: listing is complete
    eRSMsgJoinedRace       = 63,  // Server -> Client: ack for eRSMsgGameName (see ParseJoinedRace)
    // NOTE: the wire header's message-type field is only 6 bits (MakeMessageHeader in
    // ServerSocket.cpp masks with 0x3F), so every value here must stay in 0-63 or it
    // silently wraps around to a different, already-used type.
    eRSMsgStartRace        = 52,  // Client -> Server: race creator asks to start (ignored otherwise)
    eRSMsgRaceStarted      = 53,  // Server -> Client: broadcast to every player in the race at once
    eRSMsgHostRace         = 54,  // Client -> Server: create a race with explicit track/laps/weapons
    eRSMsgJoinRaceById     = 55,  // Client -> Server: join a specific race by its unique id (see JoinGameById)
    eRSMsgPlayerNameAssigned = 58,  // Server -> Client: the name it actually stored for you (see ParsePlayerNameAssigned)
};

// One race as reported by eRSMsgGameInfo.
struct RaceServerGameInfo
{
    int mRaceId = -1;
    int mNumPlayers = 0;
    bool mStarted = false;
    int mNumLaps = 0;
    std::string mName;
    std::string mTrack;
};

struct RaceServerMessage
{
    int mType = 0;
    std::vector<std::uint8_t> mData;
};

// A peer discovered via eRSMsgConnNameSet (a race member) or eRSMsgLobbyUserPresent
// (someone browsing the lobby) -- same shape either way. mClientId identifies them
// in later eRSMsgSetMainElemState updates; the server stamps that id from the
// sending connection, allowing races with multiple remote players to attribute
// updates.
struct RaceServerPeer
{
    int mClientId = -1;
    std::string mName;
};

// The server's reply to eRSMsgGameName or eRSMsgHostRace. mRaceId == -1 means the
// request was rejected (eRSMsgHostRace only: unknown track, or the race name is
// already taken).
struct RaceServerJoinAck
{
    int mRaceId = -1;
    bool mIsHost = false;  // True if this client created the race (and so may start it)
    int mClientId = -1;    // This connection's server-assigned identity
};

// Blocking TCP client for the RaceServer protocol. Not thread-safe.
class RaceServerClient
{
public:
    RaceServerClient();
    ~RaceServerClient();

    RaceServerClient(const RaceServerClient&) = delete;
    RaceServerClient& operator=(const RaceServerClient&) = delete;

    bool Connect(const std::string& host, unsigned port);
    void Disconnect();
    bool IsConnected() const;

    // Sends a raw message. pData/pLen may be null/0 for an empty payload.
    bool SendMessage(int messageType, const void* pData, std::size_t pLen);

    // Convenience: joins race server matchmaking under the given game/session name.
    // Ambiguous if more than one open race shares that name -- prefer JoinGameById
    // when a specific RaceServerGameInfo (with its unique mRaceId) is available.
    bool JoinGame(const std::string& gameName);

    // Convenience: joins the specific race identified by mRaceId (see RaceServerGameInfo),
    // disambiguating even when multiple races share the same display name.
    bool JoinGameById(int raceId);

    // Convenience: creates a race with explicit settings. Fails (returns false, no
    // message sent) if any field is oversized; the server separately rejects an
    // unknown track or an already-taken race name via eRSMsgJoinedRace(raceId=-1).
    bool HostRace(const std::string& raceName, const std::string& trackName, int numLaps,
                 bool weaponsAllowed);

    // Waits up to pTimeoutMs for one complete message. Returns false on timeout,
    // disconnect, or malformed data.
    bool PollMessage(RaceServerMessage& pOut, int pTimeoutMs);

    // Parses an eRSMsgConnNameSet payload ([4-byte little-endian clientId][name]).
    static bool ParsePeer(const RaceServerMessage& pMessage, RaceServerPeer& pOut);

    // Sets this connection's display name, shown in the lobby user list and (once
    // a race is joined/hosted) to other racers instead of the server's "Player_N"
    // fallback. Fails (no message sent) if pName is empty or over 20 bytes.
    bool SetPlayerName(const std::string& pName);

    // Requests the roster of everyone else currently browsing the lobby (not yet
    // in a race). Each of them arrives as a separate eRSMsgLobbyUserPresent message
    // (same wire shape/parser as eRSMsgConnNameSet, see ParsePeer), terminated by
    // eRSMsgLobbyUserListEnd -- unlike ListGames, this doesn't block and collect,
    // since new arrivals/departures after this snapshot come the same way as
    // eRSMsgLobbyUserPresent/eRSMsgLobbyUserLeft broadcasts from then on.
    bool ListLobbyUsers();

    // Keep-alive: sends an empty eRSMsgLagTest so the server's idle timeout
    // (see ClientConnection::IsAlive) doesn't drop this connection during
    // stretches with no other traffic, e.g. sitting in the lobby or a race's
    // waiting room. Callers should call this periodically (every few seconds,
    // well under MR_CONNECTION_TIMEOUT) whenever nothing else was just sent.
    bool Ping();

    // Parses an eRSMsgLobbyUserLeft payload ([4-byte little-endian clientId]).
    static bool ParseLobbyUserLeft(const RaceServerMessage& pMessage, int& pOutClientId);

    // Parses an eRSMsgChatMessage payload: [4-byte little-endian senderClientId]
    // [chat text bytes]. The server stamps the sender's id on before relaying (see
    // ServerSocket.cpp's MRNM_CHAT_MESSAGE case) since the recipient has no other
    // way to know who sent it; look the id up against the lobby/race roster to get
    // a display name.
    static bool ParseChatMessage(const RaceServerMessage& pMessage, int& pOutSenderClientId,
                                 std::string& pOutText);

    // The original in-race protocol carries the bitmap font's 1..95 glyph
    // indices, not ASCII. Lobby chat remains plain text, so callers use these
    // only after a race has started.
    static std::string EncodeInRaceChat(const std::string& pText);
    static std::string DecodeInRaceChat(const std::string& pText);

    // Parses an eRSMsgPlayerNameAssigned payload: the raw name bytes, nothing else
    // (unlike ParseChatMessage, this is never addressed to anyone but the
    // recipient, so there's no sender id to strip). The server sends this in
    // reply to SetPlayerName with the name it actually stored -- which may differ
    // from what was requested if that name was already taken by someone else
    // currently connected (see ServerSocket.cpp's MRNM_SET_PLAYER_NAME handler).
    static bool ParsePlayerNameAssigned(const RaceServerMessage& pMessage, std::string& pOutName);

    // Sends eRSMsgListGames and collects every eRSMsgGameInfo up to eRSMsgGameListEnd
    // (or pTimeoutMs of silence). Returns false only on a transport-level failure;
    // an empty lobby is a successful, empty pOutGames.
    bool ListGames(std::vector<RaceServerGameInfo>& pOutGames, int pTimeoutMs = 2000);

    // Parses an eRSMsgGameInfo payload.
    static bool ParseGameInfo(const RaceServerMessage& pMessage, RaceServerGameInfo& pOut);

    // Parses an eRSMsgJoinedRace payload
    // ([4-byte little-endian raceId][1-byte isHost][4-byte little-endian clientId]).
    static bool ParseJoinedRace(const RaceServerMessage& pMessage, RaceServerJoinAck& pOut);

    // Convenience: the race's creator asks the server to start it. The server only
    // honors this from the actual creator; anyone else's request is silently ignored.
    bool StartRace();

    // Sends this player's MainCharacter net state to the rest of the race. The
    // envelope includes pLocalClientId, but the server replaces it with the id of
    // the actual sending connection before relaying it to prevent impersonation.
    bool SendPlayerState(int pLocalClientId, const void* pStateData, std::size_t pStateLen);

    // Parses an eRSMsgSetMainElemState envelope built by SendPlayerState:
    // [4-byte little-endian senderClientId][raw MR_MainCharacter net state bytes].
    // pOutStateData/pOutStateLen point into pMessage's own storage.
    static bool ParsePlayerState(const RaceServerMessage& pMessage, int& pOutSenderClientId,
                                 const std::uint8_t*& pOutStateData, std::size_t& pOutStateLen);

    bool SendHit(int pTargetClientId);
    static bool ParseHit(const RaceServerMessage& pMessage, int& pOutTargetClientId);

    // Replicates a short-lived automatically-created world element (notably a
    // missile) using the legacy MRNM_CREATE_AUTO_ELEM payload:
    // [int16 dllId][int16 classId][int16 room][raw element net state].
    bool SendAutoElement(int pDllId, int pClassId, int pRoom,
                         const void* pStateData, std::size_t pStateLen);
    static bool ParseAutoElement(const RaceServerMessage& pMessage, int& pOutDllId,
                                 int& pOutClassId, int& pOutRoom,
                                 const std::uint8_t*& pOutStateData,
                                 std::size_t& pOutStateLen);

private:
    int mSocket;
    std::vector<std::uint8_t> mReceiveBuffer;
};

#endif
