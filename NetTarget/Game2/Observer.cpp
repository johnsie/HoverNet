// Observer.cpp
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

#include "StdAfx.h"

#include "Observer.h"
#include "../Model/MazeElement.h"
#include "../Util/Profiler.h"
#include "resource.h"
#include "../Util/StrRes.h"

#include <math.h>


#define NB_PLAYER_PAGE 4
#define MR_CHAT_EXPIRATION     20

#ifdef _WIN32
CString gRankTitle = Ascii2Simple( MR_LoadString( IDS_RANK_TITLE ) );
CString gHitTitle  = Ascii2Simple( MR_LoadString( IDS_HIT_TITLE ) );

CString gFirstLapStr = MR_LoadString( IDS_FLAP_STR );

CString gChartFinish     = MR_LoadString( IDS_CHART_FINISH );
CString gChart           = MR_LoadString( IDS_CHART );
CString gHitChart        = MR_LoadString( IDS_HIT_CHART );
CString gCountdownStr    = MR_LoadString( IDS_COUNTDOWN );
CString gFinishStr       = MR_LoadString( IDS_FINISH );
CString gFinishStrSingle = MR_LoadString( IDS_FINISH_SINGLE );
CString gBestLapStr      = MR_LoadString( IDS_BEST_LAP );
CString gHeaderStr       = MR_LoadString( IDS_HEADER );
CString gLastLapStr      = MR_LoadString( IDS_LAST_LAP );
CString gCurLapStr       = MR_LoadString( IDS_CUR_LAP );
#else
CString gRankTitle       = Ascii2Simple( "Rank            Result   Best Lap " );
CString gHitTitle        = Ascii2Simple( "Rank                 For   Agains" );
CString gFirstLapStr     = "%d %-0.10s #%d%*s   --First lap--  %c";
CString gChartFinish     = "%d %-0.10s #%d%*s %2d.%02d.%02d %2d.%02d.%02d%c";
CString gChart           = "%d %-0.10s #%d%*s lap:%-2d   %2d.%02d.%02d%c";
CString gHitChart        = "%d %-0.10s #%d%*s  %2d      %2d";
CString gCountdownStr    = "Start in %02d.%02d sec. for %d laps";
CString gFinishStr       = "Finish in %d.%02d.%02d  %d of %d";
CString gFinishStrSingle = "Finish in %d.%02d.%02d";
CString gBestLapStr      = "  Best lap %d.%02d.%02d  ";
CString gHeaderStr       = "%d.%02d.%02d  Lap:%d/%d";
CString gLastLapStr      = "Last lap %d.%02d.%02d  Best %d.%02d.%02d";
CString gCurLapStr       = "Current lap %d.%02d.%02d  Best %d.%02d.%02d";
#endif



MR_Observer::MR_Observer()
{
   mLastCameraPosValid = FALSE;
   mScroll = 0;
   mApperture = MR_PI/2;

   mXMargin_1024 = 0;
   mYMargin_1024 = 0;

   mMoreMessages = FALSE;
   mDispPlayers  = 1;

   mSplitMode = eNotSplit;

   mCockpitView = FALSE;

   // Try to create sprite resources, but don't fail if they don't load
   try {
      MR_ObjectFromFactoryId lBaseFontId = { 1, 1000 };
      mBaseFont     = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject( lBaseFontId );
   } catch(...) {
      mBaseFont = NULL;
   }

   try {
      MR_ObjectFromFactoryId lMissileLevelId = { 1, 1100 };
      mMissileLevel = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject( lMissileLevelId );
   } catch(...) {
      mMissileLevel = NULL;
   }

   try {
      MR_ObjectFromFactoryId lMineDispId = { 1, 1102 };
      mMineDisp = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject( lMineDispId );
   } catch(...) {
      mMineDisp = NULL;
   }

   try {
      MR_ObjectFromFactoryId lPowerUpDispId = { 1, 1103 };
      mPowerUpDisp = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject( lPowerUpDispId );
   } catch(...) {
      mPowerUpDisp = NULL;
   }

   try {
      MR_ObjectFromFactoryId lHoverIconsId = { 1, 1101 };
      mHoverIcons = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject( lHoverIconsId );
   } catch(...) {
      mHoverIcons = NULL;
   }

}

MR_Observer::~MR_Observer()
{
   delete mBaseFont;
   delete mMissileLevel;
   delete mMineDisp;
   delete mPowerUpDisp;
   delete mHoverIcons;
}

MR_Observer* MR_Observer::New()
{
   return new MR_Observer;
}

void MR_Observer::Delete()
{
   delete this;
}

void MR_Observer::SetCockpitView( BOOL pOn )
{
   mCockpitView = pOn;
}


void MR_Observer::Scroll( int pOffset )
{   
   mScroll += pOffset;

   if( mScroll < -10 )
   {
      mScroll = -10;
   }
   else if( mScroll > 10 )
   {
      mScroll = 10;
   }
}

void MR_Observer::ZoomIn()
{
   if( mApperture > MR_PI/10 )
   {
      mApperture = MR_Angle( (mApperture*4)/5 );
   }
}

void MR_Observer::ZoomOut()
{
   if( mApperture < 3*MR_PI/4 )
   {
      mApperture = MR_Angle( mApperture + mApperture/4 );
   }
}

void MR_Observer::Home( )
{
   mScroll = 0;
   mApperture = MR_PI/2;
}

void MR_Observer::EnlargeMargin()
{
   if( mXMargin_1024 < 400 )
   {
      mXMargin_1024 += 64;
   }

   if( mYMargin_1024 < 400 )
   {
      mYMargin_1024 += 64;
   }
}

void MR_Observer::ReduceMargin()
{
   mXMargin_1024 -= 64;
   mYMargin_1024 -= 64;

   if( mXMargin_1024 < 0 )
   {
      mXMargin_1024 = 0;
   }

   if( mYMargin_1024 < 0 )
   {
      mYMargin_1024 = 0;
   }
}


void MR_Observer::SetSplitMode( eSplitMode pMode )
{
   mSplitMode = pMode;
}

void MR_Observer::MoreMessages()
{
   if( mDispPlayers != 0 )
   {
      mDispPlayers = 0;
      mMoreMessages = TRUE;
   }
   else
   {
      mMoreMessages = !mMoreMessages;
   }
}

void MR_Observer::PlayersListPageDn()
{
   if( mDispPlayers == 0 )
   {
      // mMoreMessages = FALSE;
   }
   mDispPlayers++;
}



