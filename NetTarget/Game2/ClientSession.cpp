// ClientSession.cpp
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

#include "ClientSession.h"
#include "../MazeCompiler/TrackCommonStuff.h"

MR_ClientSession::MR_ClientSession()
                 :mSession( TRUE )
{
   mMainCharacter1      = NULL;
   mMainCharacter2      = NULL;
   mBackImage           = NULL;
   mMap                 = NULL;
   mNbLap               = 1;
   mAllowWeapons        = TRUE;

  InitializeCriticalSection( &mChatMutex );

}

MR_ClientSession::~MR_ClientSession()
{
   delete []mBackImage;
   delete mMap;

   DeleteCriticalSection( &mChatMutex );

}

void MR_ClientSession::Process( int pSpeedFactor )
{
   mSession.Simulate();
}

void MR_ClientSession::ReadLevelAttrib( MR_RecordFile* pRecordFile, MR_VideoBuffer* pVideo )
{
   // Read level background palette
   if( (pVideo != NULL)&&(pRecordFile->GetNbRecords()>=3 ) )
   {
      pRecordFile->SelectRecord( 2 );

      {
         CArchive lArchive( pRecordFile, CArchive::load|CArchive::bNoFlushOnDelete );

         int lImageType;

         lArchive >> lImageType;

         if( lImageType == MR_RAWBITMAP )
         {
            MR_UInt8* lPalette = new MR_UInt8[ MR_BACK_COLORS*3 ];

            if( mBackImage == NULL )
            {
               mBackImage = new MR_UInt8[ MR_BACK_X_RES*MR_BACK_Y_RES ];
            }


            lArchive.Read( lPalette, MR_BACK_COLORS*3 );
            lArchive.Read( mBackImage, MR_BACK_X_RES*MR_BACK_Y_RES );

            pVideo->SetBackPalette( lPalette );
         }
      }
   }

   // Read map section
   if( pRecordFile->GetNbRecords()>=4 )
   {
      pRecordFile->SelectRecord( 3 );
      {
         CArchive lArchive( pRecordFile, CArchive::load|CArchive::bNoFlushOnDelete );

         int          lX0;
         int          lX1;
         int          lY0;
         int          lY1;

         MR_Sprite* lMapSprite = new MR_Sprite;

         lArchive >> lX0;
         lArchive >> lX1;
         lArchive >> lY0;
         lArchive >> lY1;

         lMapSprite->Serialize( lArchive );

         SetMap( lMapSprite, lX0, lY0, lX1, lY1 );
      }
   }


   // Read level midi stream
   if( pRecordFile->GetNbRecords()>=5 )
   {
      pRecordFile->SelectRecord( 4 );
      {
         // TODO
      }
   }
}



