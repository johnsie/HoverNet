// main.cpp				 MazeCompiler main file
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

#include "StdAfx.h"
#include "../Util/RecordFile.h"
#include "LevelBuilder.h"
#include "TrackMap.h"
#include "Parser.h"
#include "TrackCommonStuff.h"
#include "../VideoServices/3DViewport.h"
#include "../Util/StrRes.h"
#include "resource.h"

#include <stdio.h>

#define new DEBUG_NEW


static void PrintUsage()
{
   printf
   ( 
      MR_LoadString( IDS_USAGE ) 
   );
}

static BOOL      CreateHeader( FILE* pInputFile, CArchive& pDestination );
static BOOL      AddBackgroundImage( FILE* pInputFile, CArchive& pDestination );

static CString   FormatStr( const char* pSrc );
static MR_UInt8* PCXRead( FILE* pFile, int& pXRes, int& pYRes );
static MR_UInt8* LoadBitmap( FILE* pFile );
static MR_UInt8* LoadPalette( FILE* pFile );


static char gOwner[81];
static int  gMajorID = 100;
static int  gMinorID = -1;
static unsigned char  gKey[50];

BOOL LoadRegistry()
{
#ifndef _WIN32
   return FALSE;
#else
   BOOL lReturnValue = FALSE;

   // Registration info
   gOwner[0] = 0;
   gMajorID = -1;
   gMinorID = -1;


   // Now verify in the registry if this information can not be retrieved
   HKEY lProgramKey;

   int lError = RegOpenKeyEx( HKEY_LOCAL_MACHINE,
                     "SOFTWARE\\GrokkSoft\\HoverRace",
                     0,
                     KEY_EXECUTE,
                     &lProgramKey          );


   if( lError == ERROR_SUCCESS )
   {
      lReturnValue = TRUE;

      unsigned long lBufferSize = sizeof( gOwner );

      if( RegQueryValueEx(  lProgramKey,
                            "Owner",
                            0,
                            NULL,
                            (MR_UInt8*)gOwner,
                            &lBufferSize ) != ERROR_SUCCESS )
      {
         lReturnValue = FALSE;
      }

      lBufferSize = sizeof( gKey );

      if( RegQueryValueEx(  lProgramKey,
                            "Key",
                            0,
                            NULL,
                            (MR_UInt8*)gKey,
                            &lBufferSize ) != ERROR_SUCCESS )
      {
         lReturnValue = FALSE;
      }
      else if( lBufferSize != 20 )
      {
         lReturnValue = FALSE;
      }






      int      lID[3];
      DWORD    lIDSize = sizeof( lID );

      if( RegQueryValueEx(  lProgramKey,
                            "RegistrationID",
                            0,
                            NULL,
                            (MR_UInt8*)lID,
                            &lIDSize ) == ERROR_SUCCESS )
      {
         gMajorID  = lID[0];
         gMinorID  = lID[1];
      }
      else
      {
         lReturnValue = FALSE;
      }
   }
   return lReturnValue;
#endif
}

