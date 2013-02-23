
// PaletteScene.cpp
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

#include "../../engine/Display/Display.h"
#include "../../engine/VideoServices/VideoBuffer.h"

#include "PaletteScene.h"

namespace HoverRace {
namespace Client {

PaletteScene::PaletteScene(Display::Display &display) :
	SUPER(),
	display(display)
{
}

PaletteScene::~PaletteScene()
{
}

void PaletteScene::Advance(Util::OS::timestamp_t)
{
	// Do nothing.
}

void PaletteScene::Render()
{
	const VideoServices::VideoBuffer &legacyDisplay = display.GetLegacyDisplay();
	int pitch = legacyDisplay.GetPitch();
	MR_UInt8 *buf = legacyDisplay.GetBuffer();
	for (int y = 0; y < 256; y++, buf += pitch) {
		if ((y % 16) == 0) continue;
		MR_UInt8 *cur = buf;
		for (int x = 0; x < 256; x++, cur++) {
			if ((x % 16) == 0) continue;
			*cur = ((y >> 4) << 4) + (x >> 4);
		}
	}
}

}  // namespace HoverScript
}  // namespace Client