// InternetRoom.cpp
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

#include "stdafx.h"

#include "InternetRoom.h"
#include "MatchReport.h"
#include "resource.h"
#include "../Util/StrRes.h"
#include "../LinuxClient/RaceServerClient.h"
#include <cstdarg>
#include <map>
#include <string>
#include <vector>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")


#define MRM_DNS_ANSWER        (WM_USER+1)
#define MRM_NET_EVENT         (WM_USER+7)
#define MRM_DLG_END_ADD       (WM_USER+10)
#define MRM_DLG_END_JOIN      (WM_USER+11)

#define MRM_BIN_BUFFER_SIZE    25000 // 25 K this is BIG enough


#define REFRESH_DELAY       1000
#define REFRESH_TIMEOUT    11000
#define OP_TIMEOUT         22000
#define FAST_OP_TIMEOUT     6000
#define CHAT_TIMEOUT       18000
#define SCORE_OP_TIMEOUT   12000


#define IMMEDIATE                 1
#define REFRESH_EVENT             1
#define REFRESH_TIMEOUT_EVENT     2
#define CHAT_TIMEOUT_EVENT        3
#define OP_TIMEOUT_EVENT          4

#define LOAD_BANNER_TIMEOUT_EVENT     8
#define ANIM_BANNER_TIMEOUT_EVENT     9

#define MR_IR_LIST "steeky.com/hover/roomlist2.txt"

#define MR_IR_LIST_PORT 443

// #endif

#define MR_MAX_SERVER_ENTRIES  12
#define MR_MAX_BANNER_ENTRIES  10

#define MR_HTTP_SCORE_SERVER     0
#define MR_HTTP_ROOM_SERVER      1
#define MR_HTTP_LADDER_ROOM      2
#define MR_NREG_BANNER_SERVER    8
#define MR_REG_BANNER_SERVER     9


class MR_InternetServerEntry
{
public:
   CString        mName;
   int            mType;
   unsigned long  mAddress;
   unsigned       mPort;
   CString        mURL;
   unsigned long  mLadderIP;
   unsigned       mLadderPort;
   CString        mLadderReportURL;

};

class MR_BannerServerEntry: public MR_InternetServerEntry
{
public:
   int            mDelay; //in sec
   CString        mClickURL; 
   BOOL           mIndirectClick; // Load location first
   CString        mLastCookie;
};

MR_InternetServerEntry gScoreServer;
MR_InternetServerEntry gServerList[MR_MAX_SERVER_ENTRIES];
MR_BannerServerEntry   gBannerList[MR_MAX_SERVER_ENTRIES];

int gNbServerEntries    = 0;
int gCurrentServerEntry = -1;
int gNbBannerEntries    = 0;
int gCurrentBannerEntry = 0;


CString gMainServer = MR_IR_LIST;

MR_InternetRoom* MR_InternetRoom::mThis = NULL;

namespace
{
   const char* const kDefaultRaceServerHost = "outiva.com";
   const unsigned kDefaultRaceServerPort = 9600;
   const UINT_PTR kRaceServerRefreshTimer = 2001;
   RaceServerClient gRaceServerLobby;
   std::vector<RaceServerGameInfo> gRaceServerGames;
   std::vector<RaceServerGameInfo> gGamesBeingListed;
   CString gRaceServerHost = kDefaultRaceServerHost;
   unsigned gRaceServerPort = kDefaultRaceServerPort;

   // Forward declarations: DrainRaceServerMessages (below) calls both of these,
   // but they're defined further down the file.
   void RefreshRaceServerSelection(HWND pWindow);
   void RenderGameList(HWND pWindow);

   // Chat messages only carry the sender's client id (see ServerSocket.cpp's
   // MRNM_CHAT_MESSAGE relay), and IDC_USER_LIST needs a way to add/remove one
   // specific row by id -- this is that id-to-name map, rebuilt fresh each time
   // the lobby dialog is opened.
   std::map<int, CString> gLobbyUserNames;

   int FindUserListRow(HWND pList, int pClientId)
   {
      LVFINDINFO lFind = {};
      lFind.flags = LVFI_PARAM;
      lFind.lParam = static_cast<LPARAM>(pClientId);
      return ListView_FindItem(pList, -1, &lFind);
   }

   void AddLobbyUser(HWND pWindow, int pClientId, const CString& pName)
   {
      HWND lList = GetDlgItem(pWindow, IDC_USER_LIST);
      if( FindUserListRow(lList, pClientId) >= 0 ) return;
      LV_ITEM lItem = {};
      lItem.mask = LVIF_TEXT | LVIF_PARAM;
      lItem.iItem = ListView_GetItemCount(lList);
      lItem.pszText = (char*)(const char*)pName;
      lItem.lParam = static_cast<LPARAM>(pClientId);
      ListView_InsertItem(lList, &lItem);
      gLobbyUserNames[pClientId] = pName;
   }

   void RemoveLobbyUser(HWND pWindow, int pClientId)
   {
      HWND lList = GetDlgItem(pWindow, IDC_USER_LIST);
      const int lRow = FindUserListRow(lList, pClientId);
      if( lRow >= 0 ) ListView_DeleteItem(lList, lRow);
      gLobbyUserNames.erase(pClientId);
   }

   // Only rebuilds IDC_GAME_LIST from whatever's already in gRaceServerGames --
   // does not touch the network. Call sites request a refresh by sending
   // eRSMsgListGames directly and clearing gGamesBeingListed; the actual list
   // repaint happens once eRSMsgGameListEnd arrives, in DrainRaceServerMessages.
   //
   // This used to be one function that both sent the request and blocked (via
   // RaceServerClient::ListGames) waiting for the reply -- but that helper's poll
   // loop silently discards any message that isn't part of the game list, so a
   // chat or lobby-roster message arriving during that wait was lost before
   // DrainRaceServerMessages ever got a chance to see it. Splitting the request
   // from the repaint lets both share one drain loop instead, exactly like the
   // Linux lobby already does.
   void RenderGameList(HWND pWindow)
   {
      HWND lList = GetDlgItem(pWindow, IDC_GAME_LIST);
      ListView_DeleteAllItems(lList);
      for( size_t i = 0; i < gRaceServerGames.size(); ++i )
      {
         const RaceServerGameInfo& lGame = gRaceServerGames[i];
         CString lLabel;
         lLabel.Format("%s  [%d player%s]%s", lGame.mName.c_str(), lGame.mNumPlayers,
                       lGame.mNumPlayers == 1 ? "" : "s", lGame.mStarted ? " (started)" : "");
         LV_ITEM lItem = {};
         lItem.mask = LVIF_TEXT | LVIF_PARAM;
         lItem.iItem = static_cast<int>(i);
         lItem.pszText = (char*)(const char*)lLabel;
         lItem.lParam = static_cast<LPARAM>(i);
         ListView_InsertItem(lList, &lItem);
      }
      if( !gRaceServerGames.empty() )
      {
         ListView_SetItemState(lList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
      }
   }

   void RequestGameListRefresh(HWND)
   {
      gGamesBeingListed.clear();
      gRaceServerLobby.SendMessage(eRSMsgListGames, nullptr, 0);
   }

   // Shares NetJoin_Debug.log with NetInterface.cpp's WaitGameNameCallBack breadcrumbs so
   // the whole join sequence -- from clicking Join to the RaceServer ack -- reads as one
   // timeline when diagnosing the "Retrieving game info..." hang.
   void LogNetJoin( const char* pFormat, ... )
   {
      FILE* lLog = NULL;
      if( lLog != NULL )
      {
         va_list lArgs;
         va_start( lArgs, pFormat );
         vfprintf( lLog, pFormat, lArgs );
         va_end( lArgs );
         fprintf( lLog, "\n" );
         fclose( lLog );
      }
   }

   int SelectedRaceIndex(HWND pWindow)
   {
      return ListView_GetNextItem(GetDlgItem(pWindow, IDC_GAME_LIST), -1, LVNI_SELECTED);
   }

   void RefreshRaceServerSelection(HWND pWindow)
   {
      const int lIndex = SelectedRaceIndex(pWindow);
      if( lIndex < 0 || lIndex >= static_cast<int>(gRaceServerGames.size()) ) return;
      const RaceServerGameInfo& lGame = gRaceServerGames[lIndex];
      SetDlgItemTextA(pWindow, IDC_TRACK_NAME, lGame.mTrack.c_str());
      SetDlgItemInt(pWindow, IDC_NB_LAP, lGame.mNumLaps, FALSE);
      SetDlgItemTextA(pWindow, IDC_WEAPONS, "Server");
      SetDlgItemTextA(pWindow, IDC_AVAIL_MESSAGE, lGame.mStarted ? "Race already started" : "Ready to join");
      CString lPlayers;
      lPlayers.Format("%d player%s connected", lGame.mNumPlayers, lGame.mNumPlayers == 1 ? "" : "s");
      SetDlgItemText(pWindow, IDC_PLAYER_LIST, lPlayers);
   }
}

// Drains every message currently waiting rather than acting on one -- lobby
// roster updates and chat share this connection with the game-list refresh and
// can arrive interleaved between timer ticks. Defined here as an actual member
// (unlike RenderGameList/RequestGameListRefresh/etc. just above, which are free
// functions in the anonymous namespace) because it needs mThis/mUser -- both
// protected, reachable only from the class's own member functions no matter how
// a free function tries to qualify them. It can still see everything declared in
// that anonymous namespace above (gRaceServerLobby, AddLobbyUser, RenderGameList,
// ...): those names stay visible for the rest of this translation unit.
void MR_InternetRoom::DrainRaceServerMessages(HWND pWindow)
{
   RaceServerMessage lMessage;
   while( gRaceServerLobby.PollMessage(lMessage, 0) )
   {
      if( lMessage.mType == eRSMsgLobbyUserPresent )
      {
         RaceServerPeer lPeer;
         if( RaceServerClient::ParsePeer(lMessage, lPeer) )
         {
            AddLobbyUser(pWindow, lPeer.mClientId, lPeer.mName.c_str());
         }
      }
      else if( lMessage.mType == eRSMsgLobbyUserLeft )
      {
         int lClientId = -1;
         if( RaceServerClient::ParseLobbyUserLeft(lMessage, lClientId) )
         {
            RemoveLobbyUser(pWindow, lClientId);
         }
      }
      else if( lMessage.mType == eRSMsgChatMessage )
      {
         int lSenderId = -1;
         std::string lText;
         if( RaceServerClient::ParseChatMessage(lMessage, lSenderId, lText) )
         {
            CString lName = "Someone";
            std::map<int, CString>::iterator lIt = gLobbyUserNames.find(lSenderId);
            if( lIt != gLobbyUserNames.end() ) lName = lIt->second;

            char lChatBuffer[4096] = { 0 };
            GetDlgItemTextA(pWindow, IDC_CHAT_OUT, lChatBuffer, sizeof(lChatBuffer));
            CString lChat = lChatBuffer;
            lChat += "\r\n";
            lChat += lName;
            lChat += ": ";
            lChat += lText.c_str();
            SetDlgItemText(pWindow, IDC_CHAT_OUT, lChat);
         }
      }
      else if( lMessage.mType == eRSMsgGameInfo )
      {
         RaceServerGameInfo lInfo;
         if( RaceServerClient::ParseGameInfo(lMessage, lInfo) )
         {
            gGamesBeingListed.push_back(lInfo);
         }
      }
      else if( lMessage.mType == eRSMsgGameListEnd )
      {
         gRaceServerGames.swap(gGamesBeingListed);
         gGamesBeingListed.clear();
         RenderGameList(pWindow);
         RefreshRaceServerSelection(pWindow);
      }
      else if( lMessage.mType == eRSMsgPlayerNameAssigned )
      {
         // The name we asked for may already be taken by someone else currently
         // connected -- the server disambiguates it (see ServerSocket.cpp's
         // MRNM_SET_PLAYER_NAME handler) and tells us the real one back here.
         // Adopt it for our own display (the self row in IDC_USER_LIST, and the
         // "You: " prefix on outgoing chat).
         std::string lAssignedName;
         if( RaceServerClient::ParsePlayerNameAssigned(lMessage, lAssignedName) )
         {
            const CString lNewName = lAssignedName.c_str();
            if( lNewName != mThis->mUser )
            {
               mThis->mUser = lNewName;
               HWND lUserList = GetDlgItem(pWindow, IDC_USER_LIST);
               const int lSelfRow = FindUserListRow(lUserList, -1);
               if( lSelfRow >= 0 )
               {
                  ListView_SetItemText(lUserList, lSelfRow, 0, (char*)(const char*)mThis->mUser);
               }
            }
         }
      }
      // eRSMsgLobbyUserListEnd and anything else: no UI action needed here.
   }
}


static BOOL gAskPassword = TRUE;

static CString     MR_Pad( const char* pSrc );
static CString     GetLine( const char* pSrc );
static int         GetLineLen( const char* pSrc );
static const char* GetNextLine( const char* pSrc );

static int         FindFocusItem( HWND pWindow );

// DNS Cache

static CString gUserNameCache;
static CString gsServerAlias;
unsigned long  gsServerIP;


// MR_InternetRequest

MR_InternetRequest::MR_InternetRequest()
{
   mSocket      = INVALID_SOCKET;
   mBinMode = FALSE;
   mBinBuffer = NULL;
   mBinIndex = 0;
}

MR_InternetRequest::~MR_InternetRequest()
{
   Close();

   if( mBinBuffer != NULL )
   {
      delete []mBinBuffer;
   }
}


void MR_InternetRequest::SetBin()
{   
   mBinMode = TRUE;

   if( mBinBuffer == NULL )
   {
      mBinBuffer = new char[MRM_BIN_BUFFER_SIZE];
   }
}

void MR_InternetRequest::Close()
{
   if( mSocket != INVALID_SOCKET )
   {
      closesocket( mSocket );
      mSocket = INVALID_SOCKET;
   }
}

BOOL MR_InternetRequest::Working()const
{
   return( mSocket!=INVALID_SOCKET );
}

void MR_InternetRequest::Clear()
{
   Close();
   mBuffer  = "";
   mRequest = "";
   mBinIndex = 0;

}

// HTTPS helper function using WinInet
static BOOL FetchHTTPSContent(const char* pHost, const char* pPath, CString& pBuffer)
{
   HINTERNET hInternet = NULL;
   HINTERNET hConnection = NULL;
   HINTERNET hRequest = NULL;
   BOOL lReturnValue = FALSE;

   // Validate inputs
   if (!pHost || !pPath || !pHost[0] || !pPath[0]) {
      OutputDebugString("FetchHTTPSContent: NULL or empty host/path!");
      return FALSE;
   }

   try {
      hInternet = InternetOpen("HoverNet/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
      if (!hInternet) {
         DWORD dwError = GetLastError();
         char szBuffer[256];
         sprintf(szBuffer, "FetchHTTPSContent: InternetOpen failed, error=%d", dwError);
         OutputDebugString(szBuffer);
         return FALSE;
      }

      hConnection = InternetConnect(
         hInternet,
         pHost,
         INTERNET_DEFAULT_HTTPS_PORT,
         NULL,
         NULL,
         3,  // INTERNET_SERVICE_HTTPS
         0,
         0
      );

      if (!hConnection) {
         DWORD dwError = GetLastError();
         char szBuffer[256];
         sprintf(szBuffer, "FetchHTTPSContent: InternetConnect failed to %s, error=%d", pHost, dwError);
         OutputDebugString(szBuffer);
         InternetCloseHandle(hInternet);
         return FALSE;
      }

      DWORD dwFlags = INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_CACHE_WRITE;
      hRequest = HttpOpenRequest(
         hConnection,
         "GET",
         pPath,
         NULL,
         NULL,
         NULL,
         dwFlags,
         0
      );

      if (!hRequest) {
         DWORD dwError = GetLastError();
         char szBuffer[256];
         sprintf(szBuffer, "FetchHTTPSContent: HttpOpenRequest failed for %s, error=%d", pPath, dwError);
         OutputDebugString(szBuffer);
         InternetCloseHandle(hConnection);
         InternetCloseHandle(hInternet);
         return FALSE;
      }

      // Ignore SSL certificate errors for self-signed certs
      DWORD dwFlags2 = SECURITY_FLAG_IGNORE_UNKNOWN_CA | 
                       SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
      InternetSetOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &dwFlags2, sizeof(dwFlags2));

      if (!HttpSendRequest(hRequest, NULL, 0, NULL, 0)) {
         DWORD dwError = GetLastError();
         char szBuffer[256];
         sprintf(szBuffer, "FetchHTTPSContent: HttpSendRequest failed, error=%d", dwError);
         OutputDebugString(szBuffer);
         InternetCloseHandle(hRequest);
         InternetCloseHandle(hConnection);
         InternetCloseHandle(hInternet);
         return FALSE;
      }

      // Query the response status
      DWORD dwStatus = 0;
      DWORD dwStatusLen = sizeof(dwStatus);
      if (HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, 
                       &dwStatus, &dwStatusLen, NULL)) {
         char szBuffer[256];
         sprintf(szBuffer, "FetchHTTPSContent: HTTP Status=%d", dwStatus);
         OutputDebugString(szBuffer);
      }

      pBuffer.Empty();
      DWORD dwSize = 0;
      BYTE szBuffer[4096];

      while (InternetReadFile(hRequest, szBuffer, sizeof(szBuffer), &dwSize)) {
         if (dwSize == 0) break;
         pBuffer.Append((const char*)szBuffer, dwSize);
      }

      if (!pBuffer.IsEmpty()) {
         OutputDebugString("FetchHTTPSContent: SUCCESS");
         lReturnValue = TRUE;
      } else {
         OutputDebugString("FetchHTTPSContent: Buffer is empty");
      }
   }
   catch (...) {
      OutputDebugString("FetchHTTPSContent: Exception caught");
      lReturnValue = FALSE;
   }

