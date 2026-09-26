// HoverRender.cpp
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

#include "HoverRender.h"
#include "ObjFac1Res.h"
#include "../ObjFacTools/ObjectFactoryData.h"
#include "../Util/FuzzyLogic.h"

// Local stuff

static class MR_ResActorFriend
{
   public:
      static void Draw( const MR_ResActor* pActor, MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pSequence, int pFrame, const MR_Bitmap* pCockpitBitmap );
};
                    
                    





MR_HoverRender::MR_HoverRender( const MR_ObjectFromFactoryId& pId )
               :MR_MainCharacterRenderer( pId )
{
   mFrame = 0;
   
   // DEFENSIVE: Check if factory data is initialized
   if( gObjectFactoryData == NULL )
   {
      mActor0 = NULL;
      mActor1 = NULL;
      mActor2 = NULL;
      mActor3 = NULL;
      mActor4 = NULL;
      mLineCrossingSound = NULL;
      mStartSound = NULL;
      mFinishSound = NULL;
      mBumpSound = NULL;
      mJumpSound = NULL;
      mFireSound = NULL;
      mMisJumpSound = NULL;
      mMisFireSound = NULL;
      mOutOfCtrlSound = NULL;
      mMotorSound = NULL;
      mFrictionSound = NULL;
      memset( mCockpitBitmap, 0, sizeof(mCockpitBitmap) );
      memset( mCockpitBitmap2, 0, sizeof(mCockpitBitmap2) );
      return;
   }
   
   mActor0 = gObjectFactoryData->mResourceLib.GetActor( MR_ELECTRO_CAR );
   mActor1 = gObjectFactoryData->mResourceLib.GetActor( MR_HITECH_CAR );
   mActor2 = gObjectFactoryData->mResourceLib.GetActor( MR_BITURBO_CAR );
   mActor3 = gObjectFactoryData->mResourceLib.GetActor( 19 );
   mActor4 = gObjectFactoryData->mResourceLib.GetActor( MR_MANTA_CRAFT );
   // DEFENSIVE: Initialize sound pointers - check for null before calling GetSound()
   auto lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_LINE_CROSSING );
   mLineCrossingSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_START );
   mStartSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_FINISH );
   mFinishSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_BUMP );
   mBumpSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_JUMP );
   mJumpSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_FIRE );
   mFireSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_MIS_JUMP );
   mMisJumpSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_MIS_FIRE );
   mMisFireSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   lSoundBuf = gObjectFactoryData->mResourceLib.GetShortSound( MR_SND_OUT_OF_CTRL );
   mOutOfCtrlSound = (lSoundBuf != NULL) ? lSoundBuf->GetSound() : NULL;
   
   auto lContSoundBuf = gObjectFactoryData->mResourceLib.GetContinuousSound( MR_SND_MOTOR );
   mMotorSound = (lContSoundBuf != NULL) ? lContSoundBuf->GetSound() : NULL;
   
   lContSoundBuf = gObjectFactoryData->mResourceLib.GetContinuousSound( MR_SND_FRICTION );
   mFrictionSound = (lContSoundBuf != NULL) ? lContSoundBuf->GetSound() : NULL;

   // Debug logging for sound system
   {
      FILE* logFile = NULL;
      if(logFile) {
         fprintf(logFile, "[HoverRender] Motor sound: %p, Friction sound: %p\n", mMotorSound, mFrictionSound);
         fflush(logFile);
         fclose(logFile);
      }
   }

   for( int lCounter = 0; lCounter < 10; lCounter++ )
   {
      mCockpitBitmap[ lCounter ] = gObjectFactoryData->mResourceLib.GetBitmap( MR_CAR_COCKPIT1+lCounter );
      mCockpitBitmap2[ lCounter ] = gObjectFactoryData->mResourceLib.GetBitmap( MR_CAR_COCKPIT21+lCounter );
   }

}

MR_HoverRender::~MR_HoverRender()
{

}