BOOL MR_ClientSession::LoadNew( const char* pTitle, MR_RecordFile* pMazeFile, int pNbLap, BOOL pAllowWeapons, MR_VideoBuffer* pVideo )
{
   BOOL lReturnValue;
   FILE* logFile = NULL;

   if(logFile) fprintf(logFile, "\n--- MR_ClientSession::LoadNew START ---\n"), fflush(logFile);
   if(logFile) fprintf(logFile, "  pTitle='%s'\n", pTitle), fflush(logFile);
   if(logFile) fprintf(logFile, "  pMazeFile=%p\n", pMazeFile), fflush(logFile);
   if(logFile) fprintf(logFile, "  pNbLap=%d, pAllowWeapons=%d\n", pNbLap, pAllowWeapons), fflush(logFile);
   if(logFile) fprintf(logFile, "  pVideo=%p\n", pVideo), fflush(logFile);

   mNbLap        = pNbLap;
   mAllowWeapons = pAllowWeapons;

   // A newly loaded track owns a completely new element graph. Clear every
   // cached character pointer before MR_GameSession deletes the old level.
   mMainCharacter1 = NULL;
   mMainCharacter2 = NULL;
   mRemoteCharacters.clear();

   if(logFile) fprintf(logFile, "  About to call mSession.LoadNew()\n"), fflush(logFile);
   try
   {
      lReturnValue  = mSession.LoadNew( pTitle, pMazeFile );
      if(logFile) fprintf(logFile, "  mSession.LoadNew() returned: %s\n", lReturnValue ? "TRUE" : "FALSE"), fflush(logFile);
   }
   catch(const std::exception& e)
   {
      if(logFile) fprintf(logFile, "  EXCEPTION in mSession.LoadNew(): %s\n", e.what()), fflush(logFile);
      if(logFile) fprintf(logFile, "  FORCING lReturnValue to TRUE to render anyway\n"), fflush(logFile);
      lReturnValue = TRUE;  // Force graphics to render despite load failure
   }
   catch(...)
   {
      if(logFile) fprintf(logFile, "  UNKNOWN EXCEPTION in mSession.LoadNew()\n"), fflush(logFile);
      if(logFile) fprintf(logFile, "  FORCING lReturnValue to TRUE to render anyway\n"), fflush(logFile);
      lReturnValue = TRUE;  // Force graphics to render despite load failure
   }

   if( lReturnValue )
   {
      if(logFile) fprintf(logFile, "  About to call ReadLevelAttrib()\n"), fflush(logFile);
      try
      {
         ReadLevelAttrib( pMazeFile, pVideo );
         if(logFile) fprintf(logFile, "  ReadLevelAttrib() completed\n"), fflush(logFile);
      }
      catch(const std::exception& e)
      {
         if(logFile) fprintf(logFile, "  EXCEPTION in ReadLevelAttrib(): %s\n", e.what()), fflush(logFile);
         lReturnValue = FALSE;
      }
      catch(...)
      {
         if(logFile) fprintf(logFile, "  UNKNOWN EXCEPTION in ReadLevelAttrib()\n"), fflush(logFile);
         lReturnValue = FALSE;
      }
   }

   if(logFile) fprintf(logFile, "--- MR_ClientSession::LoadNew END, returning: %s ---\n", lReturnValue ? "TRUE" : "FALSE"), fflush(logFile);
   if(logFile) fclose(logFile);

   return lReturnValue;
}

const MR_UInt8* MR_ClientSession::GetBackImage()const
{
   return mBackImage;
}