// Rendering functions
void MR_Observer::Render2DDebugView( MR_VideoBuffer* pDest, const MR_Level* pLevel, const MR_MainCharacter* pViewingCharacter )
{
   // WARNING Calculations are done using floats..it is only a debug view

   // Draw a surface of 100m x 60m centered on the character

   // Initialize the viewport
   int lXRes = m2DDebugView.GetXRes();
   int lYRes = m2DDebugView.GetYRes();


   // Compute the scaling factor and the origin
   double lXScaling =  pDest->GetXPixelMeter()/(500.0*1000.0); // lXRes/100000.0; // 1m == 2mm on screen
   double lYScaling = -pDest->GetYPixelMeter()/(500.0*1000.0); //-lYRes/60000.0;

   MR_3DCoordinate lCharacterPos   = pViewingCharacter->mPosition;
   MR_Angle        lOrientation    = pViewingCharacter->mOrientation;
   int             lRoom           = pViewingCharacter->mRoom;

   m2DDebugView.Clear();
   // pDest->Clear(); // Faster

   int        lRoomCount;
   const int* lRoomList = pLevel->GetVisibleZones( lRoom, lRoomCount );


   // Draw each room of the maze
   for( int lRoomId = 0; lRoomId< pLevel->GetRoomCount(); lRoomId++ )
   {
      MR_PolygonShape* lSectionShape = pLevel->GetRoomShape( lRoomId );

      
      // Determine the Drawing color
      MR_UInt8 lColor = 6;

      if( lRoomId == lRoom )
      {
         lColor = 7;
      }
      else
      {
         // verify if the zone is visible

         for( int lCounter = 0; lCounter < lRoomCount; lCounter++ )
         {
            if( lRoomList[ lCounter ] == lRoomId )
            {
               lColor = 9;
               break;
            }
         }
      }

      // Draw the contour

      int lVertexCount = lSectionShape->VertexCount();

      MR_Int32 lX0;
      MR_Int32 lY0;
      MR_Int32 lX1;
      MR_Int32 lY1;

      lX1 = (MR_Int32)(( lSectionShape->X( lVertexCount-1 )-lCharacterPos.mX )*lXScaling)+lXRes/2;
      lY1 = (MR_Int32)(( lSectionShape->Y( lVertexCount-1 )-lCharacterPos.mY )*lYScaling)+lYRes/2;

      for( int lVertex = 0; lVertex < lVertexCount; lVertex++ )
      {
         lX0 = (MR_Int32)(( lSectionShape->X( lVertex )-lCharacterPos.mX )*lXScaling)+lXRes/2;
         lY0 = (MR_Int32)(( lSectionShape->Y( lVertex )-lCharacterPos.mY )*lYScaling)+lYRes/2;

         m2DDebugView.DrawLine( lX0, lY0, lX1, lY1, lColor );

         lX1 = lX0;
         lY1 = lY0;
      }

      // Draw the FreeElements

      delete lSectionShape;
   }


   // Draw the main character
   int lXSideLen = int(1000.0*lXScaling);
   int lYSideLen = int(1000.0*lYScaling);

   m2DDebugView.DrawLine( (-lXSideLen + lXRes)/2,
                          ( lYSideLen + lYRes)/2,
                          ( lXSideLen + lXRes)/2,
                          ( lYSideLen + lYRes)/2,                    
                           9 );
  
   m2DDebugView.DrawLine( (-lXSideLen + lXRes)/2,
                          (-lYSideLen + lYRes)/2,
                          ( lXSideLen + lXRes)/2,
                          (-lYSideLen + lYRes)/2,                    
                           9 );

   m2DDebugView.DrawLine( ( lXSideLen + lXRes)/2,
                          (-lYSideLen + lYRes)/2,
                          ( lXSideLen + lXRes)/2,
                          ( lYSideLen + lYRes)/2,                    
                           9 );

   m2DDebugView.DrawLine( (-lXSideLen + lXRes)/2,
                          (-lYSideLen + lYRes)/2,
                          (-lXSideLen + lXRes)/2,
                          ( lYSideLen + lYRes)/2,                    
                           9 );

   m2DDebugView.DrawLine( lXRes/2,
                          lYRes/2,
                          ((MR_Cos[lOrientation]*4*lXSideLen/MR_TRIGO_FRACT) + lXRes)/2,
                          ((MR_Sin[lOrientation]*4*lYSideLen/MR_TRIGO_FRACT) + lYRes)/2,
                          9 );


}

void MR_Observer::RenderWireFrameView( const MR_Level* pLevel, const MR_MainCharacter* pViewingCharacter )
{

   mWireFrameView.Clear( 0 );
   // mWireFrameView.ClearZ( );

   MR_3DCoordinate lCharacterPos   = pViewingCharacter->mPosition;
   MR_Angle        lOrientation    = pViewingCharacter->mOrientation;
   int             lRoom           = pViewingCharacter->mRoom;

   lCharacterPos.mZ += 1800; // Fix the eyes at 1m80

   mWireFrameView.SetupCameraPosition( lCharacterPos,
                                       lOrientation,
                                       mScroll       );

   // Draw the walls and features of the visibles rooms
   int        lRoomCount;
   const int* lRoomList = pLevel->GetVisibleZones( lRoom, lRoomCount );

   for( int lCounter = -1; lCounter < lRoomCount; lCounter++ )
   {
      int      lRoomId;
      MR_UInt8 lColor = 7;
      
      if( lCounter == -1 )
      {
         lRoomId = lRoom;
      }
      else
      {
         lRoomId = lRoomList[ lCounter ];
      }

      // Draw the room and all the features
      for( int lCounter2 = -1; lCounter2 < pLevel->GetFeatureCount( lRoom ); lCounter2++ )
      {
   
         MR_SectionId lSectionId;

         if( lCounter2 == -1 )
         {
            lSectionId.mType  = MR_SectionId::eRoom;
            lSectionId.mId    = lRoom;
         }
         else
         {
            lSectionId.mType  = MR_SectionId::eFeature;
            lSectionId.mId    = pLevel->GetFeature( lRoom, lCounter2 );
         }
         DrawWFSection( pLevel, lSectionId, lColor );
      }
   }
   
}

void MR_Observer::DrawWFSection( const MR_Level* pLevel, const MR_SectionId& pSectionId, MR_UInt8 pColor )
{
   MR_PolygonShape* lSectionShape;
   
   if( pSectionId.mType == MR_SectionId::eRoom )
   {
      lSectionShape = pLevel->GetRoomShape( pSectionId.mId );
   }
   else
   {
      lSectionShape = pLevel->GetFeatureShape( pSectionId.mId );
   }

   // Draw the contour

   int lVertexCount = lSectionShape->VertexCount();
   MR_3DCoordinate lP0;
   MR_3DCoordinate lP1;

   lP1.mX = lSectionShape->X( lVertexCount-1 );
   lP1.mY = lSectionShape->Y( lVertexCount-1 );

   for( int lVertex = 0; lVertex < lVertexCount; lVertex++ )
   {
      MR_3DCoordinate lP0Top;

      lP0.mX = lSectionShape->X( lVertex );
      lP0.mY = lSectionShape->Y( lVertex );


      lP0.mZ    = lSectionShape->ZMax();
      lP1.mZ    = lP0.mZ;
      lP0Top    = lP0;

      mWireFrameView.DrawWFLine( lP0, lP1, pColor );

      lP0.mZ = lSectionShape->ZMin();
      lP1.mZ = lP0.mZ;

      mWireFrameView.DrawWFLine( lP0, lP1, pColor );
      mWireFrameView.DrawWFLine( lP0, lP0Top, pColor );

      lP1 = lP0;
   }

   delete lSectionShape;

}

