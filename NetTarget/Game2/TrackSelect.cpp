// TrackSelect.cpp
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
#include "TrackSelect.h"
#include "../Util/Cursor.h"
#include "resource.h"
#include "License.h"
#include "io.h"
#include "../MazeCompiler/TrackCommonStuff.h"
#include "../Util/StrRes.h"
#include <direct.h>
#include <share.h>
#include <windows.h>


class TrackEntry
{
public:
   CString mFileName;
   CString mDescription;
   int     mRegistrationMode;
   int     mSortingIndex;
};

#define TRACK_PATH1 ".\\tracks\\"
#define TRACK_PATH2 "tracks\\"
#define TRACK_EXT   ".trk"

// Local functions
static INT_PTR CALLBACK TrackSelectCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );
static BOOL          ReadTrackEntry( MR_RecordFile* pRecordFile, TrackEntry* pDest, const char* pFileName );
static void          SortList();
static void          ReadList();
static void          CleanList();

static int           CompareFunc(const void *elem1, const void *elem2 );

static FILE* OpenTrackLog(const char* pMode)
{
   char lPath[MAX_PATH] = { 0 };
   const DWORD lPathLen = GetTempPathA(sizeof(lPath), lPath);
   if( (lPathLen > 0) && (lPathLen < sizeof(lPath)) )
   {
      strcat_s(lPath, sizeof(lPath), "HoverNet-Game2-TrackLoad.log");
      FILE* lFile = _fsopen(lPath, pMode, _SH_DENYNO);
      if( lFile != NULL )
      {
         return lFile;
      }
   }

   // Logging is diagnostic only. Always return a valid stream when the
   // temp directory is unavailable or another process owns the log.
   return fopen("NUL", "w");
}


// Track discovery diagnostics are disabled in production builds.
#define TRACK_LOG(...) ((void)0)

// Local variable
#define MAX_TRACK_ENTRIES   200
// #define MAX_TRACK_ENTRIES   5

static int  gsSelectedEntry = -1;
static int  gsNbTrack       = 0;

static TrackEntry  gsTrackList[ MAX_TRACK_ENTRIES ];
static TrackEntry* gsSortedTrackList[ MAX_TRACK_ENTRIES ];
static int         gsNbLaps;
static BOOL        gsAllowWeapons  = TRUE;
static BOOL        gAllowRegistred;