   if (hRequest) InternetCloseHandle(hRequest);
   if (hConnection) InternetCloseHandle(hConnection);
   if (hInternet) InternetCloseHandle(hInternet);

   return lReturnValue;
}

BOOL MR_InternetRequest::Send( HWND pWindow, unsigned long pIP, unsigned pPort, const char* pURL, const char* pCookie )
{
   BOOL lReturnValue = FALSE;

   // DEBUG: Log connection details
   {
      char lDebugBuf[512];
      sprintf(lDebugBuf, "DEBUG MR_InternetRequest::Send: IP=%u.%u.%u.%u, Port=%u, URL=%s",
              (pIP >> 24) & 0xFF,
              (pIP >> 16) & 0xFF,
              (pIP >> 8) & 0xFF,
              (pIP) & 0xFF,
              pPort,
              pURL);
      OutputDebugString(lDebugBuf);
   }

   if( !Working() )
   {
      Clear();

      mStartTime = time( NULL );
      /*
      mRequest.Format( "GET http://%s HTTP/1.0\n\r"
                       "Accept: text/plain\n\r"
                       "UserAgent:  HoverRace/0.1\n\r"
                       "\n\r",
                       pURL                             );
      */
      
      const char* lURL = strchr( pURL, '/' );
      char        lReqBuffer[ 1024 ];

      if( (lURL == NULL)||mBinMode )
      {
         lURL = pURL;
      }

      if( pCookie == NULL )
      {
         sprintf( lReqBuffer,"GET %s HTTP/1.0\n\r"
                             // "Connection: Keep-Alive\n\r"
                             "Accept: */*\n\r"
                             "User-Agent: HoverRace/0.1\n\r"
                             // "User-Agent: Mozilla/3.0 (Win95; I)\n\r"
                             // "Host: 205.181.206.67:80\n\r"
                             // "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\n\r"
                             "\n\r"
                             "\n\r",
                             lURL   );
      }
      else
      {
         sprintf( lReqBuffer,"GET %s HTTP/1.0\n\r"
                             // "User-Agent: Mozilla/3.0 (Win95; I)\n\r"
                             // "Host: 205.181.206.67:80\n\r"
                             // "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\n\r"
                             "Accept: */*\n\r"
                             "User-Agent: HoverRace/0.1\n\r"
                             "Cookie: %s\n\r"
                             "\n\r"
                             "\n\r",
                             lURL, pCookie  );
      }


      mRequest = lReqBuffer;     

      mSocket    = socket( PF_INET, SOCK_STREAM, 0 );

      ASSERT( mSocket != INVALID_SOCKET );

      SOCKADDR_IN lAddr;

      lAddr.sin_family      = AF_INET;
      lAddr.sin_addr.s_addr = pIP;
      lAddr.sin_port        = htons( (unsigned short)pPort );

      WSAAsyncSelect( mSocket, pWindow, MRM_NET_EVENT, FD_CONNECT|FD_READ|FD_CLOSE );
                
      connect( mSocket, (struct sockaddr*)&lAddr, sizeof( lAddr ) );

      lReturnValue = TRUE;
   }
   return lReturnValue;
}


BOOL MR_InternetRequest::ProcessEvent( WPARAM pWParam, LPARAM pLParam )
{
   // static variables required to patch E-On/ICE protocol

   BOOL lReturnValue = FALSE;

   if( Working()&& (pWParam == mSocket) )
   {
      lReturnValue = TRUE;

      switch( WSAGETSELECTEVENT( pLParam) )
      {
         case FD_CONNECT:
            // We are now connected, send the request
            {
               send( mSocket, mRequest, mRequest.GetLength(), 0 );
            }
            mRequest = "";
            break;

         case FD_READ:
         case FD_CLOSE:

            int lNbRead;

            if( mBinMode )
            {
               if( mBinIndex >= MRM_BIN_BUFFER_SIZE )
               {
                  Close();
               }
               else
               {
                  lNbRead = recv( mSocket, mBinBuffer+mBinIndex, MRM_BIN_BUFFER_SIZE-mBinIndex, 0 );

                  if( lNbRead >= 0 )
                  {
                     mBinIndex += lNbRead;
                  }
                  else
                  {
                     Close();
                  }
               }

            }
            else
            {
               char lReadBuffer[1025];

               lNbRead = recv( mSocket, lReadBuffer, 1024, 0 );

               if( lNbRead > 0 )
               {
                  lReadBuffer[ lNbRead ] = 0;

                  {
                     mBuffer += lReadBuffer;
                  }
               }
            }

            if( WSAGETSELECTEVENT( pLParam) == FD_CLOSE )
            {
               Close();
            }
            break;

      }
   }

   return lReturnValue;

}

const char* MR_InternetRequest::GetBuffer()const
{
   return mBuffer;
}

const char* MR_InternetRequest::GetBinBuffer( int& pSize )const
{
   pSize = mBinIndex;
   return mBinBuffer;
}



BOOL MR_InternetRequest::IsReady()const
{
   return( ((mBinIndex!=0)||!mBuffer.IsEmpty()) && !Working() );
}









// MR_InternetRoom
MR_InternetRoom::MR_InternetRoom( BOOL pAllowRegistred, int pMajorID, int pMinorID, unsigned pKey2, unsigned pKey3 )
{
   int  lCounter;

   mBannerRequest.SetBin();

   mAllowRegistred = pAllowRegistred;
   mMajorID = pMajorID;
   mMinorID = pMinorID;
   mKey2    = pKey2;
   mKey3    = pKey3;

   for( lCounter = 0; lCounter < eMaxClient; lCounter++ )
   {
      mClientList[ lCounter ].mValid = FALSE;
   }

   for( lCounter = 0; lCounter < eMaxGame; lCounter++ )
   {
      mGameList[ lCounter ].mValid = FALSE;
   }

   mCurrentLocateRequest = NULL;
   mModelessDlg          = NULL;


   // Init WinSock
   WORD    lVersionRequested = MAKEWORD(1, 1); 
   WSADATA lWsaData;
 
   if( WSAStartup( lVersionRequested, &lWsaData) )
   {
      ASSERT( FALSE );
   }

   mNbSuccessiveRefreshTimeOut = 0;
   mCurrentBannerIndex         = 0;

}

MR_InternetRoom::~MR_InternetRoom()
{
   // Close WinSock
   WSACleanup();

   ASSERT( mModelessDlg == NULL );
}

int MR_InternetRoom::ParseState( const char* pAnswer )
{
   int lReturnValue = 0;
   const char* lLinePtr;


   lLinePtr = pAnswer;

   while( lLinePtr != NULL )
   {
      if( !strncmp( lLinePtr, "TIME_STAMP", 10 ) )
      {
         sscanf( lLinePtr, "TIME_STAMP %d", &mLastRefreshTimeStamp );
      }
      else if( !strncmp( lLinePtr, "USER", 4 ) )
      {
         int   lEntry;
         char  lOp[10];
         
         if( sscanf( lLinePtr, "USER %d %9s", &lEntry, lOp ) == 2 )
         {
            if( (lEntry >= 0) && (lEntry<eMaxClient ) )
            {
               if( !strcmp( lOp, "DEL" ) )
               {
                  lReturnValue |= eUsersModified;
                  mClientList[ lEntry ].mValid = FALSE;
               }
               else if( !strcmp( lOp, "NEW" ) )
               {
                  lLinePtr = GetNextLine( lLinePtr );

                  if( lLinePtr != NULL )
                  {
                     lReturnValue |= eUsersModified;

                     mClientList[ lEntry ].mMajorID = -1;
                     mClientList[ lEntry ].mMinorID = -1;

                     sscanf( GetLine( lLinePtr ), "%d-%d", 
                             &mClientList[ lEntry ].mMajorID, 
                             &mClientList[ lEntry ].mMinorID );

                     mClientList[ lEntry ].mValid = TRUE;
                     mClientList[ lEntry ].mGame = -1;

                     lLinePtr = GetNextLine( lLinePtr );

                     mClientList[ lEntry ].mName = GetLine( lLinePtr );
                  }
               }
            }
         }         
      }
      else if( !strncmp( lLinePtr, "GAME", 4 ) )
      {
         int   lEntry;
         char  lOp[10];
         int   lId = -1;
         
         if( sscanf( lLinePtr, "GAME %d %9s %u", &lEntry, lOp, &lId ) >= 2 )
         {
            if( (lEntry >= 0) && (lEntry<eMaxGame ) )
            {
               if( !strcmp( lOp, "DEL" ) )
               {
                  lReturnValue |= eGamesModified;
                  mGameList[ lEntry ].mValid = FALSE;
               }
               else if( !strcmp( lOp, "NEW" ) )
               {
                  lLinePtr = GetNextLine( lLinePtr );

                  if( lLinePtr != NULL )
                  {
                     FILE* lInitLog = NULL;
                     if(lInitLog) fprintf(lInitLog, "\n=== NEW Game Entry %d ===\n", lEntry), fflush(lInitLog);
                     
                     lReturnValue |= eGamesModified;

                     mGameList[ lEntry ].mValid    = TRUE;
                     mGameList[ lEntry ].mId       = lId;
                     mGameList[ lEntry ].mNbClient = 0;
                     mGameList[ lEntry ].mNbLap    = 1;
                     mGameList[ lEntry ].mAllowWeapons = FALSE;
                     mGameList[ lEntry ].mPort         = (unsigned)-1;
                     mGameList[ lEntry ].mServerHosted = FALSE;  // Phase 4: Initialize
                     
                     mGameList[ lEntry ].mName = GetLine( lLinePtr );
                     if(lInitLog) fprintf(lInitLog, "Line 1 (Name): %s\n", (const char*)mGameList[ lEntry ].mName), fflush(lInitLog);

                     lLinePtr = GetNextLine( lLinePtr );
                     mGameList[ lEntry ].mTrack = GetLine( lLinePtr );
                     if(lInitLog) fprintf(lInitLog, "Line 2 (Track): %s\n", (const char*)mGameList[ lEntry ].mTrack), fflush(lInitLog);
                     mGameList[ lEntry ].mAvailCode = MR_GetTrackAvail( mGameList[ lEntry ].mTrack, mAllowRegistred );

                     lLinePtr = GetNextLine( lLinePtr );
                     mGameList[ lEntry ].mIPAddr = GetLine( lLinePtr );
                     if(lInitLog) fprintf(lInitLog, "Line 3 (IP): %s\n", (const char*)mGameList[ lEntry ].mIPAddr), fflush(lInitLog);

                     lLinePtr = GetNextLine( lLinePtr );

                     int lNbClient;
                     int lDummyBool;
                     
                     if(lInitLog) fprintf(lInitLog, "Line 4 (Port/Laps): %s\n", GetLine(lLinePtr)), fflush(lInitLog);

                     if( sscanf( lLinePtr, "%u %d %d %d", &mGameList[ lEntry ].mPort, &mGameList[ lEntry ].mNbLap, &lDummyBool, &lNbClient ) == 4 )
                     {
                        mGameList[ lEntry ].mAllowWeapons = lDummyBool;
                        if(lInitLog) fprintf(lInitLog, "Parsed: Port=%u, Laps=%d, Weapons=%d, NbClient=%d\n", 
                           mGameList[ lEntry ].mPort, mGameList[ lEntry ].mNbLap, mGameList[ lEntry ].mAllowWeapons, lNbClient), fflush(lInitLog);

                        if( lNbClient > eMaxPlayerGame )
                        {
                           lNbClient = eMaxPlayerGame;
                        }

                        if( lNbClient != 0 )
                        {
                           lLinePtr = GetNextLine( lLinePtr );
                           if(lInitLog) fprintf(lInitLog, "Player list line: %s\n", GetLine(lLinePtr)), fflush(lInitLog);

                           if( lLinePtr != NULL )
                           {
                              const char* lPtr = lLinePtr;

                              for( int lCounter = 0; lCounter< lNbClient; lCounter++ )
                              {
                                 if( lPtr != NULL )
                                 {
                                    int lUserIndex = atoi( lPtr );

                                    if( (lUserIndex >= 0)&&(lUserIndex<eMaxClient) )
                                    {
                                       mGameList[ lEntry ].mClientList[ mGameList[ lEntry ].mNbClient++ ] = lUserIndex;

                                       mClientList[ lUserIndex ].mGame = lEntry;
                                    }
                                    lPtr = strchr( lPtr, ' ' );

                                    if( lPtr != NULL )
                                    {
                                       lPtr++;
                                    }
                                 }
                              }
                           }
                        }

                        // Phase 4: Check for SERVER_ADDR line (server-hosted race)
                        lLinePtr = GetNextLine( lLinePtr );
                        if(lInitLog) fprintf(lInitLog, "Checking SERVER_ADDR line, lLinePtr=%p\n", lLinePtr), fflush(lInitLog);
                        if( lLinePtr != NULL )
                        {
                           if(lInitLog) {
                              const char* lLineContent = GetLine(lLinePtr);
                              fprintf(lInitLog, "Line content (text): %s\n", lLineContent);
                              fflush(lInitLog);
                           }
                        }
                        if( lLinePtr != NULL && !strncmp( lLinePtr, "SERVER_ADDR", 11 ) )
                        {
                           if(lInitLog) fprintf(lInitLog, "Found SERVER_ADDR line\n"), fflush(lInitLog);
                           mGameList[ lEntry ].mServerHosted = TRUE;
                           
                           // Parse "SERVER_ADDR address:port"
                           // Use the configured HoverNet RaceServer for hosted races.
                           // (ignore what InternetRoom3 sends, as it may be incorrect)
                           const char* lLineStr = GetLine( lLinePtr );
                           if(lInitLog) fprintf(lInitLog, "Full line: '%s'\n", lLineStr), fflush(lInitLog);
                           if(lInitLog) fprintf(lInitLog, "Ignoring received SERVER_ADDR, using configured %s:%u\n",
                              (const char*)gRaceServerHost, gRaceServerPort), fflush(lInitLog);
                           
                           mGameList[ lEntry ].mServerAddr = gRaceServerHost;
                           mGameList[ lEntry ].mServerPort = gRaceServerPort;
                        }
                        else if(lLinePtr != NULL)
                        {
                           if(lInitLog) fprintf(lInitLog, "No SERVER_ADDR line - next line is: %s\n", GetLine(lLinePtr)), fflush(lInitLog);
                        }
                        if(lInitLog) fprintf(lInitLog, "Final values: ServerHosted=%d, ServerAddr=%s, ServerPort=%u\n", 
                           mGameList[lEntry].mServerHosted, (const char*)mGameList[lEntry].mServerAddr, mGameList[lEntry].mServerPort), fflush(lInitLog);
                        if(lInitLog) fclose(lInitLog);
                     }
                  }
               }
            }
         }         

      }
      else if( !strncmp( lLinePtr, "CHAT", 4 ) )
      {
         // Next line is a chat message
         lLinePtr = GetNextLine( lLinePtr );

         if( lLinePtr != NULL )
         {
            lReturnValue |= eChatModified;
            AddChatLine( GetLine( lLinePtr ) );
         }                  
      }
      
      lLinePtr = GetNextLine( lLinePtr );
   }

   return lReturnValue;

}





