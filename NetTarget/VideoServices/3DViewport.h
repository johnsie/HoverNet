// HoverNet modification notice: Modified for HoverNet; latest recorded change 2026-11-09.
// See repository history for authorship and detailed changes.
// 3DViewport.h
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


#ifndef _3D_VIEWPORT_H
#define _3D_VIEWPORT_H

#include "2DViewport.h"
#include "ColorPalette.h"
#include "Bitmap.h"
#include "Patch.h"

#if defined(_WIN32) && !defined(HOVERNET_STATIC)
#ifdef MR_VIDEO_SERVICES
   #define MR_DllDeclare   __declspec( dllexport )
#else
   #define MR_DllDeclare   __declspec( dllimport )
#endif
#else
   #define MR_DllDeclare
#endif

// define
#define MR_BACK_X_RES 2048
#define MR_BACK_Y_RES  256

// Helper class
class MR_PositionMatrix
{
   public:
      MR_Int32        mRotation[2][2];
      MR_3DCoordinate mDisplacement; 
};



// The scene as triangles for a renderer that is not this one (the graphics card: GpuFinisher.h). With a sink set on a viewport, the Render* services below
// do not draw: they hand the surface over as triangles in camera space (x forward, y to the left, z up, in mm) with texture coordinates (u along the
// bitmap's columns, v down its rows, 1.0 = the bitmap once; the texture repeats), the bitmap, the flat palette colour for a patch with no bitmap, and the
// surface tag. The camera is not touched: positions are what the software rasteriser's own transforms give.
struct MR_SceneVertex { float x, y, z, u, v; };

class MR_SceneSink
{
   public:
      virtual ~MR_SceneSink() {}
      virtual void Triangles( const MR_SceneVertex* pVertices, int pCount, const MR_Bitmap* pBitmap, MR_UInt8 pFlatIndex, MR_UInt8 pTag ) = 0;
      virtual void Shadow( int pCenterX, int pCenterY, int pRadiusX, int pRadiusY, int pDepthMm, int pMarginMm ) = 0;   // frame coordinates, as ShadeEllipse
};

class MR_3DViewPort: public MR_2DViewPort
{   
   protected:

      class BackColumn
      {
         public:
            MR_Int32 mBitmapColumn;
            MR_Int32 mLineIncrement_1024;
      };


           
      //Camera setting
      MR_3DCoordinate mPosition;
      MR_Angle        mOrientation;           // Angle from the XAxis
      int             mScroll;                // Offset to YCenter
      MR_Angle        mVAngle;                // Vertical field angle width
      MR_Int32        mPlanDist;              // ProjectionPlan (screen) distance
      MR_Int32        mPlanHW;                // Horizontal Half
      MR_Int32        mPlanVW;                // Vertical Half 

      MR_SceneSink*   mSink;                  // optional: when set the Render* services emit triangles to it instead of drawing
      MR_UInt8*       mTagBase;               // optional: a buffer laid out exactly like the video buffer, that gets a surface tag per pixel drawn
      MR_UInt8        mSurfaceTag;            // the tag the next surfaces drawn are marked with
      MR_UInt32*       mRgbBase;              // optional true-colour output: one 0x00RRGGBB word per pixel, at the same pixel offsets as the video buffer
      const MR_UInt32* mPalette;              // 256 words: what a texel's palette index means in true colour
      BOOL            mFilter;                // filter textures bilinearly (needs the colour buffer)

      MR_UInt16*  mZBuffer;
      int         mZLineLen;

      MR_UInt8**  mBufferLine;
      MR_UInt16** mZBufferLine;

      // Usefull pre-defined constants
      MR_Int32        mHVarPerDInc_16384;     // Ray divergence by HPixel
      MR_Int32        mVVarPerDInc_16384;     // Ray divergence by VPixel
      MR_Int32        mXRes_PlanDist;
      MR_Int32        mYRes_PlanDist;

