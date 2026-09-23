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
    // messageType in bits 0-5, DatagramQueue in bits 6-7, DatagramNumber in bits 8-15
    return (messageType & 0x3F);  // Just the message type in lower 6 bits
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
                ReceiveFromClient(pConn);
                if (!pConn->IsAlive()) {
                    clientsToRemove.push_back(clientId);
                }
            }
        }

        // Remove dead connections
        for (int clientId : clientsToRemove) {
            CloseConnection(clientId);
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

    int selectResult = select(0, &listenSet, NULL, NULL, &timeout);
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

void MR_ServerSocket::ReceiveFromClient(ClientConnection* pConn)
{
    if (!pConn || pConn->mTcpSocket == INVALID_SOCKET) {
        return;
    }

    // Read message from TCP socket
    // Message format: 2-byte header + 1-byte data length + data
    unsigned char buffer[258];  // Max 256 bytes for message buffer
    
    int bytesReceived = recv(pConn->mTcpSocket, (char*)buffer, sizeof(buffer), 0);
    
    if (bytesReceived <= 0) {
        // Connection closed or error
        g_Logger.Log(MR_LOG_WARN, "Client %d: Connection closed or recv error: %ld", 
                     pConn->mClientId, WSAGetLastError());
        pConn->mConnected = FALSE;
        return;
    }

    // Update last message time
    pConn->mLastMessageTime = time(NULL);

    g_Logger.Log(MR_LOG_DEBUG, "Client %d: Received %d bytes", pConn->mClientId, bytesReceived);

    // The message is: [uint16 header: datagramNum(8), datagramQueue(2), messageType(6)]
    //                [uint8 dataLen]
    //                [data...]
    
    if (bytesReceived < 3) {
        g_Logger.Log(MR_LOG_WARN, "Client %d: Message too short (%d bytes)", pConn->mClientId, bytesReceived);
        return;
    }

    // Extract message type (bits 0-5 of byte 2, or byte 1 depending on endianness)
    // Assuming the format is: byte[0] = header low, byte[1] = header high, byte[2] = data_len
    int messageDataLen = buffer[2];
    
    // For now, relay ALL messages to other players in the race
    // In production, you'd want to filter certain messages
    
    // Messages that should be broadcast to all players in race:
    // - MRNM_READY (51)
    // - MRNM_CREATE_MAIN_ELEM (2) 
    // - MRNM_SET_MAIN_ELEM_STATE (3)
    // - MRNM_LAG_TEST (47)
    
    int messageType = buffer[1] & 0x3F;  // Lower 6 bits
    
    switch (messageType) {
        case 42:  // MRNM_GAME_NAME - Client is joining a race with this game name
        {
            // Extract game name from message
            unsigned char dataLen = buffer[0] & 0xFF;  // Data length is in first byte
            if (dataLen > 0 && dataLen < 256) {
                char gameName[256];
                memcpy(gameName, &buffer[2], dataLen);
                gameName[dataLen] = '\0';
                
                g_Logger.Log(MR_LOG_INFO, "Client %d joining game: %s", pConn->mClientId, gameName);
                
                // TODO: Parse game name to extract race ID and join the race
                // For now, use client ID as race ID (simple approach)
                pConn->mRaceId = 0;  // Clients with game names join race 0
                
                g_Logger.Log(MR_LOG_INFO, "Client %d assigned to race %d", pConn->mClientId, pConn->mRaceId);
                
                // Now send CONN_NAME_SET messages for all other clients in this race
                // so this client knows about the other players
                g_Logger.Log(MR_LOG_INFO, "Sending player list to client %d for race %d", pConn->mClientId, pConn->mRaceId);
                
                for (auto& pair : mConnections) {
                    int otherId = pair.first;
                    ClientConnection* pOther = pair.second;
                    
                    // Send info about other connected clients in same race (excluding self)
                    if (pOther && pOther->mConnected && otherId != pConn->mClientId && pOther->mRaceId == pConn->mRaceId) {
                        // Build CONN_NAME_SET message: [4 bytes UDP port][player name]
                        MessageBuffer msg;
                        msg.header = MakeMessageHeader(44);  // MRNM_CONN_NAME_SET = 44
                        
                        // UDP port (4 bytes) - using a default port for now
                        unsigned int udpPort = 9601 + otherId;
                        *(unsigned int*)&msg.data[0] = udpPort;
                        
                        // Player name - for now use a generic name with client ID
                        char playerName[64];
                        snprintf(playerName, sizeof(playerName), "Player_%d", otherId);
                        int nameLen = strlen(playerName);
                        memcpy(&msg.data[4], playerName, nameLen);
                        
                        // Set data length: 4 (UDP port) + nameLen
                        msg.dataLen = 4 + nameLen;
                        
                        // Send to the new client
                        int msgSize = 3 + msg.dataLen;  // header(2) + dataLen(1) + data
                        send(pConn->mTcpSocket, (const char*)&msg, msgSize, 0);
                        g_Logger.Log(MR_LOG_DEBUG, "Sent player info for client %d to client %d", otherId, pConn->mClientId);
                    }
                }
                
                // Now notify all OTHER clients in this race about the new player
                g_Logger.Log(MR_LOG_INFO, "Broadcasting new player client %d to other race members", pConn->mClientId);
                
                for (auto& pair : mConnections) {
                    int otherId = pair.first;
                    ClientConnection* pOther = pair.second;
                    
                    // Send info about new client to other connected clients in same race
                    if (pOther && pOther->mConnected && otherId != pConn->mClientId && pOther->mRaceId == pConn->mRaceId) {
                        // Build CONN_NAME_SET message for the new client: [4 bytes UDP port][player name]
                        MessageBuffer msg;
                        msg.header = MakeMessageHeader(44);  // MRNM_CONN_NAME_SET = 44
                        
                        // UDP port (4 bytes) - using a default port for now
                        unsigned int udpPort = 9601 + pConn->mClientId;
                        *(unsigned int*)&msg.data[0] = udpPort;
                        
                        // Player name - for now use a generic name with client ID
                        char playerName[64];
                        snprintf(playerName, sizeof(playerName), "Player_%d", pConn->mClientId);
                        int nameLen = strlen(playerName);
                        memcpy(&msg.data[4], playerName, nameLen);
                        
                        // Set data length: 4 (UDP port) + nameLen
                        msg.dataLen = 4 + nameLen;
                        
                        // Send to the existing client
                        int msgSize = 3 + msg.dataLen;  // header(2) + dataLen(1) + data
                        send(pOther->mTcpSocket, (const char*)&msg, msgSize, 0);
                        g_Logger.Log(MR_LOG_DEBUG, "Notified client %d about new player client %d", otherId, pConn->mClientId);
                    }
                }
                
            } else {
                g_Logger.Log(MR_LOG_WARN, "Invalid GAME_NAME message length from client %d: %d", pConn->mClientId, dataLen);
            }
            break;
        }
        
        case 51:  // MRNM_READY
        case 2:   // MRNM_CREATE_MAIN_ELEM
        case 3:   // MRNM_SET_MAIN_ELEM_STATE
        case 47:  // MRNM_LAG_TEST
        case 6:   // MRNM_CHAT_MESSAGE
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
        default:
            g_Logger.Log(MR_LOG_DEBUG, "Client %d: Message type %d (not broadcast)", pConn->mClientId, messageType);
    }
}

void MR_ServerSocket::ReceiveDatagram()
{
    // TODO: Implement UDP datagram receive
    // Read datagram from mDatagramSocket
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

void MR_ServerSocket::CloseConnection(int clientId)
{
    auto it = mConnections.find(clientId);
    if (it != mConnections.end()) {
        ClientConnection* pConn = it->second;
        g_Logger.Log(MR_LOG_INFO, "Closing connection: ID=%d", clientId);
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
