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
   
   fprintf(logFile, "\nMR_TrackOpen: Opening track '%s'\n", pFileName);
   fflush(logFile);

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
   
   fprintf(logFile, "  Searching in: %s\n", (const char*)lPath);
   fflush(logFile);
   
   // Build the full path safely
   CString searchPath = lPath + pFileName + TRACK_EXT;
   fprintf(logFile, "  Full search path: %s\n", (const char*)searchPath);
   fflush(logFile);
   
   lHandle = _findfirst( (const char*)searchPath, &lFileInfo );

   if( lHandle == -1 )
   {
      fprintf(logFile, "  Track file not found in absolute path\n");
      fflush(logFile);
      // Try fallback to relative path
      lPath = TRACK_PATH2;
      searchPath = lPath + pFileName + TRACK_EXT;
      fprintf(logFile, "  Trying fallback path: %s\n", (const char*)searchPath);
      fflush(logFile);
      lHandle = _findfirst( (const char*)searchPath, &lFileInfo );
   }
   else
   {
      fprintf(logFile, "  Found in absolute path\n");
      fflush(logFile);
   }

   if( lHandle == -1 )
   {
       fprintf(logFile, "  ERROR: Track file not found in any path\n");
       fflush(logFile);
       MessageBox( pWindow, MR_LoadString( IDS_TRK_NOTFOUND ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );
   }
   else
   {
      _findclose( lHandle );

      lReturnValue = new MR_RecordFile;
      CString fullPath = lPath + pFileName + TRACK_EXT;
      
      fprintf(logFile, "  Opening file: %s\n", (const char*)fullPath);
      fflush(logFile);

      if( !lReturnValue->OpenForRead( fullPath, TRUE ) )
      {
         fprintf(logFile, "  ERROR: Failed to open record file\n");
         fflush(logFile);
         delete lReturnValue;
         lReturnValue = NULL;
         MessageBox( pWindow, MR_LoadString( IDS_BAD_TRK_FORMAT ), MR_LoadString( IDS_GAME_NAME ), MB_ICONERROR|MB_OK|MB_APPLMODAL );
         ASSERT( FALSE );
      }
      else
      {
         fprintf(logFile, "  Successfully opened, reading entry\n");
         fflush(logFile);
         TrackEntry lCurrentEntry;

         if( ReadTrackEntry( lReturnValue, &lCurrentEntry, pFileName ) )
         {
            fprintf(logFile, "  Successfully read entry, checking permissions\n");
            fflush(logFile);
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
      fprintf(logFile, "MR_TrackOpen returning: %p\n", lReturnValue);
      fflush(logFile);
      fclose(logFile);
   }
   return lReturnValue;

}


BOOL MR_SelectTrack( HWND pParentWindow, CString& pTrackFile, int& pNbLap, BOOL& pAllowWeapons, BOOL pAllowRegistred )
{
   BOOL lReturnValue = FALSE;
   gsSelectedEntry = -1;

   FILE* logFile = OpenTrackLog("a");
   fprintf(logFile, "\n--- MR_SelectTrack START ---\n");
   fflush(logFile);

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
      fprintf(logFile, "MR_SelectTrack: Showing track selection dialog\n");
      fflush(logFile);
      
      if( DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_TRACK_SELECT ), pParentWindow, TrackSelectCallBack ) == IDOK )
      {
         if( gsSelectedEntry != -1 )
         {
            pTrackFile = gsSortedTrackList[ gsSelectedEntry ]->mFileName;
            pNbLap = gsNbLaps;
            pAllowWeapons = gsAllowWeapons;
            lReturnValue = TRUE;
            fprintf(logFile, "MR_SelectTrack: User selected track='%s', laps=%d, weapons=%d\n", 
                              (const char*)pTrackFile, pNbLap, pAllowWeapons);
            fflush(logFile);
         }
      }
      else
      {
         fprintf(logFile, "MR_SelectTrack: User cancelled dialog\n");
         fflush(logFile);
         lReturnValue = FALSE;
      }
   }
   else
   {
      fprintf(logFile, "MR_SelectTrack: No tracks available\n");
      fflush(logFile);
      lReturnValue = FALSE;
   }
   
   CleanList();

   fprintf(logFile, "--- MR_SelectTrack END, returning %d ---\n", lReturnValue);
   fflush(logFile);
   if( logFile ) fclose(logFile);

   return lReturnValue;
}


