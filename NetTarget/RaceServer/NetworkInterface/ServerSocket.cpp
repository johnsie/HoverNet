// ServerSocket.cpp : Server socket implementation

#include "stdafx.h"
#include "ServerSocket.h"
#include "MessageDispatcher.h"
#include "RaceManager.h"
#include "ServerLogger.h"

extern MR_ServerLogger g_Logger;

// Simple message structure matching Game2's MR_NetMessageBuffer
#pragma pack(push, 1)
struct MessageBuffer {
    unsigned short header;  // DatagramNumber(8) + DatagramQueue(2) + MessageType(6)
    unsigned char dataLen;
    unsigned char data[256];
};
#pragma pack(pop)

// Helper to construct message header with message type
inline unsigned short MakeMessageHeader(int messageType) {
    // Match MR_NetMessageBuffer: DatagramNumber bits 0-7, DatagramQueue bits 8-9,
    // and MessageType bits 10-15 -- only 6 bits, so valid types are 0-63. A type
    // outside that range silently wraps into some other, already-used type instead
    // of failing loudly, so assert here rather than let that happen again.
    assert(messageType >= 0 && messageType <= 0x3F);
    return static_cast<unsigned short>((messageType & 0x3F) << 10);
}

MR_ServerSocket::MR_ServerSocket()
    : mListenSocket(INVALID_SOCKET),
      mDatagramSocket(INVALID_SOCKET),
      mNextClientId(1),
      mMaxConnections(40),
      mPort(9600)
{
}

MR_ServerSocket::~MR_ServerSocket()
{
    Shutdown();
}

BOOL MR_ServerSocket::Initialize(unsigned port, int maxConnections)
{
    mPort = port;
    mMaxConnections = maxConnections;

    // Initialize Winsock
#ifdef _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    int iResult = WSAStartup(0, NULL);
#endif
    if (iResult != 0) {
        g_Logger.Log(MR_LOG_ERROR, "WSAStartup failed: %d", iResult);
        return FALSE;
    }

    // Create TCP listening socket
    mListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mListenSocket == INVALID_SOCKET) {
        g_Logger.Log(MR_LOG_ERROR, "TCP socket creation failed: %ld", WSAGetLastError());
        WSACleanup();
        return FALSE;
    }

    // Create UDP datagram socket
    mDatagramSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mDatagramSocket == INVALID_SOCKET) {
        g_Logger.Log(MR_LOG_ERROR, "UDP socket creation failed: %ld", WSAGetLastError());
        closesocket(mListenSocket);
        WSACleanup();
        return FALSE;
    }

    // Set socket options
    if (!SetSocketOptions(mListenSocket) || !SetSocketOptions(mDatagramSocket)) {
        g_Logger.Log(MR_LOG_ERROR, "Failed to set socket options");
        closesocket(mListenSocket);
        closesocket(mDatagramSocket);
        WSACleanup();
        return FALSE;
    }

    // Bind TCP socket
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(mListenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_ERROR, "TCP bind failed: %ld", WSAGetLastError());
        closesocket(mListenSocket);
        closesocket(mDatagramSocket);
        WSACleanup();
        return FALSE;
    }

    // Bind UDP socket
    if (bind(mDatagramSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_ERROR, "UDP bind failed: %ld", WSAGetLastError());
        closesocket(mListenSocket);
        closesocket(mDatagramSocket);
        WSACleanup();
        return FALSE;
    }

    // Listen on TCP socket
    if (listen(mListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_ERROR, "listen() failed: %ld", WSAGetLastError());
        closesocket(mListenSocket);
        closesocket(mDatagramSocket);
        WSACleanup();
        return FALSE;
    }

    g_Logger.Log(MR_LOG_INFO, "Server socket initialized successfully on port %u", port);
    return TRUE;
}

