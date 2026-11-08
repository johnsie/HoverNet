// The list of everything a track can refer to by factory id: surface textures (walls, floors, ceilings) and the elements an author places.
//
// Why it exists: the game's object factory is a `switch` on a class id, repeated in the Windows plug-in, the Linux/web plug-in and the
// simulation (NetTarget/ObjFac1). Nothing can list what those switches know. The HoverNet track editor must offer exactly what the game
// can load (docs/roadmap-track-editor.md, parity rule 1), so this is the one table the editor reads, and the ctest HoverNetTrackElementCatalogue
// checks it against the real factory in both directions: an id the factory can create that is missing here fails, and so does an id listed
// here that the factory cannot create or creates as the wrong kind. Adding a track element or texture to the game therefore forces this
// table, and with it the editor, to be updated in the same change.
//
// Header only and free of platform code, so the native editors and the WebAssembly editor share it.
#pragma once

#include <cstddef>

namespace HoverNet
{
enum class TrackElementKind
{
    Surface,         // a texture for a wall, floor or ceiling (an MR_SurfaceElement)
    PlacedElement,   // a free element an author places in a room (pickups, speed pads, race gates)
    RuntimeElement,  // a free element the game creates during a race (weapons, craft); a track never places one
    Support,         // created through the same factory but not a track element (the renderer, fonts and icons)
};

struct TrackElementInfo
{
    unsigned short id;       // the class id in factory 1 (the "dll id" 1 in the track source's `1, 52` pairs)
    TrackElementKind kind;
    const char* name;        // the name the editor shows
};

// Sorted by id, ids unique. Factory number 1 is the only one tracks use. This is what the shared client (Windows x64, Linux, web) can create.
// Ids only the legacy Win32 factory (NetTarget/ObjFac1/main.cpp) knows are left out on purpose: 12 (the ball) was found missing from the shared
// plug-in by the catalogue test on 8 November 2026; a track that uses it cannot be played by the shared client.
constexpr TrackElementInfo kTrackElementCatalogue[] = {
    {1, TrackElementKind::Surface, "Classic default"},
    {2, TrackElementKind::Surface, "Classic wood"},
    {3, TrackElementKind::Surface, "Classic fire"},
    {4, TrackElementKind::Surface, "Classic big brick"},
    {5, TrackElementKind::Surface, "Classic red brick"},
    {10, TrackElementKind::RuntimeElement, "Demo fighter (test craft)"},
    {13, TrackElementKind::RuntimeElement, "Electro car (test craft)"},
    {50, TrackElementKind::Surface, "Plain (no bitmap)"},
    {51, TrackElementKind::Surface, "Standard floor"},
    {52, TrackElementKind::Surface, "Standard right wall"},
    {53, TrackElementKind::Surface, "Standard left wall"},
    {54, TrackElementKind::Surface, "Red right wall (off)"},
    {55, TrackElementKind::Surface, "Red left wall (off)"},
    {56, TrackElementKind::Surface, "Green right wall (off)"},
    {57, TrackElementKind::Surface, "Green left wall (off)"},
    {58, TrackElementKind::Surface, "Step wall"},
    {59, TrackElementKind::Surface, "Pass right wall"},
    {60, TrackElementKind::Surface, "Pass left wall"},
    {61, TrackElementKind::Surface, "Do not enter wall 1"},
    {62, TrackElementKind::Surface, "Do not enter wall 2"},
    {63, TrackElementKind::Surface, "Blue bubble floor"},
    {64, TrackElementKind::Surface, "Speed zone"},
    {65, TrackElementKind::Surface, "Fuel zone"},
    {66, TrackElementKind::Surface, "Yellow step"},
    {67, TrackElementKind::Surface, "Checker"},
    {68, TrackElementKind::Surface, "Pit word"},
    {69, TrackElementKind::Surface, "Finish word"},
    {70, TrackElementKind::Surface, "Yellow neon"},
    {71, TrackElementKind::Surface, "Yellow neon (variant)"},
    {72, TrackElementKind::Surface, "Standard wall"},
    {73, TrackElementKind::Surface, "Standard wall top"},
    {100, TrackElementKind::Support, "Craft renderer"},
    {150, TrackElementKind::RuntimeElement, "Missile"},
    {151, TrackElementKind::RuntimeElement, "Mine"},
    {152, TrackElementKind::PlacedElement, "Power-up"},
    {170, TrackElementKind::PlacedElement, "Bumper gate"},
    {200, TrackElementKind::PlacedElement, "Fuel source"},
    {201, TrackElementKind::PlacedElement, "Double speed source (speed pad)"},
    {202, TrackElementKind::PlacedElement, "Finish line"},
    {203, TrackElementKind::PlacedElement, "Checkpoint 1"},
    {204, TrackElementKind::PlacedElement, "Checkpoint 2"},
    {1000, TrackElementKind::Support, "Font sprite"},
    {1100, TrackElementKind::Support, "Missile status icon"},
    {1101, TrackElementKind::Support, "Craft icons"},
    {1102, TrackElementKind::Support, "Mine status icon"},
    {1103, TrackElementKind::Support, "Power-up status icon"},
};
constexpr std::size_t kTrackElementCatalogueSize = sizeof(kTrackElementCatalogue) / sizeof(kTrackElementCatalogue[0]);

// The entry for an id, or nullptr when the game has no such element.
inline const TrackElementInfo* FindTrackElement(unsigned short id)
{
    for (std::size_t index = 0; index < kTrackElementCatalogueSize; ++index) {
        if (kTrackElementCatalogue[index].id == id) return &kTrackElementCatalogue[index];
    }
    return nullptr;
}
}  // namespace HoverNet
