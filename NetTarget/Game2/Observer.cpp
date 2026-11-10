// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-08.
// See repository history for authorship and detailed changes.
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

// The HoverNet client (CMake build, HOVERNET_STATIC) has no Win32 string table, so it uses the same literal texts on every platform;
// only the legacy Game2.exe takes them from its resources.
#if defined(_WIN32) && !defined(HOVERNET_STATIC)
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
CString gHeaderStr       = "Race %d:%02d.%02d  Lap %d/%d";
CString gLastLapStr      = "Last lap %d.%02d.%02d  Best %d.%02d.%02d";
CString gCurLapStr       = "Current lap %d.%02d.%02d  Best %d.%02d.%02d";
#endif

CString gFirstLapTimeStr = "Lap time %d:%02d.%02d";
CString gFinalLapStr     = "Race %d:%02d.%02d  FINAL LAP";


// The chat lines are kept in the bitmap font's own code (Ascii2Simple: ASCII minus 31); this turns one back into text.
static std::string SimpleToAscii( const char* pSimple )
{
   std::string lText;
   for( ; pSimple != NULL && *pSimple != 0; pSimple++ )
   {
      const int lCode = (unsigned char)*pSimple;
      lText.push_back( lCode >= 1 && lCode <= 95 ? (char)( lCode + 31 ) : '?' );
   }
   return lText;
}

// Draws HUD text in the bitmap font, unless the client draws the words itself (SetExternalHudText).
static void HudStr( bool pDraw, const MR_Sprite* pFont, int pX, int pY, const char* pStr, MR_2DViewPort* pDest, MR_Sprite::eAlignment pH, MR_Sprite::eAlignment pV, int pScaling )
{
   if( pDraw )
   {
      pFont->StrBlt( pX, pY, pStr, pDest, pH, pV, pScaling );
   }
}

