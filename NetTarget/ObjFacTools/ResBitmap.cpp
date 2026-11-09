// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-09.
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
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/ColourGrade.h"
#include "../VideoServices/TextureEnhance.h"

#include <cstring>
#include <map>
#include <thread>
#include <vector>

#define new DEBUG_NEW

static std::mutex&                 RegistryMutex();
static std::vector<MR_ResBitmap*>& Registry();

MR_ResBitmap::MR_ResBitmap( int pResourceId )
{
   {
      std::lock_guard<std::mutex> lLock( RegistryMutex() );
      Registry().push_back( this );
   }
   mColourVersion = -1;
   mColourShift = 1;
   mAiWidth = 0;
   mAiHeight = 0;
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
   {
      std::lock_guard<std::mutex> lLock( RegistryMutex() );
      std::vector<MR_ResBitmap*>& lRegistry = Registry();
      for( size_t lIndex = 0; lIndex < lRegistry.size(); lIndex++ )
      {
         if( lRegistry[ lIndex ] == this )
         {
            lRegistry.erase( lRegistry.begin() + lIndex );
            break;
         }
      }
   }
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

// Every bitmap that exists, so that the true-colour copies can all be built in the background when the palette is set, instead of each one the first time
// it is drawn (a large texture takes tens of milliseconds, which was a visible hitch every time a new one came into view).
static std::mutex&                     RegistryMutex() { static std::mutex* m = new std::mutex; return *m; }
static std::vector<MR_ResBitmap*>&     Registry()      { static std::vector<MR_ResBitmap*>* r = new std::vector<MR_ResBitmap*>; return *r; }
static std::atomic<int>                gsWorkerGeneration( 0 );

struct ColourWorker
{
   std::thread mThread;
   ~ColourWorker() { Stop(); }
   void Stop()
   {
      gsWorkerGeneration++;   // tells a running worker to stop
      if( mThread.joinable() )
      {
         mThread.join();
      }
   }
   void Start()
   {
      Stop();
#ifndef __EMSCRIPTEN__
      const int lGeneration = gsWorkerGeneration.load();
      mThread = std::thread( [lGeneration]()
      {
         for( size_t lIndex = 0; ; lIndex++ )
         {
            std::lock_guard<std::mutex> lLock( RegistryMutex() );
            if( gsWorkerGeneration.load() != lGeneration || lIndex >= Registry().size() )
            {
               break;
            }
            Registry()[ lIndex ]->EnsureColour();
         }
      } );
#endif
   }
};
static ColourWorker& Worker() { static ColourWorker w; return w; }

static std::atomic<bool> gsColourOn( false );
static std::atomic<int>  gsColourVersion( 0 );
static std::mutex        gsColourPaletteMutex;
static MR_UInt8          gsColourPalette[ 768 ];
static float             gsColourBrightness = 1.0f;
static bool              gsColourGradeAi = false;

void MR_ResBitmap::SetColourPalette( const MR_UInt8* pPalette768, float pBrightness, bool pGradeAiImages )
{
   bool lChanged = false;
   {
      std::lock_guard<std::mutex> lLock( gsColourPaletteMutex );
      if( pPalette768 == NULL )
      {
         gsColourOn = false;
         lChanged = true;
      }
      else
      {
         if( !gsColourOn || memcmp( gsColourPalette, pPalette768, sizeof( gsColourPalette ) ) != 0 || gsColourBrightness != pBrightness || gsColourGradeAi != pGradeAiImages )
         {
            memcpy( gsColourPalette, pPalette768, sizeof( gsColourPalette ) );
            gsColourBrightness = pBrightness;
            gsColourGradeAi = pGradeAiImages;
            gsColourVersion++;
            lChanged = true;
         }
         gsColourOn = true;
      }
   }
   if( lChanged )
   {
      if( pPalette768 == NULL )
      {
         Worker().Stop();
      }
      else
      {
         Worker().Start();   // build every texture's true-colour copy now, off the drawing thread
      }
   }
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
   // The true-colour copy of each level is (1 << shift) times the 8-bit one in both directions: 2x from the palette, or the scale of the AI-enhanced image.
   const SubBitmap& lBase = mSubBitmapList[ lTop ];
   int lShift = 1;
   bool lUseAi = false;
   if( mAiWidth > 0 && mAiWidth%lBase.mXRes == 0 )
   {
      const int lScale = mAiWidth/lBase.mXRes;
      lShift = lScale == 8 ? 3 : (lScale == 4 ? 2 : 1);
      lUseAi = lScale == 2 || lScale == 4 || lScale == 8;
      if( !lUseAi )
      {
         lShift = 1;
      }
   }
   const int lPreviousShift = mColourShift;
   mColourShift = lShift;
   for( int lIndex = 0; lIndex < mSubBitmapCount; lIndex++ )
   {
      SubBitmap& lSub = mSubBitmapList[ lIndex ];
      if( lSub.mColour != NULL && lPreviousShift != lShift )
      {
         delete []lSub.mColour;
         delete []lSub.mColourColumnPtr;
         lSub.mColour = NULL;
         lSub.mColourColumnPtr = NULL;
      }
      if( lSub.mColour == NULL )
      {
         const int lW = lSub.mXRes << lShift, lH = lSub.mYRes << lShift;
         lSub.mColour = new MR_UInt32[ (size_t)lW*lH ];
         lSub.mColourColumnPtr = new MR_UInt32*[ lW ];
         for( int lColumn = 0; lColumn < lW; lColumn++ )
         {
            lSub.mColourColumnPtr[ lColumn ] = lSub.mColour + (size_t)lColumn*lH;
         }
      }
   }

   if( lUseAi )
   {
      // The AI-enhanced image is the source: the look's grade applied (it is in the game's own colours), a trace of surface variation on its flat fields, and
      // the smaller levels made from it by averaging in linear light.
      std::vector<MR_UInt32> lTopColour( mAiWords );
      if( gsColourGradeAi )
      {
         const HoverNetGfx::ColourGrade lGrade( gsColourBrightness );
         for( size_t lIndex = 0; lIndex < lTopColour.size(); lIndex++ )
         {
            lTopColour[ lIndex ] = lGrade.Apply( lTopColour[ lIndex ] );
         }
      }
      HoverNetTex::AddSurfaceDetail( lTopColour.data(), mAiWidth, mAiHeight, 1 );
      if( mRecolour )
      {
         mRecolour( lTopColour, mAiWidth, mAiHeight );
      }
      for( int lIndex = 0; lIndex < mSubBitmapCount; lIndex++ )
      {
         SubBitmap& lSub = mSubBitmapList[ lIndex ];
         const int lW = lSub.mXRes << lShift, lH = lSub.mYRes << lShift;
         if( lIndex == lTop )
         {
            memcpy( lSub.mColour, lTopColour.data(), (size_t)lW*lH*sizeof( MR_UInt32 ) );
         }
         else if( !HoverNetTex::Downsample( lTopColour.data(), mAiWidth, mAiHeight, lSub.mColour, lW, lH ) )
         {
            HoverNetTex::EnhanceBase( lSub.mBuffer, lSub.mXRes, lSub.mYRes, lPalette, lSub.mColour, false );   // not a whole division: the palette's colours, at 1x
         }
      }
      mColourVersion.store( lVersion, std::memory_order_release );
      return true;
   }

   std::vector<MR_UInt32> lTopColour( (size_t)lBase.mXRes*lBase.mYRes );
   HoverNetTex::EnhanceBase( lBase.mBuffer, lBase.mXRes, lBase.mYRes, lPalette, lTopColour.data(), true, 1 );
   if( mRecolour )
   {
      mRecolour( lTopColour, lBase.mXRes, lBase.mYRes );
   }
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

static std::mutex  gsBakeMutex;
static MR_UInt8    gsBakePalette[ 768 ];
static bool        gsBakeOn = false;
static std::atomic<int> gsBakeVersion( 0 );

void MR_ResBitmap::SetBakePalette( const MR_UInt8* pPalette768 )
{
   std::lock_guard<std::mutex> lLock( gsBakeMutex );
   if( pPalette768 == NULL )
   {
      if( gsBakeOn ) gsBakeVersion++;
      gsBakeOn = false;
      return;
   }
   if( !gsBakeOn || memcmp( gsBakePalette, pPalette768, sizeof( gsBakePalette ) ) != 0 )
   {
      memcpy( gsBakePalette, pPalette768, sizeof( gsBakePalette ) );
      gsBakeOn = true;
      gsBakeVersion++;
   }
}

int MR_ResBitmap::GetBakePaletteVersion()
{
   return gsBakeVersion.load();
}

MR_ResBitmap* MR_ResBitmap::CloneWithRecolour( const Recolour& pRecolour )const
{
   MR_ResBitmap* lCopy = new MR_ResBitmap( mResourceId );
   lCopy->mWidth = mWidth;
   lCopy->mHeight = mHeight;
   lCopy->mXRes = mXRes;
   lCopy->mYRes = mYRes;
   lCopy->mPlainColor = mPlainColor;
   lCopy->mSubBitmapCount = mSubBitmapCount;
   if( mSubBitmapCount > 0 )
   {
      lCopy->mSubBitmapList = new SubBitmap[ mSubBitmapCount ];
      for( int lIndex = 0; lIndex < mSubBitmapCount; lIndex++ )
      {
         const SubBitmap& lFrom = mSubBitmapList[ lIndex ];
         SubBitmap& lTo = lCopy->mSubBitmapList[ lIndex ];
         delete []lTo.mBuffer;
         delete []lTo.mColumnPtr;
         lTo.mXRes = lFrom.mXRes;
         lTo.mYRes = lFrom.mYRes;
         lTo.mXResShiftFactor = lFrom.mXResShiftFactor;
         lTo.mYResShiftFactor = lFrom.mYResShiftFactor;
         lTo.mHaveTransparent = lFrom.mHaveTransparent;
         lTo.mBuffer = new MR_UInt8[ lFrom.mXRes*lFrom.mYRes ];
         memcpy( lTo.mBuffer, lFrom.mBuffer, (size_t)lFrom.mXRes*lFrom.mYRes );
         lTo.mColumnPtr = new MR_UInt8*[ lFrom.mXRes ];
         for( int lColumn = 0; lColumn < lFrom.mXRes; lColumn++ )
         {
            lTo.mColumnPtr[ lColumn ] = lTo.mBuffer + (size_t)lColumn*lFrom.mYRes;
         }
      }
   }
   {
      // The AI-enhanced image is not carried over: a customised craft is small on screen, and its copies at 2x take a quarter of the memory.
   }
   // With the 8-bit palette known, the design is baked into the clone's indices: every pixel goes through the recolouring and becomes the nearest palette colour
   // (index 0 is transparent and stays). Then the 8-bit picture shows it, and so does true colour, which is made from these indices.
   MR_UInt8 lPalette[ 768 ];
   bool lBake = false;
   {
      std::lock_guard<std::mutex> lLock( gsBakeMutex );
      if( gsBakeOn )
      {
         memcpy( lPalette, gsBakePalette, sizeof( lPalette ) );
         lBake = true;
      }
   }
   if( lBake && pRecolour )
   {
      std::map< MR_UInt32, MR_UInt8 > lNearest;
      for( int lIndex = 0; lIndex < lCopy->mSubBitmapCount; lIndex++ )
      {
         SubBitmap& lSub = lCopy->mSubBitmapList[ lIndex ];
         const int lW = lSub.mXRes, lH = lSub.mYRes;
         std::vector<MR_UInt32> lWords( (size_t)lW*lH );
         for( size_t lPixel = 0; lPixel < lWords.size(); lPixel++ )
         {
            const MR_UInt8* lRgb = &lPalette[ lSub.mBuffer[ lPixel ]*3 ];
            lWords[ lPixel ] = ((MR_UInt32)lRgb[ 0 ] << 16) | ((MR_UInt32)lRgb[ 1 ] << 8) | lRgb[ 2 ];
         }
         pRecolour( lWords, lW, lH );
         for( size_t lPixel = 0; lPixel < lWords.size(); lPixel++ )
         {
            if( lSub.mBuffer[ lPixel ] == 0 )
            {
               continue;   // transparent
            }
            const MR_UInt32 lWanted = lWords[ lPixel ] & 0xFFFFFF;
            std::map< MR_UInt32, MR_UInt8 >::const_iterator lFound = lNearest.find( lWanted );
            if( lFound == lNearest.end() )
            {
               int lBestDistance = 1 << 30;
               MR_UInt8 lBest = lSub.mBuffer[ lPixel ];
               const int lR = (lWanted >> 16) & 255, lG = (lWanted >> 8) & 255, lB = lWanted & 255;
               for( int lCandidate = MR_RESERVED_COLORS_BEGINNING; lCandidate < MR_RESERVED_COLORS_BEGINNING + MR_BASIC_COLORS; lCandidate++ )
               {
                  const int lDR = lR - lPalette[ lCandidate*3 ], lDG = lG - lPalette[ lCandidate*3 + 1 ], lDB = lB - lPalette[ lCandidate*3 + 2 ];
                  const int lDistance = 3*lDR*lDR + 4*lDG*lDG + 2*lDB*lDB;   // the eye is most sensitive to green, least to blue
                  if( lDistance < lBestDistance ) { lBestDistance = lDistance; lBest = (MR_UInt8)lCandidate; }
               }
               lFound = lNearest.insert( std::make_pair( lWanted, lBest ) ).first;
            }
            lSub.mBuffer[ lPixel ] = lFound->second;
         }
      }
   }
   else
   {
      lCopy->mRecolour = pRecolour;   // no 8-bit palette known: true colour only
   }
   lCopy->mColourVersion = -1;
   return lCopy;
}

void MR_ResBitmap::SetEnhancedImage( int pWidth, int pHeight, std::vector<MR_UInt32>& pWords )
{
   if( mSubBitmapCount <= 0 || pWidth <= 0 || pHeight <= 0 || pWords.size() != (size_t)pWidth*pHeight )
   {
      return;
   }
   int lTop = 0;
   for( int lIndex = 1; lIndex < mSubBitmapCount; lIndex++ )
   {
      if( mSubBitmapList[ lIndex ].mXRes*mSubBitmapList[ lIndex ].mYRes > mSubBitmapList[ lTop ].mXRes*mSubBitmapList[ lTop ].mYRes )
      {
         lTop = lIndex;
      }
   }
   const int lXRes = mSubBitmapList[ lTop ].mXRes;
   const int lYRes = mSubBitmapList[ lTop ].mYRes;
   const int lScale = pWidth/lXRes;
   if( pWidth%lXRes != 0 || pHeight%lYRes != 0 || pHeight/lYRes != lScale || (lScale != 2 && lScale != 4 && lScale != 8) )
   {
      return;   // not this texture's size at a power-of-two scale: not used
   }
   mAiWords.swap( pWords );
   mAiWidth  = pWidth;
   mAiHeight = pHeight;
   mColourVersion = -1;
}

unsigned MR_ResBitmap::GetColourSerial()const
{
   EnsureColour();
   return (unsigned)mColourVersion.load() + 1u;
}

int MR_ResBitmap::GetColourScaleShift()const
{
   EnsureColour();
   return mColourShift;
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




