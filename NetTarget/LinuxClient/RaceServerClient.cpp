#ifdef _WIN32
// Keep the shared client independent of the legacy MFC precompiled header.
// winsock2 must precede windows.h in any translation unit that later includes it.
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "RaceServerClient.h"
#include "../RaceServer/NetworkInterface/Protocol.h"

#include <cassert>
#include <cstring>

namespace
{
    // Matches MakeMessageHeader() in NetTarget/RaceServer/NetworkInterface/ServerSocket.cpp:
    // DatagramNumber occupies bits 0-7, DatagramQueue bits 8-9, MessageType bits 10-15 --
    // only 6 bits, so valid types are 0-63; anything else silently wraps into a
    // different, already-used type instead of failing loudly.
    // Datagram fields are unused over TCP, so they are left zero.
    std::uint16_t MakeMessageHeader(int pMessageType)
    {
        assert(pMessageType >= 0 && pMessageType <= 0x3F);
        return static_cast<std::uint16_t>((pMessageType & 0x3F) << 10);
    }

    int MessageTypeFromHeader(std::uint16_t pHeader)
    {
        return (pHeader >> 10) & 0x3F;
    }

    bool TryExtractMessage(std::vector<std::uint8_t>& pBuffer, RaceServerMessage& pOut)
    {
        if (pBuffer.size() < 3)
        {
            return false;
        }

        const std::size_t lMessageLen = 3 + pBuffer[2];
        if (pBuffer.size() < lMessageLen)
        {
            return false;
        }

        const std::uint16_t lHeader = static_cast<std::uint16_t>(pBuffer[0]) |
                                      (static_cast<std::uint16_t>(pBuffer[1]) << 8);
        pOut.mType = MessageTypeFromHeader(lHeader);
        pOut.mData.assign(pBuffer.begin() + 3, pBuffer.begin() + lMessageLen);
        pBuffer.erase(pBuffer.begin(), pBuffer.begin() + lMessageLen);
        return true;
    }
}

RaceServerClient::RaceServerClient()
    : mSocket(-1), mPingToken(0), mPendingPingToken(0), mLastPingMs(-1)
{
#ifdef _WIN32
    WSADATA lWsaData;
    WSAStartup(MAKEWORD(1, 1), &lWsaData);
#endif
}

RaceServerClient::~RaceServerClient()
{
    Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RaceServerClient::Connect(const std::string& pHost, unsigned pPort)
{
    return ConnectWithProtocolVersion(
        pHost, pPort, HoverNetProtocol::Major, HoverNetProtocol::Minor);
}

bool RaceServerClient::ConnectWithProtocolVersion(
    const std::string& pHost, unsigned pPort, std::uint16_t pMajor, std::uint16_t pMinor,
    std::uint16_t pMaxPayload)
{
    Disconnect();
    mProtocolError.clear();
#ifdef _WIN32
    SOCKADDR_IN lAddr;
    std::memset(&lAddr, 0, sizeof(lAddr));
    lAddr.sin_family = AF_INET;
    lAddr.sin_port = htons(static_cast<unsigned short>(pPort));
    lAddr.sin_addr.s_addr = inet_addr(pHost.c_str());
    if (lAddr.sin_addr.s_addr == INADDR_NONE)
    {
        HOSTENT* lHost = gethostbyname(pHost.c_str());
        if (lHost == nullptr || lHost->h_addr_list[0] == nullptr) return false;
        std::memcpy(&lAddr.sin_addr, lHost->h_addr_list[0], sizeof(lAddr.sin_addr));
    }
    mSocket = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (mSocket == static_cast<int>(INVALID_SOCKET) || connect(static_cast<SOCKET>(mSocket),
        reinterpret_cast<const SOCKADDR*>(&lAddr), sizeof(lAddr)) == SOCKET_ERROR)
    {
        Disconnect();
        return false;
    }
#else
    struct addrinfo lHints;
    std::memset(&lHints, 0, sizeof(lHints));
    lHints.ai_family = AF_INET;
    lHints.ai_socktype = SOCK_STREAM;
    struct addrinfo* lResult = nullptr;
    const std::string lPortStr = std::to_string(pPort);
    if (getaddrinfo(pHost.c_str(), lPortStr.c_str(), &lHints, &lResult) != 0 || lResult == nullptr) return false;
    mSocket = socket(lResult->ai_family, lResult->ai_socktype, lResult->ai_protocol);
    if (mSocket < 0) { freeaddrinfo(lResult); return false; }
    const bool lConnected = connect(mSocket, lResult->ai_addr, lResult->ai_addrlen) == 0;
    freeaddrinfo(lResult);
    if (!lConnected) { Disconnect(); return false; }
#endif
    std::uint8_t lHello[HoverNetProtocol::HelloSize] = {'H', 'N', 'E', 'T'};
    HoverNetProtocol::WriteU16(lHello + 4, pMajor);
    HoverNetProtocol::WriteU16(lHello + 6, pMinor);
    HoverNetProtocol::WriteU16(lHello + 8, pMaxPayload);
    HoverNetProtocol::WriteU32(lHello + 10, 0);
    if (!SendMessage(HoverNetProtocol::MessageType, lHello, sizeof(lHello)))
    {
        mProtocolError = "Could not send protocol hello";
        Disconnect();
        return false;
    }

    RaceServerMessage lReply;
    if (!PollMessage(lReply, 2000) || lReply.mType != HoverNetProtocol::MessageType ||
        lReply.mData.size() < HoverNetProtocol::ReplyPrefixSize)
    {
        mProtocolError = "RaceServer did not return a valid protocol response";
        Disconnect();
        return false;
    }

    const std::uint8_t lStatus = lReply.mData[0];
    if (lStatus != HoverNetProtocol::Accepted)
    {
        mProtocolError.assign(lReply.mData.begin() + HoverNetProtocol::ReplyPrefixSize,
                              lReply.mData.end());
        if (mProtocolError.empty()) mProtocolError = "RaceServer rejected this protocol version";
        Disconnect();
        return false;
    }
    return true;
}

void RaceServerClient::Disconnect()
{
    if (mSocket >= 0)
    {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(mSocket));
#else
        close(mSocket);
#endif
        mSocket = -1;
    }
    mReceiveBuffer.clear();
    mPendingPingToken = 0;
    mLastPingMs = -1;
}

