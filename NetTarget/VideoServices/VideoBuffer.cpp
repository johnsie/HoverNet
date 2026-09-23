// VideoBuffer.cpp
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

#include "stdafx.h"

#include "VideoBuffer.h"
#include "ColorPalette.h"
#include "VideoBufferSDL2Integration.h"

#include "../Util/Profiler.h"
#include <fstream>


// Debug flag
#ifdef _DEBUG
static const BOOL gDebugMode = TRUE;
#else
static const BOOL gDebugMode = FALSE;
#endif


// Video card debuging traces

// #define _CARD_DEBUG

#ifdef _CARD_DEBUG

#include <Mmsystem.h>

static FILE* gOutputFile = NULL;

#define OPEN_LOG()                              \
   if( gOutputFile == NULL )                    \
   {                                            \
      gOutputFile = fopen( "Video.log", "a" );  \
   }                                            \

#define CLOSE_LOG()                             \
   if( gOutputFile != NULL )                    \
   {                                            \
      fclose( gOutputFile );                    \
      gOutputFile = NULL;                       \
   }                                            \

static void PrintTimeAndLine( int pLine )
{
   if( gOutputFile != NULL )
   {
      fprintf( gOutputFile, "%6d %4d : ", (int)timeGetTime(), pLine );
   }
}

static int Assert( int pCondition, int pLine )
{
   if( !pCondition && (gOutputFile != NULL) )
   {
      fprintf( gOutputFile, "%6d %4d : ASSERT FAILED", (int)timeGetTime(), pLine );
   }

   return pCondition;
}

static void PrintLog( const char* pFormat, ... )
{
   va_list lParamList;

   va_start( lParamList, pFormat );

   if( gOutputFile != NULL )
   {
      vfprintf( gOutputFile, pFormat, lParamList );
      fprintf( gOutputFile, "\n" );
      fflush( gOutputFile );
   }
}

int DDrawCall( int pFuncResult, int pLine )
{
   const char* lErrStr = "DDERR_<other>";

   switch( pFuncResult )
   {
      case DD_OK:
         lErrStr = "DD_OK";
         break;

      case DDERR_INVALIDPARAMS:
         lErrStr = "DDERR_INVALIDPARAMS";
         break;

      case DDERR_INVALIDOBJECT:
         lErrStr = "DDERR_INVALIDOBJECT";
         break;

      case DDERR_SURFACELOST:
         lErrStr = "DDERR_SURFACELOST";
         break;

      case DDERR_SURFACEBUSY:
         lErrStr = "DDERR_SURFACEBUSY";
         break;

      case DDERR_GENERIC:
         lErrStr = "DDERR_GENERIC";
         break;

      case DDERR_WASSTILLDRAWING:
         lErrStr = "DDERR_WASSTILLDRAWING";
         break;

      case DDERR_UNSUPPORTED:
         lErrStr = "DDERR_UNSUPPORTED";
         break;

      case DDERR_NOTFLIPPABLE:
         lErrStr = "DDERR_NOTFLIPPABLE";
         break;

      case DDERR_NOFLIPHW:
         lErrStr = "DDERR_NOFLIPHW";
         break;

      case DDERR_INVALIDMODE:
         lErrStr = "DDERR_INVALIDMODE";
         break;

      case DDERR_LOCKEDSURFACES:
         lErrStr = "DDERR_LOCKEDSURFACES";
         break;

      /*
      case DDERR_WASSTILLDRAWING:
         lErrStr = "DDERR_WASSTILLDRAWING";
         break;
      */

      case DDERR_NOEXCLUSIVEMODE:
         lErrStr = "DDERR_NOEXCLUSIVEMODE";
         break;

      /*
      case DDERR_INVALIDPARAMS:
         lErrStr = "DDERR_INVALIDPARAMS";
         break;
      */

      case DDERR_OUTOFVIDEOMEMORY:
         lErrStr = "DDERR_OUTOFVIDEOMEMORY";
         break;

      case DDERR_NODIRECTDRAWHW:
         lErrStr = "DDERR_NODIRECTDRAWHW";
         break;

      case DDERR_NOCOOPERATIVELEVELSET:
         lErrStr = "DDERR_NOCOOPERATIVELEVELSET";
         break;

      case DDERR_INVALIDCAPS:
         lErrStr = "DDERR_INVALIDCAPS";
         break;

      case DDERR_INVALIDPIXELFORMAT:
         lErrStr = "DDERR_INVALIDPIXELFORMAT";
         break;

      case DDERR_NOALPHAHW:
         lErrStr = "DDERR_NOALPHAHW";
         break;

      /*
      case DDERR_NOFLIPHW:
         lErrStr = "DDERR_NOFLIPHW";
         break;
      */

      case DDERR_NOZBUFFERHW:
         lErrStr = "DDERR_NOZBUFFERHW";
         break;

      /*
      case DDERR_NOEXCLUSIVEMODE:
         lErrStr = "DDERR_NOEXCLUSIVEMODE";
         break;
      */

      case DDERR_OUTOFMEMORY:
         lErrStr = "DDERR_OUTOFMEMORY";
         break;

      case DDERR_PRIMARYSURFACEALREADYEXISTS:
         lErrStr = "DDERR_PRIMARYSURFACEALREADYEXISTS";
         break;

      case DDERR_NOEMULATION	:
         lErrStr = "DDERR_NOEMULATION";
         break;

      case DDERR_INCOMPATIBLEPRIMARY:
         lErrStr = "DDERR_INCOMPATIBLEPRIMARY";
         break;
   }

   if( gOutputFile != NULL )
   {
      fprintf( gOutputFile, "%6d %4d : %d %s\n", (int)timeGetTime(), pLine, pFuncResult, lErrStr );
      fflush( gOutputFile );
   }

   return pFuncResult;
}