static INT_PTR CALLBACK TrackSelectCallBack( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   INT_PTR lReturnValue = FALSE;
   int  lCounter;
   FILE* logFile = OpenTrackLog("a");
   
   fprintf(logFile, "\nTrackSelectCallBack: Message %u\n", pMsgId);
   fflush(logFile);

   switch( pMsgId )
   {
      // Catch environment modification events
      case WM_INITDIALOG:
         fprintf(logFile, "  WM_INITDIALOG: gsNbTrack=%d\n", gsNbTrack);
         fflush(logFile);
         
         // Init track file list
         for( lCounter = 0; lCounter < gsNbTrack; lCounter++ )
         {
            fprintf(logFile, "    Adding track %d: %s\n", lCounter, (const char*)(gsSortedTrackList[ lCounter ]->mFileName));
            fflush(logFile);
            SendDlgItemMessage( pWindow, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)(const char*)(gsSortedTrackList[ lCounter ]->mFileName) );
         }

         fprintf(logFile, "  Setting lap count and weapons\n");
         fflush(logFile);
         SetDlgItemInt( pWindow, IDC_NB_LAP, gsNbLaps, FALSE );  
         SendDlgItemMessage( pWindow, IDC_WEAPONS, BM_SETCHECK,BST_CHECKED, 0 );  
         SendDlgItemMessage( pWindow, IDC_NB_LAP_SPIN, UDM_SETRANGE, 0, MAKELONG(99, 1) );

         if(gsNbTrack > 0)
         {
            gsSelectedEntry = 0;            
            SendDlgItemMessage( pWindow, IDOK, WM_ENABLE, TRUE, 0 );
            fprintf(logFile, "  Setting description for track %d\n", gsSelectedEntry);
            fflush(logFile);
            SetDlgItemText( pWindow, IDC_DESCRIPTION, gsSortedTrackList[ gsSelectedEntry ]->mDescription );                        
            SendDlgItemMessage( pWindow, IDC_LIST, LB_SETCURSEL, 0, 0 );
            fprintf(logFile, "  WM_INITDIALOG completed successfully\n");
            fflush(logFile);
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
               fprintf(logFile, "  IDOK clicked, gsSelectedEntry=%d\n", gsSelectedEntry);
               fflush(logFile);
               if( gsSelectedEntry != -1 )
               {
                  gsNbLaps = GetDlgItemInt( pWindow, IDC_NB_LAP, NULL, FALSE );
                  gsAllowWeapons = (SendDlgItemMessage( pWindow, IDC_WEAPONS, BM_GETCHECK, 0, 0 ) == BST_CHECKED );
                  fprintf(logFile, "  Selected: track index %d, laps=%d, weapons=%s\n", gsSelectedEntry, gsNbLaps, gsAllowWeapons ? "YES" : "NO");
                  fflush(logFile);

                  if( !gAllowRegistred && (gsSortedTrackList[ gsSelectedEntry ]->mRegistrationMode != MR_FREE_TRACK) )
                  {
                     fprintf(logFile, "  Track requires registration\n");
                     fflush(logFile);
                     MessageBox( pWindow, MR_LoadString( IDS_SHOULD_REG ), MR_LoadString( IDS_GAME_NAME ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else if( (gsNbLaps < 1)||(gsNbLaps >= 100 ) )
                  {
                     fprintf(logFile, "  Invalid lap count\n");
                     fflush(logFile);
                     MessageBox( pWindow, MR_LoadString( IDS_LAP_RANGE ), MR_LoadString( IDS_GAME_NAME ), MB_ICONINFORMATION|MB_OK|MB_APPLMODAL );
                  }
                  else
                  {
                     fprintf(logFile, "  Dialog OK - ending dialog\n");
                     fflush(logFile);
                     EndDialog( pWindow, IDOK );
                  }
               }
               lReturnValue = TRUE;
               break;
         }
         break;
   }


   fprintf(logFile, "TrackSelectCallBack: Returning %d\n", lReturnValue);
   fflush(logFile);
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

   fprintf(logFile, "\n    ReadTrackEntry: Starting to read track\n");
   fflush(logFile);

   pRecordFile->SelectRecord( 0 );

   {
      int lMagicNumber;

      CArchive lArchive( pRecordFile, CArchive::load|CArchive::bNoFlushOnDelete );
   
      lArchive >> lMagicNumber;
      fprintf(logFile, "    Magic number read: 0x%X (expected: 0x%X)\n", lMagicNumber, MR_MAGIC_TRACK_NUMBER);
      fflush(logFile);

      if( lMagicNumber == MR_MAGIC_TRACK_NUMBER )
      {
         int lVersion;

         lArchive >> lVersion;
         fprintf(logFile, "    Version read: %d\n", lVersion);
         fflush(logFile);

         if( lVersion == 1 )
         {
            int lMinorID;
            int lMajorID;

            lArchive >> pDest->mDescription;
            lArchive >> lMinorID;
            lArchive >> lMajorID;
            
            fprintf(logFile, "    Description: '%s', Minor ID: %d, Major ID: %d\n", 
                    (const char*)pDest->mDescription, lMinorID, lMajorID);
            fflush(logFile);

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
               fprintf(logFile, "    ID validation: lIDOk=%d\n", lIDOk);
               fflush(logFile);
            }
            else
            {
               lIDOk = TRUE;
               fprintf(logFile, "    Skipping ID validation (lMajorID=%d, pFileName=%s)\n", lMajorID, pFileName ? pFileName : "NULL");
               fflush(logFile);
            }

            if( lIDOk )
            {
               lArchive >> pDest->mSortingIndex;
               lArchive >> pDest->mRegistrationMode;

               fprintf(logFile, "    Sorting index: %d, Registration: %d\n", 
                       pDest->mSortingIndex, pDest->mRegistrationMode);
               fflush(logFile);

               if( pDest->mRegistrationMode == MR_FREE_TRACK )
               {
                  lMagicNumber = 1;
                  lArchive >> lMagicNumber;
                  
                  fprintf(logFile, "    Final magic number for FREE_TRACK: 0x%X\n", lMagicNumber);
                  fflush(logFile);

                  if( lMagicNumber == MR_MAGIC_TRACK_NUMBER )
                  {
                     lReturnValue = TRUE;
                     fprintf(logFile, "    SUCCESS: Track entry validated\n");
                     fflush(logFile);
                  }
                  else
                  {
                     fprintf(logFile, "    ERROR: Final magic number mismatch\n");
                     fflush(logFile);
                  }
               }
               else
               {
                  lReturnValue = TRUE;
                  fprintf(logFile, "    SUCCESS: Registered track validated\n");
                  fflush(logFile);
               }
            }
            else
            {
               fprintf(logFile, "    ERROR: ID validation failed\n");
               fflush(logFile);
            }
         }
         else
         {
            fprintf(logFile, "    ERROR: Version mismatch (got %d, expected 1)\n", lVersion);
            fflush(logFile);
         }
      }
      else
      {
         fprintf(logFile, "    ERROR: Magic number mismatch\n");
         fflush(logFile);
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

   fprintf(logFile, "ReadList: Starting track search\n");
   fprintf(logFile, "Executable path: %s\n", (const char*)lPath);
   fflush(logFile);

   lHandle = _findfirst( lPath+"*" + TRACK_EXT, &lFileInfo );

   if( lHandle == -1 )
   {
      fprintf(logFile, "No tracks found in primary path, search failed\n");
      fflush(logFile);
   }
   else
   {
      fprintf(logFile, "Found tracks, reading entries\n");
      fflush(logFile);
      do
      {
         gsTrackList[ gsNbTrack ].mFileName = CString(lFileInfo.name, strlen(lFileInfo.name)-strlen(TRACK_EXT) );
         fprintf(logFile, "Track %d: %s\n", gsNbTrack, (const char*)gsTrackList[ gsNbTrack ].mFileName);
         fflush(logFile);

         // Open the file and read aditionnal info
         MR_RecordFile lRecordFile;
         CString fullPath = lPath+gsTrackList[ gsNbTrack ].mFileName+TRACK_EXT;
         fprintf(logFile, "  Opening: %s\n", (const char*)fullPath);
         fflush(logFile);

         if( !lRecordFile.OpenForRead( fullPath ) )
         {
            fprintf(logFile, "  ERROR: Failed to open record file\n");
            fflush(logFile);
            ASSERT( FALSE );
         }
         else
         {
            fprintf(logFile, "  Successfully opened, reading entry\n");
            fflush(logFile);
            if( ReadTrackEntry( &lRecordFile, &(gsTrackList[ gsNbTrack ]), NULL ) )
            {
               fprintf(logFile, "  Successfully read entry\n");
               fflush(logFile);
               gsNbTrack++;
            }
            else
            {
               fprintf(logFile, "  ERROR: Failed to read track entry\n");
               fflush(logFile);
               ASSERT( FALSE );
            }
         }
      }
      while( _findnext( lHandle, &lFileInfo ) == 0 );

      _findclose( lHandle );
   }
   
   fprintf(logFile, "ReadList: Completed, found %d tracks\n", gsNbTrack);
   fflush(logFile);
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

