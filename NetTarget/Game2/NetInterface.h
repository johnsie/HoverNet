// NetInterface.h
//
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.
//
// A copy of the license should have been attached to the package from which 
// you have taken this file. If you can not find the license you can not use 
// this file.
//
//
// The author makes no representations about the suitability of
// this software for any purpose.  It is provided "as is" "AS IS",
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.
//
// See the License for the specific language governing permissions 
// and limitations under the License.
//
//

#ifndef NET_INTERFACE_H
#define NET_INTERFACE_H

#include <WINSOCK.h>

#include "../Util/MR_Types.h"


#define MR_DEFAULT_NET_PORT  9530


#define MR_OUT_QUEUE_LEN       2048
#define MR_MAX_NET_MESSAGE_LEN 255

#define MR_NET_REQUIRED         1
#define MR_NET_TRY              2
#define MR_NOT_REQUIRED         0
#define MR_NET_DATAGRAM        -1

// Phase 4: Connection mode support
enum MR_ConnectionMode
{
   MR_CONNECTION_PEER_TO_PEER,    // Traditional P2P hosting
   MR_CONNECTION_SERVER_HOSTED    // Centralized RaceServer
};

class MR_NetMessageBuffer
{
   public:
      // MR_UInt16  mSendingTime:10; // 4ms of precision on a +-2sec range (if a datagram take more than 2 sec to travel..it will be droped (UDP only)
                                     // Not used yet
      MR_UInt16  mDatagramNumber:8;  // used only for datagram
      MR_UInt16  mDatagramQueue:2;   // used only for datagram
      MR_UInt16  mMessageType:6;
      MR_UInt8   mDataLen;
      MR_UInt8   mData[ MR_MAX_NET_MESSAGE_LEN ];

      // int MessageLen()const { return mDataLen+5; }
};

#define MR_NET_HEADER_LEN  (sizeof( MR_NetMessageBuffer )-MR_MAX_NET_MESSAGE_LEN)
#define MR_TCP_INPUT_QUEUE_LEN (64*1024)


class MR_NetworkPort
{
   private:
      // CString mNetAddr;
      // int     mPort;
      SOCKET  mSocket;      

      // UDP Information
      SOCKET        mUDPRecvSocket;
      SOCKADDR_IN   mUDPRemoteAddr;

      MR_UInt8      mLastSendedDatagramNumber[4];
      MR_UInt8      mLastReceivedDatagramNumber[4];

      int           mWatchdog;

      // Lag computation
      int     mAvgLag;
      int     mMinLag;
      int     mNbLagTest;
      int     mTotalLag;

      int                 mInputMessageBufferIndex;
      MR_NetMessageBuffer mInputMessageBuffer;

      // TCP is a byte stream: one recv() may contain part of a message or many
      // messages. Keep unread bytes until Poll() can extract a complete frame.
      MR_UInt8            mTCPInputQueue[ MR_TCP_INPUT_QUEUE_LEN ];
      int                 mTCPInputQueueLen;

      // Output queue
      // This queue is only used for messages that must absolutly be sent
      // or those that are parially sended
      MR_UInt8            mOutQueue[ MR_OUT_QUEUE_LEN ]; 
      int                 mOutQueueLen;
      int                 mOutQueueHead;

   public:
      MR_NetworkPort();
      ~MR_NetworkPort();

      void Connect( SOCKET pSocket  );
      void SetRemoteUDPPort( unsigned int pPort );
      unsigned int GetUDPPort()const;
      void Disconnect();
      BOOL IsConnected()const;

      SOCKET GetSocket()const;
      SOCKET GetUDPSocket()const;

      const MR_NetMessageBuffer* Poll( );
      void                       Send( const MR_NetMessageBuffer* pMessage, int pReqLevel );
      BOOL                       UDPSend( SOCKET pSocket, MR_NetMessageBuffer* pMessage, unsigned pQueueId, BOOL pResendLast );


      // Time related stuff
      BOOL  AddLagSample( int pLag );
      BOOL  LagDone()const;

      int           GetAvgLag()const;
      int           GetMinLag()const;
      void          SetLag( int pAvgLag, int pMinLag );

};



class MR_NetworkInterface
{
   public:
      enum
      { 
         eMaxClient = 8
      };


   private:


      CString  mPlayer;
      int      mId;
      BOOL     mServerMode;
      SOCKET   mRegistrySocket;
      int      mServerPort;
      CString  mServerAddr;
      CString  mGameName;

      // Phase 4: Connection mode tracking
      MR_ConnectionMode mConnectionMode;
      CString          mRaceServerAddr;
      unsigned         mRaceServerPort;
      BOOL             mHostRaceRequest;
      CString          mHostedTrack;
      int              mHostedLaps;
      BOOL             mHostedWeapons;