#define PRINT_LOG   PrintTimeAndLine( __LINE__ );PrintLog

#define DD_CALL( pFunc )   DDrawCall( pFunc, __LINE__ )

#ifdef ASSERT
#undef ASSERT
#endif

#define ASSERT( pCondition ) Assert( pCondition, __LINE__ )

#else

void PrintLog( const char* pFormat, ... );

#define OPEN_LOG()
#define CLOSE_LOG()

#ifdef _DEBUG
   #define PRINT_LOG          1?NULL:
#else
   #define PRINT_LOG          if( FALSE )PrintLog
#endif
#define DD_CALL( pFunc )   pFunc

#endif


MR_VideoBuffer::MR_VideoBuffer( HWND pWindow, double pGamma, double pContrast, double pBrightness )
{
   OPEN_LOG();
   PRINT_LOG( "VIDEO_BUFFER_CREATION" );

   ASSERT( pWindow != NULL );

   mWindow      = pWindow;
   mDirectDraw  = NULL;
   mFrontBuffer = NULL;
   mBackBuffer  = NULL;
   mPalette     = NULL;
   mZBuffer     = NULL;
   mBuffer      = NULL;
   mClipper     = NULL;
   mBackPalette = NULL;

   mModeSettingInProgress = FALSE;
   mFullScreen            = FALSE;

   mIconMode       = IsIconic( pWindow );

   mGamma      = pGamma;
   mContrast   = pContrast;
   mBrightness = pBrightness;

   mSpecialWindowMode = FALSE;

   /*
   if( !SetVideoMode() )
   {

   }
   */
}


MR_VideoBuffer::~MR_VideoBuffer()
{
//   mFullScreen        = TRUE;
//   mSpecialWindowMode = FALSE;   // force real windows resolution
   ReturnToWindowsResolution();

   // Shut down SDL2Graphics if active
   ShutdownSDL2Graphics();

   if( mPalette != NULL )
   {
      mPalette->Release();
      mPalette = NULL;
   }

   if( mDirectDraw != NULL )
   {
      mDirectDraw->Release();
   }

   delete []mBackPalette;

   PRINT_LOG( "VIDEO_BUFFER_DESTRUCTION\n\n" );
   CLOSE_LOG();

}


BOOL MR_VideoBuffer::InitDirectDraw()
{
   PRINT_LOG( "InitDirectDraw" );

   // DirectDraw initialization is unavailable (ddraw.lib not present)
   // Return FALSE to skip DirectDraw setup
   return FALSE;
}


BOOL MR_VideoBuffer::TryToSet256ColorMode()
{

   // do it only if it is safe
   if( !mSpecialWindowMode && !mFullScreen && !mModeSettingInProgress && (mDirectDraw!=NULL) )
   {
      // Retrieve current mode info 
      mSpecialModeXRes = GetSystemMetrics( SM_CXSCREEN );
      mSpecialModeYRes = GetSystemMetrics( SM_CYSCREEN );

      GetWindowRect( mWindow, &mOriginalPos );

      if( DD_CALL(mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN| DDSCL_ALLOWMODEX |DDSCL_ALLOWREBOOT|DDSCL_NOWINDOWCHANGES )) == DD_OK )
      {
         // Try 32-bit first, then 16-bit for modern color support
         if( DD_CALL(mDirectDraw->SetDisplayMode( mSpecialModeXRes, mSpecialModeYRes, 32 )) == DD_OK ||
             DD_CALL(mDirectDraw->SetDisplayMode( mSpecialModeXRes, mSpecialModeYRes, 16 )) == DD_OK )
         {
            mSpecialWindowMode = TRUE;

            // Resize window to it's original position
            SetWindowPos( mWindow,
                          HWND_NOTOPMOST,     // This parameter have no effect when used here(I dont know why?? ask Bill)
                          mOriginalPos.left,
                          mOriginalPos.top,
                          mOriginalPos.right-mOriginalPos.left,
                          mOriginalPos.bottom-mOriginalPos.top,
                          SWP_SHOWWINDOW /*SWP_NOACTIVATE*/    );
         }
         else
         {
            DD_CALL( mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_NORMAL  ));

            // It failled BOF
            ASSERT( FALSE );
         }
      }

   }

   return mSpecialWindowMode;
}


void MR_VideoBuffer::DeleteInternalSurfaces()
{
   PRINT_LOG( "DeleteInternalSurfaces" );


   ASSERT( mBuffer == NULL ); // should be unlock

   if( mDirectDraw!= NULL )
   {
      if( mBackBuffer != NULL )
      {
         DD_CALL( mBackBuffer->Release() );
         mBackBuffer = NULL;
      }

      if( mFrontBuffer != NULL )
      {
         DD_CALL( mFrontBuffer->Release() );
         mFrontBuffer = NULL;
      }

      if( mClipper != NULL )
      {
         DD_CALL( mClipper->Release() ); 
         mClipper = NULL;
      }
   }
   delete []mZBuffer;
   mZBuffer = NULL;
   mBuffer  = NULL;
}

