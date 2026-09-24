#include "RaceServerClient.h"

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

RaceServerClient::RaceServerClient() : mSocket(-1)
{
}

RaceServerClient::~RaceServerClient()
{
    Disconnect();
}

bool RaceServerClient::Connect(const std::string& pHost, unsigned pPort)
{
    Disconnect();

    struct addrinfo lHints;
    std::memset(&lHints, 0, sizeof(lHints));
    lHints.ai_family = AF_INET;
    lHints.ai_socktype = SOCK_STREAM;

    struct addrinfo* lResult = nullptr;
    const std::string lPortStr = std::to_string(pPort);
    if (getaddrinfo(pHost.c_str(), lPortStr.c_str(), &lHints, &lResult) != 0 || lResult == nullptr)
    {
        return false;
    }

    mSocket = socket(lResult->ai_family, lResult->ai_socktype, lResult->ai_protocol);
    if (mSocket < 0)
    {
        freeaddrinfo(lResult);
        return false;
    }

    const bool lConnected = connect(mSocket, lResult->ai_addr, lResult->ai_addrlen) == 0;
    freeaddrinfo(lResult);

    if (!lConnected)
    {
        Disconnect();
        return false;
    }
    return true;
}

void RaceServerClient::Disconnect()
{
    if (mSocket >= 0)
    {
        close(mSocket);
        mSocket = -1;
    }
    mReceiveBuffer.clear();
}

bool RaceServerClient::IsConnected() const
{
    return mSocket >= 0;
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
        const ssize_t lCount = send(mSocket, lBuffer.data() + lSent, lBuffer.size() - lSent, 0);
        if (lCount < 0 && errno == EINTR)
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

bool RaceServerClient::StartRace()
{
    return SendMessage(eRSMsgStartRace, nullptr, 0);
}

bool RaceServerClient::HostRace(const std::string& pRaceName, const std::string& pTrackName, int pNumLaps,
                                bool pWeaponsAllowed)
{
    if (pTrackName.size() > 63 || pRaceName.size() > 63 || pNumLaps < 1 || pNumLaps > 255)
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
    const ssize_t lCount = recv(mSocket, lIncoming, sizeof(lIncoming), 0);
    if (lCount < 0 && errno == EINTR)
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
    if (pMessage.mType != eRSMsgConnNameSet || pMessage.mData.size() < 4)
    {
        return false;
    }

    int lClientId = 0;
    std::memcpy(&lClientId, pMessage.mData.data(), sizeof(lClientId));
    pOut.mClientId = lClientId;
    pOut.mName.assign(pMessage.mData.begin() + 4, pMessage.mData.end());
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

    int lRaceId = 0;
    std::memcpy(&lRaceId, lData, sizeof(lRaceId));
    std::size_t lOffset = sizeof(lRaceId);

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

    int lRaceId = 0;
    std::memcpy(&lRaceId, pMessage.mData.data(), sizeof(lRaceId));
    pOut.mRaceId = lRaceId;
    pOut.mIsHost = pMessage.mData[4] != 0;

    int lClientId = 0;
    std::memcpy(&lClientId, pMessage.mData.data() + 5, sizeof(lClientId));
    pOut.mClientId = lClientId;
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
    std::memcpy(lEnvelope.data(), &pLocalClientId, sizeof(pLocalClientId));
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

    int lClientId = 0;
    std::memcpy(&lClientId, pMessage.mData.data(), sizeof(lClientId));
    pOutSenderClientId = lClientId;
    pOutStateData = pMessage.mData.data() + 4;
    pOutStateLen = pMessage.mData.size() - 4;
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
    const std::int16_t lDllId = static_cast<std::int16_t>(pDllId);
    const std::int16_t lClassId = static_cast<std::int16_t>(pClassId);
    const std::int16_t lRoom = static_cast<std::int16_t>(pRoom);
    std::memcpy(lPayload.data(), &lDllId, sizeof(lDllId));
    std::memcpy(lPayload.data() + 2, &lClassId, sizeof(lClassId));
    std::memcpy(lPayload.data() + 4, &lRoom, sizeof(lRoom));
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

    std::int16_t lDllId = 0;
    std::int16_t lClassId = 0;
    std::int16_t lRoom = 0;
    std::memcpy(&lDllId, pMessage.mData.data(), sizeof(lDllId));
    std::memcpy(&lClassId, pMessage.mData.data() + 2, sizeof(lClassId));
    std::memcpy(&lRoom, pMessage.mData.data() + 4, sizeof(lRoom));
    pOutDllId = lDllId;
    pOutClassId = lClassId;
    pOutRoom = lRoom;
    pOutStateData = pMessage.mData.data() + 6;
    pOutStateLen = pMessage.mData.size() - 6;
    return true;
}