BOOL MR_InternetRoom::LocateServers( HWND pParentWindow )
{
   BOOL lReturnValue = FALSE;
   mThis = this;

   if( gNbServerEntries > 0 )
   {
      lReturnValue = TRUE;
   }
   else
   {
      // Extract hostname and path from gMainServer
      CString lHost = gMainServer;
      CString lPath = "/";
      
      int lSlashPos = lHost.Find( '/' );
      if( lSlashPos > 0 )
      {
         lPath = lHost.Mid( lSlashPos );
         lHost = lHost.Left( lSlashPos );
      }

      // Create a temporary CString to hold the buffer
      CString lBuffer;

      // Fetch using HTTPS synchronously (DIRECTLY, no dialog)
      BOOL lFetchSuccess = FetchHTTPSContent( lHost, lPath, lBuffer );
      
      if( lFetchSuccess && !lBuffer.IsEmpty() )
      {
         // Parse the server list from the buffer
         const char* lLinePtr = (const char*)lBuffer;
         gNbServerEntries = 0;
         
         while( (lLinePtr != NULL) && (gNbServerEntries < MR_MAX_SERVER_ENTRIES) )
         {
            // Skip empty lines
            if( *lLinePtr == 0 )
               break;
               
            // Parse line: Can be either:
            // Format 1: [priority] [name] [ip] [port] [path] [optional_banner_size] [optional_banner_url]
            // Format 2: [priority] [name] [full_url] [optional_banner_size] [optional_banner_url]
            // Full URL format: http://hostname:port/path or https://hostname:port/path
            
            int lPriority;
            char lName[200];
            char lIPStr[256];  // Larger buffer for full URLs
            char lPathOrUrl[256];
            unsigned short lPort = 0;
            char lPath[256];
            
            // Try to parse as full URL first
            int nScanned = sscanf(lLinePtr, "%d %s %255s", 
                                  &lPriority, lName, lIPStr);
            
            if( nScanned >= 3 )
            {
               BOOL lIsFullURL = FALSE;
               
               // Check if this looks like a full URL (contains :// or http/https)
               if( strstr(lIPStr, "://") != NULL )
               {
                  lIsFullURL = TRUE;
                  
                  // Parse full URL: http[s]://hostname[:port][/path]
                  const char* lProtoEnd = strstr(lIPStr, "://");
                  if( lProtoEnd != NULL )
                  {
                     const char* lHostStart = lProtoEnd + 3;
                     const char* lPortStart = strchr(lHostStart, ':');
                     const char* lPathStart = strchr(lHostStart, '/');
                     
                     // Determine the default port based on protocol
                     if( strstr(lIPStr, "https://") != NULL )
                     {
                        lPort = 443;
                     }
                     else
                     {
                        lPort = 80;
                     }
                     
                     // Extract hostname
                     char lHostname[256] = {0};
                     int lHostLen = 256;
                     
                     if( lPortStart != NULL && (lPathStart == NULL || lPortStart < lPathStart) )
                     {
                        // Port is specified
                        lHostLen = lPortStart - lHostStart;
                        if( sscanf(lPortStart, ":%hu", &lPort) != 1 )
                        {
                           lPort = (strstr(lIPStr, "https://") != NULL) ? 443 : 80;
                        }
                     }
                     else if( lPathStart != NULL )
                     {
                        // No port, path comes next
                        lHostLen = lPathStart - lHostStart;
                     }
                     else
                     {
                        // Just hostname
                        lHostLen = strlen(lHostStart);
                     }
                     
                     if( lHostLen > 0 && lHostLen < 256 )
                     {
                        strncpy(lHostname, lHostStart, lHostLen);
                        lHostname[lHostLen] = 0;
                        
                        // Extract path if present
                        strcpy(lPath, "/");
                        if( lPathStart != NULL )
                        {
                           strncpy(lPath, lPathStart, 255);
                           lPath[255] = 0;
                        }
                        
                        // Resolve hostname to IP
                        gServerList[gNbServerEntries].mAddress = inet_addr(lHostname);
                        
                        // If inet_addr fails (not an IP), try DNS resolution
                        if( gServerList[gNbServerEntries].mAddress == INADDR_NONE )
                        {
                           struct hostent* pHost = gethostbyname(lHostname);
                           if( pHost != NULL && pHost->h_addr != NULL )
                           {
                              gServerList[gNbServerEntries].mAddress = *(unsigned long*)pHost->h_addr;
                           }
                           else
                           {
                              // DNS resolution failed, skip this entry
                              lIsFullURL = FALSE;
                           }
                        }
                     }
                  }
                  
                  if( lIsFullURL )
                  {
                     gServerList[gNbServerEntries].mName = lName;
                     gServerList[gNbServerEntries].mPort = lPort;
                     gServerList[gNbServerEntries].mURL = lPath;
                     gServerList[gNbServerEntries].mType = 0;
                     gServerList[gNbServerEntries].mLadderIP = 0;
                     gServerList[gNbServerEntries].mLadderPort = 0;
                     
                     gNbServerEntries++;
                  }
               }
               
               // If not a URL, try original format: [priority] [name] [ip] [port] [path]
               if( !lIsFullURL )
               {
                  unsigned short lPortNum;
                  int nScannedOrig = sscanf(lLinePtr, "%d %s %s %hu %255s", 
                                           &lPriority, lName, lIPStr, &lPortNum, lPath);
                  
                  if( nScannedOrig >= 5 )
                  {
                     // Valid entry found in original format
                     gServerList[gNbServerEntries].mName = lName;
                     gServerList[gNbServerEntries].mPort = lPortNum;
                     gServerList[gNbServerEntries].mURL = lPath;
                     gServerList[gNbServerEntries].mType = 0;  // Default type
                     gServerList[gNbServerEntries].mLadderIP = 0;
                     gServerList[gNbServerEntries].mLadderPort = 0;
                     
                     // Convert IP address string to unsigned long
                     unsigned int a, b, c, d;
                     if( sscanf(lIPStr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 )
                     {
                        gServerList[gNbServerEntries].mAddress = (a << 24) | (b << 16) | (c << 8) | d;
                     }
                     else
                     {
                        gServerList[gNbServerEntries].mAddress = inet_addr(lIPStr);
                     }
                     
                     gNbServerEntries++;
                  }
               }
            }
            
            // Move to next line
            lLinePtr = GetNextLine(lLinePtr);
         }
         
         if( gNbServerEntries > 0 )
         {
            lReturnValue = TRUE;
            // Store the buffer for later use if needed
            if (mThis) {
               mThis->mOpRequest.Clear();
               mThis->mOpRequest.mBuffer = lBuffer;
            }
         }
      }
      
      // If fetch failed or returned no servers, create fallback test servers
      if( gNbServerEntries == 0 )
      {
         OutputDebugString("LocateServers: Remote server unavailable, creating fallback entries");
         
         // Add a test/demo server entry
         gServerList[0].mName = "[DEMO] Local Test Server (Offline)";
         gServerList[0].mPort = 80;
         gServerList[0].mURL = "/";
         gServerList[0].mType = MR_HTTP_ROOM_SERVER;
         gServerList[0].mLadderIP = 0;
         gServerList[0].mLadderPort = 0;
         gServerList[0].mAddress = inet_addr("127.0.0.1");
         
         gNbServerEntries = 1;
         gCurrentServerEntry = 0;
         lReturnValue = TRUE;
      }
   }
   
   return lReturnValue;
}

BOOL MR_InternetRoom::AddUserOp( HWND pParentWindow )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_CONNECT );

   mNetOpRequest.Format( "%s?=ADD_USER%%%%%d-%d%%%%1%%%%%u%%%%%u%%%%%s",                      
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mMajorID,
                         (mMinorID==-1)?-2:mMinorID,
                         mKey2,
                         mKey3,
                         (const char*)MR_Pad( mUser ) );

   // DEBUG: Log connection details
   {
      char lDebugBuf[512];
      sprintf(lDebugBuf, "DEBUG AddUserOp: ServerEntry=%d, Name=%s, URL=%s, Address=%u.%u.%u.%u, Port=%u",
              gCurrentServerEntry,
              (const char*)gServerList[gCurrentServerEntry].mName,
              (const char*)gServerList[gCurrentServerEntry].mURL,
              (gServerList[gCurrentServerEntry].mAddress >> 24) & 0xFF,
              (gServerList[gCurrentServerEntry].mAddress >> 16) & 0xFF,
              (gServerList[gCurrentServerEntry].mAddress >> 8) & 0xFF,
              (gServerList[gCurrentServerEntry].mAddress) & 0xFF,
              gServerList[gCurrentServerEntry].mPort);
      OutputDebugString(lDebugBuf);
      sprintf(lDebugBuf, "DEBUG AddUserOp: Full request=%s", (const char*)mNetOpRequest);
      OutputDebugString(lDebugBuf);
   }

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   if( lReturnValue )
   { 
      const char* lData = mOpRequest.GetBuffer();

      while( (lData != NULL)&&strncmp( lData, "SUCCESS", 7 ) )
      {
         lData = GetNextLine( lData );
      }
      if( lData == NULL )
      {
         ASSERT( FALSE );
         lReturnValue = FALSE;
      }
      else
      {
         lData = GetNextLine( lData );

         sscanf( lData, "USER_ID %d-%u", &mCurrentUserIndex, &mCurrentUserId );

         AddChatLine( MR_LoadString( IDS_UR_CONNECT ) );
         AddChatLine( MR_LoadString( IDS_IMR_WELCOME ) );

         ParseState( lData );
      }      
   }

   mOpRequest.Clear();

   return lReturnValue;
}

BOOL MR_InternetRoom::DelUserOp( HWND pParentWindow, BOOL pFastMode )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_DISCONNECT );

   mNetOpRequest.Format( "%s?=DEL_USER%%%%%d-%u",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentUserIndex,
                         mCurrentUserId            );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, pFastMode?FastNetOpCallBack : NetOpCallBack )==IDOK;

   mOpRequest.Clear();

   return lReturnValue;
}


BOOL MR_InternetRoom::AddGameOp( HWND pParentWindow, const char* pGameName, const char* pTrackName, int pNbLap, BOOL pWeapons, unsigned pPort )
{
  BOOL lReturnValue = FALSE;

  mThis = this;

  mNetOpString.LoadString( IDS_IMR_ADD_GAME );

   mNetOpRequest.Format( "%s?=ADD_GAME%%%%%d-%u%%%%%s%%%%%s%%%%%d%%%%%d%%%%%d",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentUserIndex,
                         mCurrentUserId,
                         (const char*)MR_Pad( pGameName ),
                         (const char*)MR_Pad( pTrackName ),
                         pNbLap,
                         pWeapons?1:0,
                         pPort );


   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   if( lReturnValue )
   { 
      const char* lData = mOpRequest.GetBuffer();

      while( (lData != NULL)&&strncmp( lData, "SUCCESS", 7 ) )
      {
         lData = GetNextLine( lData );
      }
      if( lData == NULL )
      {
         ASSERT( FALSE );
         lReturnValue = FALSE;
      }
      else
      {
         lData = GetNextLine( lData );

         sscanf( lData, "GAME_ID %d-%u", &mCurrentGameIndex, &mCurrentGameId );
      }      
   }

   mOpRequest.Clear();

   return lReturnValue;
}