void MR_VideoBuffer::CreatePalette( double pGamma, double pContrast, double pBrightness )
{
   PRINT_LOG( "CreatePalette" );

   PALETTEENTRY lPalette[256];

   int lCounter;

   mGamma      = pGamma;
   mContrast   = pContrast;
   mBrightness = pBrightness;

   if( mGamma < 0.2 )
   {
       mGamma = 0.2;
   }

   if( mGamma > 4.0 )
   {
       mGamma = 4.0;
   }

   if( mContrast > 1.0 )
   {
      mContrast = 1.0;
   }

   if( mContrast < 0.3 )
   {
      mContrast = 0.3;
   }

   if( mBrightness > 1 )
   {
      mBrightness = 1.0;
   }

   if( mBrightness < 0.3 )
   {
      mBrightness = 0.3;
   }

   // Clean existing pallette
   if( mPalette != NULL )
   {
      mPalette->Release();
      mPalette = NULL;
   }

   // Generate the game palette (system colors + game colors)
   {
      // Initialize with system colors (Ignore errors)
      HDC hdc = GetDC(NULL);
      if( GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE )
      {
         // get the current windows colors.
         GetSystemPaletteEntries(hdc, 0, 256, lPalette );
      }
      else
      {
         // ASSERT( FALSE );
      }
      ReleaseDC(NULL, hdc);

      // Add out own entries
      PALETTEENTRY* lOurEntries = MR_GetColors( 1.0/mGamma, mContrast*mBrightness, mBrightness-(mContrast*mBrightness) );

      for( lCounter = 0; lCounter<MR_BASIC_COLORS; lCounter++ )
      {
         lPalette[ MR_RESERVED_COLORS_BEGINNING+lCounter ] = lOurEntries[ lCounter ];
         lPalette[ MR_RESERVED_COLORS_BEGINNING+lCounter ].peFlags = PC_NOCOLLAPSE;
      }
      delete []lOurEntries;


      if( mBackPalette != NULL )
      {
         for( lCounter = 0; lCounter<MR_BACK_COLORS; lCounter++ )
         { 
            lPalette[ MR_RESERVED_COLORS_BEGINNING+MR_BASIC_COLORS+lCounter ] = 
               MR_ConvertColor( mBackPalette[ lCounter*3], mBackPalette[ lCounter*3+1], mBackPalette[ lCounter*3+2],
                                1.0/mGamma, mContrast*mBrightness, mBrightness-(mContrast*mBrightness) );
            lPalette[ MR_RESERVED_COLORS_BEGINNING+MR_BASIC_COLORS+lCounter ].peFlags = PC_NOCOLLAPSE;
         }
      }
      

      for( lCounter = 0; lCounter<MR_RESERVED_COLORS_BEGINNING; lCounter++ )
      {
         lPalette[ lCounter ].peFlags = 0;
      }
   }

   // CRITICAL: Update SDL2Graphics palette if available (before DirectDraw, so it works even if DirectDraw not initialized)
   if( IsSDL2GraphicsAvailable() && g_SDL2GraphicsAdapter != NULL )
   {
      // Convert PALETTEENTRY to RGB format (3 bytes per color)
      uint8_t* rgbPalette = new uint8_t[768];
      for( int i = 0; i < 256; i++ )
      {
         rgbPalette[i*3 + 0] = lPalette[i].peRed;
         rgbPalette[i*3 + 1] = lPalette[i].peGreen;
         rgbPalette[i*3 + 2] = lPalette[i].peBlue;
      }
      
      FILE* paletteLog = fopen("C:\\originalhr\\HoverRace\\Release\\Debug_CreatePalette.log", "a");
      if(paletteLog) {
         fprintf(paletteLog, "CreatePalette: Setting palette via adapter, adapter=%p\n", g_SDL2GraphicsAdapter);
         fprintf(paletteLog, "  Palette[0] = RGB(%d,%d,%d)\n", rgbPalette[0], rgbPalette[1], rgbPalette[2]);
         fprintf(paletteLog, "  Palette[1] = RGB(%d,%d,%d)\n", rgbPalette[3], rgbPalette[4], rgbPalette[5]);
         fflush(paletteLog);
         fclose(paletteLog);
      }
      
      g_SDL2GraphicsAdapter->SetPalette( rgbPalette );
      PRINT_LOG( "Updated SDL2Graphics palette from CreatePalette" );
      delete[] rgbPalette;
   }

   // Update DirectDraw palette if available
   if( mDirectDraw != NULL )
   {
      // Create the palette
      if( DD_CALL( mDirectDraw->CreatePalette(DDPCAPS_8BIT /*|DDPCAPS_ALLOW256*/, lPalette, &mPalette, NULL)) != DD_OK )
      {
         ASSERT( FALSE );
         mPalette = NULL;
      }

      // Assign the palette to the existing buffers
      // AssignPalette();
   }
}

void MR_VideoBuffer::GetPaletteAttrib( double& pGamma, double& pContrast, double& pBrightness )
{
   pGamma      = mGamma;
   pContrast   = mContrast;
   pBrightness = mBrightness;
}

void MR_VideoBuffer::SetBackPalette( MR_UInt8* pPalette )
{
   delete []mBackPalette;
   mBackPalette = pPalette;

   CreatePalette( mGamma, mContrast, mBrightness );
}

const MR_UInt8* MR_VideoBuffer::GetBackPalette()const
{
   return mBackPalette;
}


void MR_VideoBuffer::AssignPalette()
{
   PRINT_LOG( "AssignPalette" );

   // Currently only work in 8bit mode   
   if( (mFrontBuffer != NULL)&&(mPalette!=NULL) )
   {
      DD_CALL( mFrontBuffer->SetPalette( mPalette ) );

   }

   /*
   if( (mBackBuffer != NULL)&&( mPalette!=NULL)&&!mFullScreen )
   {
      HRESULT lErrorCode = mBackBuffer->SetPalette( mPalette );
   }
   */
   
   
}