void MR_Observer::Render3DView( const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage )
{
   // Safe entry guards - check all pointers before proceeding
   if(pSession == NULL || pViewingCharacter == NULL) {
      return;
   }

   const MR_Level* lLevel = pSession->GetCurrentLevel();
   
   // Null level check
   if(lLevel == NULL) {
      m3DView.Clear(0);
      m3DView.ClearZ();
      return;
   }

   // DEFENSIVE: Check if character position is valid (within reasonable bounds)
   // If not, it might be uninitialized garbage - skip rendering
   const double POSITION_BOUNDS = 1000000.0;  // Max reasonable position value
   BOOL position_valid = (pViewingCharacter->mPosition.mX > -POSITION_BOUNDS && pViewingCharacter->mPosition.mX < POSITION_BOUNDS &&
                          pViewingCharacter->mPosition.mY > -POSITION_BOUNDS && pViewingCharacter->mPosition.mY < POSITION_BOUNDS &&
                          pViewingCharacter->mPosition.mZ > -POSITION_BOUNDS && pViewingCharacter->mPosition.mZ < POSITION_BOUNDS);
   
   if(!position_valid) {
      // Position is corrupted - skip rendering to prevent crash
      m3DView.Clear(0);
      m3DView.ClearZ();
      return;
   }

   // STAGE 1: Camera Setup - Safe, low-level rendering 
   MR_3DCoordinate lCameraPos;
   MR_Angle        lOrientation    = pViewingCharacter->mOrientation;
   int             lRoom           = pViewingCharacter->mRoom;
   double          lAbsSpeedRatio  = pViewingCharacter->GetAbsoluteSpeed();

   if( mCockpitView )
   {
      lOrientation   = pViewingCharacter->GetCabinOrientation();   
      lCameraPos.mX  = pViewingCharacter->mPosition.mX - 256*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY - 256*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1050;
   }
   else
   {
      int lDist = 3400;
      lOrientation   = pViewingCharacter->mOrientation;   
      
      if( pTime < -3000 )
      {         
         int lFactor = (-pTime-3000)*2/3;
         lOrientation = MR_NORMALIZE_ANGLE( lOrientation+lFactor*4096/11000 );
         lDist += lFactor;
      }
      
      lCameraPos.mX  = pViewingCharacter->mPosition.mX -lDist*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY -lDist*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1700;

      if( mLastCameraPosValid )
      {
         lCameraPos.mX = (3*lCameraPos.mX + mLastCameraPos.mX )/4;
         lCameraPos.mY = (3*lCameraPos.mY + mLastCameraPos.mY )/4;
         lCameraPos.mZ = (2*lCameraPos.mZ + mLastCameraPos.mZ )/3;
      }
   }

   mLastCameraPos = lCameraPos;
   mLastCameraPosValid = TRUE;

   __try {
      m3DView.SetupCameraPosition( lCameraPos, lOrientation, mScroll );
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Camera setup crashed - just continue with safe clearing
   }

   // STAGE 2: Clear Screen with a light blue color to show rendering is active
   if( pBackImage == NULL )
   {
      // Use light blue (150) instead of black (0) to visually indicate graphics rendering
      m3DView.Clear( 150 );
   }
   else
   {
      m3DView.RenderBackground( pBackImage );
   }

   m3DView.ClearZ();
   
   // MEMORY SYNC: Force write completion of clear operation before rendering begins
   // This ensures stale or corrupted buffer data is completely overwritten
   // Critical to prevent artifacts from partial/stale renders showing through
   volatile int* pSyncPoint = (volatile int*)&m3DView;
   int syncDummy = *pSyncPoint;
   (void)syncDummy;

   // STAGE 3: Floor and ceiling rendering
   __try {
      int lTotalSections = lLevel->GetNbVisibleSurface( lRoom );
      const MR_SectionId* lFloorList     = lLevel->GetVisibleFloorList( lRoom );
      const MR_SectionId* lCeilingList   = lLevel->GetVisibleCeilingList( lRoom );

      for( int lCounter = 0; lCounter < lTotalSections; lCounter++ )
      {
         // Draw the floor
         RenderFloorOrCeiling( lLevel, lFloorList[ lCounter ], TRUE, pTime );

         // Render the ceiling
         RenderFloorOrCeiling( lLevel, lCeilingList[ lCounter ], FALSE, pTime );
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Floor/ceiling rendering crashed - continue with what we have
   }

   // STAGE 4: Wall rendering
   __try {
      int lRoomCount;
      const int* lRoomList  = lLevel->GetVisibleZones( lRoom, lRoomCount );

      for( int lCounter = -1; lCounter < lRoomCount; lCounter++ )
      {
         int lRoomId;
         
         if( lCounter == -1 )
         {
            lRoomId = lRoom;
         }
         else
         {
            lRoomId = lRoomList[ lCounter ];
         }

         // Draw all the features
         int lNbFeature = lLevel->GetFeatureCount( lRoomId );

         for( int lCounter2 = 0; lCounter2 < lNbFeature; lCounter2++ )
         {
            RenderFeatureWalls( lLevel, lLevel->GetFeature( lRoomId, lCounter2 ), pTime );
         }

         RenderRoomWalls( lLevel, lRoomId, pTime );
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Wall rendering crashed - continue with what we have
   }

   // STAGE 5: Actor rendering
   __try {
      int lRoomCount;
      const int* lRoomList  = lLevel->GetVisibleZones( lRoom, lRoomCount );

      for( int lCounter = -1; lCounter < lRoomCount; lCounter++ )
      {
         int lRoomId;
         
         if( lCounter == -1 )
         {
            lRoomId = lRoom;
         }
         else
         {
            lRoomId = lRoomList[ lCounter ];
         }

         MR_FreeElementHandle lHandle = lLevel->GetFirstFreeElement( lRoomId );
         
         while( lHandle != NULL )
         {
            MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );
            
            // Add bounds check - skip actors with impossible coordinates
            if(lElement != NULL && lElement != pViewingCharacter) {
               // Check if coordinates are within reasonable bounds (-1000000 to +1000000)
               double x = lElement->mPosition.mX;
               double y = lElement->mPosition.mY;
               double z = lElement->mPosition.mZ;
               
               BOOL valid_position = (x > -1000000 && x < 1000000 && 
                                     y > -1000000 && y < 1000000 && 
                                     z > -1000000 && z < 1000000);
               
               if(valid_position) {
                  lElement->Render( &m3DView, pTime );
               }
            }

            lHandle = MR_Level::GetNextFreeElement( lHandle );
         }
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Actor rendering crashed - continue with what we have
   }

   // STAGE 5.5: Render the viewing character (player's hovercraft)
   
   __try {
      // Call the character's render method to draw the player's hovercraft
      // Cast away const since Render() is not const (not ideal, but necessary for rendering)
      const_cast<MR_MainCharacter*>(pViewingCharacter)->Render( &m3DView, pTime );
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
   }

   // STAGE 6: Cockpit UI rendering (speed/fuel meters, weapons, map, text)
   __try {
      int lXRes = m3DView.GetXRes();
      int lYRes = m3DView.GetYRes();

      // Speed and fuel meters
      if( lXRes > 0 && lYRes > 0 )
      {
         int lSpeedMeterLen = lXRes/2;
         int lFuelMeterLen  = lXRes/4;
         int lMeterHight    = lYRes/32;
         int lXMargin       = lXRes/32;
         int lYMargin       = lMeterHight;
         
         int    lAbsSpeedLen  = lAbsSpeedRatio*lSpeedMeterLen;
         int    lDirSpeedLen  = pViewingCharacter->GetDirectionalSpeed()*lSpeedMeterLen;
         double lFuelLevel    = pViewingCharacter->GetFuelLevel();
         int    lFuelLen      = lFuelLevel*lFuelMeterLen;

         m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin, lMeterHight, lAbsSpeedLen, 54, 56 );
         if( lDirSpeedLen < 0 )
         {
            m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin+lMeterHight, lMeterHight, -lDirSpeedLen, 44, 56 );
         }
         else
         {
            m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin+lMeterHight, lMeterHight, lDirSpeedLen, 54, 56 );
         }
         m3DView.DrawHorizontalMeter( lXRes-lXMargin-lFuelMeterLen, lFuelMeterLen, lYMargin, lMeterHight*2, lFuelLen, 54, (lFuelLevel<0.20)?35:56 );
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Cockpit meters rendering crashed - continue
   }

   // STAGE 7: Weapon display
   __try {
      MR_SpriteHandle* lWeaponSprite = NULL;
      int              lWeaponSpriteIndex = 0;

      if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::eMissile )
      {
         lWeaponSprite = mMissileLevel;
         if( lWeaponSprite != NULL )
         {
            lWeaponSpriteIndex = pViewingCharacter->GetMissileRefillLevel( lWeaponSprite->GetSprite()->GetNbItem() );
         }
      }
      else if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::eMine )
      {
         lWeaponSprite = mMineDisp;
         lWeaponSpriteIndex = pViewingCharacter->GetMineCount();

         if( lWeaponSpriteIndex > 0 )
         {
            lWeaponSpriteIndex = ((lWeaponSpriteIndex-1)*2)+1;
            if( (pTime >> 9)&1 )
            {
               lWeaponSpriteIndex++;
            }
         }
      }
      else if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::ePowerUp )
      {
         lWeaponSprite = mPowerUpDisp;
         if( lWeaponSprite != NULL )
         {
            lWeaponSpriteIndex = pViewingCharacter->GetPowerUpFraction(4);
            if( lWeaponSpriteIndex == 0 )
            {
               lWeaponSpriteIndex = pViewingCharacter->GetPowerUpCount();
            }
            else
            {
               lWeaponSpriteIndex = 9-lWeaponSpriteIndex;
            }
         }
      }

      if( lWeaponSprite != NULL )
      {
         int lXRes = m3DView.GetXRes();
         int lYRes = m3DView.GetYRes();
         int lMissileScaling = 1+(310/lXRes);
         lWeaponSprite->GetSprite()->Blt( lXRes, lYRes/16, &m3DView, MR_Sprite::eRight, MR_Sprite::eTop, lWeaponSpriteIndex, lMissileScaling );
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Weapon rendering crashed - continue
   }

   // STAGE 8: Map rendering (minimap in top-left corner)
   __try {
      if( pSession != NULL && pSession->GetMap() != NULL && mHoverIcons != NULL )
      {
         const MR_Sprite* lHoverIcons = mHoverIcons->GetSprite();
         const MR_Sprite* lMapSprite = pSession->GetMap();
         
         int lMapScaling   = 1+(3*lMapSprite->GetItemHeight()/m3DView.GetYRes());
         int lIconsScaling = 1+(14*lHoverIcons->GetItemHeight()/m3DView.GetYRes());
         int lMargin       = lHoverIcons->GetItemHeight()/lIconsScaling;

         // Draw minimap in top-left corner
         lMapSprite->Blt( lMargin, lMargin, &m3DView, MR_Sprite::eLeft, MR_Sprite::eTop, 0, lMapScaling );

         // Display player icons on the map
         int lNbPlayers = pSession->GetNbPlayers();
         int lNbIcons   = lHoverIcons->GetNbItem();

         for( int lCounter = 0; (lCounter<20)&&(lNbPlayers>0); lCounter++ )
         {
            const MR_MainCharacter* lPlayer = pSession->GetPlayer( lCounter );

            if( lPlayer != NULL )
            {
               lNbPlayers--;

               int lX = lPlayer->mPosition.mX;
               int lY = lPlayer->mPosition.mY;
      
               pSession->ConvertMapCoordinate( lX, lY, lMapScaling );

               lHoverIcons->Blt( lX+lMargin, lY+lMargin, &m3DView, MR_Sprite::eCenter, MR_Sprite::eCenter, ( lPlayer->GetHoverId() )%lNbIcons, lIconsScaling );
            }
         }
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Map rendering crashed - continue without map
   }

   return;
}