BOOL MR_ServerSocket::SetSocketOptions(SOCKET sock)
{
    // Enable SO_REUSEADDR to allow quick rebind after crash
    int reuseAddr = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_WARN, "SO_REUSEADDR failed: %ld", WSAGetLastError());
    }

    // Disable Nagle's algorithm for TCP (low-latency requirement)
    if (sock != mDatagramSocket) {
        int tcpNoDelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&tcpNoDelay, sizeof(tcpNoDelay)) == SOCKET_ERROR) {
            g_Logger.Log(MR_LOG_WARN, "TCP_NODELAY failed: %ld", WSAGetLastError());
        }
    }

    // Set send buffer size
    int sendBufSize = 8192;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sendBufSize, sizeof(sendBufSize)) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_WARN, "SO_SNDBUF failed: %ld", WSAGetLastError());
    }

    // Set receive buffer size
    int recvBufSize = 8192;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufSize, sizeof(recvBufSize)) == SOCKET_ERROR) {
        g_Logger.Log(MR_LOG_WARN, "SO_RCVBUF failed: %ld", WSAGetLastError());
    }

    return TRUE;
}

void MR_ServerSocket::ProcessEvents(MR_RaceManager* pRaceManager)
{
    // Accept new incoming connections
    AcceptNewConnection();

    // Process messages from existing connections
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(mDatagramSocket, &readSet);

    // Build fd_set for all client connections
    for (auto& pair : mConnections) {
        ClientConnection* pConn = pair.second;
        if (pConn && pConn->mConnected) {
            FD_SET(pConn->mTcpSocket, &readSet);
        }
    }

    // Non-blocking select to check for ready sockets
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000;  // 1ms timeout

    int maxSocket = mDatagramSocket;
    for (const auto& pair : mConnections) {
        if (pair.second && pair.second->mConnected) {
            maxSocket = std::max(maxSocket, pair.second->mTcpSocket);
        }
    }
    int selectResult = select(maxSocket + 1, &readSet, NULL, NULL, &timeout);
    if (selectResult > 0) {
        // Check UDP datagram socket
        if (FD_ISSET(mDatagramSocket, &readSet)) {
            ReceiveDatagram();
        }

        // Check each TCP client connection
        std::vector<int> clientsToRemove;
        for (auto& pair : mConnections) {
            int clientId = pair.first;
            ClientConnection* pConn = pair.second;
            if (pConn && FD_ISSET(pConn->mTcpSocket, &readSet)) {
                ReceiveFromClient(pConn, pRaceManager);
                if (!pConn->IsAlive()) {
                    clientsToRemove.push_back(clientId);
                }
            }
        }

        // Remove dead connections
        for (int clientId : clientsToRemove) {
            CloseConnection(clientId, pRaceManager);
        }
    }
}