void MR_VideoBuffer::ReturnToWindowsResolution()
{
   PRINT_LOG( "ReturnToWindowsResolution" );

   DeleteInternalSurfaces();

   /*
   if( mSpecialMode && (GetActiveWindow()==mWindow ))
   {
      mFullScreen = TRUE;
   }
   */

   if( mDirectDraw && mFullScreen )
   {
      mFullScreen = FALSE;

      if( !mSpecialWindowMode )
      {
         DD_CALL( mDirectDraw->RestoreDisplayMode() );
      
         if( DD_CALL( mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_NORMAL  )) != DD_OK )
         {
            ASSERT( FALSE );
         }             
      }
      else
      {
         DD_CALL( mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN | DDSCL_ALLOWMODEX|DDSCL_ALLOWREBOOT|DDSCL_NOWINDOWCHANGES ));

         if( DD_CALL(mDirectDraw->SetDisplayMode( mSpecialModeXRes, mSpecialModeYRes, 8 )) == DD_OK )
         {
            /*
            if( DD_CALL( mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN | DDSCL_ALLOWMODEX|DDSCL_ALLOWREBOOT|DDSCL_NOWINDOWCHANGES )) != DD_OK )
            {
               ASSERT( FALSE );
            } 
            */
         }
         else
         {
            // ASSERT( FALSE );

            mSpecialWindowMode = FALSE;

            DD_CALL( mDirectDraw->RestoreDisplayMode() );
      
            if( DD_CALL( mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_NORMAL  )) != DD_OK )
            {
               ASSERT( FALSE );
            }             
         }

      }
   
      // if( mFullScreen )
      {
      // Adjust the window position
      SetWindowLong( mWindow, GWL_EXSTYLE, mOriginalExStyle );
      SetWindowLong( mWindow, GWL_STYLE, mOriginalStyle );

      // SetForegroundWindow( mWindow );

   
   
      SetWindowPos( mWindow,
                    HWND_NOTOPMOST,     // This parameter have no effect when used here(I dont know why?? ask Bill)
                    mOriginalPos.left,
                    mOriginalPos.top,
                    mOriginalPos.right-mOriginalPos.left,
                    mOriginalPos.bottom-mOriginalPos.top,
                    SWP_SHOWWINDOW /*SWP_NOACTIVATE*/    );
   
      CreatePalette( mGamma, mContrast, mBrightness );      
      }
   }       
}


BOOL MR_VideoBuffer::SetVideoMode()
{
   // Log to file to track when SetVideoMode is called
   {
      std::ofstream logfile("C:\\originalhr\\HoverRace\\Release\\videobuffer_setvideo_mode.log", std::ios::app);
      logfile << "=== SetVideoMode() called ===" << std::endl;
      logfile.flush();
   }
   
   PRINT_LOG( "SetVideoMode(Window)" );

   BOOL            lReturnValue;
   DDSURFACEDESC   lSurfaceDesc;
   RECT            lRect;

   ASSERT( !mModeSettingInProgress );

   mModeSettingInProgress = TRUE;

   // Retrieve window dimensions
   if( !GetClientRect( mWindow, &lRect ) )
   {
      PRINT_LOG( "GetClientRect failed" );
      mXRes = 800;
      mYRes = 600;
   }
   else
   {
      mXRes = lRect.right;
      mYRes = lRect.bottom;
   }
   
   FILE* dbgFile = fopen("c:\\originalhr\\HoverRace\\Release\\Debug_SetVideoMode_Flow.log", "a");
   if(dbgFile) { fprintf(dbgFile, "SetVideoMode: Window dimensions: %dx%d\n", mXRes, mYRes); fflush(dbgFile); }
   
   // Always try SDL2Graphics when available for consistent rendering
   BOOL used_sdl2 = FALSE;
   BOOL sdl2_available = IsSDL2GraphicsAvailable();
   if(dbgFile) { fprintf(dbgFile, "SetVideoMode: IsSDL2GraphicsAvailable() returned: %s\n", sdl2_available ? "TRUE" : "FALSE"); fflush(dbgFile); }
   
   if( sdl2_available )
   {
      if(dbgFile) { fprintf(dbgFile, "SetVideoMode: SDL2Graphics available, calling InitializeSDL2Graphics...\n"); fflush(dbgFile); }
      
      BOOL init_result = InitializeSDL2Graphics( mWindow, mXRes, mYRes );
      if(dbgFile) { fprintf(dbgFile, "SetVideoMode: InitializeSDL2Graphics returned %s, adapter=%p\n", init_result ? "TRUE" : "FALSE", g_SDL2GraphicsAdapter); fflush(dbgFile); }
      
      if( init_result && g_SDL2GraphicsAdapter != NULL )
      {
         if(dbgFile) { fprintf(dbgFile, "SetVideoMode: SDL2Graphics initialized successfully, using SDL2 mode!\n"); fflush(dbgFile); }
         
         used_sdl2 = TRUE;
         mLineLen = mXRes;  // Linear stride for SDL2 buffer
         
         // Create Z-buffer
         if( mZBuffer != NULL )
         {
            delete[] mZBuffer;
            mZBuffer = NULL;
         }
         mZBuffer = new MR_UInt16[ mXRes * mYRes ];
         
         // Create palette
         CreatePalette( mGamma, mContrast, mBrightness );
         
         mModeSettingInProgress = FALSE;
         if(dbgFile) { fprintf(dbgFile, "SetVideoMode complete (SDL2 mode)\n"); fflush(dbgFile); fclose(dbgFile); }
         
         PRINT_LOG( "SetVideoMode: SDL2Graphics active, %dx%d", mXRes, mYRes );
         return TRUE;
      }
      else
      {
         if(dbgFile) { fprintf(dbgFile, "SetVideoMode: SDL2Graphics init FAILED!\n"); fflush(dbgFile); }
      }
   }
   else
   {
      if(dbgFile) { fprintf(dbgFile, "SetVideoMode: SDL2Graphics NOT AVAILABLE\n"); fflush(dbgFile); }
   }
   
   // GDI fallback mode
   if(dbgFile) { fprintf(dbgFile, "Using GDI fallback mode\n"); fflush(dbgFile); }
   PRINT_LOG( "SetVideoMode: Using GDI fallback" );
   
   mLineLen = mXRes;
   
   // Allocate system memory buffer for GDI rendering
   if( mBuffer != NULL )
   {
      delete[] mBuffer;
      mBuffer = NULL;
   }
   
   mBuffer = new MR_UInt8[ mXRes * mYRes ];
   
   PRINT_LOG( "Allocated GDI buffer: %dx%d = %d bytes at %p", mXRes, mYRes, mXRes * mYRes, mBuffer );
   
   // Get window position
   {
      POINT lPoint = {0,0};
      if( ClientToScreen( mWindow, &lPoint ) )
      {
         mX0 = lPoint.x;
         mY0 = lPoint.y;
      }
   }
   
   // Create Z-buffer
   if( mZBuffer != NULL )
   {
      delete[] mZBuffer;
      mZBuffer = NULL;
   }
   mZBuffer = new MR_UInt16[ mXRes * mYRes ];
   
   // Create palette
   CreatePalette( mGamma, mContrast, mBrightness );
   
   mModeSettingInProgress = FALSE;
   
   if(dbgFile) { fprintf(dbgFile, "SetVideoMode complete (GDI mode)\n"); fflush(dbgFile); fclose(dbgFile); }
   PRINT_LOG( "SetVideoMode: GDI mode, %dx%d", mXRes, mYRes );
   return TRUE;
}