// Phase 4: Server-hosted races
BOOL MR_InternetRoom::AddGameHostedOp( HWND pParentWindow, const char* pGameName, const char* pTrackName, int pNbLap, BOOL pWeapons )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_ADD_GAME );

   // Send ADD_GAME_HOSTED command to InternetRoom
   mNetOpRequest.Format( "%s?=ADD_GAME_HOSTED%%%%%d-%u%%%%%s%%%%%s%%%%%d%%%%%d",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentUserIndex,
                         mCurrentUserId,
                         (const char*)MR_Pad( pGameName ),
                         (const char*)MR_Pad( pTrackName ),
                         pNbLap,
                         pWeapons?1:0 );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, FastNetOpCallBack )==IDOK;

   if( lReturnValue )
   {
      const char* lData = mOpRequest.GetBuffer();
      
      // DEBUG: Log the full response
      CString lDebugResponse = lData ? lData : "(null)";
      MessageBox( pParentWindow, "ADD_GAME_HOSTED Response:\n" + lDebugResponse, "DEBUG: ADD_GAME_HOSTED", MB_OK );

      while( (lData != NULL)&&strncmp( lData, "SUCCESS", 7 ) )
      {
         lData = GetNextLine( lData );
      }
      if( lData == NULL )
      {
         ASSERT( FALSE );
         lReturnValue = FALSE;
         MessageBox( pParentWindow, "SUCCESS not found in response!", "DEBUG: ERROR", MB_ICONERROR|MB_OK );
      }
      else
      {
         lData = GetNextLine( lData );

         sscanf( lData, "GAME_ID %d-%u", &mCurrentGameIndex, &mCurrentGameId );
      }      
   }
   else
   {
      MessageBox( pParentWindow, "FastNetOpCallBack dialog failed", "DEBUG: ERROR", MB_ICONERROR|MB_OK );
   }

   mOpRequest.Clear();

   return lReturnValue;
}

BOOL MR_InternetRoom::DelGameOp( HWND pParentWindow )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_DEL_GAME );

   mNetOpRequest.Format( "%s?=DEL_GAME%%%%%d-%u%%%%%d-%u",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentGameIndex,
                         mCurrentGameId,
                         mCurrentUserIndex,
                         mCurrentUserId            );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   mOpRequest.Clear();

   return lReturnValue;
}

BOOL MR_InternetRoom::JoinGameOp( HWND pParentWindow, int pGameIndex )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_JOIN_GAME );

   mCurrentGameIndex = pGameIndex;
   mCurrentGameId = mGameList[ pGameIndex ].mId;

   mNetOpRequest.Format( "%s?=JOIN_GAME%%%%%d-%u%%%%%d-%u",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentGameIndex,
                         mCurrentGameId,
                         mCurrentUserIndex,
                         mCurrentUserId            );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   mOpRequest.Clear();

   return lReturnValue;
}

BOOL MR_InternetRoom::LeaveGameOp( HWND pParentWindow )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString.LoadString( IDS_IMR_LEAVE_GAME );

   mNetOpRequest.Format( "%s?=LEAVE_GAME%%%%%d-%u%%%%%d-%u",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentGameIndex,
                         mCurrentGameId,
                         mCurrentUserIndex,
                         mCurrentUserId            );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   mOpRequest.Clear();

   return lReturnValue;
}

BOOL MR_InternetRoom::AddMessageOp( HWND pParentWindow, const char* pMessage, int pHours, int pMinutes )
{
   BOOL lReturnValue = FALSE;

   mThis = this;

   mNetOpString = "Sending message to the Meeting Room...";

   mNetOpRequest.Format( "%s?=MESSAGE%%%%%d-%u%%%%%d:%d%%%%%s",
                         (const char*)gServerList[gCurrentServerEntry].mURL,
                         mCurrentUserIndex,
                         mCurrentUserId,
                         pHours,
                         pMinutes,
                        (const char*)MR_Pad( pMessage )  );

   lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, NetOpCallBack )==IDOK;

   mOpRequest.Clear();

   return lReturnValue;
}


BOOL MR_InternetRoom::AskRoomParams( HWND pParentWindow )
{
   BOOL lReturnValue = FALSE;
   mThis = this;

   lReturnValue = mThis->LocateServers( pParentWindow );

   if( lReturnValue )
   {
      lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_INTERNET_PARAMS ), pParentWindow, AskParamsCallBack )==IDOK;
   }

   return lReturnValue;
}

BOOL MR_InternetRoom::DisplayChatRoom( HWND pParentWindow, MR_NetworkSession* pSession, MR_VideoBuffer* pVideoBuffer )
{
   if( pSession == NULL || pVideoBuffer == NULL ) return FALSE;

   mThis = this;
   mSession = pSession;
   mVideoBuffer = pVideoBuffer;
   mUser = pSession->GetPlayerName();
   mSession->SetPlayerName(mUser);

   if( DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_USERNAME), pParentWindow, UsernameCallBack) != IDOK ) return FALSE;
   mSession->SetPlayerName(mUser);

   gRaceServerHost = kDefaultRaceServerHost;
   gRaceServerPort = kDefaultRaceServerPort;
   char lOverride[256] = { 0 };
   if( GetEnvironmentVariableA("HOVERNET_LOBBY", lOverride, sizeof(lOverride)) > 0 )
   {
      char* lColon = strrchr(lOverride, 58);
      if( lColon != NULL )
      {
         *lColon = 0;
         const int lPort = atoi(lColon + 1);
         if( lPort > 0 && lPort <= 65535 ) gRaceServerPort = static_cast<unsigned>(lPort);
      }
      if( lOverride[0] != 0 ) gRaceServerHost = lOverride;
   }

   gRaceServerLobby.Disconnect();
   gRaceServerGames.clear();
   return DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_INTERNET_MEETING),
                    pParentWindow, RaceServerRoomCallBack) == IDOK;
}

/*
BOOL MR_InternetRoom::DisplayModeless( HWND pParentWindow, MR_NetworkSession* pSession, MR_VideoBuffer* pVideoBuffer )
{
   BOOL lReturnValue = AskRoomParams( pParentWindow );

   if( lReturnValue )
   {
      mThis = this;

      mSession = pSession;
      mVideoBuffer = pVideoBuffer;

      mSession->SetPlayerName( mUser );

      lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_INTERNET_MEETING ), pParentWindow, RoomCallBack )==IDOK;
   }
   return lReturnValue;
}


BOOL MR_InternetRoom::IsDisplayed( )const
{
   return mWindow != NULL;
}
*/

void MR_InternetRoom::AddChatLine( const char* pText )
{

   if( !mChatBuffer.IsEmpty() )
   {
      mChatBuffer += "\r\n";
   }
   mChatBuffer += pText;


   // Determine if we must cut some lines from the buffer
   while( mChatBuffer.GetLength() > (40*40) )
   {
      int lIndex = mChatBuffer.Find( '\n' );

      if( lIndex < 0 )
      {
         break;
      }
      else
      {
         mChatBuffer = mChatBuffer.Mid( lIndex+1 );
      }
   }
}

void MR_InternetRoom::SelectGameForUser( HWND pWindow )
{
   int lFocus = FindFocusItem( GetDlgItem( pWindow,IDC_USER_LIST ));

   if( lFocus != -1 )
   {
      if( (mClientList[lFocus].mValid)&&(mClientList[lFocus].mGame!=-1) )
      {
         HWND lList = GetDlgItem( pWindow, IDC_GAME_LIST );
         LV_FINDINFO lInfo;

         lInfo.flags   = LVFI_PARAM;
         lInfo.lParam = mClientList[lFocus].mGame;

         int lGameIndex = ListView_FindItem( lList, -1, &lInfo );

         if( lGameIndex != -1 )
         {
            ListView_SetItemState( lList, lGameIndex, LVIS_FOCUSED, LVIS_FOCUSED );
         }
      }
   }
}


void MR_InternetRoom::RefreshGameSelection( HWND pWindow )
{
   int lGameIndex = FindFocusItem( GetDlgItem( pWindow, IDC_GAME_LIST ) );

   if( lGameIndex == -1 )
   {
      SetDlgItemText( pWindow, IDC_TRACK_NAME, MR_LoadString( IDS_IMR_NOSELECT ) );
      SetDlgItemText( pWindow, IDC_NB_LAP, "" );
      SetDlgItemText( pWindow, IDC_WEAPONS, "" );
      SetDlgItemText( pWindow, IDC_AVAIL_MESSAGE, "" );
      SetDlgItemText( pWindow, IDC_PLAYER_LIST, "" );

      SendMessage( GetDlgItem( pWindow, IDC_JOIN ), WM_ENABLE, FALSE, 0 );
   }
   else
   {
      CString     lAvailString = "";
      CString     lPlayerList;

      switch( mGameList[ lGameIndex ].mAvailCode )
      {
         case eTrackAvail:
            lAvailString.LoadString( IDS_AVAIL );
            break;

         case eTrackNotFound:
            lAvailString.LoadString( IDS_TRACK_NOTINSTALL );
            break;

         case eMustBuy:
            lAvailString.LoadString( IDS_TRACK_NOTREG );
            break;
      }

      for( int lCounter = 0; lCounter < mGameList[ lGameIndex ].mNbClient; lCounter++ )
      {
         int lClientIndex = mGameList[ lGameIndex ].mClientList[ lCounter ];

         if( mClientList[ lClientIndex ].mValid )
         {
            if( !lPlayerList.IsEmpty() )
            {
               lPlayerList += "\r\n";
            }
            lPlayerList += mClientList[ lClientIndex ].mName;
         }

      }      

      SetDlgItemText( pWindow, IDC_TRACK_NAME, mGameList[ lGameIndex ].mTrack );
      SetDlgItemInt( pWindow, IDC_NB_LAP, mGameList[ lGameIndex ].mNbLap, FALSE );
      SetDlgItemText( pWindow, IDC_WEAPONS, mGameList[ lGameIndex ].mAllowWeapons?"on":"off" );
      SetDlgItemText( pWindow, IDC_AVAIL_MESSAGE, lAvailString );
      SetDlgItemText( pWindow, IDC_PLAYER_LIST, lPlayerList );

      SendMessage( GetDlgItem( pWindow, IDC_JOIN ), WM_ENABLE, mGameList[ lGameIndex ].mAvailCode==eTrackAvail, 0 );
   }

}

void MR_InternetRoom::RefreshGameList( HWND pWindow )
{
   HWND lList = GetDlgItem( pWindow, IDC_GAME_LIST );

   if( lList != NULL )
   {
      // Get selection
      int lSelected = FindFocusItem( lList );

      // Clear the content of the list box
      ListView_DeleteAllItems( lList );

      // Refill
      int lIndex = 0;
      for( int lCounter = 0; lCounter< eMaxGame; lCounter++ )
      {
         if( mGameList[ lCounter ].mValid )
         {
            LV_ITEM lItem;

            lItem.mask     = LVIF_TEXT|LVIF_PARAM;
            lItem.iItem    = lIndex++;
            lItem.iSubItem = 0;
            lItem.pszText  = (char*)(const char*)mGameList[ lCounter ].mName;
            lItem.lParam   = lCounter;

            if( lCounter == lSelected )
            {
               lItem.mask     |= LVIF_STATE;
               lItem.state     = LVIS_FOCUSED;
               lItem.stateMask = LVIS_FOCUSED;
            }

            ListView_InsertItem( lList, &lItem );
         }
      }
   }
   RefreshGameSelection( pWindow );
}

void MR_InternetRoom::RefreshUserList( HWND pWindow )
{
   HWND lList = GetDlgItem( pWindow, IDC_USER_LIST );

   if( lList != NULL )
   {
      // Get selection
      int lSelected = FindFocusItem( lList );

      // Clear the content of the list box
      ListView_DeleteAllItems( lList );

      // Refill
      int lIndex = 0;
      for( int lCounter = 0; lCounter< eMaxClient; lCounter++ )
      {
         if( mClientList[ lCounter ].mValid )
         {
            CString lName = mClientList[ lCounter ].mName;

            if( mClientList[ lCounter ].mMajorID != -1 )
            {
               CString lExtension;
               lExtension.Format("[%d-%d]", mClientList[ lCounter ].mMajorID, mClientList[ lCounter ].mMinorID );
               lName += lExtension;
            }
            LV_ITEM lItem;

            lItem.mask     = LVIF_TEXT|LVIF_PARAM;
            lItem.iItem    = lIndex++;
            lItem.iSubItem = 0;
            lItem.pszText  = (char*)(const char*)lName;
            lItem.lParam   = lCounter;

            if( lCounter == lSelected )
            {
               lItem.mask     |= LVIF_STATE;
               lItem.state     = LVIS_FOCUSED;
               lItem.stateMask = LVIS_FOCUSED;
            }

            int lCode = ListView_InsertItem( lList, &lItem );

            ASSERT( lCode != -1 );

         }
      }

   }

}

void MR_InternetRoom::RefreshChatOut( HWND pWindow )
{  
   HWND pDest = GetDlgItem( pWindow, IDC_CHAT_OUT );

   SetWindowText( pDest, mChatBuffer );

   SendMessage( pDest, EM_LINESCROLL, 0, 1000 );
   // SendMessage( pDest, WM_VSCROLL, SB_BOTTOM, 0 );
}

BOOL MR_InternetRoom::VerifyError( HWND pParentWindow, const char* pAnswer )
{
   BOOL lReturnValue = FALSE;
   int  lCode = -1;

   const char* lLinePtr = pAnswer;


   while( lLinePtr != NULL )
   {
      if( !strncmp( lLinePtr, "SUCCESS", 7 ) )
      {
         lReturnValue = TRUE;
         break;
      }
      else if( !strncmp( lLinePtr, "ERROR", 5 ) )
      {
         sscanf( lLinePtr, "ERROR %d", &lCode );
         lReturnValue = FALSE;
         break;
      }
      lLinePtr = GetNextLine( lLinePtr );
   }

   if( !lReturnValue && (pParentWindow != NULL ) )
   {
      BOOL    lPopDlg = TRUE;
      CString lMessage;

      if( lCode == -1 )
      {
         ASSERT( FALSE );
         lMessage.LoadString( IDS_COMM_ERROR );
      }

      while( lMessage.IsEmpty() )
      {
         switch( lCode )
         {
            case 100:
               lMessage.LoadString( IDS_CANT_ADDUSER );
               break;

            case 101:
               lMessage.LoadString( IDS_NOMORE_SHRWR );
               break;

            case 102:
               lMessage.LoadString( IDS_NOMORE_USER );
               break;

            case 103:
               lMessage.LoadString( IDS_INCOMPAT_VER );
               break;

            case 104:
               lMessage.LoadString( IDS_EXP_KEY );
               break;

            case 105:
               lMessage.LoadString( IDS_DUAL_USE );
               break;

            case 200:
               lMessage.LoadString( IDS_CANT_REFRESH );
               break;

            case 201:
               lMessage.LoadString( IDS_NOL );
               break;

            case 300:
               lMessage.LoadString( IDS_CANT_ADD_CHAT );
               break;

            case 301:
               lMessage.LoadString( IDS_NOL );
               break;

            case 400:
               lMessage.LoadString( IDS_CANT_ADD_GAME );
               break;

            case 401:
               lMessage.LoadString( IDS_NOL );
               break;

            case 402:
               lMessage.LoadString( IDS_NOMORE_ENTRY );
               break;

            case 500:
               lMessage.LoadString( IDS_CANT_JOIN );
               break;

            case 501:
               lMessage.LoadString( IDS_NOL );
               break;

            case 502:
               lMessage.LoadString( IDS_GAME_NA );
               break;

            case 503:
               lMessage.LoadString( IDS_GAME_FULL );
               break;

            case 600:
               lMessage.LoadString( IDS_CANT_DEL_GAME );
               break;

            case 601:
               lMessage.LoadString( IDS_NOL );
               lPopDlg = FALSE;
               break;

            case 602:
               lMessage.LoadString( IDS_GAME_NA );
               break;

            case 603:
               lMessage.LoadString( IDS_NOT_OWNER );
               break;

            case 700:
               lMessage.LoadString( IDS_CANT_LEAVE_GAME );
               break;

            case 701:
               lMessage.LoadString( IDS_NOL );
               lPopDlg = FALSE;
               break;

            case 702:
               lMessage.LoadString( IDS_GAME_NA );
               lPopDlg = FALSE;
               break;

            case 703:
               lMessage.LoadString( IDS_MUST_JOIN );
               break;

            case 800:
               lMessage.LoadString( IDS_CANT_DEL_USER );
               break;

            case 801:
               lMessage.LoadString( IDS_NOL );
               lPopDlg = FALSE;
               break;

            case 900:
               lMessage.LoadString( IDS_CANT_START_GAME );
               break;

            case 901:
               lMessage.LoadString( IDS_NOL );
               break;

            case 902:
               lMessage.LoadString( IDS_GAME_NA );
               break;

            case 903:
               lMessage.LoadString( IDS_NOT_OWNER );
               break;

            case 1000:
               lMessage.LoadString( IDS_CANT_ADD_MESSAGE );
               break;

            case 1001:
               lMessage.LoadString( IDS_NOL );
               break;

            case 1002:
               lMessage.LoadString( IDS_NOT_AUTH );
               break;

         }

         if( lMessage.IsEmpty() )
         {
            if( (lCode % 100)==0 )
            {
               lMessage.Format( IDS_UNKNOWN_ERROR, lCode );
            }
            else
            {
               // Restart the sequence but only with the genic number
               lCode = lCode -(lCode%100);
            }
         }
      }

      if( lPopDlg )
      {
         MessageBox( pParentWindow, lMessage, MR_LoadString( IDS_IMR ), MB_ICONSTOP|MB_OK|MB_APPLMODAL );
      }
   }
   return lReturnValue;   
}