bool RaceServerClient::IsConnected() const
{
    return mSocket >= 0;
}

const std::string& RaceServerClient::GetProtocolError() const
{
    return mProtocolError;
}

int RaceServerClient::ReleaseSocket()
{
    const int lSocket = mSocket;
    mSocket = -1;
    mReceiveBuffer.clear();
    return lSocket;
}

bool RaceServerClient::SendMessage(int pMessageType, const void* pData, std::size_t pLen)
{
    if (mSocket < 0 || pLen > 255)
    {
        return false;
    }

    const std::uint16_t lHeader = MakeMessageHeader(pMessageType);
    std::vector<std::uint8_t> lBuffer;
    lBuffer.reserve(3 + pLen);
    lBuffer.push_back(static_cast<std::uint8_t>(lHeader & 0xFF));
    lBuffer.push_back(static_cast<std::uint8_t>((lHeader >> 8) & 0xFF));
    lBuffer.push_back(static_cast<std::uint8_t>(pLen));
    if (pData != nullptr && pLen > 0)
    {
        const std::uint8_t* lSrc = static_cast<const std::uint8_t*>(pData);
        lBuffer.insert(lBuffer.end(), lSrc, lSrc + pLen);
    }

    std::size_t lSent = 0;
    while (lSent < lBuffer.size())
    {
#ifdef _WIN32
        const int lCount = send(static_cast<SOCKET>(mSocket), reinterpret_cast<const char*>(lBuffer.data() + lSent), static_cast<int>(lBuffer.size() - lSent), 0);
        if (lCount == SOCKET_ERROR && WSAGetLastError() == WSAEINTR)
#else
        const ssize_t lCount = send(mSocket, lBuffer.data() + lSent, lBuffer.size() - lSent, 0);
        if (lCount < 0 && errno == EINTR)
#endif
        {
            continue;
        }
        if (lCount <= 0)
        {
            Disconnect();
            return false;
        }
        lSent += static_cast<std::size_t>(lCount);
    }
    return true;
}

bool RaceServerClient::JoinGame(const std::string& pGameName)
{
    return SendMessage(eRSMsgGameName, pGameName.data(), pGameName.size());
}

bool RaceServerClient::JoinGameById(int pRaceId)
{
    std::uint8_t lRaceId[4];
    HoverNetProtocol::WriteI32LE(lRaceId, static_cast<std::int32_t>(pRaceId));
    return SendMessage(eRSMsgJoinRaceById, lRaceId, sizeof(lRaceId));
}

