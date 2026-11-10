// SoundServer.h // DirectSound encapsulation module
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


#ifndef SOUND_SERVER_H
#define SOUND_SERVER_H

#ifndef _WIN32
#include "../Platform/MfcCompat.h"
#endif


#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_VIDEO_SERVICES
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

class MR_ShortSound;
class MR_ContinuousSound;


namespace MR_SoundServer
{   

   MR_DllDeclare BOOL Init( HWND pWindow );
   MR_DllDeclare void Close();


   MR_DllDeclare MR_ShortSound*      CreateShortSound( const char* pData, int pNbCopy );
   MR_DllDeclare void                DeleteShortSound( MR_ShortSound* pSound );

   MR_DllDeclare void                Play( MR_ShortSound* pSound, int pDB = 0, double pSpeed = 1.0, int pPan = 0 );

   MR_DllDeclare int                 GetNbCopy( MR_ShortSound* pSound );

   // Continous play
   MR_DllDeclare MR_ContinuousSound* CreateContinuousSound( const char* pData, int pNbCopy );
   MR_DllDeclare void                DeleteContinuousSound( MR_ContinuousSound* pSound );

   MR_DllDeclare void                Play( MR_ContinuousSound* pSound, int pCopy, int pDB = 0, double pSpeed = 1.0, int pPan = 0 );

   /* Obsoletes
   MR_DllDeclare void                Pause(   MR_ContinuousSound* pSound, int pCopy );
   MR_DllDeclare void                Restart( MR_ContinuousSound* pSound, int pCopy );

   MR_DllDeclare void                SetParams( MR_ContinuousSound* pSound, int pCopy, int pDB, double pSpeed = 1.0, int pPan = 0 );
   */
   MR_DllDeclare int                 GetNbCopy( MR_ContinuousSound* pSound );

   MR_DllDeclare void                ApplyContinuousPlay();

   // Doppler: the pitch of the continuous sounds asked for between this call and the next one is multiplied by pFactor (1.0 = none). The observer sets it for each other craft and missile
   // from how fast it is closing on the listener, and puts it back to 1.0 afterwards. (Implemented by the portable server, HeadlessSoundServer.cpp.)
   MR_DllDeclare void                SetDopplerFactor( double pFactor );

   // Global output level applied on top of every sound's own volume/pan, for a
   // user-facing master volume control (see docs/roadmap-2.0.md's Phase 3
   // "audio and volume settings" item). pVolume is clamped to [0.0, 1.0];
   // defaults to 1.0 (unchanged from before this existed) until a caller sets
   // it, so existing callers that never touch this see no behavior change.
   // Music: an Ogg Vorbis stream (44.1 kHz stereo), decoded a little at a time and mixed under the sounds, looping until it is stopped or replaced. A track that is
   // already playing is faded out as the new one fades in. pVolume is 0..1 (relative: the master volume still applies). The data is copied.
   MR_DllDeclare void                MusicPlay( const unsigned char* pOgg, unsigned long pSize, double pVolume, double pFadeSeconds );
   MR_DllDeclare void                MusicStop( double pFadeSeconds );
   MR_DllDeclare void                MusicSetVolume( double pVolume );
   MR_DllDeclare bool                MusicPlaying();

   MR_DllDeclare void                SetMasterVolume( double pVolume );
   MR_DllDeclare double              GetMasterVolume();

};



#undef MR_DllDeclare

#endif



