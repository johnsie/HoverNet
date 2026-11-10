// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-09.
// See repository history for authorship and detailed changes.
// Observer.h// MR_Observer object interface
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

#ifndef OBSERVER_H
#define OBSERVER_H

#include <string>
#include <vector>
#include <map>
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/ModernGraphics.h"
#include <vector>
#include "ClientSession.h"
#include "../MainCharacter/MainCharacter.h"
#include "../ObjFacTools/SpriteHandle.h"



// What the HUD says this frame (see MR_Observer::GetHudText). Strings are plain ASCII.
struct HudText
{
   bool        valid = false;
   char        mainLine[96] = {0};    // "Race 0:05.39  Lap 1/1", "Start in 03.00 sec. for 3 laps", "Finish in ...", "Free play ..."
   char        lapLine[96] = {0};     // "Current lap ...  Best ...", "Lap time ...", "Best lap ..." (may be empty)
   char        position[32] = {0};    // "Pos 1/1"
   char        fuel[40] = {0};        // "Fuel 97%" (with a warning when low)
   char        weapon[48] = {0};      // "Missile ready", "Missile charging 40%", "Mines 2", "Power-ups 1"
   char        network[56] = {0};     // "Local", "Online 40 ms", "Replay", ...
   char        ghost[16] = {0};       // the gap to the ghost, or empty
   char        callout[64] = {0};     // "IMPACT - CONTROL DISRUPTED", "FUEL REPLENISHED  (80%)", or empty
   int         speedPercent = 0;
   int         fuelPercent = 0;
   std::vector<std::string> chat;     // newest first: the messages and chat lines shown at the bottom left
   std::string typing;                // the chat line being typed, or empty
};

class MR_Observer
{

   public:
      enum eSplitMode
           {
              eNotSplit,
              eUpperSplit,
              eLowerSplit,
           };

   private:
      MR_3DCoordinate mLastCameraPos;
      MR_SimulationTime mLastCameraTime;   // the race time the camera was last moved at: the chase smoothing follows race time, not the frame rate
      void            SmoothChaseCamera( MR_3DCoordinate& pTarget, MR_SimulationTime pTime );
      BOOL            mLastCameraPosValid;
      int             mEyeOffset;   // VR: sideways camera shift in mm (0 = normal view)
      BOOL            mReuseCamera; // VR second eye: reuse the first eye's smoothed camera
      BOOL            mCockpitView;
      const MR_MainCharacter* mGhost;   // a craft that is only drawn (a recorded run shown beside the race); never part of the level
      char            mGhostGapText[16];   // the time behind (+) or ahead of (-) the ghost for the HUD; empty when there is none
      int             mChaseScale;  // chase camera distance and height as a percentage of the classic view (100 = as 2.0)
      // -2 local race, -1 online but awaiting a sample, -3 connection lost,
      // otherwise the most recent server TCP round-trip in milliseconds.
      int             mNetworkLatencyMs;
      BOOL            mLargeHudText;