      MR_Int32        mPlanHW_PlanDist_2_XRes_16384; // 16384*mPlanHW/(mPlanDist*2*mXRes)
      MR_Int32        mXRes_PlanDist_2PlanHW_4096;   // mXRes*mPlanDist*2048/mPlanHW

      BackColumn*     mBackgroundConst;      // Constants used to display each bitmap column     

      MR_Int32  mRotationMatrix[3][3];

      void ComputeRotationMatrix();
      void ComputeBackgroundConst();

      void ApplyRotationMatrix( const MR_3DCoordinate& pSrc, MR_3DCoordinate& pDest )const;
      void ApplyRotationMatrix( const MR_2DCoordinate& pSrc, MR_2DCoordinate& pDest )const;
      void ApplyPositionMatrix( const MR_PositionMatrix& pMatrix, const MR_3DCoordinate& pSrc, MR_3DCoordinate& pDest )const;

      // The services for the sink (3DViewportScene.cpp)
      void EmitWall( const MR_3DCoordinate& pUpperLeft, const MR_3DCoordinate& pLowerRight, MR_Int32 pLen, const MR_Bitmap* pBitmap );
      void EmitHorizontal( int pNbVertex, const MR_2DCoordinate* pVertexList, MR_Int32 pLevel, BOOL pTop, const MR_Bitmap* pBitmap );
      void EmitPatch( const MR_Patch& pPatch, const MR_PositionMatrix& pMatrix, const MR_Bitmap* pBitmap, MR_UInt8 pFlatIndex );

      MR_DllDeclare void OnMetricsChange( int pMetrics );

   public:

      MR_DllDeclare MR_3DViewPort();
      MR_DllDeclare ~MR_3DViewPort();

      MR_DllDeclare void SetSceneSink( MR_SceneSink* pSink ) { mSink = pSink; }
      MR_DllDeclare MR_SceneSink* GetSceneSink() const { return mSink; }
      MR_DllDeclare void Setup( MR_VideoBuffer* pBuffer, int pX0, int pY0, int pSizeX, int pSizeY, MR_Angle pApperture, int pMetrics = eNone );

      MR_DllDeclare void SetupCameraPosition( const MR_3DCoordinate& pPosition,
                                              MR_Angle               pOrientation,
                                              int                    pScroll       );

      MR_DllDeclare void ClearZ();

      // Surface tags: a second, optional output of the rasteriser. When a tag buffer is set (a byte per pixel, laid out exactly like the video
      // buffer: the same line length and size), every pixel a wall, floor, ceiling or object is drawn on also gets the current surface tag, under
      // the same depth test as the colour. ClearZ() clears the tags to 0. Without a tag buffer (the default) nothing extra is done and the
      // picture is exactly as before. The meaning of a tag is the caller's; HoverNetGfx::SurfaceTag (ModernGraphics.h) names the ones the
      // observer uses, with 0 meaning nothing was drawn (the sky).
      MR_DllDeclare void SetTagBuffer( MR_UInt8* pBase );
      MR_DllDeclare void SetSurfaceTag( MR_UInt8 pTag ) { mSurfaceTag = pTag; }
      MR_DllDeclare const MR_UInt8* GetTagLine( int pY ) const;

      // True colour: a third, optional output. With a colour buffer set (a 32-bit word 0x00RRGGBB per pixel, at the same pixel offset as in the
      // video buffer, so the buffer must hold lineLen * height words) every pixel drawn also gets its colour, found through pPalette (256 words). With texture filtering on, magnified textures are filtered bilinearly in true colour instead of picking the nearest texel, which
      // takes the blockiness out of walls, floors and objects seen close up. The 8-bit picture is written exactly as before either way. Pixels the
      // rasteriser does not draw (the background) are left alone: the caller fills them. Pass NULL to switch it off (the default).
      MR_DllDeclare void SetColourBuffer( MR_UInt32* pRgbBase, const MR_UInt32* pPalette256 );
      MR_DllDeclare void SetTextureFiltering( BOOL pOn ) { mFilter = pOn; }
      MR_DllDeclare const MR_UInt32* GetColourLine( int pY ) const;   // the colour words of row pY of the viewport, or NULL when there is no colour buffer