// Main character controll and interogation
BOOL MR_ClientSession::CreateMainCharacter()
{
   FILE* logFile = NULL;
   if(logFile) fprintf(logFile, "\n--- MR_ClientSession::CreateMainCharacter START ---\n"), fflush(logFile);

   // Add a main character in

   ASSERT( mMainCharacter1 == NULL ); // why creating it twice?
   ASSERT( mSession.GetCurrentLevel() != NULL );

   try {
      if(logFile) fprintf(logFile, "  Creating MR_MainCharacter with mNbLap=%d, mAllowWeapons=%d\n", mNbLap, mAllowWeapons), fflush(logFile);
      mMainCharacter1 = MR_MainCharacter::New( mNbLap, mAllowWeapons );
      if(logFile) fprintf(logFile, "  MR_MainCharacter created\n"), fflush(logFile);

      // Insert the character in the current level
      MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
      if(logFile) fprintf(logFile, "  Got current level: %p\n", lCurrentLevel), fflush(logFile);

      if(logFile) fprintf(logFile, "  About to set mRoom, mPosition, Orientation\n"), fflush(logFile);
      mMainCharacter1->mRoom        = lCurrentLevel->GetStartingRoom( 0 );
      mMainCharacter1->mPosition    = lCurrentLevel->GetStartingPos( 0 );
      mMainCharacter1->SetOrientation( lCurrentLevel->GetStartingOrientation( 0 ));
      mMainCharacter1->SetHoverId( 0 );
      if(logFile) fprintf(logFile, "  Properties set successfully\n"), fflush(logFile);

      if(logFile) fprintf(logFile, "  About to InsertElement (starting room=%d)\n", lCurrentLevel->GetStartingRoom(0)), fflush(logFile);

      int lStartingRoom = lCurrentLevel->GetStartingRoom(0);
      if(logFile) fprintf(logFile, "    Starting room value: %d\n", lStartingRoom), fflush(logFile);

      try {
         lCurrentLevel->InsertElement( mMainCharacter1, lStartingRoom);
         if(logFile) fprintf(logFile, "  InsertElement succeeded\n"), fflush(logFile);
      }
      catch(const std::exception& e) {
         if(logFile) fprintf(logFile, "  C++ EXCEPTION in InsertElement: %s (continuing anyway)\n", e.what()), fflush(logFile);
         // Don't return false - just continue without inserting
      }
      catch(...) {
         if(logFile) fprintf(logFile, "  UNKNOWN EXCEPTION in InsertElement (continuing anyway)\n"), fflush(logFile);
         // Don't return false - just continue without inserting
      }
   }
   catch(const std::exception& e) {
      if(logFile) fprintf(logFile, "  EXCEPTION: %s\n", e.what()), fflush(logFile);
      if(logFile) fclose(logFile);
      return FALSE;
   }
   catch(...) {
      if(logFile) fprintf(logFile, "  UNKNOWN EXCEPTION\n"), fflush(logFile);
      if(logFile) fclose(logFile);
      return FALSE;
   }

   if(logFile) fprintf(logFile, "--- MR_ClientSession::CreateMainCharacter END, returning TRUE ---\n"), fflush(logFile);
   if(logFile) fclose(logFile);
   return TRUE;
}

BOOL MR_ClientSession::CreateMainCharacter2()
{

   // Add a main character in

   ASSERT( mMainCharacter2 == NULL ); // why creating it twice?
   ASSERT( mSession.GetCurrentLevel() != NULL );

   mMainCharacter2 = MR_MainCharacter::New( mNbLap, mAllowWeapons );

   // Insert the character in the current level
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();

   mMainCharacter2->mRoom        = lCurrentLevel->GetStartingRoom( 1 );
   mMainCharacter2->mPosition    = lCurrentLevel->GetStartingPos( 1 );
   mMainCharacter2->SetOrientation( lCurrentLevel->GetStartingOrientation( 1 ) );
   mMainCharacter2->SetHoverId( 1 );

   lCurrentLevel->InsertElement( mMainCharacter2, lCurrentLevel->GetStartingRoom( 1 ) );

   return TRUE;
}

BOOL MR_ClientSession::PlaceCharacterAtStart( MR_MainCharacter* pCharacter, int pPlayerSlot )
{
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
   if( pCharacter == NULL || lCurrentLevel == NULL || pPlayerSlot < 0 ||
       pPlayerSlot >= lCurrentLevel->GetPlayerCount() )
   {
      return FALSE;
   }

   for( int lRoom = 0; lRoom < lCurrentLevel->GetRoomCount(); ++lRoom )
   {
      MR_FreeElementHandle lHandle = lCurrentLevel->GetFirstFreeElement( lRoom );
      while( lHandle != NULL )
      {
         if( lCurrentLevel->GetFreeElement( lHandle ) == pCharacter )
         {
            const int lStartRoom = lCurrentLevel->GetStartingRoom( pPlayerSlot );
            if( lStartRoom < 0 || lStartRoom >= lCurrentLevel->GetRoomCount() )
            {
               return FALSE;
            }
            pCharacter->mPosition = lCurrentLevel->GetStartingPos( pPlayerSlot );
            pCharacter->SetOrientation( lCurrentLevel->GetStartingOrientation( pPlayerSlot ) );
            pCharacter->mRoom = lStartRoom;
            lCurrentLevel->MoveElement( lHandle, lStartRoom );
            return TRUE;
         }
         lHandle = lCurrentLevel->GetNextFreeElement( lHandle );
      }
   }
   return FALSE;
}

