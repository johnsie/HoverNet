// ResourceLib.cpp
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

#include "ResourceLib.h"

#define new DEBUG_NEW

MR_ResourceLib::MR_ResourceLib( const char* pResFile )
{
   
   DWORD startTime = GetTickCount();
   
   if( mRecordFile.OpenForRead( pResFile ) )
   {

      mRecordFile.SelectRecord( 0 );

      {
         int lMagicNumber;

         CArchive lArchive( &mRecordFile, CArchive::load );

         lArchive >> lMagicNumber;

         if( lMagicNumber == MR_RESOURCE_FILE_MAGIC )
         {
            // Load the Bitmaps
            DWORD t1 = GetTickCount();
            LoadBitmaps( lArchive );
            DWORD t2 = GetTickCount();

            // Load the Actors
            t1 = GetTickCount();
            LoadActors( lArchive );
            t2 = GetTickCount();

            // Load the sprites
            t1 = GetTickCount();
            LoadSprites( lArchive );
            t2 = GetTickCount();

            // Load the sprites
            t1 = GetTickCount();
            LoadSounds( lArchive );
            t2 = GetTickCount();

         }
         else
         {
            ASSERT( FALSE );
            AfxThrowArchiveException( CArchiveException::badSchema );
         }
      }
   }
   else
   {
      ASSERT( FALSE );
      AfxThrowFileException( CFileException::fileNotFound );
   }
   
   DWORD endTime = GetTickCount();
}

MR_ResourceLib::MR_ResourceLib()
{
}

   
MR_ResourceLib::~MR_ResourceLib()
{
   POSITION lPos;

   lPos = mActorList.GetStartPosition();

   while( lPos != NULL )
   {
      int lKey;
      MR_ResActor* lValue;

      mActorList.GetNextAssoc( lPos, lKey, lValue );

      delete lValue;
   }


   lPos = mBitmapList.GetStartPosition();

   while( lPos != NULL )
   {
      int lKey;
      MR_ResBitmap* lValue;

      mBitmapList.GetNextAssoc( lPos, lKey, lValue );

      delete lValue;
   }

   lPos = mSpriteList.GetStartPosition();

   while( lPos != NULL )
   {
      int lKey;
      MR_ResSprite* lValue;

      mSpriteList.GetNextAssoc( lPos, lKey, lValue );

      delete lValue;
   }

   lPos = mShortSoundList.GetStartPosition();

   while( lPos != NULL )
   {
      int lKey;
      MR_ResShortSound* lValue;

      mShortSoundList.GetNextAssoc( lPos, lKey, lValue );

      delete lValue;
   }

   lPos = mContinuousSoundList.GetStartPosition();

   while( lPos != NULL )
   {
      int lKey;
      MR_ResContinuousSound* lValue;

      mContinuousSoundList.GetNextAssoc( lPos, lKey, lValue );

      delete lValue;
   }

}

/*const*/ MR_ResBitmap* MR_ResourceLib::GetBitmap( int pBitmapId )
{

   MR_ResBitmap* lValue = NULL;

   mBitmapList.Lookup( pBitmapId, lValue );

   return lValue;
}

const MR_ResActor* MR_ResourceLib::GetActor(  int pActorId  )
{
   MR_ResActor* lValue = NULL;

   mActorList.Lookup( pActorId, lValue );

   return lValue;
}

const MR_ResSprite* MR_ResourceLib::GetSprite(  int pSpriteId  )
{
   MR_ResSprite* lValue = NULL;

   mSpriteList.Lookup( pSpriteId, lValue );

   return lValue;
}

const MR_ResShortSound* MR_ResourceLib::GetShortSound( int pSoundId  )
{
   MR_ResShortSound* lValue = NULL;

   mShortSoundList.Lookup( pSoundId, lValue );

   return lValue;
}

const MR_ResContinuousSound* MR_ResourceLib::GetContinuousSound( int pSoundId  )
{
   MR_ResContinuousSound* lValue = NULL;

   mContinuousSoundList.Lookup( pSoundId, lValue );

   return lValue;
}

int MR_ResourceLib::GetBitmapCount() const
{
   return mBitmapList.GetCount();
}

int MR_ResourceLib::GetActorCount() const
{
   return mActorList.GetCount();
}

int MR_ResourceLib::GetSpriteCount() const
{
   return mSpriteList.GetCount();
}

int MR_ResourceLib::GetShortSoundCount() const
{
   return mShortSoundList.GetCount();
}

int MR_ResourceLib::GetContinuousSoundCount() const
{
   return mContinuousSoundList.GetCount();
}


void MR_ResourceLib::LoadBitmaps( CArchive& pArchive )
{
   int lNbBitmap;

   pArchive >> lNbBitmap;

   for( int lCounter = 0; lCounter < lNbBitmap; lCounter++ )
   {
      int           lKey;
      MR_ResBitmap* lValue;

      pArchive >> lKey;

      lValue = new MR_ResBitmap( lKey );
      
      lValue->Serialize( pArchive );

      mBitmapList.SetAt( lKey, lValue );
   }
}


