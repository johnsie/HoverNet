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
    eRSMsgChatMessage      = 6,
    eRSMsgGameName         = 42,  // Client -> Server: join (or create) a race by name
    eRSMsgConnNameGetSet   = 43,
    eRSMsgConnNameSet      = 44,  // Server -> Client: a peer's UDP port + name
    eRSMsgClientAddrReq    = 45,
    eRSMsgLagTest          = 47,
    eRSMsgReady            = 51,
    eRSMsgListGames        = 60,  // Client -> Server: request the lobby listing
    eRSMsgGameInfo         = 61,  // Server -> Client: one open race (see ParseGameInfo)
    eRSMsgGameListEnd      = 62,  // Server -> Client: listing is complete
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

// A peer discovered via eRSMsgConnNameSet.
struct RaceServerPeer
{
    unsigned mUdpPort = 0;
    std::string mName;
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
    bool JoinGame(const std::string& gameName);

    // Waits up to pTimeoutMs for one complete message. Returns false on timeout,
    // disconnect, or malformed data.
    bool PollMessage(RaceServerMessage& pOut, int pTimeoutMs);

    // Parses an eRSMsgConnNameSet payload ([4-byte little-endian UDP port][name]).
    static bool ParsePeer(const RaceServerMessage& pMessage, RaceServerPeer& pOut);

    // Sends eRSMsgListGames and collects every eRSMsgGameInfo up to eRSMsgGameListEnd
    // (or pTimeoutMs of silence). Returns false only on a transport-level failure;
    // an empty lobby is a successful, empty pOutGames.
    bool ListGames(std::vector<RaceServerGameInfo>& pOutGames, int pTimeoutMs = 2000);

    // Parses an eRSMsgGameInfo payload.
    static bool ParseGameInfo(const RaceServerMessage& pMessage, RaceServerGameInfo& pOut);

private:
    int mSocket;
};

#endif