int MR_InternetRoom::LoadBanner( HWND pWindow, const char* pBuffer, int pBufferLen )
{
   ASSERT( pWindow != NULL );

   HWND lWindow = GetDlgItem( pWindow, IDC_PUB );


   if( lWindow == NULL )
   {
      return 0; // no more refresh
   }
   else
   {
      mBanner.Decode( (unsigned char*)pBuffer, pBufferLen );

      
      HDC hdc = GetDC( lWindow );
               
      HPALETTE lOldPalette = SelectPalette( hdc, mBanner.GetGlobalPalette(), FALSE );

      int lNbColors = RealizePalette( hdc );

      if( lOldPalette != NULL )
      {
         SelectPalette( hdc, mBanner.GetGlobalPalette(), TRUE );
      }

      ReleaseDC( lWindow, hdc );

      TRACE( "Colors2 %d  %d\n", lNbColors, GetLastError() ); 
                    
      mCurrentBannerIndex = 0;
      SendMessage( lWindow, BM_SETIMAGE, IMAGE_BITMAP, (long)mBanner.GetImage(0) );

      return mBanner.GetDelay(0);
   }
}

int MR_InternetRoom::RefreshBanner( HWND pWindow )
{
   ASSERT( pWindow != NULL );

   HWND lWindow = GetDlgItem( pWindow, IDC_PUB );


   if( (lWindow == NULL)||(mBanner.GetImageCount()==0) )
   {
      return 0; // no more refresh
   }
   else
   {
      mCurrentBannerIndex = (mCurrentBannerIndex+1)%mBanner.GetImageCount();

      SendMessage( lWindow, BM_SETIMAGE, IMAGE_BITMAP, (long)mBanner.GetImage(mCurrentBannerIndex) );

      return mBanner.GetDelay( mCurrentBannerIndex );

   }

}



BOOL CALLBACK MR_InternetRoom::AskPasswordCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            lReturnValue = TRUE;
            SetDlgItemText( pWindow, IDC_ALIAS, mThis->mUser );

         }
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               lReturnValue = TRUE;
               break;

            case IDOK:
               {
                  char lBuffer[40];
                  char lPassword[40];

                  GetDlgItemText( pWindow, IDC_ALIAS,  lBuffer,   sizeof( lBuffer ) );
                  GetDlgItemText( pWindow, IDC_PASSWD, lPassword, sizeof( lPassword) );

                  // Verify the validity of the password
                  mThis->mNetOpString.LoadString( IDS_IMR_PASSVAL );

                  mThis->mNetOpRequest.Format( "%s,%s,hover", lBuffer, lPassword );

                  int lCode = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pWindow, NetOpCallBack )==IDOK;

                  mThis->mOpRequest.Clear();

                  if( lCode )
                  {
                     EndDialog( pWindow, IDOK );
                     mThis->mUser = lBuffer;
                     gUserNameCache = lBuffer;
                  }

                  lReturnValue = TRUE;
               }
               break;
         }
         break;
   }   

   return lReturnValue;
}