/*  #### DISABLED SECTION START ####
   // ORIGINAL FULL 3D RENDERING CODE (DISABLED - CRASHES EVERY FRAME)
   // Keeping for reference when debugging
   
   MR_3DCoordinate lCameraPos;
   MR_Angle        lOrientation    = pViewingCharacter->mOrientation;
   int             lRoom           = pViewingCharacter->mRoom;
   double          lAbsSpeedRatio  = pViewingCharacter->GetAbsoluteSpeed();

   if( mCockpitView )
   {
      lOrientation   = pViewingCharacter->GetCabinOrientation();   
      lCameraPos.mX  = pViewingCharacter->mPosition.mX - 256*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY - 256*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1050;
   }

   else
   {
      int lDist = 3400;

      lOrientation   = pViewingCharacter->mOrientation;   

      
      if( pTime < -3000 )
      {         
         int lFactor = (-pTime-3000)*2/3;
         lOrientation = MR_NORMALIZE_ANGLE( lOrientation+lFactor*4096/11000 );
         lDist += lFactor;
      }
      
      lCameraPos.mX  = pViewingCharacter->mPosition.mX -lDist*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY -lDist*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1700;

      if( mLastCameraPosValid )
      {
         lCameraPos.mX = (3*lCameraPos.mX + mLastCameraPos.mX )/4;
         lCameraPos.mY = (3*lCameraPos.mY + mLastCameraPos.mY )/4;
         lCameraPos.mZ = (2*lCameraPos.mZ + mLastCameraPos.mZ )/3;
      }
   }

   mLastCameraPos = lCameraPos;
   mLastCameraPosValid = TRUE;


   m3DView.SetupCameraPosition( lCameraPos,
                                lOrientation,
                                mScroll        );



   MR_SAMPLE_START( Clear, "ClearScreen" );

   // Clear background
   if( pBackImage == NULL )
   {
      m3DView.Clear( 0 ); // Will have to be replace by a bitmapped background
   }
   else
   {
      m3DView.RenderBackground( pBackImage );
   }

   MR_SAMPLE_END( Clear);
   MR_SAMPLE_START( ClearZ, "ClearZScreen" );

   m3DView.ClearZ();

   MR_SAMPLE_END( ClearZ );



   int        lCounter;


   // Floor and ceiling drawing
   MR_SAMPLE_START( FloorRendering, "Floor Rendering" );


   int                 lTotalSections = lLevel->GetNbVisibleSurface( lRoom );
   const MR_SectionId* lFloorList     = lLevel->GetVisibleFloorList( lRoom );
   const MR_SectionId* lCeilingList   = lLevel->GetVisibleCeilingList( lRoom );

   for( lCounter = 0; lCounter < lTotalSections; lCounter++ )
   {
      // Draw the floor
      RenderFloorOrCeiling( lLevel, lFloorList[ lCounter ], TRUE, pTime );

      // Render the ceiling
      RenderFloorOrCeiling( lLevel, lCeilingList[ lCounter ], FALSE, pTime );
   
   }
  
   MR_SAMPLE_END( FloorRendering );

   // Draw the walls and features of the visibles rooms
   MR_SAMPLE_START( WallRendering, "Wall Rendering" );

   int        lRoomCount;
   const int* lRoomList  = lLevel->GetVisibleZones( lRoom, lRoomCount );

   for( lCounter = -1; lCounter < lRoomCount; lCounter++ )
   {
      int      lRoomId;
      
      if( lCounter == -1 )
      {
         lRoomId = lRoom;
      }
      else
      {
         lRoomId = lRoomList[ lCounter ];
      }

      // Draw all the features

      int lNbFeature = lLevel->GetFeatureCount( lRoomId );

      for( int lCounter2 = 0; lCounter2 < lNbFeature; lCounter2++ )
      {
         RenderFeatureWalls( lLevel, lLevel->GetFeature( lRoomId, lCounter2 ), pTime );
      }

      RenderRoomWalls( lLevel, lRoomId, pTime );
   }

   MR_SAMPLE_END( WallRendering );

   // NOTE: Actor rendering has been consolidated into a single pass above (STAGE 5)
   // The duplicate actor rendering loop that was here has been removed to prevent:
   // 1. Double-rendering of actors causing flashing artifacts
   // 2. Use of stale room list variables
   // 3. Lack of exception handling and bounds checking on second pass

   // Display cockpit
   int lXRes = m3DView.GetXRes();
   int lYRes = m3DView.GetYRes();

   int lSpeedMeterLen = lXRes/2;
   int lFuelMeterLen  = lXRes/4;
   int lMeterHight    = lYRes/32;
   int lXMargin       = lXRes/32;
   int lYMargin       = lMeterHight;
   
   int    lAbsSpeedLen  = lAbsSpeedRatio*lSpeedMeterLen;
   int    lDirSpeedLen  = pViewingCharacter->GetDirectionalSpeed()*lSpeedMeterLen;
   double lFuelLevel    = pViewingCharacter->GetFuelLevel();
   int    lFuelLen      = lFuelLevel*lFuelMeterLen;

   m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin, lMeterHight, lAbsSpeedLen, 54, 56 );
   if( lDirSpeedLen < 0 )
   {
      m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin+lMeterHight, lMeterHight, -lDirSpeedLen, 44, 56 );
   }
   else
   {
      m3DView.DrawHorizontalMeter( lXMargin, lSpeedMeterLen, lYMargin+lMeterHight, lMeterHight, lDirSpeedLen, 54, 56 );
   }
   m3DView.DrawHorizontalMeter( lXRes-lXMargin-lFuelMeterLen, lFuelMeterLen, lYMargin, lMeterHight*2, lFuelLen, 54, (lFuelLevel<0.20)?35:56 );


   // MissileLevel
   MR_SpriteHandle* lWeaponSprite = NULL;
   int              lWeaponSpriteIndex = 0;

   if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::eMissile )
   {
      lWeaponSprite = mMissileLevel;
      lWeaponSpriteIndex = pViewingCharacter->GetMissileRefillLevel( mMissileLevel->GetSprite()->GetNbItem() );
   }
   else if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::eMine )
   {
      lWeaponSprite = mMineDisp;
      lWeaponSpriteIndex = pViewingCharacter->GetMineCount();

      if( lWeaponSpriteIndex > 0 )
      {
         lWeaponSpriteIndex = ((lWeaponSpriteIndex-1)*2)+1;
         if( (pTime >> 9)&1 )
         {
            lWeaponSpriteIndex++;
         }
      }
   }
   else if( pViewingCharacter->GetCurrentWeapon() == MR_MainCharacter::ePowerUp )
   {
      lWeaponSprite = mPowerUpDisp;
      lWeaponSpriteIndex = pViewingCharacter->GetPowerUpFraction(4);
      if( lWeaponSpriteIndex == 0 )
      {
         lWeaponSpriteIndex = pViewingCharacter->GetPowerUpCount();
      }
      else
      {
         lWeaponSpriteIndex = 9-lWeaponSpriteIndex;
      }

   }

   if( lWeaponSprite != NULL )
   {
      int lMissileScaling = 1+(310/lXRes);

      lWeaponSprite->GetSprite()->Blt( lXRes, lYRes/16, &m3DView, MR_Sprite::eRight, MR_Sprite::eTop, lWeaponSpriteIndex, lMissileScaling );
   }

   // Map - Always try to render, even if GetMap is NULL (2D debug view should still show)
   if( pSession != NULL )
   {
      // Try to render the minimap sprite if available
      if( pSession->GetMap() !=NULL )
      {
         const MR_Sprite* lHoverIcons = mHoverIcons->GetSprite();

         int lMapScaling   = 1+(3*pSession->GetMap()->GetItemHeight()/lYRes);
         int lIconsScaling = 1+(14*lHoverIcons->GetItemHeight()/lYRes);
         int lMargin       = lHoverIcons->GetItemHeight()/lIconsScaling;

         pSession->GetMap()->Blt( lMargin, lMargin, &m3DView, MR_Sprite::eLeft, MR_Sprite::eTop, 0, lMapScaling );

         // display all the car icons
         int lNbPlayers = pSession->GetNbPlayers();
         int lNbIcons   = lHoverIcons->GetNbItem();

         for( int lCounter = 0; (lCounter<20)&&(lNbPlayers>0); lCounter++ )
         {
            int lX;
            int lY;
            
            const MR_MainCharacter* lPlayer = pSession->GetPlayer( lCounter );

            if( lPlayer != NULL )
            {
               lNbPlayers--;

               lX = lPlayer->mPosition.mX;
               lY = lPlayer->mPosition.mY;
      
               pSession->ConvertMapCoordinate( lX, lY, lMapScaling );

               lHoverIcons->Blt( lX+lMargin, lY+lMargin, &m3DView, MR_Sprite::eCenter, MR_Sprite::eCenter, ( lPlayer->GetHoverId() )%lNbIcons, lIconsScaling );
            }
         }   
      }
   }

   // Print text

   if( mBaseFont != NULL )
   {
      char lStrBuffer[120];

      const MR_Sprite* lFont = mBaseFont->GetSprite();

      // Display chat messages
      int lFontScaling = 1+(lFont->GetItemHeight()*12)/(lYRes);
      int lLineSpacing = lFont->GetItemHeight()/lFontScaling;

      int lXMargin  = 2*lFont->GetItemWidth()/lFontScaling;
      int lYMargin  = lYRes-lYRes/6-lLineSpacing;
      int lPrintLen = (lXRes-lXMargin)*4/(lFont->GetItemWidth()*3/lFontScaling);
                       // subject to div/0

      pSession->GetCurrentMessage( lStrBuffer );

      if( lStrBuffer[0] != 0 )
      {
         lFont->StrBlt( lXMargin, lYMargin, lStrBuffer, &m3DView, MR_Sprite::eLeft, MR_Sprite::eTop, lFontScaling );
      }
      lYMargin -= lLineSpacing;

      int lMaxDepth    = (mMoreMessages&&(mDispPlayers==0))?10:2;
      int lMessageLife = (mMoreMessages&&(mDispPlayers==0))?MR_CHAT_EXPIRATION*4:MR_CHAT_EXPIRATION;

      int lStackLevel = 0;
      int lLineLevel  = 0;
      while( pSession->GetMessageStack( lStackLevel++, lStrBuffer, lMessageLife )&&(lLineLevel<lMaxDepth)&&(lYMargin > lYRes/4) )
      {
         int lStrLen = strlen( lStrBuffer );

         if( lStrLen > lPrintLen )
         {
            lFont->StrBlt( lXMargin, lYMargin, lStrBuffer+lPrintLen, &m3DView, MR_Sprite::eLeft, MR_Sprite::eTop, lFontScaling );
            lYMargin -= lLineSpacing;
            lStrBuffer[ lPrintLen ] = 0;
            lLineLevel++;
         }
         lFont->StrBlt( lXMargin, lYMargin, lStrBuffer, &m3DView, MR_Sprite::eLeft, MR_Sprite::eTop, lFontScaling );
         lYMargin -= lLineSpacing;
         lLineLevel++;
      }
   }

   if( (mBaseFont != NULL)&&mDispPlayers )
   {

      int lNbResultAvail      = pSession->ResultAvaillable();
      int lNbPages            = (lNbResultAvail+NB_PLAYER_PAGE-1)/NB_PLAYER_PAGE;

      int  lCurrentPage = mDispPlayers = mDispPlayers%(lNbPages*2+1);

      if( mDispPlayers !=0 )
      {
         BOOL lShowHits = FALSE;

         lCurrentPage--;

         if( lCurrentPage >=lNbPages )
         {
            lShowHits = TRUE;
            lCurrentPage-=lNbPages;
         }

         // Display rank list
         int lFirstPlayer = lCurrentPage*NB_PLAYER_PAGE;
         int lLastPlayer  = min( lFirstPlayer+ NB_PLAYER_PAGE, lNbResultAvail );
         int lFontScaling = 1+(mBaseFont->GetSprite()->GetItemWidth()*25)/(lXRes);
         int lLineSpacing = mBaseFont->GetSprite()->GetItemHeight()/lFontScaling;

         int lCurrentLine = lYRes/4;

         // Display banner

         if( lShowHits )
         {
            mBaseFont->GetSprite()->StrBlt( lXRes/2, 
                                            lCurrentLine, 
                                            gHitTitle,
                                            &m3DView, 
                                            MR_Sprite::eCenter, 
                                            MR_Sprite::eTop, 
                                            lFontScaling );
         }
         else
         {
            mBaseFont->GetSprite()->StrBlt( lXRes/2, 
                                            lCurrentLine, 
                                            gRankTitle,
                                            &m3DView, 
                                            MR_Sprite::eCenter, 
                                            MR_Sprite::eTop, 
                                            lFontScaling );
         }
         lCurrentLine += lLineSpacing;

         // Display list
         for( int lCounter = lFirstPlayer; lCounter<lLastPlayer; lCounter++ )
         {
            char lBuffer[80];

            const char*       lPlayerName;
            int               lHoverId;
            BOOL              lConnected;
            int               lNbLap;
            MR_SimulationTime lFinishTime;
            MR_SimulationTime lBestLap;
            int               lNbFor;
            int               lNbAgain;

            if( lShowHits )
            {
               pSession->GetHitResult( lCounter, lPlayerName, lHoverId, lConnected, lNbFor, lNbAgain );
            }
            else
            {
               pSession->GetResult( lCounter, lPlayerName, lHoverId, lConnected, lNbLap, lFinishTime, lBestLap );
            }

            int lPlayerNameLen = min( strlen( lPlayerName ), 10 );

            if( lShowHits )
            {
               sprintf( lBuffer, 
                        gHitChart, 
                        lCounter+1,
                        lPlayerName,
                        lHoverId+1,
                        10-lPlayerNameLen,
                        "",
                        lNbFor,
                        lNbAgain );

            }
            else if( lNbLap == 0 )
            {
               sprintf( lBuffer, 
                  gFirstLapStr, 
                  lCounter+1,
                  lPlayerName,
                  lHoverId+1,
                  10-lPlayerNameLen,
                  "",
                  lConnected?'*':'-' );

            }
            else if( lNbLap == -1 )
            {
               sprintf( lBuffer, 
                  gChartFinish, 
                  lCounter+1,
                  lPlayerName,
                  lHoverId+1,
                  10-lPlayerNameLen,
                  "",
                  lFinishTime/60000, (lFinishTime%60000)/1000, (lFinishTime%1000)/10,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10,
                  lConnected?'*':'-' );

            }
            else
            {
               sprintf( lBuffer, 
                  gChart, 
                  lCounter+1,
                  lPlayerName,
                  lHoverId+1,
                  10-lPlayerNameLen,
                  "",
                  lNbLap+1,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10,
                  lConnected?'*':'-' );

            }

            mBaseFont->GetSprite()->StrBlt( lXRes/2, lCurrentLine, Ascii2Simple( lBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eTop, lFontScaling );
            lCurrentLine += lLineSpacing;
         }
      }
   }

   // DISABLED SECTION END: Original 3D rendering code

   if( mBaseFont != NULL )
   {
      // Display timers
      char lMainLineBuffer[80];
      char lLapLineBuffer[80];

      lLapLineBuffer[0] = 0;

      if( pTime< 0 )
      {
         pTime = -pTime;
         sprintf( lMainLineBuffer, gCountdownStr, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetTotalLap() );
      }
      else if( pViewingCharacter->GetTotalLap()<=pViewingCharacter->GetLap() )
      {
         MR_SimulationTime lTotalTime = pViewingCharacter->GetTotalTime();
         MR_SimulationTime lBestLap   = pViewingCharacter->GetBestLapDuration();
         
         // Race is finish
         if( pSession->GetNbPlayers() > 1 )
         {
            sprintf( lMainLineBuffer,    gFinishStr, lTotalTime/60000, (lTotalTime%60000)/1000, (lTotalTime%1000)/10, pSession->GetRank( pViewingCharacter ),pSession->GetNbPlayers() );
         }
         else
         {
            sprintf( lMainLineBuffer,    gFinishStrSingle, lTotalTime/60000, (lTotalTime%60000)/1000, (lTotalTime%1000)/10 );
         }
         sprintf( lLapLineBuffer, gBestLapStr,  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10 );


      }
      else if( pViewingCharacter->GetLap() == 0 )
      {
         // First lap
         sprintf( lMainLineBuffer, gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, 1, pViewingCharacter->GetTotalLap() );
         // sprintf( lLapLineBuffer, "Current lap %d.%02d.%02d", pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );

      }
      else if( pViewingCharacter->GetLastLapCompletion() > (pTime-8000) )
      {
         // Lap terminated less than 8 sec ago
         MR_SimulationTime lBestLap  = pViewingCharacter->GetBestLapDuration();
         MR_SimulationTime lLastLap  = pViewingCharacter->GetLastLapDuration();

         // More than one lap completed
         sprintf( lMainLineBuffer,  gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetLap()+1, pViewingCharacter->GetTotalLap() );
         sprintf( lLapLineBuffer, gLastLapStr,
                  lLastLap/60000, (lLastLap%60000)/1000, (lLastLap%1000)/10,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10);

      }
      else
      {
         MR_SimulationTime lBestLap     = pViewingCharacter->GetBestLapDuration();
         MR_SimulationTime lCurrentLap  = pTime-pViewingCharacter->GetLastLapCompletion();

         // More than one lap completed
         sprintf( lMainLineBuffer,    gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetLap()+1, pViewingCharacter->GetTotalLap() );
         sprintf( lLapLineBuffer, gCurLapStr,
                  lCurrentLap/60000, (lCurrentLap%60000)/1000, (lCurrentLap%1000)/10,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10);

      }
      
      mBaseFont->GetSprite()->StrBlt( lXRes/2, lYRes/16, Ascii2Simple( lMainLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eTop, 0 );
      mBaseFont->GetSprite()->StrBlt( lXRes/2, lYRes-1,  Ascii2Simple( lLapLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eBottom, 0 );

   }
   else
   {
   }

   MR_SAMPLE_END( ActorRendering );

}
*/