bool RaceServerClient::SetPlayerName(const std::string& pName)
{
    if (pName.empty() || pName.size() > 20)
    {
        return false;
    }
    return SendMessage(eRSMsgSetPlayerName, pName.data(), pName.size());
}

bool RaceServerClient::ListLobbyUsers()
{
    return SendMessage(eRSMsgListLobbyUsers, nullptr, 0);
}

bool RaceServerClient::Ping()
{
    // The server echoes this opaque token to the sender. It still relays the
    // legacy lag message to race peers, which preserves older client behavior;
    // only the matching sender treats the echo as its RTT response.
    ++mPingToken;
    if (mPingToken == 0) ++mPingToken;
    std::uint8_t lPayload[4];
    HoverNetProtocol::WriteU32(lPayload, mPingToken);
    if (!SendMessage(eRSMsgLagTest, lPayload, sizeof(lPayload))) return false;
    mPendingPingToken = mPingToken;
    mPingSentAt = std::chrono::steady_clock::now();
    return true;
}

bool RaceServerClient::HandlePingReply(const RaceServerMessage& pMessage)
{
    if (pMessage.mType != eRSMsgLagTest || pMessage.mData.size() != 4 ||
        mPendingPingToken == 0 ||
        HoverNetProtocol::ReadU32(pMessage.mData.data()) != mPendingPingToken) {
        return false;
    }
    const auto lElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mPingSentAt).count();
    mLastPingMs = static_cast<int>(lElapsed < 0 ? 0 : lElapsed);
    mPendingPingToken = 0;
    return true;
}

int RaceServerClient::GetLastPingMs() const
{
    return mLastPingMs;
}

bool RaceServerClient::StartRace()
{
    return SendMessage(eRSMsgStartRace, nullptr, 0);
}

bool RaceServerClient::HostRace(const std::string& pRaceName, const std::string& pTrackName, int pNumLaps,
                                bool pWeaponsAllowed)
{
    if (pTrackName.empty() || pTrackName.size() > 63 ||
        pRaceName.empty() || pRaceName.size() > 32 || pNumLaps < 1 || pNumLaps > 255)
    {
        return false;
    }

    std::vector<std::uint8_t> lPayload;
    lPayload.push_back(static_cast<std::uint8_t>(pTrackName.size()));
    lPayload.insert(lPayload.end(), pTrackName.begin(), pTrackName.end());
    lPayload.push_back(static_cast<std::uint8_t>(pNumLaps));
    lPayload.push_back(pWeaponsAllowed ? 1 : 0);
    lPayload.push_back(static_cast<std::uint8_t>(pRaceName.size()));
    lPayload.insert(lPayload.end(), pRaceName.begin(), pRaceName.end());

    if (lPayload.size() > 255)
    {
        return false;
    }
    return SendMessage(eRSMsgHostRace, lPayload.data(), lPayload.size());
}

bool RaceServerClient::PollMessage(RaceServerMessage& pOut, int pTimeoutMs)
{
    if (mSocket < 0)
    {
        return false;
    }

    if (TryExtractMessage(mReceiveBuffer, pOut))
    {
        return true;
    }

    fd_set lReadSet;
    FD_ZERO(&lReadSet);
    FD_SET(mSocket, &lReadSet);

    const int lTimeoutMs = pTimeoutMs > 0 ? pTimeoutMs : 0;
    struct timeval lTimeout;
    lTimeout.tv_sec = lTimeoutMs / 1000;
    lTimeout.tv_usec = (lTimeoutMs % 1000) * 1000;

    const int lReady = select(mSocket + 1, &lReadSet, nullptr, nullptr, &lTimeout);
    if (lReady < 0)
    {
        Disconnect();
        return false;
    }
    if (lReady == 0)
    {
        return false;
    }

    std::uint8_t lIncoming[4096];
#ifdef _WIN32
    const int lCount = recv(static_cast<SOCKET>(mSocket), reinterpret_cast<char*>(lIncoming), sizeof(lIncoming), 0);
    if (lCount == SOCKET_ERROR && WSAGetLastError() == WSAEINTR)
#else
    const ssize_t lCount = recv(mSocket, lIncoming, sizeof(lIncoming), 0);
    if (lCount < 0 && errno == EINTR)
#endif
    {
        return false;
    }
    if (lCount <= 0)
    {
        Disconnect();
        return false;
    }

    mReceiveBuffer.insert(mReceiveBuffer.end(), lIncoming, lIncoming + lCount);
    return TryExtractMessage(mReceiveBuffer, pOut);
}

