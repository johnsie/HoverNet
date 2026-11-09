#include "../MainCharacter/StdAfx.h"
#include "CraftPreview.h"

#include <algorithm>

#include "../ObjFac1/ObjFac1Res.h"
#include "../ObjFacTools/CraftLook.h"
#include "../ObjFacTools/ResActor.h"
#include "../ObjFacTools/ResBitmap.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/VideoBuffer.h"

namespace HoverNetCraft
{
namespace
{
constexpr int kPreviewSlot = 15;   // the slot the preview borrows in CraftLook; no race is running while the Garage is open
}

bool RenderCraftPreview(MR_ResourceLib& pLib, const Authority::CraftDesign* pDesign, int pCraft, int pYawDegrees, const std::uint32_t* pPalette256,
                        int pWidth, int pHeight, std::vector<std::uint32_t>& pRgba)
{
    static const int kActors[5] = {MR_ELECTRO_CAR, MR_HITECH_CAR, MR_BITURBO_CAR, 19, MR_MANTA_CRAFT};
    if (pCraft < 0 || pCraft > 4 || pWidth < 16 || pHeight < 16) return false;
    const MR_ResActor* stock = pLib.GetActor(kActors[pCraft]);
    if (stock == nullptr) return false;
    const MR_ResBitmap* cockpit = pLib.GetBitmap((pCraft == 1 ? MR_CAR_COCKPIT21 : MR_CAR_COCKPIT1) + 0);

    SetSlotDesign(kPreviewSlot, pDesign);
    const Look look = Resolve(kPreviewSlot, stock, cockpit);

    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(pWidth, pHeight) || !buffer.Lock()) { SetSlotDesign(kPreviewSlot, nullptr); return false; }
    MR_3DViewPort viewport;
    // The camera is set from the model's size so every craft fills the picture alike; this is the Garage's own view, not the race camera.
    MR_3DCoordinate lo, hi;
    stock->GetBounds(lo, hi);
    const int extent = std::max(hi.mX - lo.mX, 2 * std::max(hi.mY, -lo.mY));
    viewport.Setup(&buffer, 0, 0, pWidth, pHeight, 640);
    std::vector<MR_UInt32> colour(static_cast<std::size_t>(pWidth) * pHeight, 0);
    viewport.SetColourBuffer(colour.data(), pPalette256);
    viewport.SetTextureFiltering(TRUE);
    viewport.SetupCameraPosition(MR_3DCoordinate(-extent * 14 / 10, 0, (lo.mZ + hi.mZ) / 2 - extent / 25), 0, 0);
    viewport.Clear(255);
    viewport.ClearZ();
    MR_PositionMatrix matrix;
    const MR_Angle yaw = MR_NORMALIZE_ANGLE(pYawDegrees * MR_2PI / 360);
    bool drawn = false;
    if (viewport.ComputePositionMatrix(matrix, MR_3DCoordinate(0, 0, 0), yaw, 10000000)) {
        look.actor->Draw(&viewport, matrix, 0, 0);
        drawn = true;
    }
    pRgba.assign(static_cast<std::size_t>(pWidth) * pHeight, 0);
    const MR_UInt8* indices = buffer.GetBuffer();
    for (int y = 0; y < pHeight; ++y) {
        for (int x = 0; x < pWidth; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * pWidth + x;
            std::uint32_t rgb;
            if (indices[at] == 255) {   // not drawn: a soft dark gradient with a lighter floor
                const int shade = 26 + y * 22 / pHeight + (y > pHeight * 7 / 10 ? 14 : 0);
                rgb = (static_cast<std::uint32_t>(shade) << 16) | (static_cast<std::uint32_t>(shade) << 8) | static_cast<std::uint32_t>(shade + 6);
            }
            else rgb = colour[at] != 0 ? colour[at] : pPalette256[indices[at]];
            pRgba[at] = 0xFF000000u | ((rgb & 255) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 255);
        }
    }
    SetSlotDesign(kPreviewSlot, nullptr);
    return drawn;
}
}