void MR_Observer::RenderRoomWalls( const MR_Level* pLevel, int lRoomId, MR_SimulationTime pTime )
{
   MR_PolygonShape* lSectionShape = pLevel->GetRoomShape( lRoomId );


   int lVertexCount = lSectionShape->VertexCount();

   // Draw the walls
   MR_3DCoordinate lP0;
   MR_3DCoordinate lP1;

   MR_Int32 lFloorLevel   = lSectionShape->ZMin();
   MR_Int32 lCeilingLevel = lSectionShape->ZMax();

   lP0.mX = lSectionShape->X( 0 );
   lP0.mY = lSectionShape->Y( 0 );

   for( int lVertex = 0; lVertex < lVertexCount; lVertex++ )
   {
      int lNext = lVertex+1;

      if( lNext == lVertexCount )
      {
         lNext = 0;
      }

      lP1.mX = lSectionShape->X( lNext );
      lP1.mY = lSectionShape->Y( lNext );

      MR_SurfaceElement* lElement = pLevel->GetRoomWallElement( lRoomId, lVertex );

      if( lElement != NULL )
      {
         int lNeighbor = pLevel->GetNeighbor( lRoomId, lVertex );

         if( lNeighbor == -1 )
         {
            lP0.mZ = lCeilingLevel;
            lP1.mZ = lFloorLevel;

            lElement->RenderWallSurface( &m3DView, lP0, lP1, pLevel->GetRoomWallLen( lRoomId, lVertex ), pTime );
         }
         else
         {
            MR_Int32 lNeighborFloor   = pLevel->GetRoomBottomLevel( lNeighbor );
            MR_Int32 lNeighborCeiling = pLevel->GetRoomTopLevel( lNeighbor );

            if( lFloorLevel < lNeighborFloor )
            {
               lP0.mZ = lNeighborFloor;
               lP1.mZ = lFloorLevel;

               lElement->RenderWallSurface( &m3DView, lP0, lP1, pLevel->GetRoomWallLen( lRoomId, lVertex ), pTime );
            }

            if( lCeilingLevel > lNeighborCeiling )
            {
               lP0.mZ = lCeilingLevel;
               lP1.mZ = lNeighborCeiling;

               lElement->RenderWallSurface( &m3DView, lP0, lP1, pLevel->GetRoomWallLen( lRoomId, lVertex ), pTime );
            }
         }
      }

      lP0.mX = lP1.mX;
      lP0.mY = lP1.mY;


   }

   delete lSectionShape;
}

