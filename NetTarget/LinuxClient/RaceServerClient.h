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
    eRSMsgLagTest          = 47,
    eRSMsgReady            = 51,
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

// A peer discovered via eRSMsgConnNameSet. mClientId identifies them in later
// eRSMsgSetMainElemState updates. The server stamps that id from the sending
// connection, allowing races with multiple remote players to attribute updates.
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
