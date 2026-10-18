// CraftNames.h -- the selectable craft ("hover models") a player can pick during the countdown, by the number the simulation uses for them.
// Records are kept per craft: the craft changes how fast a hovercraft is (its top speed, acceleration, weight), so a time in one is not a time in another.
// The order is the order the pick-your-craft screen cycles through (MainCharacter.cpp, MR_SELECTABLE_HOVER_MODEL_COUNT).
#ifndef CRAFT_NAMES_H
#define CRAFT_NAMES_H

namespace Authority
{
constexpr int kCraftCount = 5;

inline const char* CraftName(int craft)
{
    static const char* const names[kCraftCount] = {"Basic", "Hi-Tech", "Bi-Turbo", "Eon", "Manta"};
    return craft >= 0 && craft < kCraftCount ? names[craft] : "Unknown craft";
}
}

#endif