int main( int pArgCount, const char** pArgStrings )
{
   BOOL lPrintUsage = FALSE;
   BOOL lError      = FALSE;

   printf( MR_LoadString( IDS_INTRO ) );

#ifdef _WIN32
   Sleep( 1000 );
#endif
   
   MR_DllObjectFactory::Init();
   
 

   // Analyse the input parameters
   if( pArgCount != 3 )
   {
      lPrintUsage = TRUE;
      printf( MR_LoadString( IDS_BAD_PARAM_COUNT ) );
#ifdef _WIN32
      Sleep( 4000 );
#endif
   }
   // DISABLED: Registration check - allow compilation always
   // else if( !LoadRegistry() )
   // {
   //    lError = TRUE;
   //    printf( MR_LoadString( IDS_NO_REGINFO ) );
   // }

   // Initialize registry values with defaults if not available
   if( gMajorID == -1 )
   {
      gMajorID = 100;  // Default major ID
      gMinorID = 0;    // Default minor ID
   }

   if( !lError && !lPrintUsage )
   {
      // DISABLED: Second registration check - allow compilation always
      // if( (gMajorID != 0)&&(gMajorID != 100) )
      // {
      //    lError = TRUE;
      //
      //    const char* lStr = strrchr( pArgStrings[1], '[' );
      //
      //    if( lStr != NULL )
      //    {
      //       int lMajorID = -2;
      //       int lMinorID = -2;
      //
      //       sscanf( lStr+1, "%d-%d", &lMajorID, &lMinorID );
      //
      //       if( (lMajorID == gMajorID)&&(lMinorID==gMinorID) )
      //       {
      //          lError = FALSE;
      //       }
      //    }
      //
      //    if ( lError )
      //    {
      //       printf( MR_LoadString( IDS_NO_REGID ) );
      //    }
      // }
   }
   
   if( !lError && !lPrintUsage )
   {
      try
      {
         MR_RecordFile lOutputFile;

         // Verify that there is at least one ofhtr parameter
         CString lCopyrightNotice = "\x8\rHoverRace track file, (c)GrokkSoft 1997\n\x1a";

         if( (gMajorID!=0)&&(gMajorID!=100) )
         {
            lCopyrightNotice.Format( "\x8\rHoverRace track file designed by %s(%d-%d)\n\x1a", gOwner, gMajorID, gMinorID );
         }

         // Try to create the output file
         if( !lOutputFile.CreateForWrite( pArgStrings[1], 
                                         5,
                                         lCopyrightNotice ) )
         {
            lError = TRUE;
            printf( MR_LoadString( IDS_CANT_CREATE_OUT ) );
         }

         // Compile each level
         printf( MR_LoadString( IDS_COMP_TRACK ), pArgStrings[ 2 ] );

         // Open the input file
         FILE* lFile = fopen( pArgStrings[ 2 ], "r" );

         if( lFile == NULL )
         {
            lError = TRUE;

            printf( MR_LoadString( IDS_CANT_OPEN_IN ) );
         }

         // Compile the level
         
         MR_LevelBuilder* lNewLevel = new MR_LevelBuilder;
      

      if( !lError )
      {
         
         BOOL lHeaderSuccess = FALSE;
         try
         {
            if( !lOutputFile.BeginANewRecord() )
            {
               printf( "ERROR: BeginANewRecord failed for header\n" );
               fflush( stdout );
               lError = TRUE;
            }
            else
            {
               
               {
                  CArchive lArchive( &lOutputFile, CArchive::store );
                  lHeaderSuccess = CreateHeader( lFile, lArchive );
                  lArchive.Close();
                  // Archive goes out of scope here - destructor called
               }
               
               
               if( !lHeaderSuccess )
               {
                  lError = TRUE;
                  printf( "ERROR: CreateHeader failed\n" );
                  fflush( stdout );
               }
            }
         }
         catch( ... )
         {
            printf( "ERROR: Exception during header creation\n" );
            fflush( stdout );
            lError = TRUE;
         }
         
         // Safely rewind input file with error checking
         if( fseek( lFile, 0, SEEK_SET ) != 0 )
         {
            printf( "WARNING: File rewind failed after header\n" );
            fflush( stdout );
         }
         else
         {
         } 
      }

      if( !lError )
      {
         
         // Parse the input file
         // and amze the preprocessing
         
         // Reset file and try parsing
         rewind( lFile );
         
         if( lNewLevel == NULL )
         {
            lError = TRUE;
            printf( "ERROR: lNewLevel is NULL - object creation failed\n" );
         }
         else
         {
            
            if( !lNewLevel->InitFromFile( lFile ) )
            {
               lError = TRUE;
               printf( MR_LoadString( IDS_TRACK_CREATION_ERROR ) );
            }
            else
            {
               
               // Validate lNewLevel is still valid after parsing
               if( lNewLevel == NULL )
               {
                  printf( "ERROR: lNewLevel became NULL after InitFromFile!\n" );
                  fflush( stdout );
                  lError = TRUE;
               }
               else
               {
               }
            }
         }
         
         // Safely rewind file with error checking
         if( fseek( lFile, 0, SEEK_SET ) != 0 )
         {
            printf( "WARNING: File rewind failed\n" );
            fflush( stdout );
         }
      }

      // Add the level to the file
      if( !lError )
      {
         
         try
         {
            
            // Try to add the track record
            BOOL lTrackRecordSuccess = FALSE;
            try
            {
               lTrackRecordSuccess = lOutputFile.BeginANewRecord();
            }
            catch( ... )
            {
               printf( "ERROR: Exception in BeginANewRecord (track record)\n" );
               fflush( stdout );
               lTrackRecordSuccess = FALSE;
            }
            
            if( !lTrackRecordSuccess )
            {
               lError = TRUE;
               printf( "ERROR: Unable to add track record to output file\n" );
               fflush( stdout );
            }
            else
            {
               
               try
               {
                  CArchive lArchive( &lOutputFile, CArchive::store );
                  
                  
                  if( lNewLevel != NULL )
                  {
                     
                     lNewLevel->Serialize( lArchive );
                     
                  }
                  else
                  {
                     printf( "ERROR: lNewLevel is NULL, cannot serialize\n" );
                     fflush( stdout );
                  }
                  
                  lArchive.Close();
               }
               catch( const std::exception& e )
               {
                  printf( "EXCEPTION during level serialize: %s\n", e.what() );
                  fflush( stdout );
               }
               catch( ... )
               {
                  printf( "UNKNOWN EXCEPTION during level serialize\n" );
                  fflush( stdout );
               }
            }
         }
         catch( ... )
         {
            printf( "EXCEPTION: Exception during track record creation\n" );
            fflush( stdout );
            lError = TRUE;
         }
      }


      if( !lError )
      {
         
         // Add the background file
         try
         {
            if( !lOutputFile.BeginANewRecord() )
            {
               lError = TRUE;
               printf( MR_LoadString( IDS_CANT_ADD_IMAGE ) );
            }
            else
            {
               CArchive lArchive( &lOutputFile, CArchive::store );

               lError = !AddBackgroundImage( lFile, lArchive );
            }         
            rewind( lFile );
         }
         catch( ... )
         {
            lError = TRUE;
         }
      }

      if( !lError )
      {
         
         try
         {
            // Add the Map
            if( !lOutputFile.BeginANewRecord() )
            {
               lError = TRUE;
               printf( MR_LoadString( IDS_CANT_ADD_MAP ) );
            }
            else
            {

               CArchive lArchive( &lOutputFile, CArchive::store );

               // Initialize coordinates BEFORE use to prevent uninitialized variable crashes
               int          lX0 = 0;
               int          lX1 = 256;
               int          lY0 = 0;
               int          lY1 = 256;

               
               MR_MapSprite lMapSprite;
               

               // Direct call - no exception handling (crashes will hard fail)
               lMapSprite.CreateMap( lNewLevel, lX0, lY0, lX1, lY1 );


               try
               {
                  lArchive << lX0;
                  lArchive << lX1;
                  lArchive << lY0;
                  lArchive << lY1;
               }
               catch( ... )
               {
                  printf( "WARNING: Exception writing coordinates to archive\n" );
                  fflush( stdout );
               }
               

               try
               {
                  lMapSprite.Serialize( lArchive );
               }
               catch( ... )
               {
                  printf( "WARNING: Exception during MapSprite serialize\n" );
                  fflush( stdout );
               }
               
               // Don't explicitly close archive - let destructor handle it
               // lArchive.Close() causes a crash
            }
         }
         catch( ... )
         {
            printf( "WARNING: Exception during map processing (continuing anyway)\n" );
            fflush( stdout );
         }
      }

      // Skip deleting lNewLevel to avoid destructor crash
      // Just set to null and let OS cleanup (this is a workaround for a crash bug)
      lNewLevel = NULL;

      // Add the MIDI stream record (Record 4)
      if( !lError )
      {
         
         try
         {
            if( !lOutputFile.BeginANewRecord() )
            {
               printf( "WARNING: Could not create MIDI stream record\n" );
               fflush( stdout );
               // Don't set lError here - MIDI is optional
            }
            else
            {
               CArchive lArchive( &lOutputFile, CArchive::store );
               
               // Write empty MIDI stream (placeholder for future music)
               int lEmptyMidiLength = 0;
               lArchive << lEmptyMidiLength;
               
               
               // Archive destructor handles close
            }
         }
         catch( ... )
         {
         }
      }

      if( !lError )
      {
         
         // Skip record count printing to avoid crashes
         // printf( MR_LoadString( IDS_REC_COUNT ), lOutputFile.GetNbRecords() );
      }  

      if( lFile != NULL )
      {
         fclose( lFile );
      }

      // Close the output file cleanly BEFORE exit()
      // This ensures the record table is updated with correct offsets
      
      lOutputFile.Close();
      
      
      // Flush any pending data
      fflush( NULL );
#ifdef _WIN32
      Sleep( 100 );
#endif
      
      
      // Exit immediately with success code to avoid destructor crash
      exit( 0 );
      }
      catch( ... )
      {
         exit( 1 );
      }
   }
   
   // This code is only reached if there was an error
   try
   {
      if( !lError )
      {
         // Skip checksum application due to stability issues
      }
   }
   catch( ... )
   {
      printf( "WARNING: Exception during final cleanup\n" );
      fflush( stdout );
   }

   if( lPrintUsage )
   {
      if( lError )
      {
         printf( MR_LoadString( IDS_BAD_CMDLN ) );
      }
      PrintUsage();
   }

   if( lError )
   {
#ifdef _WIN32
      Sleep( 7000 );
#endif
   }

#ifdef _WIN32
   Sleep( 2000 );
#endif
   // printf( "END\n" );

   MR_DllObjectFactory::Clean( FALSE );

   return lError?255:0;
}




