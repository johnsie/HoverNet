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

    bool RecvAll(int pSocket, void* pBuffer, std::size_t pLen, int pTimeoutMs)
    {
        std::uint8_t* lDest = static_cast<std::uint8_t*>(pBuffer);
        std::size_t lReceived = 0;

        while (lReceived < pLen)
        {
            fd_set lReadSet;
            FD_ZERO(&lReadSet);
            FD_SET(pSocket, &lReadSet);

            struct timeval lTimeout;
            lTimeout.tv_sec = pTimeoutMs / 1000;
            lTimeout.tv_usec = (pTimeoutMs % 1000) * 1000;

            int lReady = select(pSocket + 1, &lReadSet, nullptr, nullptr, &lTimeout);
            if (lReady <= 0)
            {
                return false;  // Timeout or error
            }

            ssize_t lCount = recv(pSocket, lDest + lReceived, pLen - lReceived, 0);
            if (lCount <= 0)
            {
                return false;  // Peer closed or error
            }
            lReceived += static_cast<std::size_t>(lCount);
        }
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

    return send(mSocket, lBuffer.data(), lBuffer.size(), 0) == static_cast<ssize_t>(lBuffer.size());
}

bool RaceServerClient::JoinGame(const std::string& pGameName)
{
    return SendMessage(eRSMsgGameName, pGameName.data(), pGameName.size());
}

bool RaceServerClient::StartRace()
{
    return SendMessage(eRSMsgStartRace, nullptr, 0);
}

bool RaceServerClient::PollMessage(RaceServerMessage& pOut, int pTimeoutMs)
{
    if (mSocket < 0)
    {
        return false;
    }

    std::uint8_t lHeaderBytes[3];
    if (!RecvAll(mSocket, lHeaderBytes, sizeof(lHeaderBytes), pTimeoutMs))
    {
        return false;
    }

    const std::uint16_t lHeader = static_cast<std::uint16_t>(lHeaderBytes[0]) |
                                   (static_cast<std::uint16_t>(lHeaderBytes[1]) << 8);
    const std::uint8_t lDataLen = lHeaderBytes[2];

    pOut.mType = MessageTypeFromHeader(lHeader);
    pOut.mData.assign(lDataLen, 0);

    if (lDataLen > 0 && !RecvAll(mSocket, pOut.mData.data(), lDataLen, pTimeoutMs))
    {
        return false;
    }
    return true;
}

bool RaceServerClient::ParsePeer(const RaceServerMessage& pMessage, RaceServerPeer& pOut)
{
    if (pMessage.mType != eRSMsgConnNameSet || pMessage.mData.size() < 4)
    {
        return false;
    }

    std::uint32_t lPort = 0;
    std::memcpy(&lPort, pMessage.mData.data(), sizeof(lPort));
    pOut.mUdpPort = lPort;
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
    if (pMessage.mType != eRSMsgJoinedRace || pMessage.mData.size() < 5)
    {
        return false;
    }

    int lRaceId = 0;
    std::memcpy(&lRaceId, pMessage.mData.data(), sizeof(lRaceId));
    pOut.mRaceId = lRaceId;
    pOut.mIsHost = pMessage.mData[4] != 0;
    return true;
}
