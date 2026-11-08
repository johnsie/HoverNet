// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-08.
// See repository history for authorship and detailed changes.
// Bitmap.h
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

#ifndef MR_BITMAP_H
#define MR_BITMAP_H

#include "../Util/MR_Types.h"

#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_VIDEO_SERVICES
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif


class MR_DllDeclare MR_Bitmap
{
   // Very flexible but may have to be change for performance issue

   public:
      // Bitmap related functions
      virtual int       GetWidth()const       = 0;  // in mm
      virtual int       GetHeight()const      = 0; // in mm
      virtual int       GetMaxXRes()const     = 0;  
      virtual int       GetMaxYRes()const     = 0;
      virtual MR_UInt8  GetPlainColor()const  = 0;

      // Subbitmap related functions
      virtual int        GetNbSubBitmap()const = 0;
      int                GetBestBitmapFor( int pXRes, int pYRes )const;
      int                GetBestBitmapForYRes( int pYRes )const;
      int                GetBestBitmapForXRes( int pXRes )const;
      int                GetBestBitmapForPitch_4096( int pPitch_4096 )const;

      virtual int        GetXRes( int pSubBitmap )const = 0;
      virtual int        GetYRes( int pSubBitmap )const = 0;
      virtual int        GetXResShiftFactor( int pSubBitmap )const = 0;
      virtual int        GetYResShiftFactor( int pSubBitmap )const = 0;
      virtual MR_UInt8*  GetBuffer( int pSubBitmap )const = 0;
      virtual MR_UInt8*  GetColumnBuffer( int pSubBitmap, int pColumn )const = 0;
      virtual MR_UInt8** GetColumnBufferTable( int pSubBitmap )const = 0;

      // True colour (optional): the same picture as 32-bit words 0x00RRGGBB at TWICE the resolution in each direction (2 * GetXRes columns of 2 * GetYRes
      // words), smoothed and upscaled from the 8-bit texels (see VideoServices/TextureEnhance.h): texel (x, y) of the 8-bit level is covered by the four
      // words (2x, 2y) .. (2x+1, 2y+1).
      // NULL when this bitmap has no true-colour copy (or true colour is off), in which case the 8-bit texels and the palette are used.
      virtual const MR_UInt32*  GetColourColumnBuffer( int /*pSubBitmap*/, int /*pColumn*/ )const { return 0; }
      virtual const MR_UInt32* const* GetColourColumnBufferTable( int /*pSubBitmap*/ )const { return 0; }
};




#undef MR_DllDeclare

#endif