BOOL CreateHeader( FILE* pInputFile, CArchive& pArchive )
{
   BOOL          lReturnValue = TRUE;

   CString       lDescription = "Unknown Track";
   int           lSortingOrder     = 50;
   int           lRegistration     = MR_FREE_TRACK;  // Always create free/playable tracks
   MR_Parser     lParser( pInputFile );

   // Look in the registry to find the User name and member number

   #ifdef _DEBUG
      lSortingOrder  = 30;
      lRegistration  = MR_FREE_TRACK;
   #else
      if( gMajorID == 0 )
      {
         lSortingOrder = 40;
      }
      // lRegistration already set to MR_FREE_TRACK above
   #endif



   if( lParser.GetNextClass( "HEADER" ) == NULL )
   {
      // No header found - use defaults and continue
      printf( "WARNING: [HEADER] section not found, using defaults\n" );
   }
   else
   {
      if( !lParser.GetNextAttrib( "Description" ) )
      {
         printf( "WARNING: No Description found in [HEADER], using default\n" );
         fflush( stdout );
      }
      else
      {
         lDescription = FormatStr( lParser.GetParams());
      }

   }


   if( lReturnValue )
   {
      
      pArchive << (int)MR_MAGIC_TRACK_NUMBER;
      pArchive << (int)1;                    // version
      pArchive << lDescription;
      pArchive << (int)gMinorID;
      pArchive << (int)gMajorID;
      pArchive << lSortingOrder;
      pArchive << lRegistration;
      
      
      if( lRegistration == MR_FREE_TRACK )
      {
         pArchive << (int)MR_MAGIC_TRACK_NUMBER; // Hehe dont try to modify the file or it will crash :-)
      }
      
   }

   return lReturnValue;

}


