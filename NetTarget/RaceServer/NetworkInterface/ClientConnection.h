// ClientConnection.h : Per-player connection state

#pragma once

#define MR_MAX_PLAYER_NAME 20
#define MR_CONNECTION_TIMEOUT 30000  // 30 seconds in milliseconds

// Fixed-window rate limits: each client gets at most N attempts of a given
// kind in any window-seconds span, then has to wait for the next window.
// Crude compared to a sliding-window/token-bucket limiter (a burst right at a
// window boundary can let through close to 2x the nominal rate), but enough
// to stop a single client from flooding chat or hammering race creation
// without tracking per-message history.
// Deliberately generous on the chat burst count: RaceServerClientSmoke.cpp
// fires 25 chat messages back-to-back with no delay to test TCP coalescing/
// fragmentation handling, which is legitimate traffic, not abuse -- 30/3s
// clears that comfortably while still capping *sustained* flooding to 10
// messages/sec, far beyond normal typing speed.
#define MR_CHAT_RATE_LIMIT_COUNT 30
#define MR_CHAT_RATE_LIMIT_WINDOW_SEC 3
#define MR_RACE_CREATE_RATE_LIMIT_COUNT 3
#define MR_RACE_CREATE_RATE_LIMIT_WINDOW_SEC 10

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
    BOOL mProtocolNegotiated;
    // Maximum payload this peer advertised in its protocol hello. Legacy
    // clients receive the deployed one-byte-frame maximum.
    unsigned short mMaxPayload;

    // Rate-limit window state -- see CheckRateLimit below. Zero-initialized by
    // the constructor; the first call to either Allow* method starts its window.
    time_t mChatWindowStart;
    int mChatCountInWindow;
    time_t mRaceCreateWindowStart;
    int mRaceCreateCountInWindow;

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

    // Shared implementation for the two rate limits below: at most pMaxCount
    // attempts within any pWindowSec-second span starting from pWindowStart. A
    // still-zero pWindowStart (a connection that has never made this kind of
    // attempt) is treated as "start a window now", not "window opened at the
    // Unix epoch", so a freshly-connected client isn't already deep into an
    // ancient window on its very first attempt.
    static BOOL CheckRateLimit(time_t& pWindowStart, int& pCountInWindow, time_t pNow,
                                int pMaxCount, int pWindowSec)
    {
        if (pWindowStart == 0 || pNow - pWindowStart >= pWindowSec) {
            pWindowStart = pNow;
            pCountInWindow = 0;
        }
        if (pCountInWindow >= pMaxCount) {
            return FALSE;
        }
        ++pCountInWindow;
        return TRUE;
    }

    BOOL AllowChatMessage(time_t pNow)
    {
        return CheckRateLimit(mChatWindowStart, mChatCountInWindow, pNow,
                              MR_CHAT_RATE_LIMIT_COUNT, MR_CHAT_RATE_LIMIT_WINDOW_SEC);
    }

    BOOL AllowRaceCreation(time_t pNow)
    {
        return CheckRateLimit(mRaceCreateWindowStart, mRaceCreateCountInWindow, pNow,
                              MR_RACE_CREATE_RATE_LIMIT_COUNT, MR_RACE_CREATE_RATE_LIMIT_WINDOW_SEC);
    }
};
