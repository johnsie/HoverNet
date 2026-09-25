// ServerSocket.h : TCP/UDP server socket for handling connections

#pragma once

#include "ClientConnection.h"

class MR_RaceManager;  // Forward declaration

class MR_ServerSocket
{
public:
    MR_ServerSocket();
    ~MR_ServerSocket();

    // Initialize server on specified port
    BOOL Initialize(unsigned port, int maxConnections = 40);

    // Process incoming connections and messages
    void ProcessEvents(MR_RaceManager* pRaceManager);

    // Broadcast message to all players in a race
    void BroadcastToRace(
        int raceId,
        const void* pMessageData,
        int messageLength,
        int excludeClientId = -1);

    // Send message to specific player
    void SendToPlayer(
        int clientId,
        const void* pMessageData,
        int messageLength);

    // Close a player connection. Removes the player from its race when pRaceManager
    // is given, so lobby listings (player counts) don't go stale after a disconnect.
    void CloseConnection(int clientId, MR_RaceManager* pRaceManager = nullptr);

    // Shutdown server
    void Shutdown();

private:
    SOCKET mListenSocket;
    SOCKET mDatagramSocket;
    std::map<int, ClientConnection*> mConnections;
    int mNextClientId;
    int mMaxConnections;
    unsigned mPort;

    // Helper methods
    void AcceptNewConnection();
    void ReceiveFromClient(ClientConnection* pConn, MR_RaceManager* pRaceManager);
    void ReceiveDatagram();
    BOOL SetSocketOptions(SOCKET sock);

    // Shared tail end of both "join by name" (eRSMsgGameName) and "host with
    // settings" (eRSMsgHostRace): acks the join (with host status) and exchanges
    // eRSMsgConnNameSet with every other player already in pConn->mRaceId.
    void FinishJoiningRace(ClientConnection* pConn, MR_RaceManager* pRaceManager);

    // Tells every other client still browsing the lobby (mRaceId == -1) that
    // clientId is no longer there -- either they disconnected or they just
    // joined/hosted a race. No-op if clientId was never announced (never sent
    // eRSMsgSetPlayerName), matching who ListLobbyUsers would have shown them to.
    void BroadcastLobbyUserLeft(int clientId);
};