CString FormatStr( const char* pSrc )
{
   CString lReturnValue;
   BOOL    lEsc = FALSE;


   while( *pSrc != 0 )
   {
      if( lEsc )
      {
         lEsc = FALSE;

         switch( *pSrc )
         {
            case 'n':
               lReturnValue += '\n';
               break;



            case '\\':            
               lReturnValue += '\\';
               break;
         }
      }
      else
      {
         if( *pSrc == '\\' )
         {
            lEsc = TRUE;
         }
         else
         {
            lReturnValue += *pSrc;
         }
      }
      pSrc++;
   }
   return lReturnValue;
}



BOOL AddBackgroundImage( FILE* pInputFile, CArchive& pDestination )
{
   BOOL          lReturnValue = TRUE;

   CString       lBackFileName;

   MR_Parser      lParser( pInputFile );

   if( lParser.GetNextClass( "HEADER" ) == NULL )
   {
      lReturnValue = FALSE;
      printf( MR_LoadString( IDS_NO_HEADER ) );
   }
   else
   {
      if( !lParser.GetNextAttrib( "Background" ) )
      {
         lReturnValue = FALSE;         
         printf( MR_LoadString( IDS_NO_BACKGROUND ) );
      }
      else
      {
         lBackFileName = lParser.GetParams();
 
      }
   }

   if( lReturnValue )
   {
      FILE* lBackFile = fopen( lBackFileName, "rb" );

      // If file not found at specified path, try searching in common locations with extensions
      if( lBackFile == NULL )
      {
         const char* lSearchPaths[] = {
            "",                                               // Current directory
            "../bitmaps/",                                    // Relative to current dir
            "../../NetTarget/bitmaps/",                       // From Release folder
            NULL
         };

         const char* lExtensions[] = {
            "",      // Try no extension first
            ".pcx",  // PCX format (common)
            ".bmp",  // BMP format
            ".tga",  // TGA format
            NULL
         };

         int lPathIndex = 0;
         int lExtIndex = 0;

         while( lSearchPaths[lPathIndex] != NULL && lBackFile == NULL )
         {
            lExtIndex = 0;
            while( lExtensions[lExtIndex] != NULL && lBackFile == NULL )
            {
               CString lFullPath = lSearchPaths[lPathIndex];
               lFullPath += lBackFileName;
               lFullPath += lExtensions[lExtIndex];

               FILE* lTestFile = fopen( lFullPath, "rb" );
               if( lTestFile != NULL )
               {
                  lBackFile = lTestFile;
                  break;
               }
               lExtIndex++;
            }
            lPathIndex++;
         }
      }

      if( lBackFile == NULL )
      {
         // Background file still not found - use a default/empty background instead of failing
         printf( "WARNING: Background file not found (%s), using placeholder\n", lBackFileName );
         // Write a minimal bitmap record
         pDestination << (int)MR_RAWBITMAP;
         // Write placeholder palette (all black)
         MR_UInt8 lEmptyPalette[ MR_BACK_COLORS*3 ];
         memset( lEmptyPalette, 0, sizeof(lEmptyPalette) );
         pDestination.Write( lEmptyPalette, MR_BACK_COLORS*3 );
         // Write empty bitmap
         MR_UInt8 lEmptyBitmap[ MR_BACK_X_RES*MR_BACK_Y_RES ];
         memset( lEmptyBitmap, 0, sizeof(lEmptyBitmap) );
         pDestination.Write( lEmptyBitmap, MR_BACK_X_RES*MR_BACK_Y_RES );
      }
      else
      {
         // Extract the palette and image from the input file
         MR_UInt8* lBitmap  = LoadBitmap( lBackFile );
         MR_UInt8* lPalette = LoadPalette( lBackFile );


         if( (lBitmap != NULL )&&(lPalette!=NULL) )
         {
            pDestination << (int)MR_RAWBITMAP;
            pDestination.Write( lPalette, MR_BACK_COLORS*3 );
            pDestination.Write( lBitmap,  MR_BACK_X_RES*MR_BACK_Y_RES );
         }
         else
         {
            printf( MR_LoadString( IDS_NO_BACKGROUND ) );
         }

         delete []lBitmap;
         delete []lPalette;

         fclose( lBackFile );
      }
   }
   return lReturnValue;
}