BOOL CALLBACK MR_InternetRoom::AskParamsCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            lReturnValue = TRUE;
            SetDlgItemText( pWindow, IDC_ALIAS, mThis->mUser );

            if( gCurrentServerEntry == -1 )
            {
               gCurrentServerEntry = 0;
            }

            for( int lCounter = 0; lCounter < gNbServerEntries; lCounter++ )
            {
               SendDlgItemMessage( pWindow, IDC_ROOMLIST, LB_ADDSTRING, 0 , (long)(const char*)gServerList[ lCounter ].mName );
            }

            // Select the current room
            SendDlgItemMessage( pWindow, IDC_ROOMLIST, LB_SETCURSEL, gCurrentServerEntry, 0 );
         }
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               lReturnValue = TRUE;
               break;

            case IDOK:
               {
                  char lBuffer[30];
                  char lURLBuffer[80];

                  GetDlgItemText( pWindow, IDC_URL, lURLBuffer, sizeof( lURLBuffer ));

                  gCurrentServerEntry = SendDlgItemMessage( pWindow, IDC_ROOMLIST, LB_GETCURSEL, 0, 0 );


                  if( GetDlgItemText( pWindow, IDC_ALIAS, lBuffer, sizeof( lBuffer ) ) <= 0 )
                  {
                     MessageBox( pWindow, MR_LoadString( IDS_ENTER_ALIAS ), MR_LoadString( IDS_IMR ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else if( (gCurrentServerEntry < 0)||(gCurrentServerEntry>=gNbServerEntries ) )
                  {
                     gCurrentServerEntry = -1;
                     MessageBox( pWindow, MR_LoadString( IDS_SELECT_ROOM ), MR_LoadString( IDS_IMR ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else
                  {
                     EndDialog( pWindow, IDOK );
                     mThis->mUser = lBuffer;
                     gUserNameCache = lBuffer;
                  }

                  lReturnValue = TRUE;
               }
               break;
         }
         break;
   }   

   return lReturnValue;

}


BOOL CALLBACK MR_InternetRoom::UsernameCallBack( HWND pWindow, UINT pMsgId, WPARAM pWParam, LPARAM )
{
   if( pMsgId == WM_INITDIALOG )
   {
      SetDlgItemText(pWindow, IDC_USERNAME, mThis->mUser);
      return TRUE;
   }
   if( pMsgId == WM_COMMAND )
   {
      if( LOWORD(pWParam) == IDOK )
      {
         char lName[40] = { 0 };
         GetDlgItemTextA(pWindow, IDC_USERNAME, lName, sizeof(lName));
         if( lName[0] == 0 )
         {
            MessageBox(pWindow, MR_LoadString(IDS_ENTER_ALIAS), MR_LoadString(IDS_IMR), MB_OK | MB_ICONINFORMATION);
            return TRUE;
         }
         mThis->mUser = lName;
         EndDialog(pWindow, IDOK);
         return TRUE;
      }
      if( LOWORD(pWParam) == IDCANCEL )
      {
         EndDialog(pWindow, IDCANCEL);
         return TRUE;
      }
   }
   return FALSE;
}

BOOL CALLBACK MR_InternetRoom::RaceServerRoomCallBack( HWND pWindow, UINT pMsgId, WPARAM pWParam, LPARAM pLParam )
{
   switch( pMsgId )
   {
      case WM_INITDIALOG:
      {
         HWND lGameList = GetDlgItem(pWindow, IDC_GAME_LIST);
         RECT lRect;
         GetClientRect(lGameList, &lRect);
         LV_COLUMN lColumn = {};
         lColumn.mask = LVCF_WIDTH | LVCF_FMT;
         lColumn.fmt = LVCFMT_LEFT;
         lColumn.cx = lRect.right - GetSystemMetrics(SM_CXVSCROLL);
         ListView_InsertColumn(lGameList, 0, &lColumn);

         HWND lUserList = GetDlgItem(pWindow, IDC_USER_LIST);
         GetClientRect(lUserList, &lRect);
         lColumn.cx = lRect.right - GetSystemMetrics(SM_CXVSCROLL);
         ListView_InsertColumn(lUserList, 0, &lColumn);
         LV_ITEM lUserItem = {};
         lUserItem.mask = LVIF_TEXT | LVIF_PARAM;
         lUserItem.iItem = 0;
         lUserItem.pszText = (char*)(const char*)mThis->mUser;
         // -1 can never collide with a real server-assigned client id (see
         // RaceServerClient.h), so AddLobbyUser/RemoveLobbyUser's id lookups can
         // never mistake this row for another player's.
         lUserItem.lParam = -1;
         ListView_InsertItem(lUserList, &lUserItem);
         gLobbyUserNames.clear();

         ShowWindow(GetDlgItem(pWindow, IDC_ADD), SW_HIDE);
         SetDlgItemTextA(pWindow, IDC_ADD_SERVER, "Host Race...");
         CString lStatus;
         lStatus.Format("Connecting to %s:%u...", (const char*)gRaceServerHost, gRaceServerPort);
         SetDlgItemText(pWindow, IDC_CHAT_OUT, lStatus);
         if( !gRaceServerLobby.Connect((const char*)gRaceServerHost, gRaceServerPort) )
         {
            MessageBoxA(pWindow, "Could not connect to the HoverNet RaceServer.", "HoverNet Lobby", MB_OK | MB_ICONERROR);
            EndDialog(pWindow, IDCANCEL);
            return TRUE;
         }
         SetDlgItemTextA(pWindow, IDC_CHAT_OUT, "Connected to the shared HoverNet lobby.");
         // Without this, the server only ever knows us by its "Player_N" fallback
         // (see RaceServerClient::SetPlayerName), so other clients' user lists and
         // chat lines would show that instead of our actual name.
         gRaceServerLobby.SetPlayerName((const char*)mThis->mUser);
         gRaceServerLobby.ListLobbyUsers();
         RequestGameListRefresh(pWindow);
         SetTimer(pWindow, kRaceServerRefreshTimer, 2000, NULL);
         return TRUE;
      }

      case WM_TIMER:
         if( pWParam == kRaceServerRefreshTimer && gRaceServerLobby.IsConnected() )
         {
            RequestGameListRefresh(pWindow);
            DrainRaceServerMessages(pWindow);
            return TRUE;
         }
         break;

      case WM_NOTIFY:
      {
         NMHDR* lHeader = reinterpret_cast<NMHDR*>(pLParam);
         if( lHeader != NULL && lHeader->idFrom == IDC_GAME_LIST && lHeader->code == LVN_ITEMCHANGED )
         {
            RefreshRaceServerSelection(pWindow);
            return TRUE;
         }
         break;
      }

      case WM_COMMAND:
         if( LOWORD(pWParam) == IDCANCEL )
         {
            KillTimer(pWindow, kRaceServerRefreshTimer);
            gRaceServerLobby.Disconnect();
            EndDialog(pWindow, IDCANCEL);
            return TRUE;
         }
         if( LOWORD(pWParam) == IDOK && GetFocus() == GetDlgItem(pWindow, IDC_CHAT_IN) )
         {
            char lMessage[200] = { 0 };
            GetDlgItemTextA(pWindow, IDC_CHAT_IN, lMessage, sizeof(lMessage));
            if( lMessage[0] != 0 )
            {
               gRaceServerLobby.SendMessage(eRSMsgChatMessage, lMessage, strlen(lMessage));
               char lChatBuffer[4096] = { 0 };
               GetDlgItemTextA(pWindow, IDC_CHAT_OUT, lChatBuffer, sizeof(lChatBuffer));
               CString lChat = lChatBuffer;
               lChat += "\r\n";
               lChat += mThis->mUser;
               lChat += ": ";
               lChat += lMessage;
               SetDlgItemText(pWindow, IDC_CHAT_OUT, lChat);
               SetDlgItemTextA(pWindow, IDC_CHAT_IN, "");
            }
            return TRUE;
         }
         if( LOWORD(pWParam) == IDC_JOIN )
         {
            const int lIndex = SelectedRaceIndex(pWindow);
            LogNetJoin( "=== IDC_JOIN: selectedIndex=%d ===", lIndex );
            if( lIndex < 0 || lIndex >= static_cast<int>(gRaceServerGames.size()) ) return TRUE;
            const RaceServerGameInfo lGame = gRaceServerGames[lIndex];
            LogNetJoin( "IDC_JOIN: race name='%s' track='%s' laps=%d started=%d players=%d",
                        lGame.mName.c_str(), lGame.mTrack.c_str(), lGame.mNumLaps, (int)lGame.mStarted,
                        lGame.mNumPlayers );
            if( lGame.mStarted )
            {
               MessageBoxA(pWindow, "That race has already started.", "HoverNet Lobby", MB_OK | MB_ICONINFORMATION);
               return TRUE;
            }
            MR_RecordFile* lTrackFile = MR_TrackOpen(pWindow, lGame.mTrack.c_str(), mThis->mAllowRegistred);
            LogNetJoin( "IDC_JOIN: MR_TrackOpen returned %p", (void*)lTrackFile );
            if( lTrackFile == NULL || !mThis->mSession->LoadNew(lGame.mTrack.c_str(), lTrackFile,
                 lGame.mNumLaps, TRUE, mThis->mVideoBuffer) )
            {
               LogNetJoin( "IDC_JOIN: track open/LoadNew failed, aborting" );
               return TRUE;
            }
            LogNetJoin( "IDC_JOIN: LoadNew ok, connecting to %s:%u",
                        (const char*)gRaceServerHost, gRaceServerPort );

            KillTimer(pWindow, kRaceServerRefreshTimer);
            gRaceServerLobby.Disconnect();
            mThis->mSession->SetIsGameCreator(FALSE);
            mThis->mSession->SetConnectionMode(MR_CONNECTION_SERVER_HOSTED, gRaceServerHost, gRaceServerPort);
            // Join by the race's unique id, not its (possibly duplicated) display
            // name -- hosting no longer rejects a repeat name, so two races can
            // legitimately show up with the same label.
            mThis->mSession->ConfigureJoinById(lGame.mRaceId);
            const BOOL lJoined = mThis->mSession->ConnectToServer(pWindow, gRaceServerHost, gRaceServerPort, lGame.mName.c_str());
            LogNetJoin( "IDC_JOIN: ConnectToServer returned %d", (int)lJoined );
            if( lJoined )
            {
               EndDialog(pWindow, IDOK);
            }
            else
            {
               gRaceServerLobby.Connect((const char*)gRaceServerHost, gRaceServerPort);
               SetTimer(pWindow, kRaceServerRefreshTimer, 2000, NULL);
            }
            return TRUE;
         }
         if( LOWORD(pWParam) == IDC_ADD_SERVER )
         {
            CString lTrack;
            int lLaps = 3;
            BOOL lWeapons = TRUE;
            if( !MR_SelectTrack(pWindow, lTrack, lLaps, lWeapons, mThis->mAllowRegistred) ) return TRUE;
            MR_RecordFile* lTrackFile = MR_TrackOpen(pWindow, lTrack, mThis->mAllowRegistred);
            if( lTrackFile == NULL || !mThis->mSession->LoadNew(lTrack, lTrackFile, lLaps, lWeapons, mThis->mVideoBuffer) ) return TRUE;

            KillTimer(pWindow, kRaceServerRefreshTimer);
            gRaceServerLobby.Disconnect();
            mThis->mSession->SetConnectionMode(MR_CONNECTION_SERVER_HOSTED, gRaceServerHost, gRaceServerPort);
            mThis->mSession->SetIsGameCreator(TRUE);
            mThis->mSession->ConfigureHostedRace(lTrack, lLaps, lWeapons);
            // The race name is just the track name, matching the Linux client --
            // no reason to make the host type one.
            if( mThis->mSession->ConnectToServer(pWindow, gRaceServerHost, gRaceServerPort, lTrack) )
            {
               EndDialog(pWindow, IDOK);
            }
            else
            {
               gRaceServerLobby.Connect((const char*)gRaceServerHost, gRaceServerPort);
               SetTimer(pWindow, kRaceServerRefreshTimer, 2000, NULL);
            }
            return TRUE;
         }
         break;
   }
   return FALSE;
}

BOOL CALLBACK MR_InternetRoom::RoomCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            RECT lRect;
            HWND lList;
            LV_COLUMN lSpec;

            // Adjust dlg items
            lList = GetDlgItem( pWindow, IDC_USER_LIST );
            GetClientRect( lList, &lRect );

            // Create list columns               
            lSpec.mask = LVCF_SUBITEM|LVCF_WIDTH |LVCF_FMT;
               
            lSpec.fmt       = LVCFMT_LEFT;
            lSpec.cx        = lRect.right-1-GetSystemMetrics(SM_CXVSCROLL);
            lSpec.iSubItem  = 0;

            ListView_InsertColumn( lList, 0, &lSpec );

            lList = GetDlgItem( pWindow, IDC_GAME_LIST );
            GetClientRect( lList, &lRect );

            lSpec.mask = LVCF_SUBITEM|LVCF_WIDTH |LVCF_FMT;
               
            lSpec.fmt       = LVCFMT_LEFT;
            lSpec.cx        = lRect.right-1-GetSystemMetrics(SM_CXVSCROLL);
            lSpec.iSubItem  = 0;

            ListView_InsertColumn( lList, 0, &lSpec );


            // Connect to server
            
            {
               if( !mThis->AddUserOp( pWindow ) )
               {
                  // Clear IP cache
                  gsServerAlias = "";

                  EndDialog( pWindow, IDCANCEL );
               }
               else
               {
                  // Start the automatic refresh sequence
                  SetTimer( pWindow, REFRESH_EVENT, 2*REFRESH_DELAY, NULL );         

                  // Init dialog lists
                  mThis->RefreshGameList( pWindow );
                  mThis->RefreshUserList( pWindow );
                  mThis->RefreshChatOut( pWindow );
               }
            }
            
         }
         lReturnValue = TRUE;


         // Initiate banners loading in 1 seconds
         if( gNbBannerEntries > 0 )
         {
            SetTimer( pWindow, LOAD_BANNER_TIMEOUT_EVENT, 1000, NULL );         
         }

         break;
            
      case WM_QUERYNEWPALETTE:
         if( mThis->mBanner.GetGlobalPalette() != NULL )
         {
            HWND lBitmapCtl = GetDlgItem( pWindow, IDC_PUB );

            if( lBitmapCtl != NULL )
            {
               TRACE( "PALSET\n" );

               HDC hdc = GetDC( lBitmapCtl );

               // UnrealizeObject( mThis->mBanner.GetGlobalPalette() );
               HPALETTE lOldPalette = SelectPalette( hdc, mThis->mBanner.GetGlobalPalette(), FALSE );
               
               if( RealizePalette( hdc ) > 0 )
               {
                  lReturnValue = TRUE;
               }

               if( lOldPalette != NULL )
               {
                  SelectPalette( hdc, lOldPalette, FALSE );
               }

               ReleaseDC( lBitmapCtl, hdc );

               // InvalidateRgn( pWindow, NULL, TRUE );
               InvalidateRgn( lBitmapCtl, NULL, TRUE );
               // UpdateWindow( pWindow );
               // UpdateWindow( lBitmapCtl );



               // lReturnValue = TRUE;
            }
         }
         break;
         
      case WM_PALETTECHANGED:
         if( (mThis->mBanner.GetGlobalPalette() != NULL)&&( (HWND)pWParam != pWindow ) )
         {
            HWND lBitmapCtl = GetDlgItem( pWindow, IDC_PUB );

            if( (pWParam != (int)lBitmapCtl)&&(lBitmapCtl != NULL) )
            {
               TRACE( "PALCHANGE\n" );
               HDC hdc = GetDC( lBitmapCtl );

               // UnrealizeObject( mThis->mBanner.GetGlobalPalette() );
               HPALETTE lOldPalette = SelectPalette( hdc, mThis->mBanner.GetGlobalPalette(), TRUE );
               
               if( RealizePalette( hdc ) > 0 )
               {
                  lReturnValue = TRUE;
               }

               if( lOldPalette != NULL )
               {
                  SelectPalette( hdc, lOldPalette, FALSE );
               }

               ReleaseDC( lBitmapCtl, hdc );

               // InvalidateRgn( pWindow, NULL, TRUE );
               InvalidateRgn( lBitmapCtl, NULL, TRUE );
               // UpdateWindow( pWindow );
               // UpdateWindow( lBitmapCtl );



               // lReturnValue = TRUE;

            }
         }
         break;


            

      case WM_TIMER:
         lReturnValue = TRUE;

         KillTimer( pWindow, pWParam );

         switch( pWParam )
         {
            case REFRESH_TIMEOUT_EVENT:
               {
                  // Cancel the pending call
                  mThis->mRefreshRequest.Clear();
                  mThis->mNbSuccessiveRefreshTimeOut++;

                  // Warn the user 
                  if( mThis->mNbSuccessiveRefreshTimeOut >=2 )
                  {
                     mThis->AddChatLine( MR_LoadString( IDS_WARN_TO ) );
                     mThis->RefreshChatOut( pWindow );
                  }

                  // Initiate a new refresh
                  SetTimer( pWindow, REFRESH_EVENT, 1/*REFRESH_DELAY*/, NULL );         


               }
               break;

            case REFRESH_EVENT:
               {
                  CString lRequest;
                  
                  lRequest.Format( "%s?=REFRESH%%%%%d-%u%%%%%d",
                                   (const char*)gServerList[gCurrentServerEntry].mURL,
                                   mThis->mCurrentUserIndex,
                                   mThis->mCurrentUserId,
                                   mThis->mLastRefreshTimeStamp                     );
                  mThis->mRefreshRequest.Send( pWindow, gServerList[gCurrentServerEntry].mAddress, gServerList[gCurrentServerEntry].mPort, lRequest );

                  // Activate timeout
                  SetTimer( pWindow, REFRESH_TIMEOUT_EVENT, REFRESH_TIMEOUT, NULL );
               }
               break;

            case CHAT_TIMEOUT_EVENT:
               {
                  // Cancel the pending call
                  mThis->mChatRequest.Clear();

                  // Warn the user 
                  mThis->AddChatLine( MR_LoadString( IDS_WARN_TO ));
                  mThis->RefreshChatOut( pWindow );
               }
               break;

            case LOAD_BANNER_TIMEOUT_EVENT:
               {
                  // Already start timer for next load
                  if( gNbBannerEntries > 0 )
                  {
                     // TRACE( "LoadBanner\n" );

                     int lNextEntry = (gCurrentBannerEntry+1)%gNbBannerEntries;

                     SetTimer( pWindow, LOAD_BANNER_TIMEOUT_EVENT, gBannerList[lNextEntry].mDelay*1000, NULL );         
                  
                     // Initiate the loading of a new banner
                  
                     if( mThis->mBannerRequest.Send( pWindow, gBannerList[lNextEntry].mAddress,gBannerList[lNextEntry].mPort, gBannerList[lNextEntry].mURL ))
                     {
                        // No timeout on that one
                     } 
                  }


               }
               break;

            case ANIM_BANNER_TIMEOUT_EVENT:
               {
                  TRACE( "RefreshBanner\n" );
                  int lNextRefresh = mThis->RefreshBanner( pWindow );

                  if( lNextRefresh != 0 )
                  {
                     SetTimer( pWindow, ANIM_BANNER_TIMEOUT_EVENT, lNextRefresh, NULL );
                  }
               }
               break;



         }
         break;



      case MRM_NET_EVENT:

         if( mThis->mChatRequest.ProcessEvent( pWParam, pLParam ) )
         {
            if( mThis->mChatRequest.IsReady() )
            {
               // Simply reset
               KillTimer( pWindow, CHAT_TIMEOUT_EVENT );
               mThis->mChatRequest.Clear();
            }
         }

         if( mThis->mRefreshRequest.ProcessEvent( pWParam, pLParam ) )
         {
            if( mThis->mRefreshRequest.IsReady() )
            {
               KillTimer( pWindow, REFRESH_TIMEOUT_EVENT );

               const char* lAnswer = mThis->mRefreshRequest.GetBuffer();

               mThis->mNbSuccessiveRefreshTimeOut = 0;

               if( !mThis->VerifyError( pWindow, lAnswer ))
               {
                  EndDialog( pWindow, IDCANCEL );
               }
               else
               {
                  // We must now parse the answer
                  int lToRefresh = mThis->ParseState( lAnswer );

                  if( lToRefresh & eGamesModified )
                  {
                     mThis->RefreshGameList( pWindow );
                  }

                  if( lToRefresh & eUsersModified )
                  {
                     mThis->RefreshUserList( pWindow );
                  }

                  if( lToRefresh & eChatModified )
                  {
                     mThis->RefreshChatOut( pWindow );
                  }

                  // Schedule a new refresh

                  SetTimer( pWindow, REFRESH_EVENT, REFRESH_DELAY, NULL );         
               }
               mThis->mRefreshRequest.Clear();
            }
         }

         if( mThis->mBannerRequest.ProcessEvent( pWParam, pLParam ) )
         {
            TRACE( "LoadBanner\n" );

            if( mThis->mBannerRequest.IsReady() )
            {
               int lBufferSize;
               const char* lBuffer = mThis->mBannerRequest.GetBinBuffer( lBufferSize );

               if( lBuffer != NULL )
               {
                  // Kill animation timer
                  KillTimer( pWindow, ANIM_BANNER_TIMEOUT_EVENT );

                  const char* lGifBuf = NULL;
                  // Find the GIF8 string indicating start of buffer
                  // I hope that they wont create a GIF97 format

                  int lEntry = (gCurrentBannerEntry+1)%gNbBannerEntries;
 
                  gBannerList[lEntry].mLastCookie = "";

                  for( int lCounter = 0; lCounter < min(lBufferSize-30, 400 ); lCounter++ )
                  {
                     if( lBuffer[ lCounter ] == 'S' )
                     {
                        if( !strncmp( lBuffer+lCounter, "Set-Cookie:", 11 ) )
                        {
                           // of we found a cookie
                           // skip spaces
                           lCounter+= 11;
                           while( isspace( lBuffer[lCounter] ) )
                           {
                              lCounter++;
                           }

                           if( !gBannerList[lEntry].mLastCookie.IsEmpty() )
                           {
                              gBannerList[lEntry].mLastCookie += "; ";
                           }

                           while( (lBuffer[lCounter]!='\n')&&(lBuffer[lCounter]!='\r')&&(lBuffer[lCounter]!=';') )
                           {
                              gBannerList[lEntry].mLastCookie += lBuffer[lCounter++];
                           }                           
                        }
                     }

                     if( lBuffer[ lCounter ] == 'G' )
                     {
                        if( !strncmp( lBuffer+lCounter, "GIF8", 4 ) )
                        {
                           lBufferSize -= lCounter;
                           lGifBuf = lBuffer+lCounter;
                           break;
                        }
                     }
                  }
               
                  if( lGifBuf != NULL )
                  {
                     int lNextRefresh = mThis->LoadBanner( pWindow, lGifBuf, lBufferSize );

                     gCurrentBannerEntry = lEntry;

                     if( lNextRefresh != 0 )
                     {
                        SetTimer( pWindow, ANIM_BANNER_TIMEOUT_EVENT, lNextRefresh, NULL );
                     }
                  }
               }
            }
         }

         if( mThis->mClickRequest.ProcessEvent( pWParam, pLParam ) )
         {
            TRACE( "ClickBannerReady\n" );

            if( mThis->mClickRequest.IsReady() )
            {
               // Find the location URL and load it
               const char* lLocation = strstr( mThis->mClickRequest.GetBuffer(), "Location:" );

               if( lLocation != NULL )
               {
                  char lURLBuffer[300]; 
                  
                  lURLBuffer[0] = 0;
                  sscanf( lLocation+9, " %299s", lURLBuffer );
                  lURLBuffer[299] = 0;

                  if( strlen( lURLBuffer ) > 0 )
                  {
                     LoadURL( pWindow, lURLBuffer );
                  }
               }
               mThis->mClickRequest.Clear();
            }
         }

         break;


      case WM_NOTIFY:
         {
            NMHDR* lNotMessage = (NMHDR*)pLParam;

            switch( lNotMessage->idFrom )
            {
               case IDC_GAME_LIST:
                  if( lNotMessage->code == LVN_ITEMCHANGED )
                  {
                     lReturnValue = TRUE;
                     mThis->RefreshGameSelection( pWindow );
                  }
                  break;

               case IDC_USER_LIST:
                  if( lNotMessage->code == LVN_ITEMCHANGED )
                  {
                     lReturnValue = TRUE;

                     // Select the game corresponding to the selected 
                     mThis->SelectGameForUser( pWindow );
                  }
                  break;
            }
         }
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDOK:
               if( GetFocus()==GetDlgItem( pWindow, IDC_CHAT_IN ) )
               {
                  char lBuffer[200];

                  lReturnValue = TRUE;

                  GetDlgItemText( pWindow, IDC_CHAT_IN, lBuffer, sizeof( lBuffer ) );

                  CString lRequest;

                  lRequest.Format( "%s?=ADD_CHAT%%%%%d-%u%%%%%s",
                                   (const char*)gServerList[gCurrentServerEntry].mURL,
                                   mThis->mCurrentUserIndex,
                                   mThis->mCurrentUserId,
                                   (const char*)MR_Pad(lBuffer) );
                  
                  if( mThis->mChatRequest.Send( pWindow, gServerList[gCurrentServerEntry].mAddress,gServerList[gCurrentServerEntry].mPort, lRequest ))
                  {
                     SetDlgItemText( pWindow, IDC_CHAT_IN, "" );                     

                     // Activate timeout
                     SetTimer( pWindow, CHAT_TIMEOUT_EVENT, CHAT_TIMEOUT, NULL );
                  } 
                  lReturnValue = TRUE;
               }
               break;

            case IDCANCEL:
               mThis->DelUserOp( pWindow );
               EndDialog( pWindow, IDCANCEL );
               lReturnValue = TRUE;
               break;

            case IDC_JOIN:               
               lReturnValue = TRUE;               

               try
               {
                  if( mThis->mModelessDlg == NULL )
                  {
                     FILE* lLog = NULL;
                     if(lLog) fprintf(lLog, "\n=== IDC_JOIN START ===\n"), fflush(lLog);
                     
                     // First verify if the selected track can be played
                     int lFocus  = FindFocusItem( GetDlgItem( pWindow, IDC_GAME_LIST ) );
                     if(lLog) fprintf(lLog, "FindFocusItem returned: %d\n", lFocus), fflush(lLog);

                     if( (lFocus != -1)&&(mThis->mGameList[lFocus].mAvailCode==eTrackAvail) )
                     {
                        BOOL lSuccess = FALSE;

                        // Cache game data BEFORE calling JoinGameOp to avoid corruption
                        if(lLog) fprintf(lLog, "Caching game data before JoinGameOp\n"), fflush(lLog);
                        CString lGameTrack = mThis->mGameList[lFocus].mTrack;
                        int lGameNbLap = mThis->mGameList[lFocus].mNbLap;
                        BOOL lGameAllowWeapons = mThis->mGameList[lFocus].mAllowWeapons;
                        CString lGameIPAddr = mThis->mGameList[lFocus].mIPAddr;
                        unsigned lGamePort = mThis->mGameList[lFocus].mPort;
                        BOOL lGameServerHosted = mThis->mGameList[lFocus].mServerHosted;
                        CString lGameServerAddr = mThis->mGameList[lFocus].mServerAddr;
                        unsigned lGameServerPort = mThis->mGameList[lFocus].mServerPort;
                        if(lLog) fprintf(lLog, "Game data cached: %s, laps=%d, IP=%s:%d\n", (const char*)lGameTrack, lGameNbLap, (const char*)lGameIPAddr, lGamePort), fflush(lLog);

                        if(lLog) fprintf(lLog, "Calling JoinGameOp\n"), fflush(lLog);
                        // Register to the InternetServer
                        lSuccess = mThis->JoinGameOp( pWindow, lFocus );
                        if(lLog) fprintf(lLog, "JoinGameOp returned: %d\n", lSuccess), fflush(lLog);

                        if( lSuccess )
                        {
                           if(lLog) fprintf(lLog, "About to call MR_TrackOpen\n"), fflush(lLog);
                           // Try to load the track using cached data
                           MR_RecordFile* lTrackFile = MR_TrackOpen( pWindow, lGameTrack, mThis->mAllowRegistred );
                           if(lLog) fprintf(lLog, "MR_TrackOpen returned\n"), fflush(lLog);
                           
                           if(lLog) fprintf(lLog, "About to call LoadNew with track=%s\n", (const char*)lGameTrack), fflush(lLog);
                           lSuccess = mThis->mSession->LoadNew( lGameTrack, lTrackFile, lGameNbLap, lGameAllowWeapons, mThis->mVideoBuffer );
                           if(lLog) fprintf(lLog, "LoadNew returned: %d\n", lSuccess), fflush(lLog);

                           if( lSuccess )
                           {
                              // connect to the game server
                              CString lCurrentTrack;

                              if(lLog) fprintf(lLog, "About to format track label\n"), fflush(lLog);
                              lCurrentTrack.Format( "%s  %d laps %s", (const char*)lGameTrack, lGameNbLap, lGameAllowWeapons?"with weapons":"no weapons" );
                              if(lLog) fprintf(lLog, "Track label: %s\n", (const char*)lCurrentTrack), fflush(lLog);

                              // Phase 4: Check if this is a server-hosted race and set connection mode
                              if(lLog) fprintf(lLog, "Server hosted: %d\n", lGameServerHosted), fflush(lLog);
                              if( lGameServerHosted )
                              {
                                 if(lLog) fprintf(lLog, "Setting SERVER_HOSTED mode with addr=%s port=%d\n", (const char*)lGameServerAddr, lGameServerPort), fflush(lLog);
                                 mThis->mSession->SetConnectionMode( MR_CONNECTION_SERVER_HOSTED, (const char*)lGameServerAddr, lGameServerPort );
                              }
                              else
                              {
                                 if(lLog) fprintf(lLog, "Setting PEER_TO_PEER mode\n"), fflush(lLog);
                                 mThis->mSession->SetConnectionMode( MR_CONNECTION_PEER_TO_PEER );
                              }

                              // For server-hosted races, connect to the server. For P2P, connect to the player's address
                              const char* lConnectAddr = lGameIPAddr;
                              unsigned lConnectPort = lGamePort;
                              if( lGameServerHosted && !lGameServerAddr.IsEmpty() )
                              {
                                 lConnectAddr = (const char*)lGameServerAddr;
                                 lConnectPort = lGameServerPort;
                              }
                              
                              if(lLog) fprintf(lLog, "About to call ConnectToServer with addr=%s port=%d (hosted=%d)\n", lConnectAddr, lConnectPort, lGameServerHosted), fflush(lLog);
                              lSuccess = mThis->mSession->ConnectToServer( pWindow, lConnectAddr, lConnectPort, lCurrentTrack, &mThis->mModelessDlg, MRM_DLG_END_JOIN );
                              if(lLog) fprintf(lLog, "ConnectToServer returned: %d\n", lSuccess), fflush(lLog);
                           }

                           if( !lSuccess )
                           {
                              if(lLog) fprintf(lLog, "lSuccess is FALSE, calling LeaveGameOp\n"), fflush(lLog);
                              // Unregister from Game
                              mThis->LeaveGameOp( pWindow );
                           }
                        }
                        
                        if(lLog) fclose(lLog);
                     }
                  }
               }
               catch(const std::exception& ex)
               {
                  FILE* lLog = NULL;
                  if(lLog) fprintf(lLog, "EXCEPTION in IDC_JOIN: %s\n", ex.what()), fflush(lLog);
                  if(lLog) fclose(lLog);
               }
               catch(...)
               {
                  FILE* lLog = NULL;
                  if(lLog) fprintf(lLog, "UNKNOWN EXCEPTION in IDC_JOIN\n"), fflush(lLog);
                  if(lLog) fclose(lLog);
               }
               
               break;

            case IDC_ADD:
               lReturnValue = TRUE;

               if( mThis->mModelessDlg == NULL )
               {
                  BOOL lSuccess = FALSE;

                  // Ask the user to select a track
                  CString lCurrentTrack;
                  int     lNbLap;
                  BOOL    lAllowWeapons;

                  lSuccess = MR_SelectTrack( pWindow, lCurrentTrack, lNbLap, lAllowWeapons, mThis->mAllowRegistred );

                  if( lSuccess )
                  {
                     // Load the track
                     MR_RecordFile* lTrackFile = MR_TrackOpen( pWindow, lCurrentTrack, mThis->mAllowRegistred );
                     lSuccess = mThis->mSession->LoadNew( lCurrentTrack, lTrackFile, lNbLap, lAllowWeapons, mThis->mVideoBuffer );
                  }

                  if( lSuccess )
                  {
                     // Register to the InternetServer
                     lSuccess = mThis->AddGameOp( pWindow, lCurrentTrack, lCurrentTrack, lNbLap, lAllowWeapons, MR_DEFAULT_NET_PORT  );

                     if( lSuccess )
                     {
                        // Wait client registration
                        CString lTrackName;

                        lTrackName.Format( "%s  %d laps %s", (const char*)lCurrentTrack, lNbLap, lAllowWeapons?"with weapons":"no weapons" );

                        lSuccess = mThis->mSession->WaitConnections( pWindow, lTrackName, FALSE, MR_DEFAULT_NET_PORT, &mThis->mModelessDlg, MRM_DLG_END_ADD );

                        if( !lSuccess )
                        {
                           // Unregister Game
                           mThis->DelGameOp( pWindow );
                        }                        
                     }
                  }                                                                          
               }
               break;

            case IDC_ADD_SERVER:
               lReturnValue = TRUE;

               if( mThis->mModelessDlg == NULL )
               {
                  BOOL lSuccess = FALSE;

                  // Ask the user to select a track
                  CString lCurrentTrack;
                  int     lNbLap;
                  BOOL    lAllowWeapons;

                  lSuccess = MR_SelectTrack( pWindow, lCurrentTrack, lNbLap, lAllowWeapons, mThis->mAllowRegistred );

                  if( lSuccess )
                  {
                     // Load the track
                     MR_RecordFile* lTrackFile = MR_TrackOpen( pWindow, lCurrentTrack, mThis->mAllowRegistred );
                     lSuccess = mThis->mSession->LoadNew( lCurrentTrack, lTrackFile, lNbLap, lAllowWeapons, mThis->mVideoBuffer );
                  }

                  if( lSuccess )
                  {
                     // Register to the RaceServer (server-hosted race)
                     // Send ADD_GAME_HOSTED command to InternetRoom
                     lSuccess = mThis->AddGameHostedOp( pWindow, lCurrentTrack, lCurrentTrack, lNbLap, lAllowWeapons );

                     MessageBox( pWindow, lSuccess ? "AddGameHostedOp succeeded" : "AddGameHostedOp FAILED", "DEBUG", MB_OK );

                     if( lSuccess )
                     {
                        // Mark this player as the game creator
                        mThis->mSession->SetIsGameCreator( TRUE );

                        // For server-hosted races, connect to the central RaceServer
                     // The host joins as a player on the HoverNet RaceServer.
                     CString lTrackName;

                        lTrackName.Format( "%s  %d laps %s", (const char*)lCurrentTrack, lNbLap, lAllowWeapons?"with weapons":"no weapons" );

                        // Set connection mode to server-hosted before connecting
                        mThis->mSession->SetConnectionMode( MR_CONNECTION_SERVER_HOSTED, gRaceServerHost, gRaceServerPort );

                        // DEBUG: Confirm we're attempting server-hosted connection
                        CString lConnectionMessage;
                        lConnectionMessage.Format( "Attempting to connect to RaceServer on %s:%u...",
                                                   (const char*)gRaceServerHost, gRaceServerPort );
                        MessageBox( pWindow, lConnectionMessage, "Server-Hosted Race", MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );

                        // Connect to RaceServer as a client (not as a peer master)
                        lSuccess = mThis->mSession->ConnectToServer( pWindow, gRaceServerHost, gRaceServerPort, (const char*)lTrackName, &mThis->mModelessDlg, MRM_DLG_END_ADD );
                        if( !lSuccess )
                        {
                           // Unregister Game
                           mThis->DelGameOp( pWindow );
                        }
                     }
                  }
               }
               break;

            case IDC_PUB:
               {
                  if( !gBannerList[gCurrentBannerEntry].mClickURL.IsEmpty() )
                  {
                     if( gBannerList[gCurrentBannerEntry].mIndirectClick )
                     {
                        mThis->mClickRequest.Clear();
                        mThis->mClickRequest.Send( pWindow, 
                                                   gBannerList[gCurrentBannerEntry].mAddress,
                                                   gBannerList[gCurrentBannerEntry].mPort,
                                                   gBannerList[gCurrentBannerEntry].mClickURL,
                                                   gBannerList[gCurrentBannerEntry].mLastCookie );

                     }
                     else
                     {
                        LoadURL( pWindow, gBannerList[gCurrentBannerEntry].mClickURL );
                     }
                  }
               }
               break;


         }
         break;

      case MRM_DLG_END_ADD:
      case MRM_DLG_END_JOIN:
            lReturnValue = TRUE;

            mThis->mModelessDlg = NULL;

            if( pWParam == IDOK )
            {
               // Unregister user and game
               mThis->DelUserOp( pWindow, TRUE );

               // Quit with a success
               EndDialog( pWindow, IDOK );
            }
            else
            {
               // Unregister Game
               if( pMsgId == MRM_DLG_END_ADD )
               {
                  mThis->DelGameOp( pWindow );
               }
               else
               {
                  mThis->LeaveGameOp( pWindow );
               }
            }                        
            break;



      case WM_DESTROY:
         {
            if( mThis->mModelessDlg != NULL )
            {
               // DestroyWindow( mThis->mModelessDlg );
               mThis->mModelessDlg = NULL;
            }
            mThis->mOpRequest.Clear();
            mThis->mChatRequest.Clear();
            mThis->mRefreshRequest.Clear();
         }
         break;
   }



   return lReturnValue;
}