bool RaceServerClient::ParsePeer(const RaceServerMessage& pMessage, RaceServerPeer& pOut)
{
    // eRSMsgLobbyUserPresent uses the identical [clientId][name] wire shape as
    // eRSMsgConnNameSet -- one parser covers both.
    if ((pMessage.mType != eRSMsgConnNameSet && pMessage.mType != eRSMsgLobbyUserPresent) ||
        pMessage.mData.size() < 4)
    {
        return false;
    }

    pOut.mClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    pOut.mName.assign(pMessage.mData.begin() + 4, pMessage.mData.end());
    return true;
}

bool RaceServerClient::ParseLobbyUserLeft(const RaceServerMessage& pMessage, int& pOutClientId)
{
    if (pMessage.mType != eRSMsgLobbyUserLeft || pMessage.mData.size() < 4)
    {
        return false;
    }
    pOutClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    return true;
}

bool RaceServerClient::ParseChatMessage(const RaceServerMessage& pMessage, int& pOutSenderClientId,
                                        std::string& pOutText)
{
    if (pMessage.mType != eRSMsgChatMessage || pMessage.mData.size() < 4)
    {
        return false;
    }
    pOutSenderClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    pOutText.assign(pMessage.mData.begin() + 4, pMessage.mData.end());
    return true;
}

std::string RaceServerClient::EncodeInRaceChat(const std::string& pText)
{
    std::string lEncoded;
    lEncoded.reserve(pText.size());
    for (unsigned char lCharacter : pText)
    {
        lEncoded.push_back(static_cast<char>(
            lCharacter >= 32 && lCharacter < 127 ? lCharacter - 31 : '_' - 31));
    }
    return lEncoded;
}

std::string RaceServerClient::DecodeInRaceChat(const std::string& pText)
{
    std::string lDecoded;
    lDecoded.reserve(pText.size());
    for (unsigned char lGlyph : pText)
    {
        lDecoded.push_back(static_cast<char>(lGlyph >= 1 && lGlyph <= 95 ? lGlyph + 31 : '_'));
    }
    return lDecoded;
}

bool RaceServerClient::ParsePlayerNameAssigned(const RaceServerMessage& pMessage, std::string& pOutName)
{
    if (pMessage.mType != eRSMsgPlayerNameAssigned || pMessage.mData.empty())
    {
        return false;
    }
    pOutName.assign(pMessage.mData.begin(), pMessage.mData.end());
    return true;
}

bool RaceServerClient::ListGames(std::vector<RaceServerGameInfo>& pOutGames, int pTimeoutMs)
{
    pOutGames.clear();

    if (!SendMessage(eRSMsgListGames, nullptr, 0))
    {
        return false;
    }

    RaceServerMessage lMessage;
    while (PollMessage(lMessage, pTimeoutMs))
    {
        if (lMessage.mType == eRSMsgGameListEnd)
        {
            return true;
        }

        RaceServerGameInfo lInfo;
        if (ParseGameInfo(lMessage, lInfo))
        {
            pOutGames.push_back(lInfo);
        }
    }
    return false;  // Timed out or disconnected before the terminator arrived
}

bool RaceServerClient::ParseGameInfo(const RaceServerMessage& pMessage, RaceServerGameInfo& pOut)
{
    if (pMessage.mType != eRSMsgGameInfo || pMessage.mData.size() < 8)
    {
        return false;
    }

    const std::uint8_t* lData = pMessage.mData.data();
    const std::size_t lLen = pMessage.mData.size();

    const std::int32_t lRaceId = HoverNetProtocol::ReadI32LE(lData);
    std::size_t lOffset = 4;

    pOut.mRaceId = lRaceId;
    pOut.mNumPlayers = lData[lOffset++];
    pOut.mStarted = lData[lOffset++] != 0;
    pOut.mNumLaps = lData[lOffset++];

    if (lOffset >= lLen)
    {
        return false;
    }
    const std::uint8_t lNameLen = lData[lOffset++];
    if (lOffset + lNameLen > lLen)
    {
        return false;
    }
    pOut.mName.assign(lData + lOffset, lData + lOffset + lNameLen);
    lOffset += lNameLen;

    if (lOffset >= lLen)
    {
        return false;
    }
    const std::uint8_t lTrackLen = lData[lOffset++];
    if (lOffset + lTrackLen > lLen)
    {
        return false;
    }
    pOut.mTrack.assign(lData + lOffset, lData + lOffset + lTrackLen);

    return true;
}

