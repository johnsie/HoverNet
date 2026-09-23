// ColorPalette.h
//
// The color palette is a module, not a class. It is the same 
// colorpalette that is always used
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


#ifndef COLOR_PALETTE_H
#define COLOR_PALETTE_H

// #include <ddraw.h>

#include "../Util/MR_Types.h"

#ifndef _WIN32
struct PALETTEENTRY
{
   MR_UInt8 peRed;
   MR_UInt8 peGreen;
   MR_UInt8 peBlue;
   MR_UInt8 peFlags;
};
#endif

#ifdef _WIN32
#ifdef MR_VIDEO_SERVICES
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

#define MR_NB_COLORS                  256
#define MR_RESERVED_COLORS            10 // Windows reserved palette entries
#define MR_RESERVED_COLORS_BEGINNING   10
#define MR_RESERVED_COLORS_END         15
#define MR_BASIC_COLORS               100 // Includes some extra space          
#define MR_BACK_COLORS                128


#define MR_NB_COLOR_INTENSITY       256
#define MR_NORMAL_INTENSITY         128


// Color convertion tables
MR_DllDeclare extern MR_UInt8 MR_ColorTable[ MR_NB_COLOR_INTENSITY ][ MR_NB_COLORS ];
MR_DllDeclare extern MR_UInt8 MR_ColorAdditionTable[ MR_NB_COLORS ][ MR_NB_COLORS ];
              extern double   MR_BasicPalette[][ 3 ];
              extern int      MR_BasicPaletteSize;

MR_DllDeclare PALETTEENTRY*       MR_GetColors( double pGamma, double pIntensity = 0.8, double pIntensityBase = 0.0 ); // return a vectors of MR_NB_COLORS-MR_RESERVED_COLORS
MR_DllDeclare const PALETTEENTRY& MR_ConvertColor( MR_UInt8 pRed, MR_UInt8 pGreen, MR_UInt8 pBlue,
                                                   double pGamma, double pIntensity = 0.8, double pIntensityBase = 0.0 );
 

#undef MR_DllDeclare

#endif





