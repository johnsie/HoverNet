// ClientConnection.cpp : Client connection implementation

#include "stdafx.h"
#include "ClientConnection.h"

ClientConnection::ClientConnection()
    : mClientId(-1),
      mTcpSocket(INVALID_SOCKET),
      mRaceId(-1),
      mRaceStarted(FALSE),
      mAvgLag(0),
      mMinLag(0),
      mNbLagTests(0),
      mTotalLag(0),
      mConnectTime(0),
      mLastMessageTime(0),
      mConnected(FALSE),
      mAuthenticated(FALSE)
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
