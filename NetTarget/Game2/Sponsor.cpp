// Sponsor.cpp
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

#include "Sponsor.h"
#include "Banner.h"
#include "resource.h"

static HBITMAP     gBitmap  = NULL;
static HPALETTE    gPalette = NULL;
static const char* gURL     = NULL;

static BOOL CALLBACK WinFunc( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam );

void DisplaySponsorWindow( HWND pWindow )
{
   // Verify if the sponsor file exist
   FILE* lFile = fopen( "source.dat", "rb" );
   
   fprintf(logFile, "DisplaySponsorWindow: source.dat fopen returned: %p\n", lFile);
   fflush(logFile);

   if( lFile != NULL )
   {
      fprintf(logFile, "DisplaySponsorWindow: source.dat found, reading...\n");
      fflush(logFile);
      
      unsigned char lBuffer[50*1024];
      int lSize;
      // Read the file
      
      lSize = fread( lBuffer, 1, sizeof( lBuffer ), lFile );
      fprintf(logFile, "DisplaySponsorWindow: fread returned %d bytes\n", lSize);
      fflush(logFile);

      fclose( lFile );

      if( lSize > 280 )
      {
         fprintf(logFile, "DisplaySponsorWindow: File size OK, decrypting...\n");
         fflush(logFile);
         
         // Decript file
         for( int lCounter = 0; lCounter < 280; lCounter++ )
         {
            lBuffer[ lCounter ] ^= 13;
         }

         // Read URL
         int lIndex;

         gURL = (char*)lBuffer;

         for( lIndex = 0; lIndex < 255; lIndex++ )
         {
            if( (lBuffer[lIndex] == 13)&&( lBuffer[lIndex]==10) )
            {
               lBuffer[lIndex] = 0;
            }
            else if( lBuffer[lIndex] == 'G' )
            {
               if( !strncmp( (const char*)lBuffer+lIndex, "GIF8", 4 ) )
               {
                  break;
               }
            }
         }

         fprintf(logFile, "DisplaySponsorWindow: GIF header found at index %d, decoding...\n", lIndex);
         fflush(logFile);
         
         // now decode that zip zile

         try {
             MR_GifDecoder lImage;
             fprintf(logFile, "DisplaySponsorWindow: MR_GifDecoder created\n");
             fflush(logFile);

             if( lImage.Decode( lBuffer+lIndex, lSize-lIndex ) )
             {
                fprintf(logFile, "DisplaySponsorWindow: GIF decoded successfully\n");
                fflush(logFile);
                
                gBitmap  = lImage.GetImage(0);
                gPalette = lImage.GetGlobalPalette();

                fprintf(logFile, "DisplaySponsorWindow: About to show dialog\n");
                fflush(logFile);
                
                // Open the Window
                DialogBox( GetModuleHandle( NULL ), MAKEINTRESOURCE( IDD_SPONSOR ), pWindow, WinFunc );
                
                fprintf(logFile, "DisplaySponsorWindow: Dialog closed\n");
                fflush(logFile);
             }
             else {
                fprintf(logFile, "DisplaySponsorWindow: GIF decode failed\n");
                fflush(logFile);
             }
         }
         catch (const std::exception& e) {
             fprintf(logFile, "DisplaySponsorWindow: Exception during GIF decode: %s\n", e.what());
             fflush(logFile);
         }
         catch (...) {
             fprintf(logFile, "DisplaySponsorWindow: Unknown exception during GIF decode\n");
             fflush(logFile);
         }
      }
      else {
         fprintf(logFile, "DisplaySponsorWindow: File size too small (%d < 280)\n", lSize);
         fflush(logFile);
      }
   }
   else {
      fprintf(logFile, "DisplaySponsorWindow: source.dat not found (expected)\n");
      fflush(logFile);
   }
   
   fprintf(logFile, "DisplaySponsorWindow: Completed\n");
   fflush(logFile);
   fclose(logFile);
}


BOOL CALLBACK WinFunc( HWND pWindow, UINT  pMsgId, WPARAM  pWParam, LPARAM  pLParam )
{
   BOOL lReturnValue = FALSE;

   switch( pMsgId )
   {
      // Catch environment modification events

      case WM_INITDIALOG:
         {
 
            HWND lBitmapCtl = GetDlgItem( pWindow, IDC_BITMAP );

            if( lBitmapCtl != NULL )
            {
               HDC      hdc = GetDC( lBitmapCtl );
               

               SelectPalette( hdc, gPalette, FALSE );

               int lNbColors = RealizePalette( hdc );

               TRACE( "Colors0 %d  %d\n", lNbColors, GetLastError() ); 

               ReleaseDC( lBitmapCtl, hdc );

               // SelectPalette(hMemDC,hPalette,FALSE);
               // RealizePalette(hMemDC);
               
               SendMessage( lBitmapCtl, BM_SETIMAGE, IMAGE_BITMAP, (long)gBitmap );
            }
            lReturnValue = TRUE;
         }
         break;

      case WM_QUERYNEWPALETTE:
         if( gPalette != NULL )
         {
            HWND lBitmapCtl = GetDlgItem( pWindow, IDC_BITMAP );

            if( lBitmapCtl != NULL )
            {
               HDC hdc = GetDC( lBitmapCtl );

               HPALETTE lOldPalette = SelectPalette( hdc, gPalette, FALSE );
               RealizePalette( hdc );

               InvalidateRgn( pWindow, NULL, TRUE );
               InvalidateRgn( lBitmapCtl, NULL, TRUE );
               UpdateWindow( pWindow );
               UpdateWindow( lBitmapCtl );


               ReleaseDC( lBitmapCtl, hdc );


               TRACE( "PAL_SET\n" );

            }
            lReturnValue = TRUE;
         }
         break;
         
      case WM_PALETTECHANGED:
         if( (gPalette != NULL)&&( (HWND)pWParam != pWindow ) )
         {
            HWND lBitmapCtl = GetDlgItem( pWindow, IDC_BITMAP );

            if( (lBitmapCtl != NULL)&&( (HWND)pWParam != lBitmapCtl ) )
            {
               HDC hdc = GetDC( lBitmapCtl );

               HPALETTE lOldPalette = SelectPalette( hdc, gPalette, FALSE );
               RealizePalette( hdc );

               UpdateColors( hdc );

               ReleaseDC( lBitmapCtl, hdc );


               TRACE( "PAL_CHANGE\n" );
            }
            lReturnValue = TRUE;
         }
         break;
      
      case WM_COMMAND:
         switch(LOWORD( pWParam))
         {  
            case IDC_BITMAP:
               LoadURL( pWindow, gURL );
               lReturnValue = TRUE;
               break;

            case IDCANCEL:
               EndDialog( pWindow, IDCANCEL );
               lReturnValue = TRUE;
               break;

            case IDOK:
               EndDialog( pWindow, IDOK );
               lReturnValue = TRUE;
               break;
         }
         break;
   }
   return lReturnValue;
}