void MR_Observer::RenderFeatureWalls( const MR_Level* pLevel, int lFeatureId, MR_SimulationTime pTime )
{
   MR_PolygonShape* lSectionShape = pLevel->GetFeatureShape( lFeatureId );


   int lVertexCount = lSectionShape->VertexCount();

   // Draw the walls
   MR_3DCoordinate lP0;
   MR_3DCoordinate lP1;

   lP0.mZ = lSectionShape->ZMax();

   lP1.mX = lSectionShape->X( 0 );
   lP1.mY = lSectionShape->Y( 0 );
   lP1.mZ = lSectionShape->ZMin();

   for( int lVertex = 0; lVertex < lVertexCount; lVertex++ )
   {
      int lNext = lVertex+1;

      if( lNext == lVertexCount )
      {
         lNext = 0;
      }

      lP0.mX = lSectionShape->X( lNext );
      lP0.mY = lSectionShape->Y( lNext );

      MR_SurfaceElement* lElement = pLevel->GetFeatureWallElement( lFeatureId, lVertex );

      if( lElement != NULL )
      {
         lElement->RenderWallSurface( &m3DView, lP0, lP1, pLevel->GetFeatureWallLen( lFeatureId, lVertex ), pTime );
      }

      lP1.mX = lP0.mX;
      lP1.mY = lP0.mY;
   }

   delete lSectionShape;
}