bool RaceServerClient::ParseJoinedRace(const RaceServerMessage& pMessage, RaceServerJoinAck& pOut)
{
    if (pMessage.mType != eRSMsgJoinedRace || pMessage.mData.size() < 9)
    {
        return false;
    }

    pOut.mRaceId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    pOut.mIsHost = pMessage.mData[4] != 0;

    pOut.mClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data() + 5);
    return true;
}

bool RaceServerClient::SendPlayerState(int pLocalClientId, const void* pStateData, std::size_t pStateLen)
{
    if (pStateLen > 251)  // 255-byte message cap minus the 4-byte clientId prefix
    {
        return false;
    }

    std::vector<std::uint8_t> lEnvelope;
    lEnvelope.reserve(4 + pStateLen);
    lEnvelope.resize(4);
    HoverNetProtocol::WriteI32LE(lEnvelope.data(), static_cast<std::int32_t>(pLocalClientId));
    if (pStateData != nullptr && pStateLen > 0)
    {
        const std::uint8_t* lSrc = static_cast<const std::uint8_t*>(pStateData);
        lEnvelope.insert(lEnvelope.end(), lSrc, lSrc + pStateLen);
    }
    return SendMessage(eRSMsgSetMainElemState, lEnvelope.data(), lEnvelope.size());
}

bool RaceServerClient::ParsePlayerState(const RaceServerMessage& pMessage, int& pOutSenderClientId,
                                        const std::uint8_t*& pOutStateData, std::size_t& pOutStateLen)
{
    if (pMessage.mType != eRSMsgSetMainElemState || pMessage.mData.size() < 4)
    {
        return false;
    }

    pOutSenderClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    pOutStateData = pMessage.mData.data() + 4;
    pOutStateLen = pMessage.mData.size() - 4;
    return true;
}

bool RaceServerClient::SendHit(int pTargetClientId)
{
    std::uint8_t lTargetClientId[4];
    HoverNetProtocol::WriteI32LE(lTargetClientId, static_cast<std::int32_t>(pTargetClientId));
    return SendMessage(eRSMsgHitMessage, lTargetClientId, sizeof(lTargetClientId));
}

bool RaceServerClient::ParseHit(const RaceServerMessage& pMessage, int& pOutTargetClientId)
{
    if (pMessage.mType != eRSMsgHitMessage || pMessage.mData.size() < 4)
    {
        return false;
    }
    pOutTargetClientId = HoverNetProtocol::ReadI32LE(pMessage.mData.data());
    return true;
}

bool RaceServerClient::SendAutoElement(int pDllId, int pClassId, int pRoom,
                                       const void* pStateData, std::size_t pStateLen)
{
    if (pStateLen > 249 || pDllId < -32768 || pDllId > 32767 ||
        pClassId < -32768 || pClassId > 32767 || pRoom < -32768 || pRoom > 32767)
    {
        return false;
    }

    std::vector<std::uint8_t> lPayload(6);
    HoverNetProtocol::WriteI16LE(lPayload.data(), static_cast<std::int16_t>(pDllId));
    HoverNetProtocol::WriteI16LE(lPayload.data() + 2, static_cast<std::int16_t>(pClassId));
    HoverNetProtocol::WriteI16LE(lPayload.data() + 4, static_cast<std::int16_t>(pRoom));
    if (pStateData != nullptr && pStateLen > 0)
    {
        const std::uint8_t* lSrc = static_cast<const std::uint8_t*>(pStateData);
        lPayload.insert(lPayload.end(), lSrc, lSrc + pStateLen);
    }
    return SendMessage(eRSMsgCreateAutoElem, lPayload.data(), lPayload.size());
}

bool RaceServerClient::ParseAutoElement(const RaceServerMessage& pMessage, int& pOutDllId,
                                        int& pOutClassId, int& pOutRoom,
                                        const std::uint8_t*& pOutStateData,
                                        std::size_t& pOutStateLen)
{
    if (pMessage.mType != eRSMsgCreateAutoElem || pMessage.mData.size() < 6)
    {
        return false;
    }

    pOutDllId = HoverNetProtocol::ReadI16LE(pMessage.mData.data());
    pOutClassId = HoverNetProtocol::ReadI16LE(pMessage.mData.data() + 2);
    pOutRoom = HoverNetProtocol::ReadI16LE(pMessage.mData.data() + 4);
    pOutStateData = pMessage.mData.data() + 6;
    pOutStateLen = pMessage.mData.size() - 6;
    return true;
}