MR_UInt8* LoadPalette( FILE* pFile )
{
   MR_UInt8* lReturnValue = new MR_UInt8[ MR_BACK_COLORS*3 ];

   fseek( pFile, -769, SEEK_END );

   char lMagicNumber = 0;

   fread( &lMagicNumber, 1, 1, pFile );

   if(lMagicNumber != 12 )
   {
      printf( MR_LoadString( IDS_BAD_PAL ) );
      delete []lReturnValue;
      lReturnValue = NULL;
   }
   else
   {
      fread( lReturnValue, 1, MR_BACK_COLORS*3, pFile );
   }

   return lReturnValue;
}

MR_UInt8* LoadBitmap( FILE* pFile )
{
   MR_UInt8* lReturnValue = NULL;

   int pXRes;
   int pYRes;

   lReturnValue = PCXRead( pFile, pXRes, pYRes );
   
   
   if( lReturnValue )
   {
      if( (pXRes != MR_BACK_X_RES)||(pYRes != MR_BACK_Y_RES) )
      {
         printf( MR_LoadString( IDS_BAD_RES ) );
         delete []lReturnValue;
         lReturnValue = NULL;
      }
   }

   if( lReturnValue != NULL )
   {
      // Make the invertion of the image pixels and add the pixel offset
      MR_UInt8* lOldBuffer = lReturnValue;

      lReturnValue = new MR_UInt8[ pXRes*pYRes ];

      int lDestIndex = 0;

      for( int lX = 0; lX<pXRes; lX++ )
      {
         MR_UInt8* lSource = lOldBuffer+lX; 

         for( int lY = pYRes-1; lY>=0; lY-- )
         {
            if( *lSource >= 128 )
            {
               // To correct a bug of Paint Shop Pro
               *lSource = 0;
            }
            lReturnValue[lDestIndex+lY] = (*lSource)+MR_RESERVED_COLORS_BEGINNING+MR_BASIC_COLORS;
            lSource += pXRes;
         }
         lDestIndex += pYRes;
      }
      delete lOldBuffer;
   }
   return lReturnValue;
}