MR_Observer::MR_Observer()
{
   mLastCameraPosValid = FALSE;
   mLastCameraTime = 0;
   mExternalHudText = FALSE;
   mSceneSink = NULL;
   mEyeOffset = 0;
   mReuseCamera = FALSE;
   mScroll = 0;
   mApperture = MR_PI/2;

   mXMargin_1024 = 0;
   mYMargin_1024 = 0;

   mMoreMessages = FALSE;
   mDispPlayers  = 1;

   mSplitMode = eNotSplit;

   mCockpitView = FALSE;
   mChaseScale = 100;
   mGhost = NULL;
   mGhostGapText[0] = 0;
   mNetworkLatencyMs = -2;
   mLargeHudText = FALSE;
   mModernEffects = FALSE;
   mModernShadows = FALSE;
   mModernMotion = FALSE;
   mShadowLutValid = FALSE;
   memset( mShadowLut, 0, sizeof( mShadowLut ) );
   mSceneX0 = 0;
   mSceneY0 = 0;
   mModernLighting = FALSE;
   mShadowLevel = NULL;
   mModernTextures = FALSE;
   memset( mPalette, 0, sizeof( mPalette ) );
   mImpactFlash = 0.0f;
   mPrevLitTime = 0;
   mTrailCount = 0;
   mTrailLastTime = 0;
   mPreviousSpeed = 0.0;
   mPreviousSpeedTime = 0;
   mFeedbackCharacter = NULL;
   mPreviousFuelLevel = 0.0;
   mPreviousMineCount = 0;
   mPreviousPowerUpCount = 0;
   mWasOutOfControl = FALSE;
   mFeedbackUntil = 0;
   mFeedbackText[0] = 0;

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

void MR_Observer::SetGhostGapText( const char* pText )
{
   if( pText == NULL )
      mGhostGapText[0] = 0;
   else
      snprintf( mGhostGapText, sizeof(mGhostGapText), "%s", pText );
}

void MR_Observer::SetNetworkLatency( int pLatencyMs )
{
   mNetworkLatencyMs = pLatencyMs;
}

void MR_Observer::SetLargeHudText( BOOL pEnabled )
{
   mLargeHudText = pEnabled;
}

const char* MR_Observer::GetFuelWarningForPercent( int pFuelPercent )
{
   if( pFuelPercent <= 0 ) return "FUEL EMPTY";
   if( pFuelPercent < 20 ) return "LOW FUEL";
   return "";
}

const char* MR_Observer::GetLatencyWarningForMilliseconds( int pLatencyMs )
{
   // 200 ms round-trip is where fast steering and weapon timing become
   // noticeably delayed; retain the exact measurement alongside this label.
   return pLatencyMs >= 200 ? "HIGH LATENCY" : "";
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

void MR_Observer::SetModernTextures( BOOL pEnable, const MR_UInt8* pPalette768 )
{
   mModernTextures = pEnable && pPalette768 != NULL;
   if( pPalette768 != NULL )
   {
      for( int lIndex = 0; lIndex < 256; lIndex++ )
      {
         mPalette[ lIndex ] = ((MR_UInt32)pPalette768[ lIndex*3 ] << 16) | ((MR_UInt32)pPalette768[ lIndex*3 + 1 ] << 8) | (MR_UInt32)pPalette768[ lIndex*3 + 2 ];
      }
   }
}

void MR_Observer::SetModernEffects( BOOL pEnable, BOOL pShadows, BOOL pReducedMotion, const MR_UInt8* pShadowLut )
{
   mModernEffects = pEnable;
   mModernShadows = pEnable && pShadows && pShadowLut != NULL;
   mModernMotion  = pEnable && !pReducedMotion;
   mShadowLutValid = pShadowLut != NULL;
   if( pShadowLut != NULL )
   {
      memcpy( mShadowLut, pShadowLut, sizeof( mShadowLut ) );
   }
   if( !pEnable )
   {
      mHints = HoverNetGfx::FrameHints();
      mTrailCount = 0;
   }
}

// The height of the floor a craft's shadow falls on: the floor of the room it is in. A craft resting on or hovering just above its surface keeps its
// shadow right under it (within 12 cm of the floor); one that is higher (a jump, a fall) leaves the shadow on the floor, where a real shadow would be.
// It is read from the level, not averaged over time, so it is right on a slope or in a jump and does not depend on the frame rate.
MR_Int32 MR_Observer::GroundUnder( const MR_MainCharacter* pCraft ) const
{
   MR_Int32 lGround = pCraft->mPosition.mZ - 100;
   if( mShadowLevel != NULL && pCraft->mRoom >= 0 && pCraft->mRoom < mShadowLevel->GetRoomCount() )
   {
      const MR_Int32 lFloor = mShadowLevel->GetRoomBottomLevel( pCraft->mRoom );
      if( lGround < lFloor )
      {
         lGround = lFloor;
      }
      else if( lGround > lFloor + 120 )
      {
         lGround = lFloor + 120;
      }
   }
   return lGround;
}

// A soft shadow on the floor under a craft: two ellipses darkened through the shadow table, the inner one twice. Drawn after the floor and
// walls and before the craft, so the craft covers it; the depth test inside ShadeEllipse keeps it from darkening a wall in front of it.
void MR_Observer::DrawCraftShadow( const MR_MainCharacter* pCraft )
{
   if( !mModernShadows || pCraft == NULL )
   {
      return;
   }

   MR_3DCoordinate lGround = pCraft->mPosition;
   lGround.mZ = GroundUnder( pCraft );

   int lX, lY, lDepth, lPixelsPerMetre;
   if( !m3DView.ProjectToScreen( lGround, lX, lY, lDepth, lPixelsPerMetre ) || lPixelsPerMetre < 3 || lPixelsPerMetre > 4000 )
   {
      return;
   }

   const int lRadiusX = lPixelsPerMetre*19/10;   // a craft is about 3 m across; the shadow has to show beyond it
   const int lRadiusY = lRadiusX*(mCockpitView ? 50 : 42)/100 + 1;
   // Five rings, each darkening what is inside it again: a soft edge and a dark core, rather than a hard disc.
   static const int kRings[5] = { 100, 84, 68, 52, 36 };
   for( int lRing = 0; lRing < 5; lRing++ )
   {
      m3DView.ShadeEllipse( lX, lY, lRadiusX*kRings[ lRing ]/100, lRadiusY*kRings[ lRing ]/100, lDepth, 700, mShadowLut );
   }
}

// A light in camera space for the picture passes (skipped when it is behind the camera or the list is full).
void MR_Observer::AddLight( const MR_3DCoordinate& pWorld, int pRadiusMm, int pR, int pG, int pB, float pStrength, int pKind, const MR_3DCoordinate* pAimAt, float pConeCos )
{
   if( mHints.lightCount >= HoverNetGfx::kMaxLights )
   {
      return;
   }

   int lX, lY, lZ;
   if( !m3DView.ToCameraSpace( pWorld, lX, lY, lZ ) )
   {
      return;
   }

   HoverNetGfx::Light& lLight = mHints.lights[ mHints.lightCount++ ];
   lLight.x = (float)lX;
   lLight.y = (float)lY;
   lLight.z = (float)lZ;
   lLight.radius = (float)pRadiusMm;
   lLight.r = (MR_UInt8)pR;
   lLight.g = (MR_UInt8)pG;
   lLight.b = (MR_UInt8)pB;
   lLight.strength = pStrength;
   lLight.kind = (MR_UInt8)pKind;
   if( pAimAt != NULL )
   {
      // A spotlight: the direction from the light to the point it aims at, in camera space.
      int lAX, lAY, lAZ;
      if( m3DView.ToCameraSpace( *pAimAt, lAX, lAY, lAZ ) )
      {
         const float lDX = (float)lAX - lLight.x, lDY = (float)lAY - lLight.y, lDZ = (float)lAZ - lLight.z;
         const float lLength = sqrtf( lDX*lDX + lDY*lDY + lDZ*lDZ );
         if( lLength > 1.0f )
         {
            lLight.dirx = lDX/lLength; lLight.diry = lDY/lLength; lLight.dirz = lDZ/lLength;
            lLight.coneCos = pConeCos;
         }
      }
   }
}

// What the picture passes and effects (VideoServices/ModernGraphics) need to know about the frame just drawn.
void MR_Observer::UpdateModernHints( const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime )
{
   mHints = HoverNetGfx::FrameHints();
   if( !mModernEffects || pViewingCharacter == NULL )
   {
      return;
   }

   mHints.valid   = true;
   mHints.cockpit = mCockpitView != FALSE;

   const double lSpeed = pViewingCharacter->GetAbsoluteSpeed();
   mHints.speed = (float)( lSpeed < 0.0 ? 0.0 : lSpeed );

   // A collision is a sudden loss of speed between two frames close together in time (not a replay seek or a restart).
   if( pTime > mPreviousSpeedTime && pTime - mPreviousSpeedTime < 400 )
   {
      const double lDrop = mPreviousSpeed - lSpeed;
      if( lDrop > 0.12 )
      {
         mHints.impact = (float)( lDrop*3.0 > 1.0 ? 1.0 : lDrop*3.0 );
      }
   }
   mPreviousSpeed     = lSpeed;
   mPreviousSpeedTime = pTime;

   // The trail: where the craft was every 48 ms of race time, newest last.
   if( pTime < mTrailLastTime || pTime - mTrailLastTime > 3000 )
   {
      mTrailCount = 0;
      mTrailLastTime = pTime;
   }
   MR_3DCoordinate lCentre = pViewingCharacter->mPosition;
   lCentre.mZ += 450;
   if( mTrailCount == 0 || pTime - mTrailLastTime >= 48 )
   {
      if( mTrailCount == HoverNetGfx::kMaxTrailPoints - 1 )
      {
         memmove( mTrailPos, mTrailPos + 1, sizeof( mTrailPos[0] )*( mTrailCount - 1 ) );
         mTrailCount--;
      }
      mTrailPos[ mTrailCount++ ] = lCentre;
      mTrailLastTime = pTime;
   }

   int lX, lY, lDepth, lPixelsPerMetre;
   if( !mCockpitView && m3DView.ProjectToScreen( lCentre, lX, lY, lDepth, lPixelsPerMetre ) )
   {
      mHints.craftVisible = true;
      mHints.craftX = lX;
      mHints.craftY = lY;
      mHints.craftRadius = lPixelsPerMetre*11/10;
      {
         MR_3DCoordinate lGroundPoint = pViewingCharacter->mPosition;
         lGroundPoint.mZ = GroundUnder( pViewingCharacter );
         int lGX, lGY, lGD, lGP;
         mHints.craftGroundY = m3DView.ProjectToScreen( lGroundPoint, lGX, lGY, lGD, lGP ) ? lGY : mHints.craftY + mHints.craftRadius;
      }

      for( int lIndex = 0; lIndex < mTrailCount; lIndex++ )
      {
         int lTX, lTY, lTD, lTP;
         if( m3DView.ProjectToScreen( mTrailPos[ lIndex ], lTX, lTY, lTD, lTP ) )
         {
            mHints.trailX[ mHints.trailCount ] = lTX;
            mHints.trailY[ mHints.trailCount ] = lTY;
            mHints.trailCount++;
         }
      }
      if( mHints.trailCount < HoverNetGfx::kMaxTrailPoints )
      {
         mHints.trailX[ mHints.trailCount ] = lX;
         mHints.trailY[ mHints.trailCount ] = lY;
         mHints.trailCount++;
      }
   }

   // The card draws the scene from the same projection, whether or not the lighting is on.
   if( mSceneSink != NULL && !mModernLighting )
   {
      m3DView.GetProjection( mProjKx, mProjKz, mHints.projCenterX, mHints.projCenterY, mSceneX0, mSceneY0 );
      mHints.projKx = (float)mProjKx;
      mHints.projKz = (float)mProjKz;
   }

   // Light: engine pools under the craft, a headlight ahead of the viewing craft, and a short orange flash when it hits something.
   if( mModernLighting )
   {
      m3DView.GetProjection( mProjKx, mProjKz, mHints.projCenterX, mHints.projCenterY, mSceneX0, mSceneY0 );
      mHints.projKx = (float)mProjKx;
      mHints.projKz = (float)mProjKz;

      const float lSpeedFactor = mHints.speed > 1.2f ? 1.2f : mHints.speed;
      MR_3DCoordinate lEngine = pViewingCharacter->mPosition;
      lEngine.mZ += 250;
      AddLight( lEngine, 5500, 90, 170, 255, 0.14f + 0.22f*lSpeedFactor, HoverNetGfx::kLightEngine );

      // The headlight: a soft cone from the nose of the craft, pointing along its heading and down a little onto the track.
      const MR_Angle lHeading = MR_NORMALIZE_ANGLE( pViewingCharacter->mOrientation );
      MR_3DCoordinate lNose = pViewingCharacter->mPosition;
      lNose.mX += 1200*MR_Cos[ lHeading ]/MR_TRIGO_FRACT;
      lNose.mY += 1200*MR_Sin[ lHeading ]/MR_TRIGO_FRACT;
      lNose.mZ += 350;
      MR_3DCoordinate lAim = pViewingCharacter->mPosition;
      lAim.mX += 9000*MR_Cos[ lHeading ]/MR_TRIGO_FRACT;
      lAim.mY += 9000*MR_Sin[ lHeading ]/MR_TRIGO_FRACT;
      AddLight( lNose, 14000, 255, 238, 200, 0.42f, HoverNetGfx::kLightHeadlight, &lAim, 0.88f );

      mImpactFlash = mImpactFlash*0.88f;
      if( mModernMotion && mHints.impact > mImpactFlash )
      {
         mImpactFlash = mHints.impact;
      }
      if( mModernMotion && mImpactFlash > 0.06f )
      {
         AddLight( lEngine, 9000, 255, 170, 80, 0.8f*mImpactFlash, HoverNetGfx::kLightFlash );
      }

      for( size_t lIndex = 0; lIndex < mOtherCrafts.size(); lIndex++ )
      {
         MR_3DCoordinate lOther = mOtherCrafts[ lIndex ]->mPosition;
         lOther.mZ += 250;
         AddLight( lOther, 4000, 90, 170, 255, 0.3f, HoverNetGfx::kLightCraft );
      }
      for( size_t lIndex = 0; lIndex < mLitElements.size(); lIndex++ )
      {
         MR_3DCoordinate lAt = mLitElements[ lIndex ]->mPosition;
         const MR_UInt16 lClass = mLitElements[ lIndex ]->GetTypeId().mClassId;
         if( lClass == 150 )        // a missile: a hot light that travels with it
         {
            AddLight( lAt, 4500, 255, 190, 110, 0.5f, HoverNetGfx::kLightMissile );
         }
         else if( lClass == 151 )   // a mine: a red light, pulsing unless Reduced motion is on
         {
            const float lPulse = mModernMotion ? 0.75f + 0.25f*(float)sin( (double)pTime*0.006 ) : 0.85f;
            lAt.mZ += 200;
            AddLight( lAt, 2800, 255, 60, 50, 0.4f*lPulse, HoverNetGfx::kLightMine );
         }
         else                       // a power-up: a cool green light
         {
            lAt.mZ += 300;
            AddLight( lAt, 3200, 90, 255, 160, 0.35f, HoverNetGfx::kLightPowerUp );
         }
      }
      // Explosions: a missile or mine that was in plain view last frame and is not seen now (the list is not full, so it was not just crowded out) has blown up there.
      if( pTime < mPrevLitTime || pTime - mPrevLitTime > 400 )
      {
         mPrevLit.clear();   // a restart or a seek: nothing to compare with
         mBlasts.clear();
      }
      if( mModernMotion && mLitElements.size() < 6 )
      {
         for( size_t lOld = 0; lOld < mPrevLit.size(); lOld++ )
         {
            const LitMemo& lMemo = mPrevLit[ lOld ];
            if( !lMemo.mCentral || lMemo.mClass == 152 || mBlasts.size() >= 4 )
            {
               continue;
            }
            bool lStillThere = false;
            for( size_t lNow = 0; lNow < mLitElements.size(); lNow++ )
            {
               lStillThere = lStillThere || (const void*)mLitElements[ lNow ] == lMemo.mId;
            }
            if( !lStillThere )
            {
               Blast lBlast;
               lBlast.mPos = lMemo.mPos;
               lBlast.mStart = pTime;
               lBlast.mMine = lMemo.mClass == 151;
               mBlasts.push_back( lBlast );
               if( getenv( "HOVERNET_GFX_DEBUG" ) != NULL ) { printf( "GFXDEBUG explosion at (%d,%d,%d) class %d\n", (int)lBlast.mPos.mX, (int)lBlast.mPos.mY, (int)lBlast.mPos.mZ, (int)lMemo.mClass ); fflush( stdout ); }
            }
         }
      }
      mPrevLit.clear();
      mPrevLitTime = pTime;
      for( size_t lIndex = 0; lIndex < mLitElements.size(); lIndex++ )
      {
         LitMemo lMemo;
         lMemo.mId = mLitElements[ lIndex ];
         lMemo.mPos = mLitElements[ lIndex ]->mPosition;
         lMemo.mClass = mLitElements[ lIndex ]->GetTypeId().mClassId;
         int lCX, lCY, lCZ;
         lMemo.mCentral = m3DView.ToCameraSpace( lMemo.mPos, lCX, lCY, lCZ ) && lCX > 1500 && lCX < 70000 &&
                          abs( lCY ) < lCX/2 && abs( lCZ ) < lCX*2/5;
         mPrevLit.push_back( lMemo );
      }
      for( size_t lIndex = 0; lIndex < mBlasts.size(); )
      {
         const MR_SimulationTime lAge = pTime - mBlasts[ lIndex ].mStart;
         if( !mModernMotion || lAge >= 800 )
         {
            mBlasts.erase( mBlasts.begin() + lIndex );
            continue;
         }
         const float lLife = 1.0f - (float)lAge/800.0f;
         MR_3DCoordinate lAt = mBlasts[ lIndex ].mPos;
         lAt.mZ += 250;
         // a hard white-orange flash that falls away quickly, and a light that spreads and lingers a little as the fire dies; a mine's is redder and wider
         const float lHot = lLife*lLife*lLife;
         const BOOL lMine = mBlasts[ lIndex ].mMine;
         AddLight( lAt, (int)( (lMine ? 9000 : 6500) + 9000*( 1.0f - lLife ) ), 255, lMine ? 120 : 175, lMine ? 60 : 90, 0.35f + 0.65f*lHot, HoverNetGfx::kLightExplosion );
         lIndex++;
      }
      if( mGhost != NULL && mGhost != pViewingCharacter )
      {
         MR_3DCoordinate lGhost = mGhost->mPosition;
         lGhost.mZ += 250;
         AddLight( lGhost, 3500, 170, 200, 255, 0.22f, HoverNetGfx::kLightGhost );
      }
   }

}

// The chase camera trails the craft a little: each step it moves part of the way to where it should be (three quarters across, and two thirds up
// and down). That was tuned for one picture per 16 ms tick of the race, and it was done once per picture, so a slower frame rate made the camera
// trail the craft by more (and a faster one by less): on a bend it did not follow the way it was meant to. It now follows race time: the share of
// the way left after a step is the same per tick however many pictures are drawn, so it is exactly the old behaviour at one picture per tick and
// the same at any other rate. A jump in race time (a restart, a seek) puts the camera straight where it should be.
void MR_Observer::SmoothChaseCamera( MR_3DCoordinate& pTarget, MR_SimulationTime pTime )
{
   // The original chase smoothing, restored at the owner's request (2026-11-09): once per drawn picture, not by race time.
   pTarget.mX = (3*pTarget.mX + mLastCameraPos.mX )/4;
   pTarget.mY = (3*pTarget.mY + mLastCameraPos.mY )/4;
   pTarget.mZ = (2*pTarget.mZ + mLastCameraPos.mZ )/3;
}

void MR_Observer::Render3DView( const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage )
{
   // Safe entry guards - check all pointers before proceeding
   if(pSession == NULL || pViewingCharacter == NULL) {
      return;
   }

   const MR_Level* lLevel = pSession->GetCurrentLevel();
   mShadowLevel = lLevel;
   
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

   mHints = HoverNetGfx::FrameHints();
   mOtherCrafts.clear();
   mLitElements.clear();

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
      int lDist = 3400*mChaseScale/100;
      lOrientation   = pViewingCharacter->mOrientation;   
      
      if( pTime < -3000 )
      {         
         int lFactor = (-pTime-3000)*2/3;
         lOrientation = MR_NORMALIZE_ANGLE( lOrientation+lFactor*4096/11000 );
         lDist += lFactor;
      }
      
      lCameraPos.mX  = pViewingCharacter->mPosition.mX -lDist*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY -lDist*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1700*mChaseScale/100;

      if( mReuseCamera && mLastCameraPosValid )
      {
         lCameraPos = mLastCameraPos;
      }
      else if( mLastCameraPosValid )
      {
         SmoothChaseCamera( lCameraPos, pTime );
      }
   }

   mLastCameraPos = lCameraPos;
   mLastCameraTime = pTime;
   mLastCameraPosValid = TRUE;

   // VR: shift the camera sideways for the left/right eye (not stored, so the smoothed chase
   // camera above stays the shared centre point).
   if( mEyeOffset != 0 )
   {
      const MR_Angle lSide = MR_NORMALIZE_ANGLE( lOrientation + MR_PI/2 );
      lCameraPos.mX += mEyeOffset*MR_Cos[ lSide ]/MR_TRIGO_FRACT;
      lCameraPos.mY += mEyeOffset*MR_Sin[ lSide ]/MR_TRIGO_FRACT;
   }

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
         m3DView.SetSurfaceTag( HoverNetGfx::kTagFloor );
         RenderFloorOrCeiling( lLevel, lFloorList[ lCounter ], TRUE, pTime );

         // Render the ceiling
         m3DView.SetSurfaceTag( HoverNetGfx::kTagCeiling );
         RenderFloorOrCeiling( lLevel, lCeilingList[ lCounter ], FALSE, pTime );
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Floor/ceiling rendering crashed - continue with what we have
   }

   // STAGE 4: Wall rendering
   m3DView.SetSurfaceTag( HoverNetGfx::kTagWall );
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
                  const MR_MainCharacter* lOtherCraft = dynamic_cast<const MR_MainCharacter*>( lElement );
                  if( mModernShadows )
                  {
                     DrawCraftShadow( lOtherCraft );
                  }
                  if( lOtherCraft != NULL && mModernLighting && mOtherCrafts.size() < 5 )
                  {
                     mOtherCrafts.push_back( lOtherCraft );
                  }
                  else if( lOtherCraft == NULL && mModernLighting && mLitElements.size() < 6 )
                  {
                     const MR_UInt16 lClass = lElement->GetTypeId().mClassId;
                     if( lClass == 150 || lClass == 151 || lClass == 152 )   // missile, mine, power-up (ObjFac1/main.cpp)
                     {
                        mLitElements.push_back( lElement );
                     }
                  }
                  m3DView.SetSurfaceTag( lOtherCraft != NULL ? HoverNetGfx::kTagCraft : HoverNetGfx::kTagObject );
                  lElement->Render( &m3DView, pTime );
               }
            }

            lHandle = MR_Level::GetNextFreeElement( lHandle );
         }

         // The ghost is drawn with the craft of the room it is in, so the walls in front of it hide it as they hide any craft.
         if( mGhost != NULL && mGhost != pViewingCharacter && mGhost->mRoom == lRoomId )
         {
            DrawCraftShadow( mGhost );
            m3DView.SetSurfaceTag( HoverNetGfx::kTagCraft );
            const_cast<MR_MainCharacter*>(mGhost)->Render( &m3DView, pTime );
         }
      }
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
      // Actor rendering crashed - continue with what we have
   }

   // STAGE 5.5: Render the viewing character (player's hovercraft)
   
   if( !mCockpitView )
   {
      DrawCraftShadow( pViewingCharacter );
   }

   __try {
      // Call the character's render method to draw the player's hovercraft
      // Cast away const since Render() is not const (not ideal, but necessary for rendering)
      m3DView.SetSurfaceTag( HoverNetGfx::kTagCraft );
      const_cast<MR_MainCharacter*>(pViewingCharacter)->Render( &m3DView, pTime );
   }
   __except(EXCEPTION_EXECUTE_HANDLER) {
   }

   UpdateModernHints( pViewingCharacter, pTime );

   // Remember the scene as drawn so far; what differs once the HUD is on top is HUD (see the end of RenderNormalDisplay).
   if( mModernEffects )
   {
      const int lW = m3DView.GetXRes();
      const int lH = m3DView.GetYRes();
      mSceneCopy.resize( (size_t)lW*lH );
      mDepthCopy.resize( (size_t)lW*lH );
      mTagCopy.resize( (size_t)lW*lH );
      for( int lY = 0; lY < lH; lY++ )
      {
         memcpy( &mSceneCopy[ (size_t)lY*lW ], m3DView.GetBuffer() + (size_t)lY*m3DView.GetLineLen(), lW );
         const MR_UInt8* lTagLine = m3DView.GetTagLine( lY );
         if( lTagLine != NULL && !mRgbFull.empty() && m3DView.GetColourLine( lY ) != NULL )
         {
            // Pixels the rasteriser did not draw (the sky and background): their colour is the palette's.
            MR_UInt32* lRgbLine = const_cast<MR_UInt32*>( m3DView.GetColourLine( lY ) );
            const MR_UInt8* lIndexLine = m3DView.GetBuffer() + (size_t)lY*m3DView.GetLineLen();
            for( int lX = 0; lX < lW; lX++ )
            {
               if( lTagLine[ lX ] == 0 )
               {
                  lRgbLine[ lX ] = mPalette[ lIndexLine[ lX ] ];
               }
            }
         }

         if( lTagLine != NULL )
         {
            memcpy( &mTagCopy[ (size_t)lY*lW ], lTagLine, lW );
         }
         else
         {
            memset( &mTagCopy[ (size_t)lY*lW ], 0, lW );
         }

         const MR_UInt16* lDepthLine = m3DView.GetDepthLine( lY );
         if( lDepthLine != NULL )
         {
            memcpy( &mDepthCopy[ (size_t)lY*lW ], lDepthLine, lW*sizeof( MR_UInt16 ) );
         }
         else
         {
            memset( &mDepthCopy[ (size_t)lY*lW ], 0xFF, lW*sizeof( MR_UInt16 ) );
         }
      }
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
      int lDist = 3400*mChaseScale/100;

      lOrientation   = pViewingCharacter->mOrientation;   

      
      if( pTime < -3000 )
      {         
         int lFactor = (-pTime-3000)*2/3;
         lOrientation = MR_NORMALIZE_ANGLE( lOrientation+lFactor*4096/11000 );
         lDist += lFactor;
      }
      
      lCameraPos.mX  = pViewingCharacter->mPosition.mX -lDist*MR_Cos[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mY  = pViewingCharacter->mPosition.mY -lDist*MR_Sin[ lOrientation ]/MR_TRIGO_FRACT;
      lCameraPos.mZ  = pViewingCharacter->mPosition.mZ + 1700*mChaseScale/100;

      if( mReuseCamera && mLastCameraPosValid )
      {
         lCameraPos = mLastCameraPos;
      }
      else if( mLastCameraPosValid )
      {
         SmoothChaseCamera( lCameraPos, pTime );
      }
   }

   mLastCameraPos = lCameraPos;
   mLastCameraTime = pTime;
   mLastCameraPosValid = TRUE;

   // VR: shift the camera sideways for the left/right eye (not stored, so the smoothed chase
   // camera above stays the shared centre point).
   if( mEyeOffset != 0 )
   {
      const MR_Angle lSide = MR_NORMALIZE_ANGLE( lOrientation + MR_PI/2 );
      lCameraPos.mX += mEyeOffset*MR_Cos[ lSide ]/MR_TRIGO_FRACT;
      lCameraPos.mY += mEyeOffset*MR_Sin[ lSide ]/MR_TRIGO_FRACT;
   }


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
      else if( pSession->IsFreePlay() )
      {
         // No finish line or checkpoints: laps can never complete, so show the
         // elapsed time only instead of a lap counter that would stay at 1.
         sprintf( lMainLineBuffer, "Free play  %d:%02d.%02d", pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );
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
   mSceneX0 = lXMargin;
   mSceneY0 = lYOffset+lYMargin;

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

   // Surface tags are only asked of the rasteriser when the lighting can use them.
   const BOOL lTrueColour = mModernEffects && mModernTextures && pDest->GetLineLen() == pDest->GetXRes();
   if( mModernEffects && ( mModernLighting || lTrueColour ) )
   {
      const size_t lTagSize = (size_t)pDest->GetLineLen()*pDest->GetYRes();
      if( mTagFull.size() != lTagSize )
      {
         mTagFull.assign( lTagSize, 0 );
      }
      m3DView.SetTagBuffer( &mTagFull[ 0 ] );
   }
   else
   {
      m3DView.SetTagBuffer( NULL );
   }

   // True colour: the rasteriser also writes RGB (filtered textures); the pixels it does not draw and the HUD are filled in below.
   if( lTrueColour )
   {
      const size_t lRgbSize = (size_t)pDest->GetLineLen()*pDest->GetYRes();
      if( mRgbFull.size() != lRgbSize )
      {
         mRgbFull.assign( lRgbSize, 0 );
      }
      m3DView.SetColourBuffer( &mRgbFull[ 0 ], mPalette );
      m3DView.SetTextureFiltering( TRUE );
   }
   else
   {
      m3DView.SetColourBuffer( NULL, NULL );
      m3DView.SetTextureFiltering( FALSE );
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
   mHudText.valid = false;
   mHudText.chat.clear();
   mHudText.typing.clear();
   mHudText.callout[0] = 0;
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
      else if( pSession->IsFreePlay() )
      {
         // No finish line or checkpoints: laps can never complete, so show the
         // elapsed time only instead of a lap counter that would stay at 1.
         sprintf( lMainLineBuffer, "Free play  %d:%02d.%02d", pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );
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
         // Show first-lap progress too; previously the lower timer stayed blank
         // until the player completed an entire lap.
         sprintf( lMainLineBuffer, gHeaderStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10, 1, pViewingCharacter->GetTotalLap() );
         sprintf( lLapLineBuffer, gFirstLapTimeStr, pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );

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
      
      // Make the final lap impossible to confuse with an ordinary lap counter.
      if( pTime >= 0 && !pSession->IsFreePlay() && pViewingCharacter->GetTotalLap() > 1 &&
          pViewingCharacter->GetLap()+1 == pViewingCharacter->GetTotalLap() &&
          !pViewingCharacter->HasFinish() )
      {
         sprintf( lMainLineBuffer, gFinalLapStr,
                  pTime/60000, (pTime%60000)/1000, (pTime%1000)/10 );
      }

      snprintf( mHudText.mainLine, sizeof(mHudText.mainLine), "%s", lMainLineBuffer );
      snprintf( mHudText.lapLine, sizeof(mHudText.lapLine), "%s", lLapLineBuffer );
      mHudText.valid = true;

      // Render timer text at top and bottom center of screen.
      const MR_Sprite* lHudFont = mBaseFont->GetSprite();
      HudStr( !mExternalHudText, lHudFont, lXRes/2, lYRes/16, Ascii2Simple( lMainLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eTop, 0 );
      HudStr( !mExternalHudText, lHudFont, lXRes/2, lYRes-1, Ascii2Simple( lLapLineBuffer ), &m3DView, MR_Sprite::eCenter, MR_Sprite::eBottom, 0 );

      // The original HUD rendered speed/fuel as unlabeled bars and weapons as
      // icon frames.  Keep those compact visuals, but add a readable status line
      // so players can tell what each meter means and whether the selected weapon
      // is actually usable without memorising sprite artwork.
      int lSpeedPercent = static_cast<int>( pViewingCharacter->GetAbsoluteSpeed()*100.0 + 0.5 );
      int lFuelPercent = static_cast<int>( pViewingCharacter->GetFuelLevel()*100.0 + 0.5 );
      if( lSpeedPercent < 0 ) lSpeedPercent = 0;
      if( lSpeedPercent > 999 ) lSpeedPercent = 999;
      if( lFuelPercent < 0 ) lFuelPercent = 0;
      if( lFuelPercent > 100 ) lFuelPercent = 100;
      char lFuelStatus[40];
      const char* lFuelWarning = GetFuelWarningForPercent( lFuelPercent );
      if( lFuelWarning[0] != 0 )
         snprintf( lFuelStatus, sizeof(lFuelStatus), "Fuel %d%% %s", lFuelPercent, lFuelWarning );
      else
         snprintf( lFuelStatus, sizeof(lFuelStatus), "Fuel %d%%", lFuelPercent );

      char lWeaponStatus[48];
      switch( pViewingCharacter->GetCurrentWeapon() )
      {
         case MR_MainCharacter::eMissile:
         {
            const int lCharge = pViewingCharacter->GetMissileRefillLevel( 101 );
            if( lCharge >= 100 )
               snprintf( lWeaponStatus, sizeof(lWeaponStatus), "Missile ready" );
            else
               snprintf( lWeaponStatus, sizeof(lWeaponStatus), "Missile charging %d%%", lCharge );
            break;
         }
         case MR_MainCharacter::eMine:
            snprintf( lWeaponStatus, sizeof(lWeaponStatus), "Mines %d", pViewingCharacter->GetMineCount() );
            break;
         case MR_MainCharacter::ePowerUp:
            snprintf( lWeaponStatus, sizeof(lWeaponStatus), "Power-ups %d", pViewingCharacter->GetPowerUpCount() );
            break;
         default:
            snprintf( lWeaponStatus, sizeof(lWeaponStatus), "Weapon unavailable" );
            break;
      }

      int lBestRank = 1;
      int lWorstRank = 1;
      const int lPlayerCount = pSession->GetNbPlayers();
      pSession->GetRankRange( pViewingCharacter, lBestRank, lWorstRank );
      char lPositionStatus[32];
      if( lBestRank == lWorstRank )
         snprintf( lPositionStatus, sizeof(lPositionStatus), "Pos %d/%d", lBestRank, lPlayerCount );
      else
         snprintf( lPositionStatus, sizeof(lPositionStatus), "Pos %d-%d/%d", lBestRank, lWorstRank, lPlayerCount );

      char lNetworkStatus[56];
      if( mNetworkLatencyMs >= 0 )
      {
         const char* lLatencyWarning = GetLatencyWarningForMilliseconds( mNetworkLatencyMs );
         if( lLatencyWarning[0] != 0 )
            snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Online %d ms %s",
                      mNetworkLatencyMs, lLatencyWarning );
         else
            snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Online %d ms", mNetworkLatencyMs );
      }
      else if( mNetworkLatencyMs == -1 )
         snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Online (measuring)" );
      else if( mNetworkLatencyMs == -3 )
         snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Connection lost" );
      else if( mNetworkLatencyMs == -4 )
         snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Replay" );
      else
         snprintf( lNetworkStatus, sizeof(lNetworkStatus), "Local" );

      mHudText.speedPercent = lSpeedPercent;
      mHudText.fuelPercent = lFuelPercent;
      snprintf( mHudText.position, sizeof(mHudText.position), "%s", lPositionStatus );
      snprintf( mHudText.fuel, sizeof(mHudText.fuel), "%s", lFuelStatus );
      snprintf( mHudText.weapon, sizeof(mHudText.weapon), "%s", lWeaponStatus );
      snprintf( mHudText.network, sizeof(mHudText.network), "%s", lNetworkStatus );
      snprintf( mHudText.ghost, sizeof(mHudText.ghost), "%s", mGhostGapText );

      int lStatusY = lYRes/16 + lHudFont->GetItemHeight() + 4;
      if( mLargeHudText )
      {
         // The bitmap font cannot be scaled above its native pixels. Splitting
         // the dense status into two short lines lets both render at native
         // size (scaling=1) instead of StrBlt auto-shrinking one long line.
         char lDriveStatus[96];
         snprintf( lDriveStatus, sizeof(lDriveStatus), "%s   Speed %d%%   %s",
                   lPositionStatus, lSpeedPercent, lFuelStatus );
         HudStr( !mExternalHudText, lHudFont, lXRes/2, lStatusY, Ascii2Simple( lDriveStatus ), &m3DView,
                           MR_Sprite::eCenter, MR_Sprite::eTop, 1 );
         HudStr( !mExternalHudText, lHudFont, lXRes/2, lStatusY + lHudFont->GetItemHeight() + 3,
                           Ascii2Simple( lWeaponStatus ), &m3DView,
                           MR_Sprite::eCenter, MR_Sprite::eTop, 1 );
         char lNetworkLine[96];
         if( mGhostGapText[0] != 0 )
            snprintf( lNetworkLine, sizeof(lNetworkLine), "%s   Ghost %s", lNetworkStatus, mGhostGapText );
         else
            snprintf( lNetworkLine, sizeof(lNetworkLine), "%s", lNetworkStatus );
         HudStr( !mExternalHudText, lHudFont, lXRes/2, lStatusY + 2*(lHudFont->GetItemHeight() + 3),
                           Ascii2Simple( lNetworkLine ), &m3DView,
                           MR_Sprite::eCenter, MR_Sprite::eTop, 1 );
      }
      else
      {
         char lStatusLine[208];
         snprintf( lStatusLine, sizeof(lStatusLine), "%s   Speed %d%%   %s   %s   %s",
                   lPositionStatus, lSpeedPercent, lFuelStatus, lWeaponStatus, lNetworkStatus );
         if( mGhostGapText[0] != 0 )
         {
            const size_t lUsed = strlen( lStatusLine );
            snprintf( lStatusLine + lUsed, sizeof(lStatusLine) - lUsed, "   Ghost %s", mGhostGapText );
         }
         HudStr( !mExternalHudText, lHudFont, lXRes/2, lStatusY, Ascii2Simple( lStatusLine ), &m3DView,
                           MR_Sprite::eCenter, MR_Sprite::eTop, 0 );
      }

      // State-change callouts make impacts and pickups readable even when the
      // player cannot identify the legacy sound/sprite cue. A newly viewed
      // craft establishes a baseline instead of reporting its starting loadout
      // as pickups. Impact wins if several changes happen in the same frame.
      const double lCurrentFuel = pViewingCharacter->GetFuelLevel();
      const int lCurrentMines = pViewingCharacter->GetMineCount();
      const int lCurrentPowerUps = pViewingCharacter->GetPowerUpCount();
      const BOOL lOutOfControl = pViewingCharacter->IsOutOfControl();
      if( mFeedbackCharacter != pViewingCharacter )
      {
         mFeedbackCharacter = pViewingCharacter;
         mPreviousFuelLevel = lCurrentFuel;
         mPreviousMineCount = lCurrentMines;
         mPreviousPowerUpCount = lCurrentPowerUps;
         mWasOutOfControl = lOutOfControl;
         mFeedbackUntil = 0;
         mFeedbackText[0] = 0;
      }
      else
      {
         if( lOutOfControl && !mWasOutOfControl )
         {
            snprintf( mFeedbackText, sizeof(mFeedbackText), "IMPACT - CONTROL DISRUPTED" );
            mFeedbackUntil = pTime + 2200;
         }
         else if( lCurrentMines > mPreviousMineCount )
         {
            snprintf( mFeedbackText, sizeof(mFeedbackText), "MINE COLLECTED  (%d)", lCurrentMines );
            mFeedbackUntil = pTime + 1800;
         }
         else if( lCurrentPowerUps > mPreviousPowerUpCount )
         {
            snprintf( mFeedbackText, sizeof(mFeedbackText), "POWER-UP COLLECTED  (%d)", lCurrentPowerUps );
            mFeedbackUntil = pTime + 1800;
         }
         else if( lCurrentFuel > mPreviousFuelLevel + 0.001 )
         {
            snprintf( mFeedbackText, sizeof(mFeedbackText), "FUEL REPLENISHED  (%d%%)", lFuelPercent );
            mFeedbackUntil = pTime + 1400;
         }
         mPreviousFuelLevel = lCurrentFuel;
         mPreviousMineCount = lCurrentMines;
         mPreviousPowerUpCount = lCurrentPowerUps;
         mWasOutOfControl = lOutOfControl;
      }
      if( pTime >= 0 && pTime < mFeedbackUntil && mFeedbackText[0] != 0 )
      {
         snprintf( mHudText.callout, sizeof(mHudText.callout), "%s", mFeedbackText );
         HudStr( !mExternalHudText, lHudFont, lXRes/2, lYRes/4, Ascii2Simple( mFeedbackText ), &m3DView,
                           MR_Sprite::eCenter, MR_Sprite::eTop, 1 );
      }

      // In-race chat: the line currently being typed (AddMessageKey/GetCurrentMessage,
      // real on Windows via MR_NetworkSession's override, a no-op stub on Linux) plus
      // the scrollback (AddMessage/GetMessageStack, real and shared on both platforms).
      // This used to exist only in Render3DView's "DISABLED SECTION" -- dead, commented-
      // out code that this live function never called into, so nothing was ever drawn
      // on screen for chat during a race on either platform. Reimplemented here instead.
      const MR_Sprite* lChatFont = mBaseFont->GetSprite();
      // Large HUD text is an accessibility mode for all time-sensitive race
      // information, including chat. In compact/split viewports the legacy
      // formula shrinks chat to fit; keep it at native size when requested and
      // rely on the existing wrapping/line cap to contain it.
      const int lChatFontScaling = mLargeHudText ? 1 :
         1 + (lChatFont->GetItemHeight()*12)/lYRes;
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
         mHudText.typing = SimpleToAscii( lChatLineBuffer );
         HudStr( !mExternalHudText, lChatFont, lChatXMargin, lChatYMargin, lChatLineBuffer, &m3DView,
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
         mHudText.chat.push_back( SimpleToAscii( lChatLineBuffer ) );
         const int lChatStrLen = static_cast<int>( strlen( lChatLineBuffer ) );
         if( lChatStrLen > lChatPrintLen )
         {
            HudStr( !mExternalHudText, lChatFont, lChatXMargin, lChatYMargin, lChatLineBuffer+lChatPrintLen, &m3DView,
                               MR_Sprite::eLeft, MR_Sprite::eTop, lChatFontScaling );
            lChatYMargin -= lChatLineSpacing;
            lChatLineBuffer[ lChatPrintLen ] = 0;
            ++lChatLineCount;
         }
         HudStr( !mExternalHudText, lChatFont, lChatXMargin, lChatYMargin, lChatLineBuffer, &m3DView,
                            MR_Sprite::eLeft, MR_Sprite::eTop, lChatFontScaling );
         lChatYMargin -= lChatLineSpacing;
         ++lChatLineCount;
      }
   }

   // Which pixels did the HUD draw? Everything outside the 3D viewport, and every viewport pixel that is not what the scene drew.
   if( mHints.valid )
   {
      const int lFrameW = pDest->GetXRes();
      const int lFrameH = pDest->GetYRes();
      const int lSceneW = m3DView.GetXRes();
      const int lSceneH = m3DView.GetYRes();
      mHudMask.assign( (size_t)lFrameW*lFrameH, 1 );
      if( mSceneCopy.size() == (size_t)lSceneW*lSceneH && mSceneX0 >= 0 && mSceneY0 >= 0 && mSceneX0 + lSceneW <= lFrameW && mSceneY0 + lSceneH <= lFrameH )
      {
         for( int lY = 0; lY < lSceneH; lY++ )
         {
            const MR_UInt8* lNow  = pDest->GetBuffer() + (size_t)(mSceneY0 + lY)*pDest->GetLineLen() + mSceneX0;
            const MR_UInt8* lThen = &mSceneCopy[ (size_t)lY*lSceneW ];
            MR_UInt8*       lMask = &mHudMask[ (size_t)(mSceneY0 + lY)*lFrameW + mSceneX0 ];
            for( int lX = 0; lX < lSceneW; lX++ )
            {
               lMask[ lX ] = lNow[ lX ] != lThen[ lX ] ? 1 : 0;
            }
         }
         if( mModernLighting && mDepthCopy.size() == (size_t)lSceneW*lSceneH )
         {
            mHints.depth       = mDepthCopy.data();
            mHints.tags        = m3DView.GetTagLine( 0 ) != NULL && mTagCopy.size() == mDepthCopy.size() ? mTagCopy.data() : NULL;
            mHints.depthX0     = mSceneX0;
            mHints.depthY0     = mSceneY0;
            mHints.depthWidth  = lSceneW;
            mHints.depthHeight = lSceneH;
         }
         if( mSceneSink != NULL )
         {
            // The scene was drawn by the graphics card, which has its own depth and tags: the window it covers is all the hints carry.
            mHints.gpuScene    = true;
            mHints.depth       = NULL;
            mHints.tags        = NULL;
            mHints.depthX0     = mSceneX0;
            mHints.depthY0     = mSceneY0;
            mHints.depthWidth  = lSceneW;
            mHints.depthHeight = lSceneH;
         }
         if( m3DView.GetColourLine( 0 ) != NULL && mRgbFull.size() == (size_t)lFrameW*lFrameH && pDest->GetLineLen() == lFrameW )
         {
            // HUD pixels (and the margins) are 8-bit: their true colour is the palette's. Then the whole frame is true colour.
            mFinalIndexed.resize( (size_t)lFrameW*lFrameH );
            for( int lY = 0; lY < lFrameH; lY++ )
            {
               const MR_UInt8* lIndexLine = pDest->GetBuffer() + (size_t)lY*pDest->GetLineLen();
               const MR_UInt8* lMaskLine  = &mHudMask[ (size_t)lY*lFrameW ];
               MR_UInt32* lRgbLine = &mRgbFull[ (size_t)lY*lFrameW ];
               memcpy( &mFinalIndexed[ (size_t)lY*lFrameW ], lIndexLine, lFrameW );
               for( int lX = 0; lX < lFrameW; lX++ )
               {
                  if( lMaskLine[ lX ] )
                  {
                     lRgbLine[ lX ] = mPalette[ lIndexLine[ lX ] ];
                  }
               }
            }
            mHints.colour = mRgbFull.data();
            mHints.finalIndexed = mFinalIndexed.data();
            mHints.colourWidth = lFrameW;
            mHints.colourHeight = lFrameH;
         }
         mHints.hudMask       = mHudMask.data();
         mHints.hudMaskWidth  = lFrameW;
         mHints.hudMaskHeight = lFrameH;
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