BOOL MR_VideoBuffer::SetVideoMode( int pXRes, int pYRes )
{
   PRINT_LOG( "SetVideoMode %dx%d", pXRes, pYRes );

   // Set afull screen video mode
   HRESULT         lErrorCode;
   BOOL            lReturnValue;
   DDSURFACEDESC   lSurfaceDesc;
   // DDCAPS          lDDCaps;


   ASSERT( !mModeSettingInProgress );

   mModeSettingInProgress = TRUE;

   lReturnValue = InitDirectDraw();


   if( lReturnValue )
   {
      DeleteInternalSurfaces();

      if( !mFullScreen )
      {
         // Save current position and style to be able to restore the current mode
         mOriginalExStyle = GetWindowLong( mWindow, GWL_EXSTYLE );
         mOriginalStyle   = GetWindowLong( mWindow, GWL_STYLE );
         GetWindowRect( mWindow, &mOriginalPos );

      }
      // Make the window a non-borderwindow      
      // SetWindowLong( mWindow, GWL_STYLE, mOriginalStyle & ~(WS_THICKFRAME ) );
      

      if( DD_CALL(mDirectDraw->SetCooperativeLevel( mWindow, DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN | DDSCL_ALLOWMODEX|DDSCL_ALLOWREBOOT/*|DDSCL_NOWINDOWCHANGES*/)) != DD_OK )
      {
         ASSERT( FALSE );
         lReturnValue = FALSE;
      }
   }


   if( lReturnValue ) 
   {
      mXRes = pXRes;
      mYRes = pYRes;
      mLineLen = mXRes;

      mFullScreen = TRUE;
   }

   if( lReturnValue )
   {
      // ASSERT( FALSE );

      // Try 32-bit color first, fall back to 16-bit if needed
      if( DD_CALL(mDirectDraw->SetDisplayMode( pXRes, pYRes, 32 )) != DD_OK )
      {
         // Try 16-bit
         if( DD_CALL(mDirectDraw->SetDisplayMode( pXRes, pYRes, 16 )) != DD_OK )
         {
            lReturnValue = FALSE;
            ASSERT( FALSE );
         }
      }
   }

   if( lReturnValue )
   {
      // Resize to full screen
      ShowWindow( mWindow, SW_MAXIMIZE );

   }

   // Create a front buffer
   if( lReturnValue )
   {
      memset( &lSurfaceDesc, 0, sizeof( lSurfaceDesc ) );
      lSurfaceDesc.dwSize = sizeof( lSurfaceDesc );

      lSurfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
      lSurfaceDesc.dwBackBufferCount = 1;
      lSurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX |DDSCAPS_SYSTEMMEMORY;


      if( (lErrorCode = DD_CALL(mDirectDraw->CreateSurface( &lSurfaceDesc, &mFrontBuffer, NULL ))) != DD_OK )
      {
         ASSERT( FALSE );

         lSurfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX /*|DDSCAPS_SYSTEMMEMORY*/;

         if( (lErrorCode = DD_CALL(mDirectDraw->CreateSurface( &lSurfaceDesc, &mFrontBuffer, NULL ))) != DD_OK )
         {
            ASSERT( FALSE );
            lReturnValue =FALSE;
         }
      }
      
      if( lReturnValue )
      {
         // Create (retrieve already created) the working surface
         DDSCAPS lDDSCaps;
         
         lDDSCaps.dwCaps = DDSCAPS_BACKBUFFER;

         if( DD_CALL(mFrontBuffer->GetAttachedSurface( &lDDSCaps, &mBackBuffer )) != DD_OK )
         {
            ASSERT( FALSE );
            lReturnValue = FALSE;
         }
      }
   }


   if( lReturnValue )
   {
      // Create a local memory ZBuffer
      // We do not use DirectDrawZBuffer for now

      mZBuffer = new MR_UInt16[ mXRes*mYRes ];

   }

   if( !lReturnValue )
   {
      ReturnToWindowsResolution();
   }

   // AssignPalette();

   mModeSettingInProgress = FALSE;

   return lReturnValue;
}

BOOL MR_VideoBuffer::IsWindowMode()const
{
   return !mFullScreen;
}

BOOL MR_VideoBuffer::IsIconMode()const
{
   return mIconMode;
}

BOOL MR_VideoBuffer::IsModeSettingInProgress()const
{
   return mModeSettingInProgress;
}

int MR_VideoBuffer::GetXRes()const
{
   return mXRes;
}

int MR_VideoBuffer::GetYRes()const
{
   return mYRes;
}

int MR_VideoBuffer::GetLineLen()const
{
   return mLineLen;
}

