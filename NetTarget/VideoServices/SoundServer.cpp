// SoundServer.cpp
//
// OpenAL-based sound implementation for HoverRace
// Replaces legacy DirectSound with modern cross-platform audio
//
// Copyright (c) 1995-1998 - Richard Langlois and Grokksoft Inc.
// Modernized with OpenAL support (2025)
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
#include "SoundServer.h"
#include "../Util/MR_Types.h"
#include <mmreg.h>

// OpenAL headers
#include "AL/al.h"
#include "AL/alc.h"

#define MR_MAX_SOUND_COPY 6
#define MR_MAX_BUFFERS 256
#define MR_MAX_SOURCES 128

// Global OpenAL state
static ALCdevice* gOpenALDevice = NULL;
static ALCcontext* gOpenALContext = NULL;
static ALuint gOpenALSources[MR_MAX_SOURCES];
static int gOpenALSourceCount = 0;

class MR_SoundBuffer
{
   private:
      static MR_SoundBuffer* mList;
      MR_SoundBuffer*        mNext;

   protected:

      int     mNbCopy;
      ALuint  mALBuffers[MR_MAX_SOUND_COPY];
      ALuint  mALSources[MR_MAX_SOUND_COPY];
      int     mNormalFreq;

   public:
      MR_SoundBuffer();
      virtual ~MR_SoundBuffer();

      BOOL Init( const char* pData, int pNbCopy );

      void SetParams( int pCopy, int pDB, double pSpeed, int pPan );

      int GetNbCopy()const;

      static void DeleteAll();

      virtual void ApplyCumCommand();
      
      static void ApplyCumCommandForAll();

};


class MR_ShortSound:public MR_SoundBuffer
{

   protected:
      int mCurrentCopy;


   public:
      MR_ShortSound();
      ~MR_ShortSound();

      void Play( int pDB, double pSpeed, int pPan );
};


class MR_ContinuousSound:public MR_SoundBuffer
{
   protected:
      BOOL   mOn[MR_MAX_SOUND_COPY];
      int    mMaxDB[MR_MAX_SOUND_COPY];
      double mMaxSpeed[MR_MAX_SOUND_COPY];


      void ResetCumStat();

   public:
      MR_ContinuousSound();
      ~MR_ContinuousSound();

      void Pause(   int pCopy );
      void Restart( int pCopy );

      void ApplyCumCommand();
      void CumPlay( int pCopy, int pDB, double pSpeed );


};


// Variables
MR_SoundBuffer* MR_SoundBuffer::mList = NULL;


// Implementation
MR_SoundBuffer::MR_SoundBuffer()
{
   mNbCopy = 0;
   mNormalFreq = 44100;

   for( int lCounter = 0; lCounter < MR_MAX_SOUND_COPY; lCounter++ )
   {
      mALBuffers[lCounter] = 0;
      mALSources[lCounter] = 0;
   }

   // Add the new buffer to the list
   mNext = mList;
   mList = this;
}

MR_SoundBuffer::~MR_SoundBuffer()
{
   try
   {
      // Remove form list
      if( mList == this )
      {
         mList = mNext;
         mNext = NULL;
      }
      else
      {
         MR_SoundBuffer* mPrev = mList;

         while( mPrev->mNext != this )
         {
            mPrev = mPrev->mNext;
         }

         mPrev->mNext = mNext;
         mNext = NULL;
      }

      // Delete the sound buffers
      for( int lCounter = 0; lCounter < mNbCopy; lCounter++ )
      {
         if( mALSources[lCounter] != 0 )
         {
            alDeleteSources(1, &mALSources[lCounter]);
         }
         if( mALBuffers[lCounter] != 0 )
         {
            alDeleteBuffers(1, &mALBuffers[lCounter]);
         }
      }
   }
   catch(...)
   {
      // Ignore cleanup errors
   }
}

void MR_SoundBuffer::ApplyCumCommand()
{
   // Do notting by default
}

void MR_SoundBuffer::ApplyCumCommandForAll()
{
   MR_SoundBuffer* mCurrent = mList;

   while( mCurrent != NULL )
   {
      mCurrent->ApplyCumCommand();
      mCurrent = mCurrent->mNext;
   }
}

void MR_SoundBuffer::DeleteAll()
{
   while( mList != NULL )
   {
      delete mList;
   }
}

