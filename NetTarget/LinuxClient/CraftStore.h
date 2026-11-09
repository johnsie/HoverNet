// CraftStore.h -- where a player's craft designs are kept on this device: <config>/craft/ with up to kSavedDesigns designs and the one in use.
// On the web client the config folder is the browser's IndexedDB-backed folder, so "kept in the browser" is this same code. The optional signed-in
// account service has a separate craft-design API; the Garage does not currently synchronize with it (see docs/craft-designer.md). Nothing here is
// ever read by the simulation.
#ifndef HOVERNET_CRAFT_STORE_H
#define HOVERNET_CRAFT_STORE_H

#include "../Authority/CraftDesign.h"

#include <string>
#include <vector>

namespace HoverNetCraft
{
constexpr int kSavedDesigns = 8;

class CraftStore
{
public:
    explicit CraftStore(const std::string& configDir) : mDir(configDir + "/craft") {}
    // The design in saved slot 0..kSavedDesigns-1; false when the slot is empty or damaged.
    bool Load(int index, Authority::CraftDesign& design) const;
    bool Save(int index, const Authority::CraftDesign& design) const;
    bool Delete(int index) const;
    // The saved slot used in races, or -1 for the stock craft.
    int Active() const;
    bool SetActive(int index) const;
    // The design to race with (the active one); false = stock.
    bool LoadActive(Authority::CraftDesign& design) const;
    std::vector<std::uint8_t> ActiveBytes() const;   // EncodeCraftDesign of it, empty when stock
private:
    std::string PathFor(int index) const;
    std::string mDir;
};
}

#endif