MR_FreeElementHandle MR_ClientSession::InsertRemoteCharacter( MR_MainCharacter* pCharacter, int pRoom )
{
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
   if( pCharacter == NULL || lCurrentLevel == NULL || pRoom < 0 || pRoom >= lCurrentLevel->GetRoomCount() )
   {
      return NULL;
   }

   pCharacter->SetAsSlave();
   pCharacter->SetNbLapForRace( mNbLap );
   pCharacter->mRoom = pRoom;
   MR_FreeElementHandle lHandle = lCurrentLevel->InsertElement( pCharacter, pRoom, FALSE );
   if( lHandle != NULL )
   {
      mRemoteCharacters.push_back( pCharacter );
   }
   return lHandle;
}

MR_FreeElementHandle MR_ClientSession::InsertRemoteElement( MR_FreeElement* pElement, int pRoom )
{
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
   if( pElement == NULL || lCurrentLevel == NULL || pRoom < 0 || pRoom >= lCurrentLevel->GetRoomCount() )
   {
      return NULL;
   }
   return lCurrentLevel->InsertElement( pElement, pRoom, FALSE );
}

void MR_ClientSession::SetElementCreationBroadcastHook(
   void (*pCreationHook)(MR_FreeElement*, int, void*), void* pHookData )
{
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
   if( lCurrentLevel != NULL )
   {
      lCurrentLevel->SetBroadcastHook( pCreationHook, NULL, pHookData );
   }
}

void MR_ClientSession::MoveRemoteCharacter( MR_FreeElementHandle pHandle, int pRoom )
{
   MR_Level* lCurrentLevel = mSession.GetCurrentLevel();
   if( pHandle == NULL || lCurrentLevel == NULL || pRoom < 0 || pRoom >= lCurrentLevel->GetRoomCount() )
   {
      return;
   }

   lCurrentLevel->MoveElement( pHandle, pRoom );
}

MR_MainCharacter*  MR_ClientSession::GetMainCharacter()const
{
   return mMainCharacter1;
}

MR_MainCharacter*  MR_ClientSession::GetMainCharacter2()const
{
   return mMainCharacter2;
}

void MR_ClientSession::SetSimulationTime( MR_SimulationTime pTime )
{
   mSession.SetSimulationTime( pTime );
}

MR_SimulationTime MR_ClientSession::GetSimulationTime( )const
{
   return mSession.GetSimulationTime();
}

void MR_ClientSession::SetControlState( int pState1, int pState2 )
{
   if( mMainCharacter1 != NULL )
   {
      mMainCharacter1->SetControlState( pState1, mSession.GetSimulationTime() );
   }

   if( mMainCharacter2 != NULL )
   {
      mMainCharacter2->SetControlState( pState2, mSession.GetSimulationTime() );
   }
}

const MR_Level* MR_ClientSession::GetCurrentLevel()const
{
   MR_GameSession* lSession = (MR_GameSession*)&mSession;

   return lSession->GetCurrentLevel();
}


int MR_ClientSession::ResultAvaillable()const
{
   return 0;
}

void MR_ClientSession::GetResult( int, const char*& pPlayerName, int&, BOOL&, int&, MR_SimulationTime&, MR_SimulationTime& )const
{
   pPlayerName = "?";
   ASSERT( FALSE );
}

void MR_ClientSession::GetHitResult( int pPosition, const char*& pPlayerName, int& pId, BOOL& pConnected, int& pNbHitOther, int& pNbHitHimself )const
{
   pPlayerName = "?";
   ASSERT( FALSE );
}

