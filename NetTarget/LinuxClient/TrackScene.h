// A stylised 3D scene of a track for menu backdrops, drawn with the graphics card (SDL_RenderGeometry): a dusk sky with a sun, a glowing grid, the track as lit slabs with
// neon rails and glowing pads, orbiting. It is built from a TrackPlan (the real level's floors, walls and pads), has its own free camera and is for menus only: it never touches a race.
#pragma once

#include "TrackPlan.h"

#include <SDL.h>

#include <vector>

namespace HoverNetScene
{
// Draws into the current render target, w x h pixels. `yawDegrees` is where the eye is on its orbit; `seconds` drives the slow pulse of the neon.
// `pictures` are the plan's floor and wall pictures on the card, each tiled four by four (null where one could not be made): the track is drawn with the game's own textures where it has them.
void Draw(SDL_Renderer* renderer, int w, int h, const HoverNetPlan::Plan& plan, const std::vector<SDL_Texture*>& pictures, float yawDegrees, float seconds);
}