void MR_ResourceLib::LoadActors( CArchive& pArchive )
{
   int lNbActor;

   pArchive >> lNbActor;

   for( int lCounter = 0; lCounter < lNbActor; lCounter++ )
   {
      int lKey;
      MR_ResActor* lValue;     

      pArchive >> lKey;

      lValue = new MR_ResActor( lKey );
      lValue->Serialize( pArchive, this );

      mActorList.SetAt( lKey, lValue );
   }
}


void MR_ResourceLib::LoadSprites( CArchive& pArchive )
{
   int lNbSprite;

   pArchive >> lNbSprite;

   for( int lCounter = 0; lCounter < lNbSprite; lCounter++ )
   {
      int lKey;
      MR_ResSprite* lValue;     

      pArchive >> lKey;

      lValue = new MR_ResSprite( lKey );
      lValue->Serialize( pArchive );

      mSpriteList.SetAt( lKey, lValue );
   }
}


void MR_ResourceLib::LoadSounds( CArchive& pArchive )
{
   int lNbSound;
   int lCounter;

   pArchive >> lNbSound;

   for( lCounter = 0; lCounter < lNbSound; lCounter++ )
   {
      int lKey;
      MR_ResShortSound* lValue;     

      pArchive >> lKey;

      lValue = new MR_ResShortSound( lKey );
      lValue->Serialize( pArchive );

      mShortSoundList.SetAt( lKey, lValue );
   }  

   pArchive >> lNbSound;

   for( lCounter = 0; lCounter < lNbSound; lCounter++ )
   {
      int lKey;
      MR_ResContinuousSound* lValue;     

      pArchive >> lKey;

      lValue = new MR_ResContinuousSound( lKey );
      lValue->Serialize( pArchive );

      mContinuousSoundList.SetAt( lKey, lValue );
   }  

}




void MR_ResourceLib::ReplaceBitmap( MR_ResBitmap* pBitmap )
{
   if( pBitmap == NULL ) return;
   MR_ResBitmap* lOld = NULL;
   if( mBitmapList.Lookup( pBitmap->GetResourceId(), lOld ) ) delete lOld;
   mBitmapList.SetAt( pBitmap->GetResourceId(), pBitmap );
}

void MR_ResourceLib::ReplaceActor( MR_ResActor* pActor )
{
   if( pActor == NULL ) return;
   MR_ResActor* lOld = NULL;
   if( mActorList.Lookup( pActor->GetResourceId(), lOld ) ) delete lOld;
   mActorList.SetAt( pActor->GetResourceId(), pActor );
}

BOOL MR_ResourceLib::Export( const char* pFileName )
{
   MR_RecordFile lFile;
   if( !lFile.CreateForWrite( pFileName, 1,
      "\x8\rFireball object factory resource file, (c)GrokkSoft 1996\n\x1a" ) ) return FALSE;
   lFile.BeginANewRecord();
   CArchive lArchive( &lFile, CArchive::store );
   lArchive << (int)MR_RESOURCE_FILE_MAGIC;

   int lCount = mBitmapList.GetCount(); lArchive << lCount;
   POSITION lPos = mBitmapList.GetStartPosition();
   while( lPos != NULL ) { int lKey; MR_ResBitmap* lValue; mBitmapList.GetNextAssoc(lPos,lKey,lValue); lArchive << lKey; lValue->Serialize(lArchive); }
   lCount = mActorList.GetCount(); lArchive << lCount;
   lPos = mActorList.GetStartPosition();
   while( lPos != NULL ) { int lKey; MR_ResActor* lValue; mActorList.GetNextAssoc(lPos,lKey,lValue); lArchive << lKey; lValue->Serialize(lArchive, this); }
   lCount = mSpriteList.GetCount(); lArchive << lCount;
   lPos = mSpriteList.GetStartPosition();
   while( lPos != NULL ) { int lKey; MR_ResSprite* lValue; mSpriteList.GetNextAssoc(lPos,lKey,lValue); lArchive << lKey; lValue->Serialize(lArchive); }
   lCount = mShortSoundList.GetCount(); lArchive << lCount;
   lPos = mShortSoundList.GetStartPosition();
   while( lPos != NULL ) { int lKey; MR_ResShortSound* lValue; mShortSoundList.GetNextAssoc(lPos,lKey,lValue); lArchive << lKey; lValue->Serialize(lArchive); }
   lCount = mContinuousSoundList.GetCount(); lArchive << lCount;
   lPos = mContinuousSoundList.GetStartPosition();
   while( lPos != NULL ) { int lKey; MR_ResContinuousSound* lValue; mContinuousSoundList.GetNextAssoc(lPos,lKey,lValue); lArchive << lKey; lValue->Serialize(lArchive); }
   lArchive.Close();
   return TRUE;
}
