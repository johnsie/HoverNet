// Keeps the editor's element catalogue (TrackElementCatalogue.h) equal to what the game's object factory can create.
// docs/roadmap-track-editor.md, parity rule 1. Two programs' worth of factory are checked: the ObjFac1 plug-in the clients and the
// compiler load, and the simulation's own registry (the server and the determinism tools use only the latter).
#include "../Model/MazeElement.h"
#include "../ObjFac1/SimElements.h"
#include "../Util/DllObjectFactory.h"
#include "TrackElementCatalogue.h"

#include <cstdio>
#include <memory>
#include <set>

namespace
{
const char* KindName(HoverNet::TrackElementKind kind)
{
    switch (kind) {
    case HoverNet::TrackElementKind::Surface: return "surface";
    case HoverNet::TrackElementKind::PlacedElement: return "placed element";
    case HoverNet::TrackElementKind::RuntimeElement: return "runtime element";
    case HoverNet::TrackElementKind::Support: return "support";
    }
    return "?";
}

// What a created object really is.
const char* ActualKind(MR_ObjectFromFactory* object)
{
    if (object == nullptr) return "none";
    if (dynamic_cast<MR_SurfaceElement*>(object) != nullptr) return "surface";
    if (dynamic_cast<MR_FreeElement*>(object) != nullptr) return "free element";
    return "other";
}

bool KindMatches(HoverNet::TrackElementKind wanted, const char* actual)
{
    const std::string a = actual;
    switch (wanted) {
    case HoverNet::TrackElementKind::Surface: return a == "surface";
    case HoverNet::TrackElementKind::PlacedElement:
    case HoverNet::TrackElementKind::RuntimeElement: return a == "free element";
    case HoverNet::TrackElementKind::Support: return a != "none";
    }
    return false;
}
}  // namespace

int main()
{
    int failures = 0;
    auto fail = [&](const char* what, unsigned id, const char* detail) {
        std::fprintf(stderr, "FAIL %s: id %u %s\n", what, id, detail);
        ++failures;
    };

    // The table itself: sorted, unique, named.
    for (std::size_t index = 0; index < HoverNet::kTrackElementCatalogueSize; ++index) {
        const HoverNet::TrackElementInfo& entry = HoverNet::kTrackElementCatalogue[index];
        if (entry.name == nullptr || entry.name[0] == '\0') fail("catalogue", entry.id, "has no name");
        if (index > 0 && HoverNet::kTrackElementCatalogue[index - 1].id >= entry.id) fail("catalogue", entry.id, "is not in ascending order or is repeated");
    }

    // Every id from 0 up to past the highest known one, in both registries.
    std::set<unsigned> factoryIds;
    for (unsigned id = 0; id <= 1300; ++id) {
        const MR_ObjectFromFactoryId factoryId = {1, static_cast<MR_UInt16>(id)};
        std::unique_ptr<MR_ObjectFromFactory> plugin(MR_DllObjectFactory::CreateObject(factoryId));
        std::unique_ptr<MR_ObjectFromFactory> sim(MR_CreateSimElement(static_cast<MR_UInt16>(id)));
        const HoverNet::TrackElementInfo* entry = HoverNet::FindTrackElement(static_cast<unsigned short>(id));

        if (plugin) factoryIds.insert(id);
        if (plugin && entry == nullptr) fail("plug-in", id, "can be created but is not in the catalogue: add it to TrackElementCatalogue.h (and to the editor)");
        if (entry != nullptr) {
            if (!plugin) fail("plug-in", id, "is in the catalogue but the plug-in cannot create it");
            else if (!KindMatches(entry->kind, ActualKind(plugin.get()))) {
                std::fprintf(stderr, "FAIL plug-in: id %u (%s) is catalogued as %s but the plug-in makes a %s\n", id, entry->name, KindName(entry->kind), ActualKind(plugin.get()));
                ++failures;
            }
            // The simulation (server, replays) must know every surface and every element a race can contain.
            if (entry->kind != HoverNet::TrackElementKind::Support && entry->kind != HoverNet::TrackElementKind::Surface) {
                // craft and weapons are created by the simulation too, except the test craft, which only the clients use
                const bool simOptional = id == 10 || id == 13;
                if (!sim && !simOptional) fail("simulation", id, "is in the catalogue but the simulation registry cannot create it");
            }
            if (entry->kind == HoverNet::TrackElementKind::Surface && !sim) fail("simulation", id, "surface is not known to the simulation registry");
        }
        else if (sim) {
            fail("simulation", id, "can be created but is not in the catalogue");
        }
    }

    // Every placed element must be creatable by the compiler's route (a free element), and tracks only use the ones catalogued.
    std::size_t placed = 0, surfaces = 0;
    for (std::size_t index = 0; index < HoverNet::kTrackElementCatalogueSize; ++index) {
        placed += HoverNet::kTrackElementCatalogue[index].kind == HoverNet::TrackElementKind::PlacedElement;
        surfaces += HoverNet::kTrackElementCatalogue[index].kind == HoverNet::TrackElementKind::Surface;
    }
    if (failures == 0) std::printf("Track element catalogue matches the object factory: %zu entries (%zu surfaces, %zu placed elements, %zu ids creatable)\n",
                                   HoverNet::kTrackElementCatalogueSize, surfaces, placed, factoryIds.size());
    return failures == 0 ? 0 : 1;
}
