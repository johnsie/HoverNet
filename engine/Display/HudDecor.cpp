
// HudDecor.cpp
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

#include "HudDecor.h"

namespace HoverRace {
namespace Display {

/**
 * Constructor.
 * @param display The display child elements will be attached to.
 * @param layoutFlags Optional layout flags.
 */
HudDecor::HudDecor(Display &display, uiLayoutFlags_t layoutFlags) :
	SUPER(display, layoutFlags),
	player(nullptr)
{
}

/**
 * Change the target player for this HUD element.
 * @param player The player (may be @c nullptr).
 */
void HudDecor::SetPlayer(MainCharacter::MainCharacter *player)
{
	if (this->player != player) {
		this->player = player;
		FireModelUpdate(Props::PLAYER);
	}
}

}  // namespace Display
}  // namespace HoverRace
