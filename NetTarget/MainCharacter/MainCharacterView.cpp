// MainCharacterView.cpp
//
// The presentation side of the hovercraft: its renderer, its sounds and the drawing call. The simulation
// (MainCharacter.cpp) only says *what happened* (QueueSound) and never touches video or audio types, so it can
// be linked into the server, the replay tools and the tests without them (docs/adr/0001-simulation-core.md).
//
// Licensed under GrokkSoft HoverRace SourceCode License v1.0(the "License");
// you may not use this file except in compliance with the License.

#include "StdAfx.h"

#include <math.h>

#include "MainCharacter.h"
#include "MainCharacterRenderer.h"
#include "../ObjFacTools/ResActor.h"

#define MR_NB_HOVER_MODEL 8

// Resource IDs for actor models (from ObjFac1Res.h)
#define MR_ELECTRO_CAR  10
#define MR_HITECH_CAR   11
#define MR_BITURBO_CAR  12
#define MR_EON_CRAFT  19
#define MR_MANTA_CRAFT 24
#define MR_SELECTABLE_HOVER_MODEL_COUNT 5

void MR_MainCharacter::ReleaseRenderer()
{
   delete mRenderer;
   mRenderer = NULL;
}

// Records a sound for the presentation layer. Nothing is queued without a renderer (headless runs).
void MR_MainCharacter::QueueSound( eSoundEvent pSound, BOOL pInternal, BOOL pExternal )
{
   if( mRenderer == NULL ) return;

   MR_ShortSound* lSound = NULL;
   switch( pSound )
   {
      case eSoundJump:         lSound = mRenderer->GetJumpSound();         break;
      case eSoundMisJump:      lSound = mRenderer->GetMisJumpSound();      break;
      case eSoundFire:         lSound = mRenderer->GetFireSound();         break;
      case eSoundBump:         lSound = mRenderer->GetBumpSound();         break;
      case eSoundOutOfControl: lSound = mRenderer->GetOutOfCtrlSound();    break;
      case eSoundFinish:       lSound = mRenderer->GetFinishSound();       break;
      case eSoundLineCrossing: lSound = mRenderer->GetLineCrossingSound(); break;
   }

   if( pInternal ) mInternalSoundList.Add( lSound );
   if( pExternal ) mExternalSoundList.Add( lSound );
}

void MR_MainCharacter::AddRenderer()
{
   if( mRenderer == NULL )
   {
      // Create a simple renderer from the MainCharacter DLL
      MR_ObjectFromFactoryId lId = { MR_MAIN_CHARACTER_DLL_ID, 100 };
      
#ifdef _WIN32
      __try
      {
         mRenderer = (MR_MainCharacterRenderer*) MR_DllObjectFactory::CreateObject( lId );
      }
      __except( EXCEPTION_EXECUTE_HANDLER )
      {
         // Renderer creation failed - will use fallback
         mRenderer = NULL;
      }
#else
      try
      {
         mRenderer = (MR_MainCharacterRenderer*) MR_DllObjectFactory::CreateObject( lId );
      }
      catch(...)
      {
         mRenderer = NULL;
      }
#endif
   }
}

void MR_MainCharacter::Render( MR_3DViewPort* pDest, MR_SimulationTime /*pTime*/ )
{
   if( pDest == NULL ) return;
   if( mRenderer == NULL ) return;  // No renderer available - can't render
   
   // DEFENSIVE: Use mPosition as-is for rendering
   // Even if corrupted, the 3D view will handle it with frustum culling
   // We don't try to "fix" the position here - just render it
   
   // DEFENSIVE: Validate mHoverModel is in range (0-3)
   // If corrupted, use default model 0
   int lSafeModel = mHoverModel;
   if( lSafeModel < 0 || lSafeModel >= MR_SELECTABLE_HOVER_MODEL_COUNT )
   {
      lSafeModel = 0;  // Use default model instead of garbage
   }
   
   // DEFENSIVE: Validate mHoverId is reasonable (0-9 range for multiplayer)
   // If out of range, use default ID 0
   int lSafeHoverId = mHoverId;
   if( lSafeHoverId < 0 || lSafeHoverId > 15 )
   {
      lSafeHoverId = 0;  // Use default hover ID instead of garbage
   }
   
   // Map model index (0-3) to actor resource IDs (10, 11, 12, 19)
   int lActorId;
   switch( lSafeModel )
   {
      case 0: lActorId = MR_ELECTRO_CAR; break;   // Actor 10
      case 1: lActorId = MR_HITECH_CAR; break;    // Actor 11
      case 2: lActorId = MR_BITURBO_CAR; break;   // Actor 12
      case 3: lActorId = MR_EON_CRAFT; break;     // Actor 19
      case 4: lActorId = MR_MANTA_CRAFT; break;   // Actor 24
      default: lActorId = MR_ELECTRO_CAR; break;  // Default to Electro Car
   }
   
   // Call the renderer - should be MR_HoverRender from ObjFac1
   // Use MFC TRY/CATCH to catch BOTH SEH and C++ exceptions
   TRY
   {
      mRenderer->Render( pDest, mPosition, mCabinOrientation, mMotorDisplay > 0, lSafeHoverId, lActorId );
   }
   CATCH_ALL(e)
   {
      // If renderer crashes (C++ exception), silently clean up
      e->Delete();
   }
   END_CATCH_ALL
}

void MR_MainCharacter::PlayInternalSounds()
{
   if( mRenderer != NULL )
   {
      // Sound events
      while( !mInternalSoundList.IsEmpty() )
      {
         MR_SoundServer::Play( mInternalSoundList.GetHead() );
         mInternalSoundList.Remove();
      }

      // Continuous sounds
      MR_ContinuousSound* lWindSound  = mRenderer->GetFrictionSound();
      MR_ContinuousSound* lMotorSound = mRenderer->GetMotorSound();
      double lAbsSpeed = GetNormalisedSpeed();

      if( lAbsSpeed > 0.02 )
      {
         MR_SoundServer::Play( lWindSound, 0, 0, 1.5*lAbsSpeed );
      }

      if( mControlState&eMotorOn )
      {
         MR_SoundServer::Play( lMotorSound, 0, 0 );
      }
   }
}

void MR_MainCharacter::PlayExternalSounds( int pDB, int pPan )
{
   if( mRenderer != NULL )
   {
      // Sound events
      while( !mExternalSoundList.IsEmpty() )
      {
         MR_SoundServer::Play( mExternalSoundList.GetHead(), pDB, 1.0, pPan );
         mExternalSoundList.Remove();
      }

      // Continuous sounds
      MR_ContinuousSound* lWindSound  = mRenderer->GetFrictionSound();
      MR_ContinuousSound* lMotorSound = mRenderer->GetMotorSound();
      double lAbsSpeed = GetNormalisedSpeed();


      if( lAbsSpeed > 0.02 )
      {
         MR_SoundServer::Play( lWindSound, 1, pDB, 1.5*lAbsSpeed, pPan );
      }

      if( mControlState&eMotorOn )
      {
         MR_SoundServer::Play( lMotorSound, 1 , pDB, 1.0, pPan );
      }
   }
}