      MR_DllDeclare BOOL ComputePositionMatrix( MR_PositionMatrix& pMatrix, const MR_3DCoordinate& pPosition, MR_Angle pOrientation, MR_Int32 pMaxObjRay );

      // WireFrame services
      MR_DllDeclare void DrawWFLine(  const MR_3DCoordinate& pP0, const MR_3DCoordinate& pP1, MR_UInt8 pColor );

      // Rendering services ( availlable in the file 3DViewportRendering.cpp )
      MR_DllDeclare void RenderWallSurface( const MR_3DCoordinate& pUpperLeft, const MR_3DCoordinate& pLowerRight, MR_Int32 pLen, const MR_Bitmap* pBitmap );
      MR_DllDeclare void RenderAlternateWallSurface( const MR_3DCoordinate& pUpperLeft, const MR_3DCoordinate& pLowerRight, MR_Int32 pLen, const MR_Bitmap* pBitmap, const MR_Bitmap* pBitmap2, int pSerialLen, int pSerialStart );

      MR_DllDeclare void RenderHorizontalSurface( int lNbVertex, const MR_2DCoordinate* pVertexList, MR_Int32 pLevel, BOOL lTop, const MR_Bitmap* pBitmap );

      MR_DllDeclare void RenderPatch( const MR_Patch& pPatch, const MR_PositionMatrix& pMatrix , const MR_Bitmap* pBitmap );
      MR_DllDeclare void RenderPatch( const MR_Patch& pPatch, const MR_PositionMatrix& pMatrix , MR_UInt8 pColor );


      MR_DllDeclare void RenderBackground( const MR_UInt8* pBitmap );

      // Presentation helpers for the optional modern look (docs/roadmap-3.0.md); the classic renderer never calls them.
      // Where a world point lands in the framebuffer (not the viewport), how far it is from the camera in mm, and how many pixels a
      // metre is across at that distance. FALSE when the point is behind the camera.
      MR_DllDeclare BOOL ProjectToScreen( const MR_3DCoordinate& pPoint, int& pX, int& pY, int& pDepthMm, int& pPixelsPerMetre ) const;

      // Darkens an ellipse of the picture (a soft shadow): each pixel inside is replaced through pDarkLut. Only pixels at least as far
      // from the camera as pDepthMm-pMarginMm are touched, so a wall between the camera and the shadow is left alone.
      // A world point in camera space, in mm: x forward (the depth), y to the left, z up. FALSE when it is behind the camera.
      MR_DllDeclare BOOL ToCameraSpace( const MR_3DCoordinate& pPoint, int& pX, int& pY, int& pZ ) const;

      // What is needed to turn a pixel and its depth back into camera space (see ModernGraphics.h): mm of sideways / up distance per pixel
      // per mm of depth, the pixel row of the horizon line, and the viewport's corner in the framebuffer.
      MR_DllDeclare void GetProjection( double& pKx, double& pKz, int& pCenterX, int& pCenterY, int& pOriginX, int& pOriginY ) const;

      // One row of the depth buffer, in 4 mm units (0xFFFF where nothing was drawn).
      MR_DllDeclare const MR_UInt16* GetDepthLine( int pY ) const;

      MR_DllDeclare void ShadeEllipse( int pCenterX, int pCenterY, int pRadiusX, int pRadiusY, int pDepthMm, int pMarginMm, const MR_UInt8* pDarkLut );
};

// Local constants (Used by the cpp of this module)
#define MR_ZBUFFER_LIMIT  0xFFFE
#define MR_ZBUFFER_UNIT        4 // Each increment is 4 mm
                                 // max deep of field is 260m
#define MR_MAX_POLYGON_VERTEX 16


#undef MR_DllDeclare

#endif



