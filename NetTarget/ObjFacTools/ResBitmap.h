// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-08.
// See repository history for authorship and detailed changes.
// ResBitmap.h
//
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


#ifndef MR_RES_BITMAP_H
#define MR_RES_BITMAP_H

#include "../VideoServices/Bitmap.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_OBJ_FAC_TOOLS
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

class MR_ResBitmap: public MR_Bitmap
{
   protected:
      class SubBitmap
      {
      public:
        int        mXRes;
        int        mYRes;
        int        mXResShiftFactor;
        int        mYResShiftFactor;
        BOOL       mHaveTransparent;

        MR_UInt8*      mBuffer;
        MR_UInt8**     mColumnPtr;
        MR_UInt32*     mColour;          // optional true-colour copy of mBuffer (see MR_Bitmap::GetColourColumnBuffer)
        MR_UInt32**    mColourColumnPtr;

        MR_DllDeclare SubBitmap();
        MR_DllDeclare ~SubBitmap();

        void Serialize( CArchive& pArchive );

      };

      int           mResourceId;
      int           mWidth;          // in milimeters
      int           mHeight;         // in milimeters
      int           mXRes;
      int           mYRes;
      int           mSubBitmapCount;
      MR_UInt8      mPlainColor;     // 0 mean transparent

      SubBitmap*    mSubBitmapList;

      // The true-colour copy is built the first time it is wanted after SetColourPalette: the largest level from the palette indices (with the dither
      // and banding smoothed, see VideoServices/TextureEnhance.h), the smaller ones by averaging it in linear light.
      mutable std::atomic<int> mColourVersion;
      mutable std::mutex       mColourMutex;
      bool EnsureColour()const;
      friend struct ColourWorker;



   public:
      MR_DllDeclare MR_ResBitmap( int pResourceId );   // Only availlable for resourceLib and construction
      MR_DllDeclare virtual ~MR_ResBitmap();

      MR_DllDeclare int  GetResourceId()const;
      MR_DllDeclare void Serialize( CArchive& pArchive );

      MR_DllDeclare int       GetWidth()const;
      MR_DllDeclare int       GetHeight()const;
      MR_DllDeclare int       GetMaxXRes()const;
      MR_DllDeclare int       GetMaxYRes()const;
      MR_DllDeclare MR_UInt8  GetPlainColor()const;

      MR_DllDeclare void      SetWidthHeight( int pWidth, int pHeight );

      // Subbitmap related functions
      MR_DllDeclare int        GetNbSubBitmap()const;
      MR_DllDeclare int        GetXRes( int pSubBitmap )const;
      MR_DllDeclare int        GetYRes( int pSubBitmap )const;
      MR_DllDeclare int        GetXResShiftFactor( int pSubBitmap )const;
      MR_DllDeclare int        GetYResShiftFactor( int pSubBitmap )const;
      MR_DllDeclare MR_UInt8*  GetBuffer( int pSubBitmap )const;
      MR_DllDeclare MR_UInt8*  GetColumnBuffer( int pSubBitmap, int pColumn )const;
      MR_DllDeclare MR_UInt8** GetColumnBufferTable( int pSubBitmap )const;
      MR_DllDeclare const MR_UInt32*  GetColourColumnBuffer( int pSubBitmap, int pColumn )const;
      MR_DllDeclare const MR_UInt32* const* GetColourColumnBufferTable( int pSubBitmap )const;

      // True-colour textures for every bitmap in this program: pPalette768 is what the palette indices mean (256 RGB triples, copied; NULL switches
      // true-colour textures off). Safe to call from any thread; textures are rebuilt lazily, one at a time, when next used.
      MR_DllDeclare static void SetColourPalette( const MR_UInt8* pPalette768 );

};



#undef MR_DllDeclare

#endif