int MR_VideoBuffer::GetZLineLen()const
{
   return mXRes;
}


MR_UInt8* MR_VideoBuffer::GetBuffer()
{
   return mBuffer;
}

MR_UInt16* MR_VideoBuffer::GetZBuffer()
{
   return mZBuffer;
}

int MR_VideoBuffer::GetXPixelMeter()const
{
   if( mFullScreen )
   {
      return mXRes*3;
   }
   else
   {
      return 3*GetSystemMetrics( SM_CXSCREEN );
   }
}

int MR_VideoBuffer::GetYPixelMeter()const
{
   if( mFullScreen )
   {
      return mYRes*4;
   }
   else
   {
      return 4*GetSystemMetrics( SM_CYSCREEN );
   }
}


BOOL MR_VideoBuffer::Lock()
{
   PRINT_LOG( "Lock" );

   MR_SAMPLE_CONTEXT( "LockVideoBuffer" );

   HRESULT lErrorCode;
   BOOL lReturnValue = TRUE;
   
   static int frame_count = 0;
   frame_count++;

   // Check if we're in SDL2Graphics mode (modern graphics backend)
   if( IsSDL2GraphicsAvailable() && g_SDL2GraphicsAdapter != NULL )
   {
      // SDL2Graphics mode - use adapter for buffer access
      PRINT_LOG( "Lock: SDL2Graphics mode - requesting buffer" );
      FILE *logFile = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_VideoBuffer_Lock.log", "a");
      if(logFile) { 
         fprintf(logFile, "[Frame %d] Lock: Before Lock() - mXRes=%d, mYRes=%d, mLineLen=%d, mBuffer=%p\n", frame_count, mXRes, mYRes, mLineLen, mBuffer);
         fprintf(logFile, "[Frame %d] Lock: About to call g_SDL2GraphicsAdapter->Lock(), adapter=%p\n", frame_count, g_SDL2GraphicsAdapter); 
         fflush(logFile); fclose(logFile); 
      }
      
      uint8_t* lBuffer = nullptr;
      
      // Add check to ensure adapter is still valid
      try {
         if( g_SDL2GraphicsAdapter->Lock( lBuffer ) )
         {
            logFile = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_VideoBuffer_Lock.log", "a");
            if(logFile) { 
               fprintf(logFile, "[Frame %d] Lock: Lock() succeeded, buffer=%p (mXRes=%d, mYRes=%d, expected size=%d)\n", 
                       frame_count, lBuffer, mXRes, mYRes, mXRes * mYRes);
               fflush(logFile); fclose(logFile); 
            }
            
            mBuffer = lBuffer;
            PRINT_LOG( "Lock: SDL2Graphics buffer acquired, returning TRUE" );
            return TRUE;
         }
         else
         {
            logFile = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_VideoBuffer_Lock.log", "a");
            if(logFile) { fprintf(logFile, "[Frame %d] Lock: Lock() returned FALSE (already locked)\n", frame_count); fflush(logFile); fclose(logFile); }
            
            PRINT_LOG( "Lock: SDL2Graphics buffer acquisition failed" );
            return FALSE;
         }
      }
      catch(...) {
         logFile = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_VideoBuffer_Lock.log", "a");
         if(logFile) { fprintf(logFile, "[Frame %d] Lock: EXCEPTION caught in Lock()\n", frame_count); fflush(logFile); fclose(logFile); }
         return FALSE;
      }
   }

   // Check if we're in GDI fallback mode (buffer in system memory, no DirectDraw)
   if( mBuffer != NULL && mBackBuffer == NULL && mDirectDraw == NULL )
   {
      // GDI mode - buffer already allocated and ready
      PRINT_LOG( "Lock: GDI mode - returning TRUE" );
      return TRUE;
   }

   // If buffer already allocated but we don't know the mode, it's an error
   if( mBuffer != NULL )
   {
      PRINT_LOG( "Lock: Buffer exists but mode unclear, asserting" );
      ASSERT( FALSE );
      return FALSE;
   }

   // DirectDraw mode - proceed with original logic
   ASSERT( mDirectDraw != NULL );

   if( mIconMode )
   {
      lReturnValue = FALSE;
   }

   if( mBackBuffer == NULL )
   {
      // No surface 
      lReturnValue = FALSE;
   }
   
   // Restore lost buffers
   if( lReturnValue )
   {
      if( DD_CALL( mFrontBuffer->IsLost() ) == DDERR_SURFACELOST )
      {
         if( DD_CALL(mFrontBuffer->Restore() ) != DD_OK )
         {
            ASSERT( FALSE );
            lReturnValue = FALSE;
         }
      }
      if( DD_CALL(mBackBuffer->IsLost()) == DDERR_SURFACELOST )
      {
         if( DD_CALL(mBackBuffer->Restore()) != DD_OK )
         {
            ASSERT( FALSE );
            lReturnValue = FALSE;
         }
      }

   }

   // Do the lock
   if( lReturnValue )
   {
      if( !gDebugMode )
      {
         DDSURFACEDESC   lSurfaceDesc;

         lSurfaceDesc.dwSize = sizeof( lSurfaceDesc );

         if( DD_CALL(mBackBuffer->Lock( NULL, &lSurfaceDesc, DDLOCK_SURFACEMEMORYPTR|DDLOCK_WAIT, NULL )) != DD_OK )
         {
            ASSERT( FALSE );
            lReturnValue = FALSE;
         }
         else
         {
            mLineLen = lSurfaceDesc.lPitch;
            mBuffer  = (MR_UInt8*)lSurfaceDesc.lpSurface;
         }
      }
      else
      {
         // Debug lock type
         mBuffer    = new MR_UInt8[ mXRes*mYRes ];
         mLineLen   = mXRes;
      }
   }

   return lReturnValue;
}