BOOL CALLBACK MR_InternetRoom::GetAddrCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      case WM_INITDIALOG:
         {
            OutputDebugString("GetAddrCallBack: WM_INITDIALOG");
            
            // Setup message
            SetDlgItemText( pWindow, IDC_TEXT, MR_LoadString( IDS_LOC_MSERVER ) );

            // Don't fetch here - use a timer to fetch after dialog is ready
            SetTimer( pWindow, 100, 100, NULL );
            
            lReturnValue = TRUE;
         }
         break;

      case WM_TIMER:
         {
            if( pWParam == 100 )
            {
               KillTimer( pWindow, 100 );
               lReturnValue = TRUE;

               OutputDebugString("GetAddrCallBack: Timer 100 - Starting HTTPS fetch");
               
               try {
                  // Extract hostname and path from gMainServer
                  CString lHost = gMainServer;
                  CString lPath = "/";
                  
                  int lSlashPos = lHost.Find( '/' );
                  if( lSlashPos > 0 )
                  {
                     lPath = lHost.Mid( lSlashPos );
                     lHost = lHost.Left( lSlashPos );
                  }
                  
                  char lDebugMsg[256];
                  sprintf(lDebugMsg, "HTTPS Fetch: Host=%s, Path=%s", (const char*)lHost, (const char*)lPath);
                  OutputDebugString(lDebugMsg);

                  // Create a temporary CString to hold the buffer
                  CString lBuffer;

                  // Fetch using HTTPS synchronously
                  BOOL lFetchSuccess = FetchHTTPSContent( lHost, lPath, lBuffer );
                  
                  sprintf(lDebugMsg, "HTTPS Fetch Result: Success=%d, BufferLen=%d", lFetchSuccess, lBuffer.GetLength());
                  OutputDebugString(lDebugMsg);
                  
                  if( lFetchSuccess && !lBuffer.IsEmpty() )
                  {
                     OutputDebugString("GetAddrCallBack: HTTPS fetch succeeded, storing buffer");
                     
                     // Store result in mOpRequest - but only if mThis is valid
                     if (mThis) {
                        mThis->mOpRequest.Clear();
                        mThis->mOpRequest.mBuffer = lBuffer;
                        OutputDebugString("Buffer stored in mOpRequest");
                     } else {
                        OutputDebugString("ERROR: mThis is NULL!");
                     }

                     // Success - process immediately
                     EndDialog( pWindow, IDOK );
                  }
                  else
                  {
                     // Failed - show error
                     char lErrorMsg[512];
                     DWORD dwError = GetLastError();
                     sprintf(lErrorMsg, "Server error.\nFetch Success: %d\nBuffer Empty: %d\nGetLastError: %lu", 
                            lFetchSuccess, lBuffer.IsEmpty(), dwError);
                     OutputDebugString("GetAddrCallBack: HTTPS Fetch FAILED");
                     OutputDebugString(lErrorMsg);
                     MessageBox( pWindow, lErrorMsg,
                                          MR_LoadString( IDS_IMR ), MB_ICONSTOP|MB_OK|MB_APPLMODAL );
                     EndDialog( pWindow, IDCANCEL );
                  }
               }
               catch (...) {
                  OutputDebugString("GetAddrCallBack: EXCEPTION caught!");
                  MessageBox( pWindow, "Exception occurred during HTTPS fetch",
                                       MR_LoadString( IDS_IMR ), MB_ICONSTOP|MB_OK|MB_APPLMODAL );
                  EndDialog( pWindow, IDCANCEL );
               }
            }
         }
         break;

      case WM_COMMAND:
         {
            switch(LOWORD( pWParam))
            {
               case IDCANCEL:
                  EndDialog( pWindow, IDCANCEL );
                  lReturnValue = TRUE;
                  break;
            }
         }
         break;
         
      default:
         break;
   }   

   return lReturnValue;

}