void MR_Observer::RenderFloorOrCeiling( const MR_Level* pLevel, const MR_SectionId& pSectionId, BOOL pFloor, MR_SimulationTime pTime )
{
   int                lCounter;

   MR_Int32           lLevel;
   int                lNbVertex;
   MR_2DCoordinate    lVertexList[ MR_MAX_POLYGON_VERTEX ];

   MR_PolygonShape*   lShape;
   MR_SurfaceElement* lElement;

   // Extract the surface geometry
   if( pSectionId.mType == MR_SectionId::eRoom )
   {
      lShape = pLevel->GetRoomShape( pSectionId.mId );
      if( pFloor )
      {
         lLevel   = lShape->ZMin();
         lElement = pLevel->GetRoomBottomElement( pSectionId.mId );
      }
      else
      {
         lLevel   = lShape->ZMax();
         lElement = pLevel->GetRoomTopElement( pSectionId.mId );
      }
   }
   else
   {
      lShape = pLevel->GetFeatureShape( pSectionId.mId );

      if( !pFloor )
      {
         lLevel   = lShape->ZMin();
         lElement = pLevel->GetFeatureBottomElement( pSectionId.mId );
      }
      else
      {
         lLevel   = lShape->ZMax();
         lElement = pLevel->GetFeatureTopElement( pSectionId.mId );
      }
   }


   if( lElement != NULL )
   {
      lNbVertex = lShape->VertexCount();

      for( lCounter = 0; lCounter<lNbVertex; lCounter++ )
      {
         lVertexList[ lCounter ].mX = lShape->X( lCounter );
         lVertexList[ lCounter ].mY = lShape->Y( lCounter );
      }
   
      lElement->RenderHorizontalSurface( &m3DView, lNbVertex, lVertexList, lLevel, !pFloor, pTime );
   }

   delete lShape;
}


void MR_Observer::RenderDebugDisplay( MR_VideoBuffer* pDest, const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage )
{
   // Debug view - displays wireframe and 2D debug information
   // This is only called when mCurrentMode == eDebugView (not during normal gameplay)
   
   int lXRes    = pDest->GetXRes();
   int lYRes    = pDest->GetYRes();
   int lYOffset = 0;

   switch( mSplitMode )
   {
      case eUpperSplit:
         lYRes /= 2;
         break;

      case eLowerSplit:
         lYRes /= 2;
         lYOffset = lYRes;
         break;
   }

   // Setup viewports for debug display
   mWireFrameView.Setup( pDest, 0, lYOffset, lXRes/2, lYRes, mApperture );
   m2DDebugView.Setup(   pDest, lXRes/2, lYOffset, lXRes/2, lYRes, mApperture );
   
   const MR_Level* lLevel = pSession->GetCurrentLevel();
   
   if( lLevel != NULL )
   {
      // Left side: wireframe view
      RenderWireFrameView( lLevel, pViewingCharacter );
      
      // Right side: 2D debug view  
      Render2DDebugView( pDest, lLevel, pViewingCharacter );
   }

}

// Separate helper to call Render3DView with SEH (no C++ objects requiring unwinding)
void MR_Observer::CallRender3DViewSafe( const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage )
{
   static int success_count = 0;
   
   // Try to render normally - this function has its own internal try/except blocks
   Render3DView( pSession, pViewingCharacter, pTime, pBackImage );
   success_count++;
   
}

