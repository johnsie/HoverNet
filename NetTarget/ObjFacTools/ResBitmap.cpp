// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-08.
// See repository history for authorship and detailed changes.
// ResBitmap.cpp
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
#include "ResBitmap.h"
#include "../VideoServices/TextureEnhance.h"

#include <cstring>
#include <vector>

#define new DEBUG_NEW

MR_ResBitmap::MR_ResBitmap( int pResourceId )
{
   mColourVersion = -1;
   mResourceId     = pResourceId;
   mWidth          = 1;         
   mHeight         = 1;        
   mXRes           = 1;
   mYRes           = 1;
   mSubBitmapCount = 0;
   mPlainColor     = 255;    
   mSubBitmapList  = NULL;
}


MR_ResBitmap::~MR_ResBitmap()
{
   delete []mSubBitmapList;
}

int MR_ResBitmap::GetResourceId()const
{
   return mResourceId;
}

void MR_ResBitmap::Serialize( CArchive& pArchive )
{
   int lCounter;

   if( pArchive.IsStoring() )
   {
      pArchive << mWidth;         
      pArchive << mHeight;        
      pArchive << mXRes;
      pArchive << mYRes;
      pArchive << mSubBitmapCount;
      pArchive << mPlainColor;    

   }
   else
   {
      delete []mSubBitmapList;
      mSubBitmapList = NULL;

      pArchive >> mWidth;         
      pArchive >> mHeight;        
      pArchive >> mXRes;
      pArchive >> mYRes;
      pArchive >> mSubBitmapCount;
      pArchive >> mPlainColor;    


      if( mSubBitmapCount > 0 )
      {
         mSubBitmapList = new SubBitmap[ mSubBitmapCount ];
         mColourVersion = -1;
      }

   }


   for( lCounter = 0; lCounter< mSubBitmapCount; lCounter++ )
   {
      mSubBitmapList[ lCounter ].Serialize( pArchive );
   }



}


void MR_ResBitmap::SetWidthHeight( int pWidth, int pHeight )
{
   mWidth = pWidth;
   mHeight = pHeight;
}


// MR_Bitmap overloaded functions
int MR_ResBitmap::GetWidth()const
{
   return mWidth;
}

int MR_ResBitmap::GetHeight()const
{
   return mHeight;
}

int MR_ResBitmap::GetMaxXRes()const
{
   return mXRes;
}

int MR_ResBitmap::GetMaxYRes()const
{
   return mYRes;
}

MR_UInt8 MR_ResBitmap::GetPlainColor()const
{
   return mPlainColor;
}

int MR_ResBitmap::GetNbSubBitmap()const
{
   return mSubBitmapCount;   
}

int MR_ResBitmap::GetXRes( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mXRes;
}

int MR_ResBitmap::GetYRes( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mYRes;
}

int MR_ResBitmap::GetXResShiftFactor( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mXResShiftFactor;
}

int MR_ResBitmap::GetYResShiftFactor( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mYResShiftFactor;
}


MR_UInt8* MR_ResBitmap::GetBuffer( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mBuffer;
}

MR_UInt8* MR_ResBitmap::GetColumnBuffer( int pSubBitmap, int pColumn )const
{
   return mSubBitmapList[ pSubBitmap ].mColumnPtr[ pColumn ];
}

MR_UInt8** MR_ResBitmap::GetColumnBufferTable( int pSubBitmap )const
{
   return mSubBitmapList[ pSubBitmap ].mColumnPtr;
}

MR_ResBitmap::SubBitmap::SubBitmap()
{
   mBuffer    = NULL;
   mColumnPtr = NULL;
   mColour    = NULL;
   mColourColumnPtr = NULL;
}

// ---- true colour -------------------------------------------------------------------------------------------------------------------------

static std::atomic<bool> gsColourOn( false );
static std::atomic<int>  gsColourVersion( 0 );
static std::mutex        gsColourPaletteMutex;
static MR_UInt8          gsColourPalette[ 768 ];

void MR_ResBitmap::SetColourPalette( const MR_UInt8* pPalette768 )
{
   std::lock_guard<std::mutex> lLock( gsColourPaletteMutex );
   if( pPalette768 == NULL )
   {
      gsColourOn = false;
      return;
   }
   if( !gsColourOn || memcmp( gsColourPalette, pPalette768, sizeof( gsColourPalette ) ) != 0 )
   {
      memcpy( gsColourPalette, pPalette768, sizeof( gsColourPalette ) );
      gsColourVersion++;
   }
   gsColourOn = true;
}