void MR_ServerSocket::AcceptNewConnection()
{
    fd_set listenSet;
    FD_ZERO(&listenSet);
    FD_SET(mListenSocket, &listenSet);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;  // Non-blocking

    int selectResult = select(mListenSocket + 1, &listenSet, NULL, NULL, &timeout);
    if (selectResult <= 0) {
        return;  // No pending connections
    }

    if (!FD_ISSET(mListenSocket, &listenSet)) {
        return;
    }

    // Check if we have room for more connections
    if ((int)mConnections.size() >= mMaxConnections) {
        // Too many connections, accept and immediately close
        struct sockaddr_in clientAddr;
        SocketLength clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(mListenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
        }
        return;
    }

    // Accept new connection
    struct sockaddr_in clientAddr;
    SocketLength clientAddrLen = sizeof(clientAddr);
    SOCKET clientSocket = accept(mListenSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

    if (clientSocket == INVALID_SOCKET) {
        g_Logger.Log(MR_LOG_WARN, "accept() failed: %ld", WSAGetLastError());
        return;
    }

    // Create new client connection
    ClientConnection* pNewConn = new ClientConnection();
    pNewConn->mClientId = mNextClientId++;
    pNewConn->mTcpSocket = clientSocket;
    pNewConn->mConnected = TRUE;
    pNewConn->mConnectTime = time(NULL);
    pNewConn->mLastMessageTime = pNewConn->mConnectTime;
    pNewConn->mUdpAddr = clientAddr;

    mConnections[pNewConn->mClientId] = pNewConn;

    g_Logger.Log(MR_LOG_INFO, "New client connection: ID=%d from %s:%u",
                pNewConn->mClientId,
                inet_ntoa(clientAddr.sin_addr),
                ntohs(clientAddr.sin_port));
}

void MR_ServerSocket::ReceiveFromClient(ClientConnection* pConn, MR_RaceManager* pRaceManager)
{
    if (!pConn || pConn->mTcpSocket == INVALID_SOCKET) {
        return;
    }

    // TCP is a byte stream, not a message stream: a single recv() can deliver
    // zero, one, or several complete messages, plus a trailing partial one
    // (e.g. when the client sends several small messages back-to-back and the
    // kernel coalesces them, or a message straddles two packets). Buffer raw
    // bytes per-connection and drain only whole messages from the front.
    unsigned char recvChunk[4096];
    int bytesRead = recv(pConn->mTcpSocket, (char*)recvChunk, sizeof(recvChunk), 0);

    if (bytesRead <= 0) {
        // Connection closed or error
        g_Logger.Log(MR_LOG_WARN, "Client %d: Connection closed or recv error: %ld",
                     pConn->mClientId, WSAGetLastError());
        pConn->mConnected = FALSE;
        return;
    }

    // Update last message time
    pConn->mLastMessageTime = time(NULL);

    g_Logger.Log(MR_LOG_DEBUG, "Client %d: Received %d bytes", pConn->mClientId, bytesRead);

    pConn->mRecvBuffer.insert(pConn->mRecvBuffer.end(), recvChunk, recvChunk + bytesRead);

    // The message is: [uint16 header: datagramNum(8), datagramQueue(2), messageType(6)]
    //                [uint8 dataLen]
    //                [data...]
    while (pConn->mRecvBuffer.size() >= 3) {
    unsigned char buffer[258];  // Max 256 bytes for message buffer
    int messageDataLen = pConn->mRecvBuffer[2];
    int bytesReceived = 3 + messageDataLen;

    if (static_cast<int>(pConn->mRecvBuffer.size()) < bytesReceived) {
        break;  // Rest of this message hasn't arrived yet
    }

    memcpy(buffer, pConn->mRecvBuffer.data(), bytesReceived);
    pConn->mRecvBuffer.erase(pConn->mRecvBuffer.begin(), pConn->mRecvBuffer.begin() + bytesReceived);

    // For now, relay ALL messages to other players in the race
    // In production, you'd want to filter certain messages

    // Messages that should be broadcast to all players in race:
    // - MRNM_READY (51)
    // - MRNM_CREATE_MAIN_ELEM (2)
    // - MRNM_SET_MAIN_ELEM_STATE (3)
    // - MRNM_CREATE_AUTO_ELEM (4)
    // - MRNM_LAG_TEST (47)

    unsigned short messageHeader = static_cast<unsigned short>(buffer[0]) |
                                   (static_cast<unsigned short>(buffer[1]) << 8);
    int messageType = (messageHeader >> 10) & 0x3F;

    switch (messageType) {
        case 42:  // MRNM_GAME_NAME - Client is joining a race with this game name
        {
            // Extract game name from message
            unsigned char dataLen = static_cast<unsigned char>(messageDataLen);
            if (dataLen > 0 && dataLen < 256) {
                char gameName[256];
                memcpy(gameName, &buffer[3], dataLen);
                gameName[dataLen] = '\0';
                
                g_Logger.Log(MR_LOG_INFO, "Client %d joining game: %s", pConn->mClientId, gameName);

                // Each distinct game name is its own isolated race: the first client to
                // name it creates it (as a lobby "host" would), later clients with the
                // same name join it. The wire message only carries a name today, so a
                // freshly-created race uses fixed defaults; picking a track/lap count
                // when hosting would need a richer message than eRSMsgGameName.
                pConn->mRaceId = pRaceManager->FindOrCreateRace(
                    gameName, "ClassicH", 3, TRUE, pConn->mClientId);

                if (pConn->mRaceId < 0) {
                    g_Logger.Log(MR_LOG_WARN, "Client %d: could not join or create race '%s'",
                                 pConn->mClientId, gameName);
                    break;
                }

                snprintf(pConn->mPlayerName, sizeof(pConn->mPlayerName), "Player_%d", pConn->mClientId);
                pRaceManager->JoinRace(pConn->mRaceId, pConn->mClientId, pConn->mPlayerName);

                g_Logger.Log(MR_LOG_INFO, "Client %d assigned to race %d", pConn->mClientId, pConn->mRaceId);

                FinishJoiningRace(pConn, pRaceManager);
            } else {
                g_Logger.Log(MR_LOG_WARN, "Invalid GAME_NAME message length from client %d: %d", pConn->mClientId, dataLen);
            }
            break;
        }

        case 54:  // MRNM_HOST_RACE - create a race with explicit track/laps/weapons
        {
            // Payload: [1B trackLen][track][1B laps][1B weapons(0/1)][1B nameLen][name]
            const unsigned char* p = &buffer[3];
            const unsigned char* pEnd = &buffer[3] + messageDataLen;

            if (p >= pEnd) { g_Logger.Log(MR_LOG_WARN, "Client %d: empty HOST_RACE message", pConn->mClientId); break; }
            const unsigned char trackLen = *p++;
            if (p + trackLen + 2 > pEnd) { g_Logger.Log(MR_LOG_WARN, "Client %d: malformed HOST_RACE message", pConn->mClientId); break; }
            char trackName[64];
            const unsigned char clampedTrackLen = static_cast<unsigned char>(std::min<size_t>(trackLen, sizeof(trackName) - 1));
            memcpy(trackName, p, clampedTrackLen);
            trackName[clampedTrackLen] = '\0';
            p += trackLen;

            const unsigned char numLaps = *p++;
            const unsigned char weaponsAllowed = *p++;

            if (p >= pEnd) { g_Logger.Log(MR_LOG_WARN, "Client %d: HOST_RACE missing race name", pConn->mClientId); break; }
            const unsigned char nameLen = *p++;
            if (p + nameLen > pEnd) { g_Logger.Log(MR_LOG_WARN, "Client %d: HOST_RACE race name overruns message", pConn->mClientId); break; }
            char raceName[64];
            const unsigned char clampedNameLen = static_cast<unsigned char>(std::min<size_t>(nameLen, sizeof(raceName) - 1));
            memcpy(raceName, p, clampedNameLen);
            raceName[clampedNameLen] = '\0';

            static const char* const kValidTracks[] = {"ClassicH", "Steeplechase", "The Alley2", "The River"};
            bool trackOk = false;
            for (const char* t : kValidTracks) { if (strcmp(t, trackName) == 0) { trackOk = true; break; } }

            if (!trackOk || pRaceManager->FindRaceByName(raceName) >= 0) {
                g_Logger.Log(MR_LOG_WARN, "Client %d: HOST_RACE rejected (track_ok=%d, name='%s' taken=%d)",
                             pConn->mClientId, trackOk, raceName, pRaceManager->FindRaceByName(raceName) >= 0);
                MessageBuffer failMsg;
                failMsg.header = MakeMessageHeader(63);  // MRNM_JOINED_RACE, raceId=-1 means "failed"
                const int failId = -1;
                memcpy(&failMsg.data[0], &failId, sizeof(failId));
                failMsg.data[4] = 0;
                memcpy(&failMsg.data[5], &pConn->mClientId, sizeof(pConn->mClientId));
                failMsg.dataLen = 9;
                send(pConn->mTcpSocket, (const char*)&failMsg, 3 + failMsg.dataLen, 0);
                break;
            }

            pConn->mRaceId = pRaceManager->CreateRace(raceName, trackName, numLaps, weaponsAllowed ? TRUE : FALSE,
                                                       pConn->mClientId);
            if (pConn->mRaceId < 0) {
                g_Logger.Log(MR_LOG_WARN, "Client %d: could not create race '%s'", pConn->mClientId, raceName);
                break;
            }

            snprintf(pConn->mPlayerName, sizeof(pConn->mPlayerName), "Player_%d", pConn->mClientId);
            pRaceManager->JoinRace(pConn->mRaceId, pConn->mClientId, pConn->mPlayerName);
            g_Logger.Log(MR_LOG_INFO, "Client %d hosted race %d '%s': track=%s laps=%d weapons=%s",
                         pConn->mClientId, pConn->mRaceId, raceName, trackName, numLaps,
                         weaponsAllowed ? "yes" : "no");

            FinishJoiningRace(pConn, pRaceManager);
            break;
        }

        case 6:   // MRNM_CHAT_MESSAGE
        {
            // Chat is scoped to wherever the sender currently is: other members of
            // their race if they've joined one, or everyone else still browsing the
            // lobby (mRaceId == -1) if they haven't -- this is what makes lobby chat
            // work without a separate message type or connection state.
            g_Logger.Log(MR_LOG_INFO, "Client %d (Race %d): Relaying chat", pConn->mClientId, pConn->mRaceId);

            for (auto& pair : mConnections) {
                int targetId = pair.first;
                ClientConnection* pTarget = pair.second;

                if (pTarget && pTarget->mConnected && targetId != pConn->mClientId &&
                    pTarget->mRaceId == pConn->mRaceId) {
                    int sendResult = send(pTarget->mTcpSocket, (const char*)buffer, bytesReceived, 0);
                    if (sendResult == SOCKET_ERROR) {
                        g_Logger.Log(MR_LOG_WARN, "Failed to send chat to client %d: %ld", targetId, WSAGetLastError());
                    }
                }
            }
            break;
        }
        case 3:   // MRNM_SET_MAIN_ELEM_STATE
            if (messageDataLen < static_cast<int>(sizeof(pConn->mClientId))) {
                g_Logger.Log(MR_LOG_WARN, "Client %d sent an undersized player state", pConn->mClientId);
                break;
            }
            // Sender identity is connection metadata, not client-controlled state.
            // Replace the claimed id before relaying so one player cannot update
            // another player's craft by spoofing its envelope prefix.
            memcpy(&buffer[3], &pConn->mClientId, sizeof(pConn->mClientId));
            [[fallthrough]];
        case 51:  // MRNM_READY
        case 2:   // MRNM_CREATE_MAIN_ELEM
        case 4:   // MRNM_CREATE_AUTO_ELEM (missiles and other transient elements)
        case 47:  // MRNM_LAG_TEST
        {
            g_Logger.Log(MR_LOG_INFO, "Client %d (Race %d): Relaying message type %d to race members",
                         pConn->mClientId, pConn->mRaceId, messageType);

            // Broadcast this message to all OTHER clients in the SAME race
            if (pConn->mRaceId >= 0) {
                for (auto& pair : mConnections) {
                    int targetId = pair.first;
                    ClientConnection* pTarget = pair.second;

                    // Send to all clients in the same race EXCEPT the sender
                    if (pTarget && pTarget->mConnected && targetId != pConn->mClientId && pTarget->mRaceId == pConn->mRaceId) {
                        int sendResult = send(pTarget->mTcpSocket, (const char*)buffer, bytesReceived, 0);
                        if (sendResult == SOCKET_ERROR) {
                            g_Logger.Log(MR_LOG_WARN, "Failed to send message to client %d: %ld", targetId, WSAGetLastError());
                        } else {
                            g_Logger.Log(MR_LOG_DEBUG, "Relayed %d bytes from client %d to client %d",
                                         sendResult, pConn->mClientId, targetId);
                        }
                    }
                }
            } else {
                g_Logger.Log(MR_LOG_WARN, "Client %d sent message but not in any race (mRaceId=%d)",
                             pConn->mClientId, pConn->mRaceId);
            }
            break;
        }
        case 60:  // MRNM_LIST_GAMES - client wants the current lobby listing
        {
            std::vector<RaceSummary> races;
            pRaceManager->ListRaces(races);

            for (const RaceSummary& race : races) {
                MessageBuffer msg;
                msg.header = MakeMessageHeader(61);  // MRNM_GAME_INFO

                const unsigned char nameLen = static_cast<unsigned char>(
                    std::min<size_t>(race.mName.size(), 100));
                const unsigned char trackLen = static_cast<unsigned char>(
                    std::min<size_t>(race.mTrack.size(), 100));

                unsigned char* p = msg.data;
                const int raceId = race.mRaceId;
                memcpy(p, &raceId, sizeof(raceId));
                p += sizeof(raceId);
                *p++ = static_cast<unsigned char>(std::min(race.mNumPlayers, 255));
                *p++ = race.mStarted ? 1 : 0;
                *p++ = static_cast<unsigned char>(std::min(race.mNumLaps, 255));
                *p++ = nameLen;
                memcpy(p, race.mName.data(), nameLen);
                p += nameLen;
                *p++ = trackLen;
                memcpy(p, race.mTrack.data(), trackLen);
                p += trackLen;

                msg.dataLen = static_cast<unsigned char>(p - msg.data);
                const int msgSize = 3 + msg.dataLen;
                send(pConn->mTcpSocket, (const char*)&msg, msgSize, 0);
            }

            MessageBuffer endMsg;
            endMsg.header = MakeMessageHeader(62);  // MRNM_GAME_LIST_END
            endMsg.dataLen = 0;
            send(pConn->mTcpSocket, (const char*)&endMsg, 3, 0);
            break;
        }
        case 52:  // MRNM_START_RACE - only the race's creator may start it
        {
            RaceSession* pRace = (pConn->mRaceId >= 0) ? pRaceManager->GetRace(pConn->mRaceId) : nullptr;
            if (pRace == nullptr || !pRace->IsCreator(pConn->mClientId)) {
                g_Logger.Log(MR_LOG_WARN, "Client %d tried to start race %d but is not its creator",
                             pConn->mClientId, pConn->mRaceId);
                break;
            }

            pRaceManager->StartRace(pConn->mRaceId);
            g_Logger.Log(MR_LOG_INFO, "Race %d started by creator client %d", pConn->mRaceId, pConn->mClientId);

            // Broadcast to every player in the race, including the creator, so
            // everyone transitions from the lobby into the race on the same signal.
            MessageBuffer startedMsg;
            startedMsg.header = MakeMessageHeader(53);  // MRNM_RACE_STARTED
            startedMsg.dataLen = 0;
            for (auto& pair : mConnections) {
                ClientConnection* pTarget = pair.second;
                if (pTarget && pTarget->mConnected && pTarget->mRaceId == pConn->mRaceId) {
                    send(pTarget->mTcpSocket, (const char*)&startedMsg, 3, 0);
                }
            }
            break;
        }
        default:
            g_Logger.Log(MR_LOG_DEBUG, "Client %d: Message type %d (not broadcast)", pConn->mClientId, messageType);
    }
    }
}

void MR_ServerSocket::ReceiveDatagram()
{
    // TODO: Implement UDP datagram receive
    // Read datagram from mDatagramSocket
}

void MR_ServerSocket::FinishJoiningRace(ClientConnection* pConn, MR_RaceManager* pRaceManager)
{
    // Ack the join so the client knows its race id, whether it's the creator (only
    // the creator may later start the race), and its server-assigned client id.
    {
        RaceSession* pRace = pRaceManager->GetRace(pConn->mRaceId);
        MessageBuffer ackMsg;
        ackMsg.header = MakeMessageHeader(63);  // MRNM_JOINED_RACE
        memcpy(&ackMsg.data[0], &pConn->mRaceId, sizeof(pConn->mRaceId));
        ackMsg.data[4] = (pRace != nullptr && pRace->IsCreator(pConn->mClientId)) ? 1 : 0;
        memcpy(&ackMsg.data[5], &pConn->mClientId, sizeof(pConn->mClientId));
        ackMsg.dataLen = 9;
        send(pConn->mTcpSocket, (const char*)&ackMsg, 3 + ackMsg.dataLen, 0);
    }

    // Send CONN_NAME_SET messages for all other clients already in this race so
    // this client knows about the other players. The first field used to be a
    // fabricated "UDP port" left over from the old peer-to-peer protocol (nothing
    // ever used it -- there's no direct client-to-client connection here); it's the
    // peer's real client id now, which SetMainElemState relaying needs so a
    // multi-player race can tell whose position update is whose.
    for (auto& pair : mConnections) {
        int otherId = pair.first;
        ClientConnection* pOther = pair.second;

        if (pOther && pOther->mConnected && otherId != pConn->mClientId && pOther->mRaceId == pConn->mRaceId) {
            MessageBuffer msg;
            msg.header = MakeMessageHeader(44);  // MRNM_CONN_NAME_SET = 44

            memcpy(&msg.data[0], &otherId, sizeof(otherId));

            int nameLen = strlen(pOther->mPlayerName);
            memcpy(&msg.data[4], pOther->mPlayerName, nameLen);
            msg.dataLen = 4 + nameLen;

            int msgSize = 3 + msg.dataLen;
            send(pConn->mTcpSocket, (const char*)&msg, msgSize, 0);
        }
    }

    // Notify all OTHER clients in this race about the new player.
    for (auto& pair : mConnections) {
        int otherId = pair.first;
        ClientConnection* pOther = pair.second;

        if (pOther && pOther->mConnected && otherId != pConn->mClientId && pOther->mRaceId == pConn->mRaceId) {
            MessageBuffer msg;
            msg.header = MakeMessageHeader(44);  // MRNM_CONN_NAME_SET = 44

            memcpy(&msg.data[0], &pConn->mClientId, sizeof(pConn->mClientId));

            int nameLen = strlen(pConn->mPlayerName);
            memcpy(&msg.data[4], pConn->mPlayerName, nameLen);
            msg.dataLen = 4 + nameLen;

            int msgSize = 3 + msg.dataLen;
            send(pOther->mTcpSocket, (const char*)&msg, msgSize, 0);
        }
    }
}

void MR_ServerSocket::BroadcastToRace(
    int raceId,
    const void* pMessageData,
    int messageLength,
    int excludeClientId)
{
    // TODO: Send message to all clients in specified race
}

void MR_ServerSocket::SendToPlayer(
    int clientId,
    const void* pMessageData,
    int messageLength)
{
    // TODO: Send message to specific client
}

void MR_ServerSocket::CloseConnection(int clientId, MR_RaceManager* pRaceManager)
{
    auto it = mConnections.find(clientId);
    if (it != mConnections.end()) {
        ClientConnection* pConn = it->second;
        g_Logger.Log(MR_LOG_INFO, "Closing connection: ID=%d", clientId);
        if (pRaceManager && pConn->mRaceId >= 0) {
            pRaceManager->LeaveRace(pConn->mRaceId, clientId);
        }
        if (pConn->mTcpSocket != INVALID_SOCKET) {
            closesocket(pConn->mTcpSocket);
        }
        delete pConn;
        mConnections.erase(it);
    }
}

void MR_ServerSocket::Shutdown()
{
    // Close all client connections
    for (auto& pair : mConnections) {
        ClientConnection* pConn = pair.second;
        if (pConn) {
            if (pConn->mTcpSocket != INVALID_SOCKET) {
                closesocket(pConn->mTcpSocket);
            }
            delete pConn;
        }
    }
    mConnections.clear();

    // Close server sockets
    if (mListenSocket != INVALID_SOCKET) {
        closesocket(mListenSocket);
        mListenSocket = INVALID_SOCKET;
    }
    if (mDatagramSocket != INVALID_SOCKET) {
        closesocket(mDatagramSocket);
        mDatagramSocket = INVALID_SOCKET;
    }

    WSACleanup();
    g_Logger.Log(MR_LOG_INFO, "Server socket shutdown complete");
}