void MR_Observer::RenderNormalDisplay( MR_VideoBuffer* pDest, const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage )
{
   MR_SAMPLE_CONTEXT( "RenderNormalDisplay" );

   int lXRes = pDest->GetXRes();
   int lYRes = pDest->GetYRes();
   int lYOffset = 0;
   int lYMargin_1024 = mYMargin_1024;


   switch( mSplitMode )
   {
      case eUpperSplit:
         lYRes /= 2;
         lYMargin_1024 -= 200;
         if( lYMargin_1024 < 0 )
         {
            lYMargin_1024 = 0;
         }

         break;

      case eLowerSplit:
         lYRes /= 2;
         lYOffset = lYRes;
         lYMargin_1024 -= 200;
         if( lYMargin_1024 < 0 )
         {
            lYMargin_1024 = 0;
         }
         break;
   }

   int lXMargin = (mXMargin_1024 * lXRes/1024)&0xFFFFFFFC; // rounded to 32 bit boundary for best performances
   int lYMargin = lYMargin_1024 * lYRes/1024;

   // Wrap Setup in try-catch to prevent crashes
   try {
      m3DView.Setup( pDest, lXMargin, lYOffset+lYMargin, lXRes-2*lXMargin, lYRes-2*lYMargin, mApperture );
      
      // CRITICAL FIX: Force memory synchronization after Setup to ensure buffer pointers are fresh
      // This prevents stale pointer issues when rendering resumes after menu interactions
      // Add a volatile barrier to prevent compiler optimization of the pointer refresh
      volatile MR_UInt8* ptrCheck = pDest->GetBuffer();
      if (ptrCheck != NULL) {
         // Access first byte to ensure pointer is valid and data is initialized
         // This forces cache coherency and prevents buffer corruption artifacts
         volatile int dummy = ptrCheck[0];
         (void)dummy;  // Prevent unused variable warning
      }
   }
   catch(...) {
      return;
   }

   // Clear screen if needed
   if( lXMargin > 0 )
   {

   }

   if( lYMargin > 0 )
   {
   }

   {
      CallRender3DViewSafe( pSession, pViewingCharacter, pTime, pBackImage );
   }

   // Display race timers at top and bottom of screen
   if( mBaseFont != NULL )
   {
      char lMainLineBuffer[80];
      char lLapLineBuffer[80];

      lLapLineBuffer[0] = 0;

      if( pTime< 0 )
      {
         pTime = -pTime;
         sprintf( lMainLineBuffer, gCountdownStr, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetTotalLap() );
      }
      else if( pViewingCharacter->GetTotalLap()<=pViewingCharacter->GetLap() )
      {
         MR_SimulationTime lTotalTime = pViewingCharacter->GetTotalTime();
         MR_SimulationTime lBestLap   = pViewingCharacter->GetBestLapDuration();
         
         // Race is finish
         if( pSession->GetNbPlayers() > 1 )
         {
            sprintf( lMainLineBuffer,    gFinishStr, lTotalTime/60000, (lTotalTime%60000)/1000, (lTotalTime%1000)/10, pSession->GetRank( pViewingCharacter ),pSession->GetNbPlayers() );
         }
         else
         {
            sprintf( lMainLineBuffer,    gFinishStrSingle, lTotalTime/60000, (lTotalTime%60000)/1000, (lTotalTime%1000)/10 );
         }
         sprintf( lLapLineBuffer, gBestLapStr,  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10 );


      }
      else if( pViewingCharacter->GetLap() == 0 )
      {
         // First lap
         sprintf( lMainLineBuffer, gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, 1, pViewingCharacter->GetTotalLap() );
         // sprintf( lLapLineBuffer, "Current lap %d.%02d.%02d", pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );

      }
      else if( pViewingCharacter->GetLastLapCompletion() > (pTime-8000) )
      {
         // Lap terminated less than 8 sec ago
         MR_SimulationTime lBestLap  = pViewingCharacter->GetBestLapDuration();
         MR_SimulationTime lLastLap  = pViewingCharacter->GetLastLapDuration();

         // More than one lap completed
         sprintf( lMainLineBuffer,  gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetLap()+1, pViewingCharacter->GetTotalLap() );
         sprintf( lLapLineBuffer, gLastLapStr,
                  lLastLap/60000, (lLastLap%60000)/1000, (lLastLap%1000)/10,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10);

      }
      else
      {
         MR_SimulationTime lBestLap     = pViewingCharacter->GetBestLapDuration();
         MR_SimulationTime lCurrentLap  = pTime-pViewingCharacter->GetLastLapCompletion();

         // More than one lap completed
         sprintf( lMainLineBuffer,    gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, pViewingCharacter->GetLap()+1, pViewingCharacter->GetTotalLap() );
         sprintf( lLapLineBuffer, gCurLapStr,
                  lCurrentLap/60000, (lCurrentLap%60000)/1000, (lCurrentLap%1000)/10,
                  lBestLap/60000, (lBestLap%60000)/1000, (lBestLap%1000)/10);

      }
      
      // Render timer text at top and bottom center of screen
      mBaseFont->GetSprite()->StrBlt( lXRes/2, lYRes/16, Ascii2Simple( lMainLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eTop, 0 );
      mBaseFont->GetSprite()->StrBlt( lXRes/2, lYRes-1,  Ascii2Simple( lLapLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eBottom, 0 );

      // In-race chat: the line currently being typed (AddMessageKey/GetCurrentMessage,
      // real on Windows via MR_NetworkSession's override, a no-op stub on Linux) plus
      // the scrollback (AddMessage/GetMessageStack, real and shared on both platforms).
      // This used to exist only in Render3DView's "DISABLED SECTION" -- dead, commented-
      // out code that this live function never called into, so nothing was ever drawn
      // on screen for chat during a race on either platform. Reimplemented here instead.
      const MR_Sprite* lChatFont = mBaseFont->GetSprite();
      const int lChatFontScaling = 1 + (lChatFont->GetItemHeight()*12)/lYRes;
      const int lChatLineSpacing = lChatFont->GetItemHeight()/lChatFontScaling;
      const int lChatXMargin = 2*lChatFont->GetItemWidth()/lChatFontScaling;
      int lChatYMargin = lYRes - lYRes/6 - lChatLineSpacing;
      // The original (disabled) version of this logic divided directly by
      // (ItemWidth*3/lChatFontScaling), flagged in its own comment as "subject to
      // div/0" -- at small enough resolutions/scalings that inner division
      // truncates to exactly 0. Clamp it before using it as a divisor. Plain
      // comparisons rather than std::max: this file's precompiled header pulls in
      // windows.h without NOMINMAX, which macro-replaces any bare "max(" token
      // sequence -- including std::max(...) -- and breaks the parse (the same
      // issue hit RaceServer's own build previously; see its commit history).
      int lChatCharWidth = lChatFont->GetItemWidth()*3/lChatFontScaling;
      if( lChatCharWidth < 1 ) lChatCharWidth = 1;
      int lChatPrintLen = (lXRes-lChatXMargin)*4/lChatCharWidth;
      if( lChatPrintLen < 1 ) lChatPrintLen = 1;

      char lChatLineBuffer[120];
      pSession->GetCurrentMessage( lChatLineBuffer );
      if( lChatLineBuffer[0] != 0 )
      {
         lChatFont->StrBlt( lChatXMargin, lChatYMargin, lChatLineBuffer, &m3DView,
                            MR_Sprite::eLeft, MR_Sprite::eTop, lChatFontScaling );
      }
      lChatYMargin -= lChatLineSpacing;

      constexpr int kChatMaxLines = 4;
      constexpr int kChatMessageLifeSeconds = 20;
      int lChatStackLevel = 0;
      int lChatLineCount = 0;
      while( pSession->GetMessageStack( lChatStackLevel++, lChatLineBuffer, kChatMessageLifeSeconds ) &&
             lChatLineCount < kChatMaxLines && lChatYMargin > lYRes/4 )
      {
         const int lChatStrLen = static_cast<int>( strlen( lChatLineBuffer ) );
         if( lChatStrLen > lChatPrintLen )
         {
            lChatFont->StrBlt( lChatXMargin, lChatYMargin, lChatLineBuffer+lChatPrintLen, &m3DView,
                               MR_Sprite::eLeft, MR_Sprite::eTop, lChatFontScaling );
            lChatYMargin -= lChatLineSpacing;
            lChatLineBuffer[ lChatPrintLen ] = 0;
            ++lChatLineCount;
         }
         lChatFont->StrBlt( lChatXMargin, lChatYMargin, lChatLineBuffer, &m3DView,
                            MR_Sprite::eLeft, MR_Sprite::eTop, lChatFontScaling );
         lChatYMargin -= lChatLineSpacing;
         ++lChatLineCount;
      }
   }
}

void MR_Observer::PlaySounds( const MR_Level* pLevel, MR_MainCharacter* pViewingCharacter )
{
   // Play the sound of all moving elements around
   try
   {
      if( pLevel == NULL || pViewingCharacter == NULL )
      {
         return;
      }

      int lCurrentRoom   = pViewingCharacter->mRoom;
      int lNeighborCount = pLevel->GetRoomVertexCount( lCurrentRoom );

      for( int lCounter = -1; lCounter < lNeighborCount; lCounter++ )
      {
         try
         {
            int      lRoomId;
            
            if( lCounter == -1 )
            {
               lRoomId = lCurrentRoom;
            }
            else
            {
               lRoomId = pLevel->GetNeighbor( lCurrentRoom, lCounter );
            }

            if( lRoomId != -1 )
            {
               MR_FreeElementHandle lHandle = pLevel->GetFirstFreeElement( lRoomId );

               while( lHandle != NULL )
               {
                  try
                  {
                     MR_FreeElement* lElement = MR_Level::GetFreeElement( lHandle );

                     if( lElement != NULL && lElement != pViewingCharacter )
                     {
                        double lXDist = pViewingCharacter->mPosition.mX-lElement->mPosition.mX;
                        double lYDist = pViewingCharacter->mPosition.mY-lElement->mPosition.mY;

                        int lDB = -(int)sqrt( lXDist*lXDist+lYDist*lYDist )/15;

                        try
                        {
                           lElement->PlayExternalSounds( lDB, 0 );
                        }
                        catch(...)
                        {
                           // Silently ignore sound play errors
                        }
                     }
                  }
                  catch(...)
                  {
                     // Silently ignore element sound errors
                  }

                  lHandle = MR_Level::GetNextFreeElement( lHandle );
               }
            }
         }
         catch(...)
         {
            // Silently ignore room sound errors
         }
      }

      try
      {
         pViewingCharacter->PlayInternalSounds();
      }
      catch(...)
      {
         // Silently ignore viewing character sound errors
      }
   }
   catch(...)
   {
      // Silently ignore all sound errors
   }
}

void MR_Observer::PlaySoundsSafe( const MR_Level* pLevel, MR_MainCharacter* pViewingCharacter )
{
   // SEH wrapper for PlaySounds to catch OS-level exceptions
   __try {
      PlaySounds( pLevel, pViewingCharacter );
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Silently catch and continue on any crash in sound system
   }
}