      // UDP port
      SOCKET   mUDPOutShortPort;
      SOCKET   mUDPOutLongPort;

      // Data
      MR_NetworkPort mClient[ eMaxClient ];
      CString        mClientName[ eMaxClient ];
      BOOL           mAllPreLoguedRecv; // Used by client to know if all prelogued have been received
      BOOL           mCanBePreLogued[ eMaxClient ];
      BOOL           mPreLoguedClient[ eMaxClient ];
      BOOL           mConnected[ eMaxClient ];       // Correctly connected state used by the server
      DWORD          mClientAddr[ eMaxClient ];      // Used only in server mode
      int            mClientPort[ eMaxClient ];      // Used only in server mode

      int            mReturnMessage; // Message to return to the parent window in modeless mode

      BOOL           mGameReady;     // TRUE when game is ready to start (after MRNM_READY received)
      int            mLocalClientId; // This player's client ID in server-hosted mode (-1 if not assigned)
      int            mNextClientSlot; // Next slot to assign for server-hosted joining players (starts at 1)
      int            mServerPeerId[ eMaxClient ]; // RaceServer client id represented by each local player slot

      // Dialog functions
      static MR_NetworkInterface* mActiveInterface;
      static BOOL CALLBACK ServerPortCallBack(   HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );
      static BOOL CALLBACK ServerAddrCallBack(   HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );
      static BOOL CALLBACK WaitGameNameCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );
      static BOOL CALLBACK ListCallBack(         HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );

      // Helper func
      void SendConnectionDoneIfNeeded();
      int  RegisterServerPeer( int pServerClientId, const char* pName = NULL, int pNameLen = 0 );

   public:
      // Creation and destruction
      MR_NetworkInterface(); 
      ~MR_NetworkInterface();

      void  SetPlayerName( const char* pPlayerName );
      const char* GetPlayerName()const;

      // Phase 4: Set connection mode for server-hosted races
      void  SetConnectionMode( MR_ConnectionMode pMode, const char* pServerAddr = NULL, unsigned pServerPort = 0 );
      MR_ConnectionMode GetConnectionMode()const;
      void  ConfigureHostedRace( const char* pTrack, int pLaps, BOOL pWeapons );

      // Game creator flag accessors
      void  SetIsGameCreator( BOOL pIsCreator );
      BOOL  GetIsGameCreator()const;

      // Game ready flag accessors
      void  SetGameReady( BOOL pReady );
      BOOL  GetGameReady()const;

      // Signal that game is ready to start (for server-hosted mode)
      void  SignalGameReady();

      // Get this player's client ID in server-hosted mode
      int   GetLocalClientId()const;
      void  SetLocalClientId( int pClientId );
      int   GetServerPeerId( int pSlot )const;
      int   GetServerRaceSlot( int pServerClientId )const;

      BOOL MasterConnect( HWND pWindow, const char* pGameName, BOOL pPromptForPort = TRUE, unsigned pDefaultPort = MR_DEFAULT_NET_PORT, HWND* pModalessDlg = NULL, int pReturnMessage = 0);
      BOOL SlavePreConnect( HWND pWindow, CString& pGameName );
      BOOL SlaveConnect( HWND pWindow, const char* pServerIP=NULL, unsigned pPort = MR_DEFAULT_NET_PORT, const char* pGameName = NULL, HWND* pModalessDlg = NULL, int pReturnMessage = 0 );

      void Disconnect();

      int  GetClientCount()const;
      int  GetId()const;

      int  GetLagFromServer()const;
      int  GetAvgLag( int pClient )const;
      int  GetMinLag( int pClient )const;

      BOOL UDPSend( int pClient, MR_NetMessageBuffer* pMessage, BOOL pLongPort, BOOL pResendLast = FALSE ); // return TRUE if queue not full
      BOOL BroadcastMessage( MR_NetMessageBuffer* pMessage, int pReqLevel );
      // BOOL BroadcastMessage( DWORD  pTimeStamp, int  pMessageType, int pMessageLen, const MR_UInt8* pMessage );
      BOOL FetchMessage(     DWORD& pTimeStamp, int& pMessageType, int& pMessageLen, const MR_UInt8*& pMessage, int& pClientId ); // pTimeStamp must be set to current time stamp before fetch

      const char* GetPlayerName( int pIndex )const;
      BOOL        IsConnected( int pIndex )const;

   private:
      BOOL     mIsGameCreator;  // TRUE if this player created the hosted game
      
};


      
#endif