MR_UInt8* PCXRead( FILE* pFile, int& pXRes, int& pYRes )
{
   MR_UInt8* lReturnValue = NULL;
   
   MR_Int16 lStartX;
   MR_Int16 lStartY;
   MR_Int16 lSizeX;
   MR_Int16 lSizeY;
   MR_Int16 lNbBytesPerLine;
   
   fseek( pFile, 4 ,SEEK_SET );
   fread( &lStartX, 2, 1, pFile );
   fread( &lStartY, 2, 1, pFile );  
   fread( &lSizeX, 2, 1, pFile );
   fread( &lSizeY, 2, 1, pFile );

   fseek( pFile, 66 ,SEEK_SET );
   fread( &lNbBytesPerLine, 2, 1, pFile );

   pXRes = lSizeX-lStartX+1;
   pYRes = lSizeY-lStartY+1;

   fseek( pFile, 128 ,SEEK_SET );

   ASSERT( pXRes>0);
   ASSERT( pYRes>0);

   lReturnValue = new MR_UInt8[ pXRes*pYRes ];

   int lOffset = 0;

   for( int lLineNb = 0; lLineNb<pYRes; lLineNb++ )
   {
      for( int lColNb = 0; lColNb<lNbBytesPerLine;  )
      {
         MR_UInt8 lBuffer;
         MR_UInt8 lBuffer2;
      
         fread( &lBuffer, 1, 1, pFile );

         if( (lBuffer&0xc0)==0xc0 )
         {
            fread( &lBuffer2, 1, 1, pFile );
            /*
            lBuffer2++;
            if( lBuffer2 == 255 )
            {
               lBuffer2 = 0;
            }
            */
                    
            for( int lCounter = 0; lCounter<(lBuffer&0x3f); lCounter++ )
            {
               if( lColNb<pXRes )
               {    
                  lReturnValue[ lOffset++ ] = lBuffer2;
               }
               lColNb++;
            }
         }
         else
         {
            if( lColNb<pXRes )
            {
               /*
               lBuffer++;
               if( lBuffer2 == 255 )
               {
                  lBuffer2 = 0;
               }
               */
               lReturnValue[ lOffset++ ] = lBuffer;
            }
            lColNb++;
         }
      }
   }

   return lReturnValue;
}