BOOL CALLBACK MR_InternetRoom::NetOpCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            // Setup message
            SetDlgItemText( pWindow, IDC_TEXT, mThis->mNetOpString );

            // Check if trying to connect to offline demo server
            if( gCurrentServerEntry >= 0 && gCurrentServerEntry < gNbServerEntries )
            {
               const char* lServerName = (const char*)gServerList[gCurrentServerEntry].mName;
               
               // If this is a demo/offline server, show immediate error
               if( lServerName && strstr(lServerName, "DEMO") && strstr(lServerName, "Offline") )
               {
                  CString lMessage;
                  lMessage = "This is a demo server entry that is currently offline.\n\n"
                            "The main HoverNet Internet Room server at steeky.com is not available.\n\n"
                            "Internet multiplayer rooms require an active server to function.\n\n"
                            "For single-player or local network play, use the other game modes.";
                  
                  MessageBox( pWindow, lMessage,
                             MR_LoadString( IDS_IMR ), MB_ICONSTOP|MB_OK|MB_APPLMODAL );
                  
                  EndDialog( pWindow, IDCANCEL );
                  break;
               }
            }

            // Initiate the request
            mThis->mOpRequest.Send( pWindow, gServerList[gCurrentServerEntry].mAddress, gServerList[gCurrentServerEntry].mPort, mThis->mNetOpRequest );

            // DEBUG: Log the actual send call
            {
               char lDebugBuf[512];
               sprintf(lDebugBuf, "DEBUG NetOpCallBack: Calling Send with ServerEntry=%d, Name=%s, IP=%u.%u.%u.%u, Port=%u",
                       gCurrentServerEntry,
                       (const char*)gServerList[gCurrentServerEntry].mName,
                       (gServerList[gCurrentServerEntry].mAddress >> 24) & 0xFF,
                       (gServerList[gCurrentServerEntry].mAddress >> 16) & 0xFF,
                       (gServerList[gCurrentServerEntry].mAddress >> 8) & 0xFF,
                       (gServerList[gCurrentServerEntry].mAddress) & 0xFF,
                       gServerList[gCurrentServerEntry].mPort);
               OutputDebugString(lDebugBuf);
            }

            // start a timeout timer
            SetTimer( pWindow, OP_TIMEOUT_EVENT, OP_TIMEOUT, NULL );            
         }
         break;

      case WM_TIMER:
         {
            KillTimer( pWindow, pWParam );
            lReturnValue = TRUE;

            MessageBox( pWindow, MR_LoadString( IDS_TO ),
                                 MR_LoadString( IDS_IMR ), MB_ICONSTOP|MB_OK|MB_APPLMODAL );

            EndDialog( pWindow, IDCANCEL );

            mThis->mOpRequest.Clear();

         }
         break;


      case MRM_NET_EVENT:
         mThis->mOpRequest.ProcessEvent( pWParam, pLParam );

         if( mThis->mOpRequest.IsReady() )
         {
            KillTimer( pWindow, OP_TIMEOUT_EVENT );

            BOOL lError = mThis->VerifyError( pWindow, mThis->mOpRequest.GetBuffer() );

            EndDialog( pWindow, lError?IDOK:IDCANCEL );
         }
         lReturnValue = TRUE;
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               KillTimer( pWindow, OP_TIMEOUT_EVENT );
               mThis->mOpRequest.Clear();

               lReturnValue = TRUE;
               break;
         }
         break;
   }
   return lReturnValue;

}



BOOL CALLBACK MR_InternetRoom::FastNetOpCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            // Setup message
            SetDlgItemText( pWindow, IDC_TEXT, mThis->mNetOpString );

            // Initiate the request
            mThis->mOpRequest.Send( pWindow, gServerList[gCurrentServerEntry].mAddress, gServerList[gCurrentServerEntry].mPort, mThis->mNetOpRequest );

            // start a timeout timer
            SetTimer( pWindow, OP_TIMEOUT_EVENT, FAST_OP_TIMEOUT, NULL );            
         }
         break;

      case WM_TIMER:
         {
            KillTimer( pWindow, pWParam );
            lReturnValue = TRUE;
            EndDialog( pWindow, IDCANCEL );
         }
         break;


      case MRM_NET_EVENT:
         mThis->mOpRequest.ProcessEvent( pWParam, pLParam );

         if( mThis->mOpRequest.IsReady() )
         {
            KillTimer( pWindow, OP_TIMEOUT_EVENT );

            // BOOL lError = mThis->VerifyError( pWindow, mThis->mOpRequest.GetBuffer() );

            EndDialog( pWindow, IDOK ); // humm always return IDOK
         }
         lReturnValue = TRUE;
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               KillTimer( pWindow, OP_TIMEOUT_EVENT );
               mThis->mOpRequest.Clear();

               lReturnValue = TRUE;
               break;
         }
         break;
   }
   return lReturnValue;
}

CString            gScoreRequestStr;
MR_InternetRequest gScoreRequest;

BOOL CALLBACK UpdateScoresCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         {
            // Setup message
            SetDlgItemText( pWindow, IDC_TEXT, MR_LoadString( IDS_REG_BEST_LAP ) );

            // Initiate the request
            gScoreRequest.Send( pWindow, gScoreServer.mAddress, gScoreServer.mPort, gScoreRequestStr );

            // start a timeout timer
            SetTimer( pWindow, OP_TIMEOUT_EVENT, SCORE_OP_TIMEOUT, NULL );            
         }
         break;

      case WM_TIMER:
         {
            // Timeout
            gScoreRequest.Clear();
            KillTimer( pWindow, pWParam );

            // Ask the user if he want to retry
            if( MessageBox( pWindow, MR_LoadString( IDS_TO ),
                            MR_LoadString( IDS_GAME_NAME ), MB_ICONSTOP|MB_RETRYCANCEL|MB_APPLMODAL ) == IDRETRY )
            {
               // Initiate the request
               gScoreRequest.Send( pWindow, gScoreServer.mAddress, gScoreServer.mPort, gScoreRequestStr );
               // start a timeout timer
               SetTimer( pWindow, OP_TIMEOUT_EVENT, SCORE_OP_TIMEOUT+3000, NULL );
            }
            else
            {
               EndDialog( pWindow, IDCANCEL );
            }
            lReturnValue = TRUE;
         }
         break;


      case MRM_NET_EVENT:
         gScoreRequest.ProcessEvent( pWParam, pLParam );

         if( gScoreRequest.IsReady() )
         {
            KillTimer( pWindow, OP_TIMEOUT_EVENT );

            EndDialog( pWindow, IDOK ); // humm always return IDOK
         }
         lReturnValue = TRUE;
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               KillTimer( pWindow, OP_TIMEOUT_EVENT );
               gScoreRequest.Clear();

               lReturnValue = TRUE;
               break;
         }
         break;
   }
   return lReturnValue;
}

BOOL MR_SendLadderResult( HWND pParentWindow,
                          const char* pWinAlias,  int pWinMajorID,  int pWinMinorID,
                          const char* pLoseAlias, int pLoseMajorID, int pLoseMinorID,
                          const char* pTrack, int pNbLap
                        )
{
   BOOL lReturnValue = FALSE;

   // First verify that both user are regs and the room is a ladder room
   if( (gCurrentServerEntry!=-1)&&(gServerList[gCurrentServerEntry].mType==2)&&(pWinMajorID!=-1)&&(pLoseMajorID!=-1) )
   {
      CString lWinID;
      CString lLoseID;
      CString lLaps;

      lWinID.Format(  "%d-%d", pWinMajorID, pWinMinorID );
      lLoseID.Format( "%d-%d", pLoseMajorID, pLoseMinorID );
      lLaps.Format( "%d", pNbLap );

      MReport_AddVariable( "winalias", pWinAlias, "Winner" );
      MReport_AddVariable( "winid", lWinID, "Winner ID" );
      MReport_AddVariable( "losealias", pLoseAlias, "Loser" );
      MReport_AddVariable( "loseid", lLoseID, "Loser ID" );
      MReport_AddVariable( "track", pTrack );
      MReport_AddVariable( "laps", lLaps, "Laps" );

      lReturnValue = MReport_Process( pParentWindow, gServerList[gCurrentServerEntry].mLadderIP, gServerList[gCurrentServerEntry].mLadderPort, gServerList[gCurrentServerEntry].mLadderReportURL );

      MReport_Clear( TRUE );
   }   
   return lReturnValue;
}


BOOL MR_SendRaceResult( HWND pParentWindow, const char* pTrack, int pBestLapTime, int pMajorID, int pMinorID, const char* pAlias, unsigned int pTrackSum, int pHoverModel, int pTotalTime, int pNbLap, int pNbPlayer )
{
   BOOL lReturnValue = FALSE;

   if( gScoreServer.mURL.GetLength() > 0 )
   {
      // Create the RequestStrign
      if( pMajorID != -1 )
      {
         gScoreRequestStr.Format( "%s?=RESULT%%%%%u%%%%%s%%%%%s[%d-%d]%%%%%u%%%%%d%%%%%d%%%%%d%%%%%d",
                                 (const char*)gScoreServer.mURL,
                                 pBestLapTime,
                                 (const char*)MR_Pad( pTrack ),
                                 (const char*)MR_Pad( pAlias ),
                                 pMajorID,
                                 pMinorID,
                                 pTrackSum,
                                 pHoverModel,
                                 pTotalTime,
                                 pNbLap,
                                 pNbPlayer
                                 );
      }
      else
      {
         gScoreRequestStr.Format( "%s?=RESULT%%%%%u%%%%%s%%%%%s%%%%%u%%%%%d%%%%%d%%%%%d%%%%%d",
                                 (const char*)gScoreServer.mURL,
                                 pBestLapTime,
                                 (const char*)MR_Pad( pTrack ),
                                 (const char*)MR_Pad( pAlias ),
                                 pTrackSum,
                                 pHoverModel,
                                 pTotalTime,
                                 pNbLap,
                                 pNbPlayer
                                 );
      }

      lReturnValue = DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_NET_PROGRESS ), pParentWindow, UpdateScoresCallBack  )==IDOK;
      
   }
   return lReturnValue;
}



CString MR_Pad( const char* pStr )
{
   CString lReturnValue;

   while( *pStr != 0 )
   {
      if( isalnum(*pStr) )
      {
         lReturnValue += *pStr;

      }
      else if( (*(const unsigned char*)pStr) <= 32 )
      {
         lReturnValue += "%20";
      }
      else
      {
         switch( *(const unsigned char*)pStr )
         {
            case 187:
               // Reserved character for prompt �
               break;

            case '$':
            case '-':
            case '_':
            case '.':
            case '+':
            case '!':
            case '*':
            case '\'':
            case '(':
            case ')':
            case ',':
            case ':':
            case '@':
            case '&':
            case '=':

               lReturnValue += *pStr;
               break;

            default:
               {
                  CString lNumber;

                  lNumber.Format( "%%%02x", *pStr );

                  lReturnValue += lNumber;
               }
         }
      }
      pStr++;
   }


   return lReturnValue;

}

CString     GetLine( const char* pSrc )
{
   return CString( pSrc, GetLineLen( pSrc ) );
}

int         GetLineLen( const char* pSrc )
{
   int lReturnValue = 0;

   if( pSrc != NULL )
   {
      const char* lEoL = strchr( pSrc, '\n' );

      if( lEoL != NULL )
      {
         lReturnValue = lEoL - pSrc;
      }
      else
      {
         lReturnValue = strlen( pSrc );
      }
   }
   return lReturnValue;
}


const char* GetNextLine( const char* pSrc )
{
   const char* lReturnValue = NULL;

   if( pSrc != NULL )
   {
      lReturnValue = pSrc+GetLineLen( pSrc );

      if( *lReturnValue == 0 )
      {
         lReturnValue = NULL;
      }
      else
      {
         lReturnValue++;
      }
   }
   return lReturnValue;
}


int FindFocusItem( HWND pWindow )
{
   int lReturnValue = -1;

   
   int lCount = ListView_GetItemCount( pWindow );

   for( int lCounter = 0; (lReturnValue==-1)&&(lCounter<lCount); lCounter++ )
   {
      if( ListView_GetItemState( pWindow, lCounter, LVIS_FOCUSED )==LVIS_FOCUSED )
      {
         LV_ITEM lItemData;

         lItemData.mask    = LVIF_PARAM;
         lItemData.iItem    = lCounter;
         lItemData.iSubItem = 0;

         if( ListView_GetItem( pWindow, &lItemData ) )
         {
            lReturnValue = lItemData.lParam;
         }
      }
   }

   return lReturnValue;
}









