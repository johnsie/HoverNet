// main.cpp		 Fireball game entry point
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

#include "GameApp.h"


// Entry point
int WINAPI WinMain( HINSTANCE pInstance, HINSTANCE pPrevInstance, LPSTR /*pCmdLine*/,int pCmdShow )
{
    char debugPath[MAX_PATH] = {0};
    const DWORD debugPathLen = GetTempPathA(sizeof(debugPath), debugPath);
    FILE* debugLog = NULL;
    if( debugPathLen > 0 && debugPathLen < sizeof(debugPath) )
    {
        strcat_s(debugPath, sizeof(debugPath), "HoverNet-Game2.log");
        debugLog = fopen(debugPath, "a");
    }
    if( debugLog == NULL )
    {
        debugLog = fopen("NUL", "w");
    }
    fprintf(debugLog, "\n=== WinMain START ===\n");
    fflush(debugLog);
    
    BOOL lReturnValue = TRUE;
    int  lErrorCode   = -1;

    fprintf(debugLog, "Creating MR_GameApp...\n");
    fflush(debugLog);
    
    try {
        MR_GameApp lGame( pInstance );
        fprintf(debugLog, "MR_GameApp created successfully\n");
        fflush(debugLog);

        // Allow only one instance of HoverRace; press CAPS_LOCK to bypass
        GetAsyncKeyState( VK_CAPITAL ); // Reset the function
        if( !GetAsyncKeyState( VK_CAPITAL ) )
        {
           fprintf(debugLog, "Checking first instance...\n");
           fflush(debugLog);
           lReturnValue = lGame.IsFirstInstance();
           fprintf(debugLog, "IsFirstInstance returned: %d\n", lReturnValue);
           fflush(debugLog);
        }

        if( lReturnValue )
        {
           fprintf(debugLog, "Skipping DisplayLoginWindow entirely, going straight to game init\n");
           fflush(debugLog);
           lReturnValue = TRUE;
        }    

        GetAsyncKeyState( VK_CAPITAL ); // Reset the function
        if( lReturnValue && !GetAsyncKeyState( VK_CAPITAL ) )
        {
           fprintf(debugLog, "Checking first instance (2nd time)...\n");
           fflush(debugLog);
           lReturnValue = lGame.IsFirstInstance();
           fprintf(debugLog, "IsFirstInstance (2nd) returned: %d\n", lReturnValue);
           fflush(debugLog);
        }

        if( lReturnValue &&(pPrevInstance ==  NULL) )
        {
           fprintf(debugLog, "Calling InitApplication...\n");
           fflush(debugLog);
           lReturnValue = lGame.InitApplication();
           fprintf(debugLog, "InitApplication returned: %d\n", lReturnValue);
           fflush(debugLog);
        }

        if( lReturnValue )
        {
           fprintf(debugLog, "Calling InitGame...\n");
           fflush(debugLog);
           try {
               lReturnValue = lGame.InitGame();
               fprintf(debugLog, "InitGame returned: %d\n", lReturnValue);
               fflush(debugLog);
           }
           catch (const std::exception& e) {
               fprintf(debugLog, "EXCEPTION in InitGame: %s\n", e.what());
               fflush(debugLog);
               lReturnValue = FALSE;
           }
           catch (...) {
               fprintf(debugLog, "UNKNOWN EXCEPTION in InitGame\n");
               fflush(debugLog);
               lReturnValue = FALSE;
           }
        }

        if( lReturnValue )
        {
           fprintf(debugLog, "Calling MainLoop...\n");
           fflush(debugLog);
           lErrorCode = lGame.MainLoop();
           fprintf(debugLog, "MainLoop returned: %d\n", lErrorCode);
           fflush(debugLog);
        }
    }
    catch (const std::exception& e) {
        fprintf(debugLog, "EXCEPTION: %s\n", e.what());
        fflush(debugLog);
        lErrorCode = -1;
    }
    catch (...) {
        fprintf(debugLog, "UNKNOWN EXCEPTION\n");
        fflush(debugLog);
        lErrorCode = -1;
    }
   
    fprintf(debugLog, "=== WinMain END, returning %d ===\n", lErrorCode);
    fflush(debugLog);
    fclose(debugLog);
    
    return lErrorCode;
}