void MR_VideoBuffer::Unlock()
{
   FILE *debugLog = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_Unlock_Called.log", "a");
   if(debugLog) { fprintf(debugLog, "Unlock() called\n"); fflush(debugLog); fclose(debugLog); }
   
   PRINT_LOG( "Unlock: START" );

   MR_SAMPLE_CONTEXT( "UnlockVideoBuffer" );

   if( mBuffer == NULL )
   {
      PRINT_LOG( "Unlock: ERROR - No buffer to unlock" );
      return;
   }

   PRINT_LOG( "Unlock: Buffer OK, checking mode" );
   
   FILE *logFile = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_Unlock_Path.log", "a");
   if(logFile) {
      fprintf(logFile, "Unlock called: mBuffer=%p, IsSDL2Available=%d, adapter=%p\n", 
              mBuffer, IsSDL2GraphicsAvailable(), g_SDL2GraphicsAdapter);
      fprintf(logFile, "  mBackBuffer=%p, mDirectDraw=%p\n", mBackBuffer, mDirectDraw);
      fflush(logFile);
   }

   // Check if we're in SDL2Graphics mode (modern graphics backend)
   if( IsSDL2GraphicsAvailable() && g_SDL2GraphicsAdapter != NULL )
   {
      if(logFile) {
         fprintf(logFile, "  -> Taking SDL2Graphics path\n");
         fflush(logFile); fclose(logFile);
      }
      // SDL2Graphics mode - pass our buffer to adapter for display
      PRINT_LOG( "Unlock: SDL2Graphics mode, calling adapter Unlock with buffer=%p", mBuffer );
      FILE *logFile2 = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_VideoBuffer_Unlock.log", "a");
      if(logFile2) {
         fprintf(logFile2, "Unlock: SDL2Graphics mode - buffer=%p, mXRes=%d, mYRes=%d, mLineLen=%d\n", 
                 mBuffer, mXRes, mYRes, mLineLen);
         fflush(logFile2); fclose(logFile2);
      }
      
      // Wrap SDL2 Unlock call to catch potential crashes
      try {
         g_SDL2GraphicsAdapter->Unlock(mBuffer);  // Pass our buffer for display
         
         // Log successful SDL2 unlock
         FILE *logFile3 = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_SDL2_Unlock_Success.log", "a");
         if(logFile3) {
            fprintf(logFile3, "SDL2 Unlock succeeded\n");
            fflush(logFile3);
            fclose(logFile3);
         }
         
         // DON'T set mBuffer to NULL - keep it for the next frame
         // Setting it to NULL was causing buffer re-allocation issues
         // The buffer remains valid across Lock/Unlock cycles
         FILE *logFile4 = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_Unlock_Lifecycle.log", "a");
         if(logFile4) {
            fprintf(logFile4, "Keeping mBuffer at %p (NOT setting to NULL)\n", mBuffer);
            fflush(logFile4);
            fclose(logFile4);
         }
      }
      catch(...) {
         // SDL2 Unlock crashed!
         FILE *crashLog = fopen("c:\\originalhr\\HoverRace\\Release\\Game2_SDL2_Unlock_CRASHED.log", "a");
         if(crashLog) {
            fprintf(crashLog, "SDL2 Unlock CRASHED! mBuffer was %p\n", mBuffer);
            fflush(crashLog);
            fclose(crashLog);
         }
         // Don't set mBuffer to NULL if the unlock crashed, to preserve state for debugging
      }
      
      PRINT_LOG( "Unlock: END (SDL2Graphics mode)" );
      return;
   }

   // Check if we're in GDI fallback mode (mBackBuffer is NULL but mBuffer is allocated)
   if( mBackBuffer == NULL )
   {
      if(logFile) {
         fprintf(logFile, "  -> Taking GDI fallback path\n");
         fflush(logFile); fclose(logFile);
      }
      // GDI fallback mode - buffer is in system memory
      PRINT_LOG( "Unlock: Detected GDI mode, calling Flip()" );
      Flip();  // Display the buffer
      PRINT_LOG( "Unlock: END (GDI mode)" );
      return;
   }
   
   if(logFile) {
      fprintf(logFile, "  -> Taking DirectDraw path\n");
      fflush(logFile); fclose(logFile);
   }

   // DirectDraw mode - follow original path
   if( mDirectDraw == NULL )
   {
      PRINT_LOG( "Unlock: ERROR - DirectDraw not available in non-GDI mode" );
      return;
   }

   if( !gDebugMode )
   {
      if( DD_CALL(mBackBuffer->Unlock( NULL )) != DD_OK )
      {
         ASSERT( FALSE );
      }
      mBuffer = NULL;
   }
   else
   {
      // Lock the back buffer and copy mBuffer
      if( DD_CALL(mFrontBuffer->IsLost()) == DDERR_SURFACELOST )
      {
         if( DD_CALL(mFrontBuffer->Restore()) != DD_OK )
         {
            // ASSERT( FALSE );
         }
      }
      if( DD_CALL(mBackBuffer->IsLost()) == DDERR_SURFACELOST )
      {
         if( DD_CALL(mBackBuffer->Restore()) != DD_OK )
         {
            // ASSERT( FALSE );
         }
      }

      DDSURFACEDESC   lSurfaceDesc;

      lSurfaceDesc.dwSize = sizeof( lSurfaceDesc );

      if( DD_CALL(mBackBuffer->Lock( NULL, &lSurfaceDesc, DDLOCK_SURFACEMEMORYPTR|DDLOCK_WAIT, NULL )) != DD_OK )
      {
         // ASSERT( FALSE );
      }
      else
      {
         int       lLineLen = lSurfaceDesc.lPitch;
         MR_UInt8* lDest    = (MR_UInt8*)lSurfaceDesc.lpSurface;
         MR_UInt8* lSrc     = mBuffer;

         MR_SAMPLE_START( CopyVideoBuffer, "CopyVideoBuffer" );

         for( int lCounter = 0; lCounter < mYRes; lCounter++ )
         {
            memcpy( lDest, lSrc, mXRes );
            lDest += lLineLen;
            lSrc  += mLineLen;
         }

         MR_SAMPLE_END( CopyVideoBuffer );

      }

      // Unlock
      if( DD_CALL(mBackBuffer->Unlock( NULL )) != DD_OK )
      {
         // ASSERT( FALSE );
      }

      delete mBuffer;
      mBuffer = NULL;
   }

   Flip();
}