BOOL MR_SoundBuffer::Init( const char* pData, int pNbCopy )
{
   BOOL lReturnValue = TRUE;

   try
   {
      if( pNbCopy > MR_MAX_SOUND_COPY )
      {
         pNbCopy = MR_MAX_SOUND_COPY;
      }

      mNbCopy = pNbCopy;

      if( gOpenALContext == NULL )
      {
         return FALSE;
      }

      // Parse pData
      MR_UInt32       lBufferLen  = *(MR_UInt32*)pData;
      WAVEFORMATEX*   lWaveFormat = (WAVEFORMATEX*)(pData+sizeof(MR_UInt32));
      const char*     lSoundData  = pData+sizeof( MR_UInt32 ) + sizeof( WAVEFORMATEX );

      mNormalFreq = lWaveFormat->nSamplesPerSec;

      // Determine OpenAL format from WAV parameters
      ALenum format = AL_NONE;
      if( lWaveFormat->nChannels == 1 && lWaveFormat->wBitsPerSample == 16 )
         format = AL_FORMAT_MONO16;
      else if( lWaveFormat->nChannels == 2 && lWaveFormat->wBitsPerSample == 16 )
         format = AL_FORMAT_STEREO16;
      else if( lWaveFormat->nChannels == 1 && lWaveFormat->wBitsPerSample == 8 )
         format = AL_FORMAT_MONO8;
      else if( lWaveFormat->nChannels == 2 && lWaveFormat->wBitsPerSample == 8 )
         format = AL_FORMAT_STEREO8;
      else
         return FALSE;

      // Create first buffer
      alGenBuffers(1, &mALBuffers[0]);
      if( alGetError() != AL_NO_ERROR )
         return FALSE;

      alBufferData(mALBuffers[0], format, (void*)lSoundData, lBufferLen, mNormalFreq);
      if( alGetError() != AL_NO_ERROR )
         return FALSE;

      // Create sources for each copy
      for( int i = 0; i < mNbCopy; i++ )
      {
         alGenSources(1, &mALSources[i]);
         if( alGetError() != AL_NO_ERROR )
            return FALSE;

         alSourcei(mALSources[i], AL_BUFFER, mALBuffers[0]);
         alSourcef(mALSources[i], AL_PITCH, 1.0f);
         alSourcef(mALSources[i], AL_GAIN, 1.0f);
         alSource3f(mALSources[i], AL_POSITION, 0, 0, 0);
      }

      return TRUE;
   }
   catch(...)
   {
      // If sound buffer initialization crashes, continue without sound
      return FALSE;
   }
}



void MR_SoundBuffer::SetParams( int pCopy, int pDB, double pSpeed, int pPan )
{
   if( pCopy >= mNbCopy )
   {
      pCopy = mNbCopy-1;
   }

   if( pCopy >= 0 && pCopy < mNbCopy && mALSources[pCopy] != 0 )
   {
      try
      {
         // Convert dB to linear gain (0 dB = 1.0)
         float lGain = powf(10.0f, pDB / 20.0f);
         if( lGain < 0.0f ) lGain = 0.0f;
         if( lGain > 1.0f ) lGain = 1.0f;

         alSourcef(mALSources[pCopy], AL_GAIN, lGain);

         // Set pitch (speed)
         float lPitch = (float)pSpeed;
         if( lPitch < 0.5f ) lPitch = 0.5f;
         if( lPitch > 2.0f ) lPitch = 2.0f;
         alSourcef(mALSources[pCopy], AL_PITCH, lPitch);

         // Set pan (position on X axis for stereo effect)
         float lPanf = pPan / 100.0f;
         if( lPanf < -1.0f ) lPanf = -1.0f;
         if( lPanf > 1.0f ) lPanf = 1.0f;
         alSource3f(mALSources[pCopy], AL_POSITION, lPanf, 0, 0);
      }
      catch(...)
      {
         // Silently ignore OpenAL parameter errors
      }
   }
}

int MR_SoundBuffer::GetNbCopy()const
{
   return mNbCopy;
}


// MR_ShortSound
MR_ShortSound::MR_ShortSound()
{
   mCurrentCopy = 0;
}

MR_ShortSound::~MR_ShortSound()
{
}