MR_RecordFile* MR_TrackOpen( HWND pWindow, const char* pFileName, BOOL pAllowRegistred )
{
   MR_RecordFile* lReturnValue = NULL;
   FILE* logFile = OpenTrackLog("a");

   TRACK_LOG( "\nMR_TrackOpen: Opening track '%s'\n", pFileName);
   (void)0;

   long lHandle;
   struct _finddata_t lFileInfo;

   // Get the executable directory
   char szPath[MAX_PATH];
   GetModuleFileNameA(NULL, szPath, sizeof(szPath));

   // Remove the filename to get just the directory
   char *pLastSlash = strrchr(szPath, '\\');
   if (pLastSlash) {
      *(pLastSlash + 1) = '\0';  // Include the backslash
   }

   // Append "tracks\" to get the track directory
   strcat_s(szPath, sizeof(szPath), "tracks\\");
   CString lPath = szPath;

   TRACK_LOG( "  Searching in: %s\n", (const char*)lPath);
   (void)0;

   // Build the full path safely
   CString searchPath = lPath + pFileName + TRACK_EXT;
   TRACK_LOG( "  Full search path: %s\n", (const char*)searchPath);
   (void)0;

   lHandle = _findfirst( (const char*)searchPath, &lFileInfo );

   if( lHandle == -1 )
   {
      TRACK_LOG( "  Track file not found in absolute path\n");
      (void)0;
      // Try fallback to relative path
      lPath = TRACK_PATH2;
      searchPath = lPath + pFileName + TRACK_EXT;
      TRACK_LOG( "  Trying fallback path: %s\n", (const char*)searchPath);
      (void)0;
      lHandle = _findfirst( (const char*)searchPath, &lFileInfo );
   }
   else
   {
      TRACK_LOG( "  Found in absolute path\n");
      (void)0;
   }

   if( lHandle == -1 )
   {
       TRACK_LOG( "  ERROR: Track file not found in any path\n");
       (void)0;
       MessageBox( pWindow, MR_LoadString( IDS_TRK_NOTFOUND ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );
   }
   else
   {
      _findclose( lHandle );

      lReturnValue = new MR_RecordFile;
      CString fullPath = lPath + pFileName + TRACK_EXT;

      TRACK_LOG( "  Opening file: %s\n", (const char*)fullPath);
      (void)0;

      if( !lReturnValue->OpenForRead( fullPath, TRUE ) )
      {
         TRACK_LOG( "  ERROR: Failed to open record file\n");
         (void)0;
         delete lReturnValue;
         lReturnValue = NULL;
         MessageBox( pWindow, MR_LoadString( IDS_BAD_TRK_FORMAT ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );
         ASSERT( FALSE );
      }
      else
      {
         TRACK_LOG( "  Successfully opened, reading entry\n");
         (void)0;
         TrackEntry lCurrentEntry;

         if( ReadTrackEntry( lReturnValue, &lCurrentEntry, pFileName ) )
         {
            TRACK_LOG( "  Successfully read entry, checking permissions\n");
            (void)0;
            if( !pAllowRegistred && (lCurrentEntry.mRegistrationMode != MR_FREE_TRACK) )
            {
               delete lReturnValue;
               lReturnValue = NULL;
               MessageBox( pWindow, MR_LoadString( IDS_SHOULD_REG ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );
            }
         }
         else
         {
            delete lReturnValue;
            lReturnValue = NULL;

            MessageBox( pWindow, MR_LoadString( IDS_BAD_TRK_FORMAT ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );

         }
      }

   }

   if(logFile) {
      TRACK_LOG( "MR_TrackOpen returning: %p\n", lReturnValue);
      (void)0;
      fclose(logFile);
   }
   return lReturnValue;

}


BOOL MR_SelectTrack( HWND pParentWindow, CString& pTrackFile, int& pNbLap, BOOL& pAllowWeapons, BOOL pAllowRegistred )
{
   BOOL lReturnValue = FALSE;
   gsSelectedEntry = -1;

   FILE* logFile = OpenTrackLog("a");
   TRACK_LOG( "\n--- MR_SelectTrack START ---\n");
   (void)0;

   // Load the entry list
   {
      MR_WAIT_CURSOR

      ReadList();
      SortList();

      gsNbLaps = 5; // Default value
      gsAllowWeapons = TRUE;

   }

   gAllowRegistred = pAllowRegistred;

   // Show the track selection dialog to let user choose
   if( gsNbTrack > 0 )
   {
      TRACK_LOG( "MR_SelectTrack: Showing track selection dialog\n");
      (void)0;

      if( DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_TRACK_SELECT ), pParentWindow, TrackSelectCallBack ) == IDOK )
      {
         if( gsSelectedEntry != -1 )
         {
            pTrackFile = gsSortedTrackList[ gsSelectedEntry ]->mFileName;
            pNbLap = gsNbLaps;
            pAllowWeapons = gsAllowWeapons;
            lReturnValue = TRUE;
            TRACK_LOG( "MR_SelectTrack: User selected track='%s', laps=%d, weapons=%d\n",
                              (const char*)pTrackFile, pNbLap, pAllowWeapons);
            (void)0;
         }
      }
      else
      {
         TRACK_LOG( "MR_SelectTrack: User cancelled dialog\n");
         (void)0;
         lReturnValue = FALSE;
      }
   }
   else
   {
      TRACK_LOG( "MR_SelectTrack: No tracks available\n");
      (void)0;
      lReturnValue = FALSE;
   }

   CleanList();

   TRACK_LOG( "--- MR_SelectTrack END, returning %d ---\n", lReturnValue);
   (void)0;
   if( logFile ) fclose(logFile);

   return lReturnValue;
}


static INT_PTR CALLBACK TrackSelectCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   INT_PTR lReturnValue = FALSE;
   int  lCounter;
   FILE* logFile = OpenTrackLog("a");

   TRACK_LOG( "\nTrackSelectCallBack: Message %u\n", pMsgId);
   (void)0;

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         TRACK_LOG( "  WM_INITDIALOG: gsNbTrack=%d\n", gsNbTrack);
         (void)0;

         // Init track file list
         for( lCounter = 0; lCounter < gsNbTrack; lCounter++ )
         {
            TRACK_LOG( "    Adding track %d: %s\n", lCounter, (const char*)(gsSortedTrackList[ lCounter ]->mFileName));
            (void)0;
            SendDlgItemMessage( pWindow, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(const char*)(gsSortedTrackList[ lCounter ]->mFileName) );
         }

         TRACK_LOG( "  Setting lap count and weapons\n");
         (void)0;
         SetDlgItemInt( pWindow, IDC_NB_LAP, gsNbLaps, FALSE );
         SendDlgItemMessage( pWindow, IDC_WEAPONS, BM_SETCHECK,BST_CHECKED, 0 );
         SendDlgItemMessage( pWindow, IDC_NB_LAP_SPIN, UDM_SETRANGE, 0, MAKELONG(99, 1) );

         if(gsNbTrack > 0)
         {
            gsSelectedEntry = 0;
            SendDlgItemMessage( pWindow, IDOK, WM_ENABLE, TRUE, 0 );
            TRACK_LOG( "  Setting description for track %d\n", gsSelectedEntry);
            (void)0;
            SetDlgItemText( pWindow, IDC_DESCRIPTION, gsSortedTrackList[ gsSelectedEntry ]->mDescription );
            SendDlgItemMessage( pWindow, IDC_LIST, LB_SETCURSEL, 0, 0 );
            TRACK_LOG( "  WM_INITDIALOG completed successfully\n");
            (void)0;
         }
         else
         {

            gsSelectedEntry = -1;
            SendDlgItemMessage( pWindow, IDOK, WM_ENABLE, FALSE, 0 );
            SetDlgItemText( pWindow, IDC_DESCRIPTION, MR_LoadString( IDS_NO_SELECT ) );
            SendDlgItemMessage( pWindow, IDC_LIST, LB_SETCURSEL, -1, 0 );
         }
         lReturnValue = TRUE;
         break;

      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {
            case IDC_LIST:
               switch( HIWORD( pWParam ) )
               {
                  case LBN_SELCHANGE:
                     gsSelectedEntry = SendDlgItemMessage( pWindow, IDC_LIST, LB_GETCURSEL, 0, 0 );

                     if( (gsNbTrack==0)||(gsSelectedEntry == -1) )
                     {
                        SendDlgItemMessage( pWindow, IDOK, WM_ENABLE, FALSE, 0 );
                        SetDlgItemText( pWindow, IDC_DESCRIPTION, MR_LoadString( IDS_NO_SELECT ) );
                     }
                     else
                     {
                        SendDlgItemMessage( pWindow, IDOK, WM_ENABLE, TRUE, 0 );
                        SetDlgItemText( pWindow, IDC_DESCRIPTION, gsSortedTrackList[ gsSelectedEntry ]->mDescription );
                     }
                     break;
               }
               break;


            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               lReturnValue = TRUE;
               break;

            case IDOK:
               TRACK_LOG( "  IDOK clicked, gsSelectedEntry=%d\n", gsSelectedEntry);
               (void)0;
               if( gsSelectedEntry != -1 )
               {
                  gsNbLaps = GetDlgItemInt( pWindow, IDC_NB_LAP, NULL, FALSE );
                  gsAllowWeapons = (SendDlgItemMessage( pWindow, IDC_WEAPONS, BM_GETCHECK, 0, 0 ) == BST_CHECKED );
                  TRACK_LOG( "  Selected: track index %d, laps=%d, weapons=%s\n", gsSelectedEntry, gsNbLaps, gsAllowWeapons ? "YES" : "NO");
                  (void)0;

                  if( !gAllowRegistred && (gsSortedTrackList[ gsSelectedEntry ]->mRegistrationMode != MR_FREE_TRACK) )
                  {
                     TRACK_LOG( "  Track requires registration\n");
                     (void)0;
                     MessageBox( pWindow, MR_LoadString( IDS_SHOULD_REG ), MR_LoadString( IDS_GAME_NAME ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else if( (gsNbLaps < 1)||(gsNbLaps >= 100 ) )
                  {
                     TRACK_LOG( "  Invalid lap count\n");
                     (void)0;
                     MessageBox( pWindow, MR_LoadString( IDS_LAP_RANGE ), MR_LoadString( IDS_GAME_NAME ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else
                  {
                     TRACK_LOG( "  Dialog OK - ending dialog\n");
                     (void)0;
                     EndDialog( pWindow, IDOK );
                  }
               }
               lReturnValue = TRUE;
               break;
         }
         break;
   }


   TRACK_LOG( "TrackSelectCallBack: Returning %d\n", lReturnValue);
   (void)0;
   if( logFile ) fclose(logFile);

   return lReturnValue;
}

MR_TrackAvail MR_GetTrackAvail( const char* pFileName, BOOL pAllowRegistred )
{
   MR_TrackAvail lReturnValue = eTrackNotFound;

   long lHandle;
   struct _finddata_t lFileInfo;
   CString            lPath = TRACK_PATH1;

   lHandle = _findfirst( lPath+pFileName + TRACK_EXT, &lFileInfo );

   if( lHandle == -1 )
   {
      lPath = TRACK_PATH2;
      lHandle = _findfirst( lPath+pFileName + TRACK_EXT, &lFileInfo );
   }

   if( lHandle != -1 )
   {
      _findclose( lHandle );

      MR_RecordFile lFile;

      if( !lFile.OpenForRead( lPath+pFileName+TRACK_EXT ) )
      {
         ASSERT( FALSE );
      }
      else
      {
         TrackEntry lCurrentEntry;

         if( ReadTrackEntry( &lFile, &lCurrentEntry, pFileName ) )
         {

            if( !pAllowRegistred && (lCurrentEntry.mRegistrationMode != MR_FREE_TRACK) )
            {
               lReturnValue = eMustBuy;
            }
            else
            {
               lReturnValue = eTrackAvail;
            }
         }
      }
   }
   return lReturnValue;
}


BOOL ReadTrackEntry( MR_RecordFile* pRecordFile, TrackEntry* pDest, const char* pFileName )
{
   BOOL lReturnValue = FALSE;
   FILE* logFile = OpenTrackLog("a");

   TRACK_LOG( "\n    ReadTrackEntry: Starting to read track\n");
   (void)0;

   pRecordFile->SelectRecord( 0 );

   {
      int lMagicNumber;

      CArchive lArchive( pRecordFile, CArchive::load|CArchive::bNoFlushOnDelete );

      lArchive >> lMagicNumber;
      TRACK_LOG( "    Magic number read: 0x%X (expected: 0x%X)\n", lMagicNumber, MR_MAGIC_TRACK_NUMBER);
      (void)0;

      if( lMagicNumber == MR_MAGIC_TRACK_NUMBER )
      {
         int lVersion;

         lArchive >> lVersion;
         TRACK_LOG( "    Version read: %d\n", lVersion);
         (void)0;

         if( lVersion == 1 )
         {
            int lMinorID;
            int lMajorID;

            lArchive >> pDest->mDescription;
            lArchive >> lMinorID;
            lArchive >> lMajorID;

            TRACK_LOG( "    Description: '%s', Minor ID: %d, Major ID: %d\n",
                    (const char*)pDest->mDescription, lMinorID, lMajorID);
            (void)0;

            BOOL lIDOk = FALSE;

            if( (lMajorID != 0)&&(pFileName!=NULL) )
            {
               // Verify that filename fit with userID
               int lMinID = -1;
               int lMajID = -1;

               const char* lStr = strrchr( pFileName, '[' );

               if( lStr != NULL )
               {
                  sscanf( lStr+1, "%d-%d", &lMajID, &lMinID );

                  if( (lMinID == lMinorID)&&(lMajorID==lMajID) )
                  {
                     lIDOk = TRUE;
                  }
               }
               TRACK_LOG( "    ID validation: lIDOk=%d\n", lIDOk);
               (void)0;
            }
            else
            {
               lIDOk = TRUE;
               TRACK_LOG( "    Skipping ID validation (lMajorID=%d, pFileName=%s)\n", lMajorID, pFileName ? pFileName : "NULL");
               (void)0;
            }

            if( lIDOk )
            {
               lArchive >> pDest->mSortingIndex;
               lArchive >> pDest->mRegistrationMode;

               TRACK_LOG( "    Sorting index: %d, Registration: %d\n",
                       pDest->mSortingIndex, pDest->mRegistrationMode);
               (void)0;

               if( pDest->mRegistrationMode == MR_FREE_TRACK )
               {
                  lMagicNumber = 1;
                  lArchive >> lMagicNumber;

                  TRACK_LOG( "    Final magic number for FREE_TRACK: 0x%X\n", lMagicNumber);
                  (void)0;

                  if( lMagicNumber == MR_MAGIC_TRACK_NUMBER )
                  {
                     lReturnValue = TRUE;
                     TRACK_LOG( "    SUCCESS: Track entry validated\n");
                     (void)0;
                  }
                  else
                  {
                     TRACK_LOG( "    ERROR: Final magic number mismatch\n");
                     (void)0;
                  }
               }
               else
               {
                  lReturnValue = TRUE;
                  TRACK_LOG( "    SUCCESS: Registered track validated\n");
                  (void)0;
               }
            }
            else
            {
               TRACK_LOG( "    ERROR: ID validation failed\n");
               (void)0;
            }
         }
         else
         {
            TRACK_LOG( "    ERROR: Version mismatch (got %d, expected 1)\n", lVersion);
            (void)0;
         }
      }
      else
      {
         TRACK_LOG( "    ERROR: Magic number mismatch\n");
         (void)0;
      }
   }
   if(logFile) fclose(logFile);
   return lReturnValue;
}

void SortList()
{
   // Init pointer list
   if( gsNbTrack > 0 )
   {
      for( int lCounter = 0; lCounter < gsNbTrack; lCounter++ )
      {
         gsSortedTrackList[ lCounter ] = &(gsTrackList[lCounter]);
      }

      qsort( gsSortedTrackList,
             gsNbTrack,
             sizeof( gsSortedTrackList[0] ),
             CompareFunc                       );


   }

}

int CompareFunc(const void *elem1, const void *elem2 )
{
   int lReturnValue = 0;
   const TrackEntry** lElem1 = (const TrackEntry**)elem1;
   const TrackEntry** lElem2 = (const TrackEntry**)elem2;

   lReturnValue = (*lElem1)->mSortingIndex-(*lElem2)->mSortingIndex;

   if( lReturnValue == 0 )
   {
      lReturnValue = strcmp( (*lElem1)->mFileName,(*lElem2)->mFileName );
   }

   return lReturnValue;
}


void ReadList()
{
   long lHandle;
   struct _finddata_t lFileInfo;
   FILE* logFile = OpenTrackLog("w");

   // Get the executable directory
   char szPath[MAX_PATH];
   GetModuleFileNameA(NULL, szPath, sizeof(szPath));

   // Remove the filename to get just the directory
   char *pLastSlash = strrchr(szPath, '\\');
   if (pLastSlash) {
      *(pLastSlash + 1) = '\0';  // Include the backslash
   }

   // Append "tracks\" to get the track directory
   strcat_s(szPath, sizeof(szPath), "tracks\\");
   CString lPath = szPath;

   CleanList();

   TRACK_LOG( "ReadList: Starting track search\n");
   TRACK_LOG( "Executable path: %s\n", (const char*)lPath);
   (void)0;

   lHandle = _findfirst( lPath+"*" + TRACK_EXT, &lFileInfo );

   if( lHandle == -1 )
   {
      TRACK_LOG( "No tracks found in primary path, search failed\n");
      (void)0;
   }
   else
   {
      TRACK_LOG( "Found tracks, reading entries\n");
      (void)0;
      do
      {
         gsTrackList[ gsNbTrack ].mFileName = CString(lFileInfo.name, strlen(lFileInfo.name)-strlen(TRACK_EXT) );
         TRACK_LOG( "Track %d: %s\n", gsNbTrack, (const char*)gsTrackList[ gsNbTrack ].mFileName);
         (void)0;

         // Open the file and read aditionnal info
         MR_RecordFile lRecordFile;
         CString fullPath = lPath+gsTrackList[ gsNbTrack ].mFileName+TRACK_EXT;
         TRACK_LOG( "  Opening: %s\n", (const char*)fullPath);
         (void)0;

         if( !lRecordFile.OpenForRead( fullPath ) )
         {
            TRACK_LOG( "  ERROR: Failed to open record file\n");
            (void)0;
            ASSERT( FALSE );
         }
         else
         {
            TRACK_LOG( "  Successfully opened, reading entry\n");
            (void)0;
            if( ReadTrackEntry( &lRecordFile, &(gsTrackList[ gsNbTrack ]), NULL ) )
            {
               TRACK_LOG( "  Successfully read entry\n");
               (void)0;
               gsNbTrack++;
            }
            else
            {
               TRACK_LOG( "  ERROR: Failed to read track entry\n");
               (void)0;
               ASSERT( FALSE );
            }
         }
      }
      while( _findnext( lHandle, &lFileInfo ) == 0 );

      _findclose( lHandle );
   }

   TRACK_LOG( "ReadList: Completed, found %d tracks\n", gsNbTrack);
   (void)0;
   if( logFile ) fclose(logFile);
}

void CleanList()
{
   for( int lCounter = 0; lCounter < gsNbTrack; lCounter++ )
   {
      gsTrackList[ lCounter ].mFileName    = "";
      gsTrackList[ lCounter ].mDescription = "";
   }
   gsNbTrack = 0;
}

