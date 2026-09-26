// ClientConnection.h : Per-player connection state

#pragma once

#define MR_MAX_PLAYER_NAME 20
#define MR_CONNECTION_TIMEOUT 30000  // 30 seconds in milliseconds

class ClientConnection
{
public:
    ClientConnection();
    ~ClientConnection();

    // Connection identifiers
    int mClientId;
    char mPlayerName[MR_MAX_PLAYER_NAME + 1];
    
    // Network state
    SOCKET mTcpSocket;
    struct sockaddr_in mUdpAddr;

    // Bytes read from mTcpSocket that haven't formed a complete message yet
    // (TCP is a byte stream: one recv() can contain multiple messages, part
    // of a message, or a mix of both).
    std::vector<unsigned char> mRecvBuffer;
    
    // Race participation
    int mRaceId;
    // False while mRaceId's race is still a waiting room (hosted/joined but not
    // yet started) -- chat is scoped by mRaceStarted, not mRaceId, so players
    // filling a waiting room stay part of the wider lobby conversation, and only
    // narrow down to their own race once it actually gets under way.
    BOOL mRaceStarted;
    
    // Lag statistics
    int mAvgLag;                      // Average latency in ms
    int mMinLag;                      // Minimum latency in ms
    int mNbLagTests;                  // Number of ping tests performed
    int mTotalLag;                    // Sum for averaging

    // Connection timestamps
    time_t mConnectTime;
    time_t mLastMessageTime;
    
    // State
    BOOL mConnected;
    BOOL mAuthenticated;

    // Helper methods
    BOOL IsAlive() const 
    { 
        return mConnected && (time(NULL) - mLastMessageTime < MR_CONNECTION_TIMEOUT / 1000); 
    }

    void UpdateLagStats(int pingMs)
    {
        mTotalLag += pingMs;
        mNbLagTests++;
        if (mMinLag == 0 || pingMs < mMinLag) {
            mMinLag = pingMs;
        }
        mAvgLag = mTotalLag / mNbLagTests;
    }

    void ResetLagStats()
    {
        mAvgLag = 0;
        mMinLag = 0;
        mNbLagTests = 0;
        mTotalLag = 0;
    }
};