      // The optional modern look (docs/roadmap-3.0.md): presentation only, off unless the client asks, so the classic frame is unchanged.
      BOOL            mModernEffects;        // collect hints and draw craft shadows (the camera is never changed)
      BOOL            mModernShadows;
      BOOL            mModernMotion;         // false with "Reduced motion": no impact flash, no pulsing mine light
      MR_UInt8        mShadowLut[256];       // palette index -> the nearest palette index at about 55% brightness
      BOOL            mShadowLutValid;
      HoverNetGfx::FrameHints mHints;
      std::vector<MR_UInt8> mSceneCopy;      // the 3D viewport as drawn before the HUD, to find which pixels the HUD drew
      std::vector<MR_UInt32> mRgbFull;       // the rasteriser's true-colour picture (0x00RRGGBB per pixel), laid out like the video buffer
      std::vector<MR_UInt8>  mFinalIndexed;  // the 8-bit picture as it was when this observer finished drawing (width*height)
      MR_UInt32              mPalette[256];  // what the 8-bit colours mean in true colour (0x00RRGGBB)
      BOOL                   mModernTextures;
      std::vector<MR_UInt8>  mTagFull;       // surface tags written by the rasteriser, laid out like the video buffer
      std::vector<MR_UInt8>  mTagCopy;       // the viewport's part of them, copied for the picture passes
      std::vector<MR_UInt16> mDepthCopy;     // the 3D viewport's depth buffer, for light and haze
      std::vector<const MR_FreeElement*> mLitElements;     // missiles, mines and power-ups seen this frame, for their lights
      std::vector<const MR_MainCharacter*> mOtherCrafts;   // craft seen this frame (not the viewing one), for their lights
      BOOL            mModernLighting;
      const MR_Level* mShadowLevel;         // the level being drawn, to find the floor under a craft
      MR_Int32        GroundUnder( const MR_MainCharacter* pCraft ) const;   // the height of the floor a craft's shadow falls on
      float           mImpactFlash;
      // Explosions, for the picture passes only: a missile or mine that was in plain view last frame and is gone this frame is taken to have blown up there (or been picked
      // up), and a flash light and a burst of embers are shown at that spot for most of a second. Nothing here reaches the simulation. Matching is by address and never dereferenced.
      struct LitMemo { const void* mId; MR_3DCoordinate mPos; MR_UInt16 mClass; BOOL mCentral; };
      struct Blast { MR_3DCoordinate mPos; MR_SimulationTime mStart; BOOL mMine; int mKind; };   // mKind: 0 a missile blew up, 1 a mine, 2 a missile was launched, 3 a power-up was taken
      BOOL                 mPrevLitHave;
      // Sound from the side and from a moving source: for each other craft and missile the pan (from where it is, relative to the way the viewing craft faces) and the Doppler pitch
      // (from how fast it closes on the viewing craft, measured over at least 60 ms and smoothed). Presentation only.
      struct SoundTrack { double mDistance; double mSampleMs; double mClosing; };   // distance (mm) at the last sample, when, and the closing speed (mm/ms, positive = approaching)
      std::map<const void*, SoundTrack> mSoundTracks;
      std::vector<LitMemo> mPrevLit;
      std::vector<Blast>   mBlasts;
      MR_SimulationTime    mPrevLitTime;
      double          mProjKx, mProjKz;
      void            AddLight( const MR_3DCoordinate& pWorld, int pRadiusMm, int pR, int pG, int pB, float pStrength, int pKind = 0, const MR_3DCoordinate* pAimAt = NULL, float pConeCos = -1.0f );
      std::vector<MR_UInt8> mHudMask;        // 1 where the HUD drew (or outside the viewport), for the whole frame
      int             mSceneX0, mSceneY0;    // the viewport's corner in the frame
      MR_3DCoordinate mTrailPos[HoverNetGfx::kMaxTrailPoints];
      int             mTrailCount;
      MR_SimulationTime mTrailLastTime;
      double          mPreviousSpeed;
      MR_SimulationTime mPreviousSpeedTime;
      void            DrawCraftShadow( const MR_MainCharacter* pCraft );
      void            UpdateModernHints( const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime );

      // Short, high-visibility gameplay feedback derived from state changes.
      // Kept per observer so local/split views never share stale pickup state.
      const MR_MainCharacter* mFeedbackCharacter;
      double          mPreviousFuelLevel;
      int             mPreviousMineCount;
      int             mPreviousPowerUpCount;
      BOOL            mWasOutOfControl;
      MR_SimulationTime mFeedbackUntil;
      char            mFeedbackText[64];
      MR_SceneSink*   mSceneSink;
      BOOL            mExternalHudText;     // the client draws the HUD's words itself (from GetHudText), so they are not drawn here in the bitmap font
      HudText         mHudText;

      MR_2DViewPort m2DDebugView;
      MR_3DViewPort mWireFrameView;
      MR_3DViewPort m3DView;

      eSplitMode    mSplitMode;

      int           mScroll;
      MR_Angle      mApperture;
      int           mXMargin_1024;  // Fraction of Horizontal screen that is a margin
      int           mYMargin_1024;  // Fraction of vertical screen that is a margin

      BOOL mMoreMessages;
      int  mDispPlayers;   // Index of the players list to display
                           // 0 mean do not display


      MR_SpriteHandle* mBaseFont;
      MR_SpriteHandle* mMissileLevel;
      MR_SpriteHandle* mMineDisp;
      MR_SpriteHandle* mPowerUpDisp;
      MR_SpriteHandle* mHoverIcons;

      MR_Observer();
      ~MR_Observer();

