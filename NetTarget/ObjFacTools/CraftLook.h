// CraftLook.h -- draws a player's craft design (Authority/CraftDesign.h) on the game's own craft models: reshaped, tinted and painted. A look only.
//
// The simulation, the hit boxes and every race number come from the craft the player drives (CraftNames.h) and never see a design; this module only
// builds the model that is drawn. Designs are kept by player slot (the number the renderer is given as the "hover id"); a slot with no design draws stock.
#ifndef HOVERNET_CRAFT_LOOK_H
#define HOVERNET_CRAFT_LOOK_H

#include "../Authority/CraftDesign.h"

class MR_ResActor;
class MR_ResBitmap;

namespace HoverNetCraft
{
// Sets (or, with nullptr, clears) the design drawn on the craft of a player slot (0..15). Safe to call from any thread.
void SetSlotDesign(int slot, const Authority::CraftDesign* design);
bool HasSlotDesign(int slot);
void ClearSlotDesigns();

struct Look
{
    const MR_ResActor* actor;           // the model to draw: the stock one, or the reshaped copy
    const MR_ResBitmap* cockpit;        // the cockpit bitmap to substitute (stock, or tinted in the accent colour); may be null
};
// What to draw for a slot: the stock model and cockpit when the slot has no design, otherwise a copy made (once, then kept) for that design.
Look Resolve(int slot, const MR_ResActor* stockActor, const MR_ResBitmap* stockCockpit);

// Frees every copy (when the track or the resource library goes away).
void ClearCache();

// Pure colour functions, exposed for the tests and for the editor's flat preview. Colours are 0x00RRGGBB.
std::uint32_t TintColour(std::uint32_t pixel, const std::uint8_t rgb[3], int strength);
std::uint32_t PaintColour(std::uint32_t pixel, const std::uint8_t rgb[3]);   // opaque paint, shaded by the pixel under it
// Which of a craft's textures are hull (body tint and paint apply), cockpit/trim (accent), or neither (engines, driver, flames).
enum class Part { eHull, eCockpit, eOther };
Part PartOfBitmap(int resourceId);
}

#endif