void MR_HoverRender::Render( MR_3DViewPort* pDest, 
                             const MR_3DCoordinate& pPosition, 
                             MR_Angle pOrientation,
                             BOOL     pMotorOn, 
                             int      pHoverId,
                             int      pModel        )
{
   // DIAGNOSTIC: Log RECEIVED parameters at function entry
   // This happens BEFORE any modifications to detect if they're already corrupted
   static int entry_count = 0;
   if( entry_count % 30 == 0 )
   {
      FILE* entryLog = NULL;
      if( entryLog )
      {
         fprintf(entryLog, "[Entry #%d] RECEIVED: Id=%d Mod=%d (pDest=%p)\n",
            entry_count, pHoverId, pModel, pDest);
         fflush(entryLog);
         fclose(entryLog);
      }
   }
   entry_count++;

   // DEFENSIVE: Validate all input parameters
   if( pDest == NULL ) return;
   
   // CRITICAL FIX: pModel parameter is actually the ACTOR RESOURCE ID (10, 11, 12, 19)
   // NOT a model index (0-2). We need to map actor IDs to model indices.
   // MainCharacter.cpp passes: Actor 10 (Electro), 11 (HiTech), 12 (BiTurbo), 19 (Eon)
   // But HoverRender expects: Model 0 (Electro), 1 (HiTech), 2 (BiTurbo)
   
   // DEFAULT SAFE VALUES
   int lModel = 0;        // Default to model 0 (Electro Car - Actor 10)
   int lHoverId = 0;      // Default to ID 0 (default cockpit)
   
   // MAP ACTOR RESOURCE IDs TO MODEL INDICES
   if( pModel == 10 )
   {
      lModel = 0;        // Electro Car
   }
   else if( pModel == 11 )
   {
      lModel = 1;        // HiTech Car
   }
   else if( pModel == 12 )
   {
      lModel = 2;        // BiTurbo Car
   }
   else if( pModel == 19 )
   {
      lModel = 3;
   }
   else if( pModel == MR_MANTA_CRAFT )
   {
      lModel = 4;
   }
   else if( pModel >= 0 && pModel <= 4 )
   {
      // Fallback: if pModel is already a model index, use it directly
      lModel = pModel;
   }
   // else: stick with default model 0 for any other invalid value
   
   // ONLY use pHoverId if it's in the EXACT valid range  
   if( pHoverId >= 0 && pHoverId <= 15 )
   {
      lHoverId = pHoverId;  // Valid ID - use it
   }
   // else: stick with default ID 0
   
   // DIAGNOSTIC: Log FINAL parameters to be used for rendering
   static int final_count = 0;
   if( final_count % 30 == 0 )
   {
      FILE* finalLog = NULL;
      if( finalLog )
      {
         fprintf(finalLog, "[Final #%d] Using: Model=%d HoverId=%d (Received: %d, %d)\n",
            final_count, lModel, lHoverId, pModel, pHoverId);
         fflush(finalLog);
         fclose(finalLog);
      }
   }
   final_count++;
   
   // DEFENSIVE: Check for NaN or infinity in position
   if( !_finite(pPosition.mX) || !_finite(pPosition.mY) || !_finite(pPosition.mZ) )
   {
      return;  // Invalid coordinates - abort rendering
   }

   // Compute the required rotation matrix
   // CRITICAL FIX: For the viewing character (player's own hovercraft), use a VERY large tolerance
   // The hovercraft should NEVER disappear from the player's view, regardless of camera frustum
   // Use 10000000 units (10 million) - effectively disable frustum culling for player craft
   MR_PositionMatrix lMatrix;
   BOOL matrix_ok = pDest->ComputePositionMatrix( lMatrix, pPosition, pOrientation, 10000000 );
   
   // IMPORTANT: We only render if we have a valid matrix
   // The large tolerance above should make this succeed in almost all cases
   // If it still fails, skip rendering rather than crash
   
   // DIAGNOSTIC: Aggressive logging to trace hovercraft rendering
   static int total_render_calls = 0;
   static int successful_renders = 0;
   static int failed_matrix_calls = 0;
   static int first_call = 1;
   
   total_render_calls++;
   
   // Log STARTUP info once
   if( first_call )
   {
      FILE* startLog = NULL;
      if( startLog )
      {
         fprintf(startLog, "=== RENDER FUNCTION CALLED ===\n");
         fprintf(startLog, "Calling Render with:\n");
         fprintf(startLog, "  pHoverId (int) = %d\n", pHoverId);
         fprintf(startLog, "  pModel (int) = %d\n", pModel);
         fprintf(startLog, "  pMotorOn (BOOL) = %d\n", pMotorOn);
         fprintf(startLog, "  Position.X = %.1f\n", pPosition.mX);
         fprintf(startLog, "  Position.Y = %.1f\n", pPosition.mY);
         fprintf(startLog, "  Position.Z = %.1f\n", pPosition.mZ);
         fprintf(startLog, "  MatrixOK = %d\n", matrix_ok);
         fflush(startLog);
         fclose(startLog);
      }
      first_call = 0;
   }
   
   // Log every 30th call  
   if( total_render_calls % 30 == 0 )
   {
      FILE* allLog = NULL;
      if( allLog )
      {
         fprintf(allLog, "[#%5d] X=%.0f Y=%.0f Z=%.0f Id=%d Mod=%d OK=%d\n",
            total_render_calls, pPosition.mX, pPosition.mY, pPosition.mZ, lHoverId, lModel, matrix_ok);
         fflush(allLog);
         fclose(allLog);
      }
   }
   
   if( matrix_ok )
   {
      successful_renders++;
   }
   else
   {
      failed_matrix_calls++;
      FILE* failLog = NULL;
      if( failLog )
      {
         fprintf(failLog, "[FAIL #%d] at X=%.0f Y=%.0f Z=%.0f, HoverId=%d Model=%d\n",
            failed_matrix_calls, pPosition.mX, pPosition.mY, pPosition.mZ, lHoverId, lModel);
         fflush(failLog);
         fclose(failLog);
      }
   }
   
   if( matrix_ok )
   {
      int lSeq   = pMotorOn?1:0;

      if( pMotorOn )
      {
         mFrame = (mFrame+1)%2;
      }
      else
      {
         mFrame =0;
      }

      // DEFENSIVE: Select actor with null checks
      const MR_ResActor* lActor = NULL;
      if( lModel == 1 )
      {
         lActor = mActor1;
      }
      else if( lModel == 2 )
      {
         lActor = mActor2;
      }
      else if( lModel == 3 )
      {
         lActor = mActor3;  // Eon Craft
      }
      else if( lModel == 4 )
      {
         lActor = mActor4;  // Manta
      }
      else
      {
         lActor = mActor0;  // Default to Electro Car
      }

      // DEBUG: Log render calls to diagnose coloring issue
      static int render_count = 0;
      FILE* logFile = NULL;
      if( logFile && (render_count % 60 == 0) )  // Log every 60th call
      {
         fprintf(logFile, "[Render #%d] Model=%d, Motor=%s, HoverId=%d, Actor=%p, Draw=%s, Matrix=%s\n",
            render_count, lModel, pMotorOn ? "ON" : "OFF", lHoverId,
            lActor, lActor != NULL ? "YES" : "NO", matrix_ok ? "OK" : "FAILED");
         
         if( lActor == NULL )
         {
            fprintf(logFile, "  WARNING: Actor is NULL! mActor0=%p, mActor1=%p, mActor2=%p\n",
               mActor0, mActor1, mActor2);
            fprintf(logFile, "  gObjectFactoryData=%p\n", gObjectFactoryData);
         }
         fflush(logFile);
         fclose(logFile);
      }
      render_count++;

      // DEFENSIVE: Only draw if actor is valid
      if( lActor != NULL )
      {
         const MR_Bitmap* lCockpitBitmap = NULL;
         if( lModel == 1 )
         {
            lCockpitBitmap = mCockpitBitmap2[ pHoverId%10 ];
         }
         else
         {
            lCockpitBitmap = mCockpitBitmap[ pHoverId%10 ];
         }

         MR_ResActorFriend::Draw( lActor, pDest, lMatrix, lSeq, mFrame, lCockpitBitmap );
      }
   }
   else
   {
      // Matrix computation failed even with 10 million unit tolerance
      // IMPORTANT: Still render with fallback identity matrix so craft doesn't disappear
      static int failure_count = 0;
      failure_count++;
      
      if( failure_count <= 3 )
      {
         FILE* failLog = NULL;
         if( failLog )
         {
            fprintf(failLog, "[FAIL #%d] Pos=(%.0f, %.0f, %.0f) HoverId=%d Model=%d\n",
               failure_count, pPosition.mX, pPosition.mY, pPosition.mZ, pHoverId, pModel);
            fflush(failLog);
            fclose(failLog);
         }
      }
      
      // Create identity matrix as safe fallback
      // Identity rotation: [1 0; 0 1], Displacement at origin
      MR_PositionMatrix identityMatrix;
      identityMatrix.mRotation[0][0] = 1024;  // MR_COS[0] = 1024
      identityMatrix.mRotation[0][1] = 0;
      identityMatrix.mRotation[1][0] = 0;
      identityMatrix.mRotation[1][1] = 1024;  // MR_COS[0] = 1024
      identityMatrix.mDisplacement.mX = 0;
      identityMatrix.mDisplacement.mY = 0;
      identityMatrix.mDisplacement.mZ = 0;
      
      int lSeq = pMotorOn ? 1 : 0;
      if( pMotorOn )
      {
         mFrame = (mFrame+1)%2;
      }
      else
      {
         mFrame = 0;
      }
      
      const MR_ResActor* lActor = NULL;
      if( pModel == 1 )
      {
         lActor = mActor1;
      }
      else if( pModel == 2 )
      {
         lActor = mActor2;
      }
      else
      {
         lActor = mActor0;
      }
      
      if( lActor != NULL )
      {
         const MR_Bitmap* lCockpitBitmap = NULL;
         if( pModel == 1 )
         {
            lCockpitBitmap = mCockpitBitmap2[ pHoverId%10 ];
         }
         else
         {
            lCockpitBitmap = mCockpitBitmap[ pHoverId%10 ];
         }
         
         // Render with identity matrix as fallback
         MR_ResActorFriend::Draw( lActor, pDest, identityMatrix, lSeq, mFrame, lCockpitBitmap );
      }
   }
}