      void Render2DDebugView(   MR_VideoBuffer* pDest,  const MR_Level* pLevel, const MR_MainCharacter* pViewingCharacter );
      void RenderWireFrameView( const MR_Level* pLevel, const MR_MainCharacter* pViewingCharacter );
      void Render3DView(        const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage );
      void CallRender3DViewSafe( const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage );

      void DrawWFSection       ( const MR_Level* pLevel, const MR_SectionId& pSectionId, MR_UInt8 pColor );
      void RenderRoomWalls     ( const MR_Level* pLevel, int pRoomId, MR_SimulationTime pTime );
      void RenderFeatureWalls  ( const MR_Level* pLevel, int pFeatureId, MR_SimulationTime pTime );
      void RenderFloorOrCeiling( const MR_Level* pLevel, const MR_SectionId& pSectionId, BOOL pFloor, MR_SimulationTime pTime );

      static void DrawBackground( MR_VideoBuffer* pDest );

   public:
      static MR_Observer* New();        // Local construction and destruction
      void                Delete();     // to be able to change the dll without problem


      // Camera control
      void Scroll( int pOffset );
      void ZoomIn();
      void ZoomOut();
      void Home();

      void EnlargeMargin();
      // VR stereo: render the next frame from an eye shifted pOffsetMm sideways. pReuseCamera makes
      // the second eye of a frame use exactly the same smoothed centre camera as the first.
      void SetEyeOffset( int pOffsetMm, BOOL pReuseCamera ) { mEyeOffset = pOffsetMm; mReuseCamera = pReuseCamera; }
      void ReduceMargin();

      void MoreMessages();
      void PlayersListPageDn();

      void SetCockpitView( BOOL pOn );
      void SetGhost( const MR_MainCharacter* pGhost ) { mGhost = pGhost; }
      void SetGhostGapText( const char* pText );
      // Modern look. pEnable turns the hints and shadows on (the camera and the field of view are never touched); pLut maps a palette index to a darker one (256 entries,
      // copied) for the shadows; pReducedMotion keeps the impact flash and the mine's pulse still.
      void SetModernEffects( BOOL pEnable, BOOL pShadows, BOOL pReducedMotion, const MR_UInt8* pShadowLut );
      // True colour with filtered textures (needs SetModernEffects): pPalette768 is the palette the picture is shown through (256 RGB triples, copied).
      void SetModernTextures( BOOL pEnable, const MR_UInt8* pPalette768 );
      // The HUD's words (race clock, lap, status, callouts, chat) as text, for a client that draws them in a typeface. The bars, meters and map are still drawn here.
      void SetExternalHudText( BOOL pExternal ) { mExternalHudText = pExternal; }
      const HudText& GetHudText() const { return mHudText; }
      // The graphics card draws the scene (GpuFinisher.h): walls, floors and craft are handed to the sink as triangles instead of being rasterised here, and the frame's hints
      // say so. Pass NULL to draw on the processor again. The camera is the same either way.
      void SetSceneSink( MR_SceneSink* pSink ) { mSceneSink = pSink; m3DView.SetSceneSink( pSink ); }
      void SetModernLighting( BOOL pEnable ) { mModernLighting = pEnable; }   // light pools, headlight and haze (needs SetModernEffects)
      const HoverNetGfx::FrameHints& GetFrameHints() const { return mHints; }
      void SetChaseScale( int pPercent ) { mChaseScale = pPercent < 40 ? 40 : (pPercent > 300 ? 300 : pPercent); }
      void SetNetworkLatency( int pLatencyMs );
      void SetLargeHudText( BOOL pEnabled );
      // Text paired with the legacy colour-changing fuel meter, so critical
      // state never depends on colour perception alone.
      static const char* GetFuelWarningForPercent( int pFuelPercent );
      static const char* GetLatencyWarningForMilliseconds( int pLatencyMs );


      void SetSplitMode( eSplitMode pMode );
      
      // Rendering function
      void RenderDebugDisplay( MR_VideoBuffer* pDest, const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage );
      void RenderNormalDisplay( MR_VideoBuffer* pDest, const MR_ClientSession* pSession, const MR_MainCharacter* pViewingCharacter, MR_SimulationTime pTime, const MR_UInt8* pBackImage );

      void PlaySounds( const MR_Level* pLevel, MR_MainCharacter* pViewingCharacter );
      void PlaySoundsSafe( const MR_Level* pLevel, MR_MainCharacter* pViewingCharacter );

};



#undef MR_DllDeclare

#endif