void MR_ShortSound::Play( int pDB, double pSpeed, int pPan )
{
   try
   {
      if( mCurrentCopy < 0 || mCurrentCopy >= mNbCopy || mALSources[mCurrentCopy] == 0 )
         return;

      // Stop any currently playing sound
      alSourceStop(mALSources[mCurrentCopy]);

      // Rewind to beginning
      alSourceRewind(mALSources[mCurrentCopy]);

      // Set parameters
      SetParams( mCurrentCopy, pDB, pSpeed, pPan );

      // Play the sound
      alSourcePlay(mALSources[mCurrentCopy]);

      // Move to next copy for next play call
      mCurrentCopy++;
      if( mCurrentCopy >= mNbCopy )
         mCurrentCopy = 0;
   }
   catch(...)
   {
      // Silently ignore play errors
   }
}


// class MR_ContinuousSound
MR_ContinuousSound::MR_ContinuousSound()
{
   ResetCumStat();
}
 
MR_ContinuousSound::~MR_ContinuousSound()
{
}

void MR_ContinuousSound::ResetCumStat()
{
   for( int lCounter = 0; lCounter < mNbCopy; lCounter++ )
   {
      mOn[ lCounter ] = FALSE;
      mMaxSpeed[ lCounter ] = 0;
      mMaxDB[ lCounter ] = -10000;
   }
}


void MR_ContinuousSound::Pause( int pCopy )
{
   try
   {
      if( pCopy >= mNbCopy )
         pCopy = mNbCopy-1;

      if( pCopy >= 0 && pCopy < mNbCopy && mALSources[pCopy] != 0 )
      {
         alSourcePause(mALSources[pCopy]);
      }
   }
   catch(...)
   {
      // Ignore pause errors
   }
}

void MR_ContinuousSound::Restart( int pCopy )
{
   try
   {
      if( pCopy >= mNbCopy )
         pCopy = mNbCopy-1;

      if( pCopy >= 0 && pCopy < mNbCopy && mALSources[pCopy] != 0 )
      {
         // Check if already playing
         ALint state;
         alGetSourcei(mALSources[pCopy], AL_SOURCE_STATE, &state);

         if( state != AL_PLAYING )
         {
            alSourcePlay(mALSources[pCopy]);
         }
      }
   }
   catch(...)
   {
      // Ignore restart errors
   }
}

void MR_ContinuousSound::ApplyCumCommand( )
{
   try
   {
      for( int lCounter = 0; lCounter < mNbCopy; lCounter++ )
      {
         try
         {
            if( mOn[ lCounter ] )
            {
               SetParams( lCounter, mMaxDB[ lCounter ], mMaxSpeed[ lCounter ], 0 );
               Restart( lCounter );
            }
            else
            {
               Pause( lCounter );
            }
         }
         catch(...)
         {
            // Silently ignore DirectSound errors for individual copies
         }
      }
   }
   catch(...)
   {
      // Silently ignore any DirectSound errors
   }
   ResetCumStat();

}

void MR_ContinuousSound::CumPlay( int pCopy, int pDB, double pSpeed )
{
   if( pCopy >= mNbCopy )
   {
      pCopy = mNbCopy-1;
   }

   mOn[ pCopy ]       = TRUE;
   mMaxDB[ pCopy ]    = max( mMaxDB   [ pCopy ], pDB );
   mMaxSpeed[ pCopy ] = max( mMaxSpeed[ pCopy ], pSpeed );

}




// namespace MR_SoundServer

BOOL MR_SoundServer::Init( HWND pWindow )
{
   try
   {
      // Try common audio device names that might work
      const char* commonDevices[] = {
         "Generic Software",
         "ALSA Software Mixer",
         "PulseAudio Software",
         "Multimedia audio controller",
         NULL  // End marker
      };
      
      gOpenALDevice = NULL;
      
      // First try the common device names
      for(int i = 0; commonDevices[i] != NULL; i++) {
         gOpenALDevice = alcOpenDevice(commonDevices[i]);
         if(gOpenALDevice != NULL) {
            break;
         }
      }

      // If that didn't work, try default
      if(gOpenALDevice == NULL) {
         gOpenALDevice = alcOpenDevice(NULL);
      }

      // If we still don't have a device, try with empty string
      if(gOpenALDevice == NULL) {
         gOpenALDevice = alcOpenDevice("");
      }

      if( gOpenALDevice == NULL )
         return FALSE;

      // Create audio context
      gOpenALContext = alcCreateContext(gOpenALDevice, NULL);
      if( gOpenALContext == NULL )
      {
         alcCloseDevice(gOpenALDevice);
         gOpenALDevice = NULL;
         return FALSE;
      }

      // Make context current
      if( !alcMakeContextCurrent(gOpenALContext) )
      {
         alcDestroyContext(gOpenALContext);
         alcCloseDevice(gOpenALDevice);
         gOpenALContext = NULL;
         gOpenALDevice = NULL;
         return FALSE;
      }

      // Set listener parameters
      alListener3f(AL_POSITION, 0, 0, 0);
      alListener3f(AL_VELOCITY, 0, 0, 0);
      alListenerf(AL_GAIN, 1.0f);  // Ensure listener gain is at max
      ALfloat lOrient[] = {0, 0, -1, 0, 1, 0};
      alListenerfv(AL_ORIENTATION, lOrient);

      return TRUE;
   }
   catch(...)
   {
      return FALSE;
   }
}

