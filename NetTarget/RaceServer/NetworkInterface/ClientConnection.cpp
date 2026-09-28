// ClientConnection.cpp : Client connection implementation

#include "stdafx.h"
#include "ClientConnection.h"

ClientConnection::ClientConnection()
    : mClientId(-1),
      mTcpSocket(INVALID_SOCKET),
      mPartialFrameStart(0),
      mRaceId(-1),
      mRaceStarted(FALSE),
      mAvgLag(0),
      mMinLag(0),
      mNbLagTests(0),
      mTotalLag(0),
      mConnectTime(0),
      mLastMessageTime(0),
      mDisconnectTimeoutSec(MR_CONNECTION_TIMEOUT / 1000),
      mConnected(FALSE),
      mAuthenticated(FALSE),
      mProtocolNegotiated(FALSE),
      mMaxPayload(255),
      mChatWindowStart(0),
      mChatCountInWindow(0),
      mRaceCreateWindowStart(0),
      mRaceCreateCountInWindow(0),
      mInvalidMessageWindowStart(0),
      mInvalidMessageCountInWindow(0)
{
    mPlayerName[0] = '\0';
    memset(&mUdpAddr, 0, sizeof(mUdpAddr));
}

ClientConnection::~ClientConnection()
{
    if (mTcpSocket != INVALID_SOCKET) {
        closesocket(mTcpSocket);
        mTcpSocket = INVALID_SOCKET;
    }
}
