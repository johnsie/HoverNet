
// Hud.cpp
//
// Copyright (c) 2013 Michael Imamura.
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

#include "StdAfx.h"

#include "Hud.h"

namespace HoverRace {
namespace Display {

/**
 * Constructor.
 * @param display The display child elements will be attached to.
 * @param size The size of the container.
 * @param clip Enable (default) or disable clipping of child widgets.
 * @param layoutFlags Optional layout flags.
 */
Hud::Hud(Display &display, MainCharacter::MainCharacter *player,
         const Vec2 &size, bool clip,
         uiLayoutFlags_t layoutFlags) :
	SUPER(display, size, clip, layoutFlags),
	player(player), visible(true)
{
}

/**
 * Change the player being targeted by the HUD.
 * @param player The target player (may be @c nullptr).
 */
void Hud::SetPlayer(MainCharacter::MainCharacter *player)
{
	if (this->player != player) {
		this->player = player;
		ForEachHudChild([&](std::shared_ptr<HudDecor> &child) {
			child->SetPlayer(player);
		});
	}
}

/**
 * Set the visibility of the HUD.
 * @param visible @c true for a visible HUD, @c false for invisible.
 */
void Hud::SetVisible(bool visible)
{
	if (this->visible != visible) {
		this->visible = visible;
		FireModelUpdate(Props::VISIBLE);
	}
}

void Hud::Advance(Util::OS::timestamp_t tick)
{
	ForEachHudChild([&](std::shared_ptr<HudDecor> &child) {
		child->Advance(tick);
	});
}

}  // namespace Display
}  // namespace HoverRace