void MR_SoundServer::Close()
{
   try
   {
      MR_SoundBuffer::DeleteAll();

      if( gOpenALContext != NULL )
      {
         alcMakeContextCurrent(NULL);
         alcDestroyContext(gOpenALContext);
         gOpenALContext = NULL;
      }

      if( gOpenALDevice != NULL )
      {
         alcCloseDevice(gOpenALDevice);
         gOpenALDevice = NULL;
      }
   }
   catch(...)
   {
      // Ignore cleanup errors
   }
}


MR_ShortSound* MR_SoundServer::CreateShortSound( const char* pData, int pNbCopy )
{
   try
   {
      if( gOpenALContext == NULL )
         return NULL;

      MR_ShortSound* lSound = new MR_ShortSound;
      if( !lSound->Init( pData, pNbCopy ) )
      {
         delete lSound;
         return NULL;
      }
      return lSound;
   }
   catch(...)
   {
      return NULL;
   }
}

void MR_SoundServer::DeleteShortSound( MR_ShortSound* pSound )
{
   delete pSound;
}

void MR_SoundServer::Play( MR_ShortSound* pSound, int pDB, double pSpeed, int pPan )
{
   if( pSound != NULL )
   {
      pSound->Play( pDB, pSpeed, pPan );
   }
}

int MR_SoundServer::GetNbCopy( MR_ShortSound* pSound )
{
   if( pSound != NULL )
   {
      return pSound->GetNbCopy();
   }
   else
   {
      return 1;
   }
}

MR_ContinuousSound* MR_SoundServer::CreateContinuousSound( const char* pData, int pNbCopy )
{
   try
   {
      if( gOpenALContext == NULL )
         return NULL;

      MR_ContinuousSound* lSound = new MR_ContinuousSound;
      if( !lSound->Init( pData, pNbCopy ) )
      {
         delete lSound;
         return NULL;
      }
      return lSound;
   }
   catch(...)
   {
      return NULL;
   }
}

void MR_SoundServer::DeleteContinuousSound( MR_ContinuousSound* pSound )
{
   delete pSound;
}

void MR_SoundServer::Play( MR_ContinuousSound* pSound, int pCopy, int pDB, double pSpeed, int /*pPan*/ )
{
   if( pSound != NULL )
   {
      pSound->CumPlay( pCopy, pDB, pSpeed );
   }
}


void MR_SoundServer::ApplyContinuousPlay()
{
   try
   {
      if( gOpenALContext != NULL )
      {
         MR_SoundBuffer::ApplyCumCommandForAll();
      }
   }
   catch(...)
   {
   }
}

/*
void MR_SoundServer::Pause(   MR_ContinuousSound* pSound, int pCopy )
{
   if( pSound != NULL )
   {
      pSound->Pause( pCopy );
   }
}

void MR_SoundServer::Restart( MR_ContinuousSound* pSound, int pCopy )
{
   if( pSound != NULL )
   {
      pSound->Restart( pCopy );
   }
}

void MR_SoundServer::SetParams( MR_ContinuousSound* pSound, int pCopy, int pDB, double pSpeed, int pPan )
{
   if( pSound != NULL )
   {
      pSound->SetParams( pCopy, pDB, pSpeed, pPan );
   }
}
*/

int MR_SoundServer::GetNbCopy( MR_ContinuousSound* pSound )
{
   if( pSound != NULL )
   {
      return pSound->GetNbCopy();
   }
   else
   {
      return 1;
   }
}
