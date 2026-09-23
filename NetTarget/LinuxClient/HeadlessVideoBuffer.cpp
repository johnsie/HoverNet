#include "../VideoServices/VideoBuffer.h"

#include <cstring>

MR_VideoBuffer::MR_VideoBuffer(HWND, double gamma, double contrast, double brightness)
    : mSpecialWindowMode(FALSE), mSpecialModeXRes(0), mSpecialModeYRes(0),
      mModeSettingInProgress(FALSE), mX0(0), mY0(0), mXRes(0), mYRes(0), mLineLen(0),
      mZBuffer(nullptr), mBuffer(nullptr), mBackPalette(nullptr), mIconMode(FALSE),
      mGamma(gamma), mContrast(contrast), mBrightness(brightness)
{
}

MR_VideoBuffer::~MR_VideoBuffer()
{
    delete []mBuffer;
    delete []mZBuffer;
    delete []mBackPalette;
}

BOOL MR_VideoBuffer::SetVideoMode() { return SetVideoMode(800, 600); }

BOOL MR_VideoBuffer::SetVideoMode(int width, int height)
{
    if (width <= 0 || height <= 0) return FALSE;
    delete []mBuffer;
    delete []mZBuffer;
    mXRes = width;
    mYRes = height;
    mLineLen = width;
    mBuffer = new MR_UInt8[width * height];
    mZBuffer = new MR_UInt16[width * height];
    Clear();
    ClearZ();
    return TRUE;
}

void MR_VideoBuffer::Flip() {}
void MR_VideoBuffer::EnterIconMode() { mIconMode = TRUE; }
void MR_VideoBuffer::ExitIconMode() { mIconMode = FALSE; }
void MR_VideoBuffer::AssignPalette() {}
BOOL MR_VideoBuffer::TryToSet256ColorMode() { return FALSE; }
BOOL MR_VideoBuffer::IsWindowMode() const { return TRUE; }
BOOL MR_VideoBuffer::IsIconMode() const { return mIconMode; }
BOOL MR_VideoBuffer::IsModeSettingInProgress() const { return mModeSettingInProgress; }
void MR_VideoBuffer::CreatePalette(double gamma, double contrast, double brightness)
{
    mGamma = gamma;
    mContrast = contrast;
    mBrightness = brightness;
}
void MR_VideoBuffer::GetPaletteAttrib(double& gamma, double& contrast, double& brightness)
{
    gamma = mGamma;
    contrast = mContrast;
    brightness = mBrightness;
}
void MR_VideoBuffer::SetBackPalette(MR_UInt8* palette)
{
    delete []mBackPalette;
    mBackPalette = palette;
}
const MR_UInt8* MR_VideoBuffer::GetBackPalette()const { return mBackPalette; }
BOOL MR_VideoBuffer::Lock() { return mBuffer != nullptr; }
void MR_VideoBuffer::Unlock() {}
int MR_VideoBuffer::GetXRes() const { return mXRes; }
int MR_VideoBuffer::GetYRes() const { return mYRes; }
int MR_VideoBuffer::GetLineLen() const { return mLineLen; }
int MR_VideoBuffer::GetZLineLen() const { return mXRes; }
MR_UInt8* MR_VideoBuffer::GetBuffer() { return mBuffer; }
MR_UInt16* MR_VideoBuffer::GetZBuffer() { return mZBuffer; }
int MR_VideoBuffer::GetXPixelMeter() const { return mXRes * 3; }
int MR_VideoBuffer::GetYPixelMeter() const { return mYRes * 4; }
void MR_VideoBuffer::Clear(MR_UInt8 color)
{
    if (mBuffer) std::memset(mBuffer, color, mLineLen * mYRes);
}
void MR_VideoBuffer::ClearZ(MR_UInt8 depth)
{
    if (mZBuffer) std::memset(mZBuffer, depth, sizeof(MR_UInt16) * mXRes * mYRes);
}
BOOL MR_VideoBuffer::InitDirectDraw() { return FALSE; }
void MR_VideoBuffer::DeleteInternalSurfaces() {}
void MR_VideoBuffer::ReturnToWindowsResolution() {}