MR_ShortSound* MR_HoverRender::GetLineCrossingSound()
{
   return mLineCrossingSound;
}

MR_ShortSound* MR_HoverRender::GetStartSound()
{
   return mStartSound;
}

MR_ShortSound* MR_HoverRender::GetFinishSound()
{
   return mFinishSound;
}

MR_ShortSound* MR_HoverRender::GetBumpSound()
{
   return mBumpSound;
}

MR_ShortSound* MR_HoverRender::GetJumpSound()
{
   return mJumpSound;
}

MR_ShortSound* MR_HoverRender::GetFireSound()
{
   return mFireSound;
}

MR_ShortSound* MR_HoverRender::GetMisJumpSound()
{
   return mMisJumpSound;
}

MR_ShortSound* MR_HoverRender::GetMisFireSound()
{
   return mMisFireSound;
}

MR_ShortSound* MR_HoverRender::GetOutOfCtrlSound()
{
   return mOutOfCtrlSound;
}

MR_ContinuousSound* MR_HoverRender::GetMotorSound()
{
   return mMotorSound;
}

MR_ContinuousSound* MR_HoverRender::GetFrictionSound()
{
   return mFrictionSound;
}


void MR_ResActorFriend::Draw( const MR_ResActor* pActor, MR_3DViewPort* pDest, const MR_PositionMatrix& pMatrix, int pSequence, int pFrame, const MR_Bitmap* pCockpitBitmap )
{
   // DEFENSIVE: Validate all pointers and parameters
   if( pActor == NULL ) return;
   if( pDest == NULL ) return;
   if( pActor->mSequenceList == NULL || pActor->mNbSequence <= 0 ) return;
   if( pSequence < 0 ) pSequence = 0;
   pSequence %= pActor->mNbSequence;

   // Manta sequence 1 contains only the animated thrust overlay. Keep its
   // closed hull and fan ducts visible by drawing the static base first.
   if( pActor->GetResourceId() == MR_MANTA_CRAFT && pSequence == 1 )
   {
      Draw( pActor, pDest, pMatrix, 0, pFrame, pCockpitBitmap );
   }

   MR_ResActor::Sequence* lSequence = &(pActor->mSequenceList[pSequence]);
   if( lSequence->mFrameList == NULL || lSequence->mNbFrame <= 0 ) return;
   // Static actors may contain one frame while the renderer's shared animation
   // counter keeps advancing. Fold it into this actor's actual frame count.
   if( pFrame < 0 ) pFrame = 0;
   pFrame %= lSequence->mNbFrame;
   
   MR_ResActor::Frame* lFrame = &(lSequence->mFrameList[pFrame]);

   if( lFrame == NULL ) return;  // Frame is invalid
   if( lFrame->mComponentList == NULL ) return;  // No components to render
   if( lFrame->mNbComponent <= 0 ) return;  // No components

   for( int lCounter = 0; lCounter < lFrame->mNbComponent; lCounter++ )
   {
      // DEFENSIVE: Validate array access
      if( lCounter < 0 || lCounter >= lFrame->mNbComponent ) break;
      
      MR_ResActor::Patch* lPatch = (MR_ResActor::Patch*)lFrame->mComponentList[ lCounter ];

      if( lPatch == NULL ) continue;  // Skip NULL patches
      if( lPatch->mBitmap == NULL ) continue;  // Skip patches with no bitmap

      int lBitmapResId = lPatch->mBitmap->GetResourceId();

      if( (lBitmapResId == MR_CAR_COCKPIT)||(lBitmapResId == MR_CAR2_COCKPIT)||(lBitmapResId == MR_ECAR_COCKPIT) )
      {
         if( pCockpitBitmap != NULL )
         {
            pDest->RenderPatch( *lPatch, pMatrix , pCockpitBitmap );
         }
      }
      else
      {
         pDest->RenderPatch( *lPatch, pMatrix , lPatch->mBitmap );
      }
   }
}