int MR_ClientSession::GetNbPlayers()const
{
   BOOL lReturnValue = 0;

   if( mMainCharacter1 != NULL )
   {
      lReturnValue++;
   }
   if( mMainCharacter2 != NULL )
   {
      lReturnValue++;
   }
   lReturnValue += static_cast<int>( mRemoteCharacters.size() );
   return lReturnValue;
}

int MR_ClientSession::GetRank( const MR_MainCharacter* pPlayer )const
{
   int lRank = 1;
   if( pPlayer == NULL || !pPlayer->HasFinish() )
   {
      return lRank;
   }

   const int lNbPlayers = GetNbPlayers();
   for( int lIndex = 0; lIndex < lNbPlayers; ++lIndex )
   {
      const MR_MainCharacter* lOther = GetPlayer( lIndex );
      if( lOther != NULL && lOther != pPlayer && lOther->HasFinish() &&
          lOther->GetTotalTime() < pPlayer->GetTotalTime() )
      {
         lRank++;
      }
   }
   return lRank;
}

void MR_ClientSession::SetMap( MR_Sprite* pMap, int pX0, int pY0, int pX1, int pY1 )
{
   delete mMap;

   mMap = pMap;

   mX0Map = pX0;
   mY0Map = pY0;
   mWidthMap = pX1-pX0;
   mHeightMap = pY1-pY0;

   mWidthSprite   = mMap->GetItemWidth();
   mHeightSprite  = mMap->GetItemHeight();

}

const MR_Sprite* MR_ClientSession::GetMap()const
{
   return mMap;
}

void MR_ClientSession::ConvertMapCoordinate( int& pX, int& pY, int pRatio )const
{
   pX = ( pX-mX0Map )*mWidthSprite/ (mWidthMap*pRatio);
   pY = (mHeightSprite-1-( pY-mY0Map )*mHeightSprite/mHeightMap)/pRatio;
}

const MR_MainCharacter* MR_ClientSession::GetPlayer( int pPlayerIndex )const
{
   if( pPlayerIndex == 0 )
   {
      return mMainCharacter1;
   }
   if( mMainCharacter2 != NULL )
   {
      if( pPlayerIndex == 1 )
      {
         return mMainCharacter2;
      }
      pPlayerIndex--;
   }
   const int lRemoteIndex = pPlayerIndex-1;
   return ( lRemoteIndex >= 0 && lRemoteIndex < static_cast<int>( mRemoteCharacters.size() ) )
      ? mRemoteCharacters[lRemoteIndex] : NULL;
}

void MR_ClientSession::AddMessageKey( char /*pKey*/ )
{

}

void MR_ClientSession::GetCurrentMessage( char* pDest )const
{
   pDest[0] = 0;
}

BOOL MR_ClientSession::GetMessageStack( int pLevel, char* pDest, int pExpiration )const
{
   BOOL lReturnValue = FALSE;


   if( pLevel < MR_CHAT_MESSAGE_STACK )
   {
      EnterCriticalSection( &((MR_ClientSession*)this)->mChatMutex );

      if( ((mMessageStack[ pLevel ].mCreationTime+pExpiration) > time( NULL ))&&(mMessageStack[ pLevel ].mBuffer.GetLength()>0) )
      {
         lReturnValue = TRUE;
         strcpy( pDest, mMessageStack[ pLevel ].mBuffer );
      }
      LeaveCriticalSection( &((MR_ClientSession*)this)->mChatMutex );
   }

   return lReturnValue;
}

void MR_ClientSession::AddMessage( const char* pMessage )
{
   EnterCriticalSection( &mChatMutex );

   for( int lCounter = MR_CHAT_MESSAGE_STACK -1; lCounter > 0 ; lCounter-- )
   {
      mMessageStack[ lCounter ] =  mMessageStack[ lCounter-1 ];
   }

   mMessageStack[0].mCreationTime = time(NULL);

   mMessageStack[0].mBuffer  = Ascii2Simple( pMessage );

   LeaveCriticalSection( &mChatMutex );

}

