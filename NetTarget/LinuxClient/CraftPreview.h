// CraftPreview.h -- draws a craft, with a player's design on it, into a picture for the Garage (not the race camera: it has its own fixed view).
#ifndef HOVERNET_CRAFT_PREVIEW_H
#define HOVERNET_CRAFT_PREVIEW_H

#include "../Authority/CraftDesign.h"

#include <cstdint>
#include <vector>

class MR_ResourceLib;

namespace HoverNetCraft
{
// Fills pRgba (width x height, 0xAABBGGRR as SDL's RGBA32 wants on a little-endian machine) with the craft (0..4, CraftNames.h) turned pYawDegrees,
// wearing pDesign (nullptr = stock). pPalette256 is the game's palette as 0x00RRGGBB words. False when the craft's model is missing.
// MR_ResBitmap::SetColourPalette must have been called (the design's tint and paint are drawn in true colour).
bool RenderCraftPreview(MR_ResourceLib& pLib, const Authority::CraftDesign* pDesign, int pCraft, int pYawDegrees, const std::uint32_t* pPalette256,
                        int pWidth, int pHeight, std::vector<std::uint32_t>& pRgba, bool pTransparentBackground = false);   // transparent: pixels the craft does not cover get alpha 0 (the main menu)
}

#endif