void MR_VideoBuffer::Flip()
{
   PRINT_LOG( "Flip: START" );
   
   // GDI SOFTWARE RENDERING (Option 1 Modernization)
   // Uses Windows GDI to display 8-bit paletted buffer to screen
   // This replaces deprecated DirectDraw with modern GDI fallback
   
   try
   {
      // Validate buffer exists before trying to flip
      if( mBuffer == NULL || mXRes == 0 || mYRes == 0 || mWindow == NULL )
      {
         PRINT_LOG( "Flip: Invalid state - buffer/window/resolution not ready" );
         return;
      }
      
      PRINT_LOG( "Flip: Getting device context" );
      
      // Get device context for the window
      HDC hdc = GetDC( mWindow );
      if( hdc == NULL )
      {
         PRINT_LOG( "Flip: GetDC failed" );
         return;  // Window not available, skip frame
      }

      try
      {
         // Create simple 8-bit DIB header for StretchDIBits
         struct {
            BITMAPINFOHEADER bmiHeader;
            RGBQUAD palette[256];
         } bmi;

         ZeroMemory( &bmi, sizeof(bmi) );
         bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
         bmi.bmiHeader.biWidth       = mXRes;
         bmi.bmiHeader.biHeight      = -mYRes;  // Negative = top-down
         bmi.bmiHeader.biPlanes      = 1;
         bmi.bmiHeader.biBitCount    = 8;
         bmi.bmiHeader.biCompression = BI_RGB;

         // Build palette for GDI rendering
         // For GDI, we need to convert from the game's RGB format to RGBQUAD format
         if( mBackPalette != NULL )
         {
            // Convert 8-bit RGB palette (3 bytes per color) to RGBQUAD format (4 bytes per color)
            for( int i = 0; i < 256; i++ )
            {
               if( i * 3 + 2 < 256 * 3 )  // Safety check
               {
                  bmi.palette[i].rgbRed   = mBackPalette[i*3 + 0];      // R
                  bmi.palette[i].rgbGreen = mBackPalette[i*3 + 1];      // G
                  bmi.palette[i].rgbBlue  = mBackPalette[i*3 + 2];      // B
                  bmi.palette[i].rgbReserved = 0;
               }
            }
         }
         else
         {
            // Create a simple grayscale palette as fallback
            for( int i = 0; i < 256; i++ )
            {
               bmi.palette[i].rgbRed   = (BYTE)i;
               bmi.palette[i].rgbGreen = (BYTE)i;
               bmi.palette[i].rgbBlue  = (BYTE)i;
               bmi.palette[i].rgbReserved = 0;
            }
         }

         PRINT_LOG( "Flip: Calling StretchDIBits" );
         
         // Use StretchDIBits to directly display the buffer
         // This is more reliable than CreateDIBitmap for simple 8-bit display
         int result = StretchDIBits(
            hdc,                    // destination DC
            0, 0,                   // destination position
            mXRes, mYRes,           // destination size
            0, 0,                   // source position
            mXRes, mYRes,           // source size
            mBuffer,                // source buffer (8-bit paletted)
            (BITMAPINFO*)&bmi,      // DIB info with palette
            DIB_RGB_COLORS,         // color mode
            SRCCOPY                 // copy operation
         );

         if( result == GDI_ERROR )
         {
            PRINT_LOG( "Flip: StretchDIBits returned GDI_ERROR" );
         }
         else
         {
            PRINT_LOG( "Flip: StretchDIBits succeeded" );
         }
      }
      catch(...)
      {
         PRINT_LOG( "Flip: EXCEPTION in DIB operations" );
      }

      // Clean up device context
      ReleaseDC( mWindow, hdc );
      PRINT_LOG( "Flip: END" );
   }
   catch(...)
   {
      // Silently ignore all GDI errors
      PRINT_LOG( "Flip: EXCEPTION in Flip()" );
   }
}

void MR_VideoBuffer::Clear( MR_UInt8 pColor )
{
   ASSERT( mBuffer != NULL );
   ASSERT( mDirectDraw != NULL );
   ASSERT( mBackBuffer != NULL );

   memset( mBuffer, pColor, mLineLen*mYRes );
}

void MR_VideoBuffer::EnterIconMode()
{
   PRINT_LOG( "EnterIconMode" );

   if( !mIconMode )
   {
      mIconMode = TRUE;

      /*
      if( !mFullScreen )
      {
         mBeforeIconXRes = 0;
         mBeforeIconYRes = 0;
      }
      else
      {
         // mBeforeIconXRes = 0;
         // mBeforeIconYRes = 0;
         
         mBeforeIconXRes = mXRes;
         mBeforeIconYRes = mYRes;         
      }
      */
   }
}

void MR_VideoBuffer::ExitIconMode()
{
   PRINT_LOG( "ExitIconMode" );

   if( mIconMode )
   {
      mIconMode = FALSE;

      /*
      if( mBeforeIconXRes != 0 )
      {
         mFullScreen = TRUE;
         SetVideoMode();
        
         
         // SetFocus( mWindow );
         // SetVideoMode( mBeforeIconXRes, mBeforeIconYRes );         
      } 
      */
           
   }
}