bool MR_ResBitmap::EnsureColour()const
{
   if( !gsColourOn.load() || mSubBitmapCount <= 0 )
   {
      return false;
   }
   if( mColourVersion.load( std::memory_order_acquire ) == gsColourVersion.load() )
   {
      return true;
   }

   std::lock_guard<std::mutex> lLock( mColourMutex );
   MR_UInt8 lPalette[ 768 ];
   int      lVersion;
   {
      std::lock_guard<std::mutex> lPaletteLock( gsColourPaletteMutex );
      memcpy( lPalette, gsColourPalette, sizeof( lPalette ) );
      lVersion = gsColourVersion.load();
   }
   if( mColourVersion.load() == lVersion )
   {
      return true;
   }

   // The largest level is the source of all the others.
   int lTop = 0;
   for( int lIndex = 1; lIndex < mSubBitmapCount; lIndex++ )
   {
      if( mSubBitmapList[ lIndex ].mXRes*mSubBitmapList[ lIndex ].mYRes > mSubBitmapList[ lTop ].mXRes*mSubBitmapList[ lTop ].mYRes )
      {
         lTop = lIndex;
      }
   }
   // The true-colour copy of each level is twice the size of the 8-bit one in both directions (see MR_Bitmap::GetColourColumnBuffer).
   for( int lIndex = 0; lIndex < mSubBitmapCount; lIndex++ )
   {
      SubBitmap& lSub = mSubBitmapList[ lIndex ];
      if( lSub.mColour == NULL )
      {
         lSub.mColour = new MR_UInt32[ 4*lSub.mXRes*lSub.mYRes ];
         lSub.mColourColumnPtr = new MR_UInt32*[ 2*lSub.mXRes ];
         for( int lColumn = 0; lColumn < 2*lSub.mXRes; lColumn++ )
         {
            lSub.mColourColumnPtr[ lColumn ] = lSub.mColour + lColumn*2*lSub.mYRes;
         }
      }
   }
   const SubBitmap& lBase = mSubBitmapList[ lTop ];
   std::vector<MR_UInt32> lTopColour( (size_t)lBase.mXRes*lBase.mYRes );
   HoverNetTex::EnhanceBase( lBase.mBuffer, lBase.mXRes, lBase.mYRes, lPalette, lTopColour.data(), true, 1 );
   for( int lIndex = 0; lIndex < mSubBitmapCount; lIndex++ )
   {
      SubBitmap& lSub = mSubBitmapList[ lIndex ];
      std::vector<MR_UInt32> lLevel( (size_t)lSub.mXRes*lSub.mYRes );
      if( lIndex == lTop )
      {
         lLevel = lTopColour;
      }
      else if( !HoverNetTex::Downsample( lTopColour.data(), lBase.mXRes, lBase.mYRes, lLevel.data(), lSub.mXRes, lSub.mYRes ) )
      {
         HoverNetTex::EnhanceBase( lSub.mBuffer, lSub.mXRes, lSub.mYRes, lPalette, lLevel.data(), false );   // a size that does not divide: the palette's colours
      }
      HoverNetTex::Upscale2x( lLevel.data(), lSub.mXRes, lSub.mYRes, lSub.mColour );
   }
   mColourVersion.store( lVersion, std::memory_order_release );
   return true;
}

const MR_UInt32* MR_ResBitmap::GetColourColumnBuffer( int pSubBitmap, int pColumn )const
{
   return EnsureColour() ? mSubBitmapList[ pSubBitmap ].mColourColumnPtr[ pColumn ] : NULL;
}

const MR_UInt32* const* MR_ResBitmap::GetColourColumnBufferTable( int pSubBitmap )const
{
   return EnsureColour() ? mSubBitmapList[ pSubBitmap ].mColourColumnPtr : NULL;
}

MR_ResBitmap::SubBitmap::~SubBitmap()
{
   delete []mBuffer;
   delete []mColumnPtr;
   delete []mColour;
   delete []mColourColumnPtr;
}

void MR_ResBitmap::SubBitmap::Serialize( CArchive& pArchive )
{
   if( pArchive.IsStoring() )
   {

      pArchive << mXRes;
      pArchive << mYRes;
      pArchive << mXResShiftFactor;
      pArchive << mYResShiftFactor;
      pArchive << mHaveTransparent;

      pArchive.Write( mBuffer, mXRes*mYRes );
   }
   else
   {
      delete []mBuffer;
      delete []mColumnPtr;

      pArchive >> mXRes;
      pArchive >> mYRes;
      pArchive >> mXResShiftFactor;
      pArchive >> mYResShiftFactor;
      pArchive >> mHaveTransparent;

      mBuffer    = new MR_UInt8[ mXRes*mYRes ];
      mColumnPtr = new MR_UInt8*[ mXRes ];

      MR_UInt8* lPtr = mBuffer;

      for( int lCounter = 0; lCounter < mXRes; lCounter++ )
      {
         mColumnPtr[ lCounter ] = lPtr;
         lPtr += mYRes;
      }
      pArchive.Read( mBuffer, mXRes*mYRes );
   }
}




