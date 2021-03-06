
// HighObserver.cpp
// Global HUD.
//
// Copyright (c) 2009 Michael Imamura.
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

#include <boost/format.hpp>

#include "../../engine/Util/Config.h"
#include "../../engine/VideoServices/Viewport2D.h"
#include "../../engine/VideoServices/FontSpec.h"
#include "../../engine/VideoServices/MultipartText.h"
#include "../../engine/VideoServices/NumericGlyphs.h"
#include "../../engine/VideoServices/StaticText.h"
#include "../../engine/VideoServices/VideoBuffer.h"

#include "ClientSession.h"

#include "HighObserver.h"

using namespace HoverRace;
using HoverRace::Util::Config;
using HoverRace::Util::OS;
using namespace HoverRace::VideoServices;

#define STATS_COLOR 0x47

namespace HoverRace {
namespace Client {

HighObserver::HighObserver()
{
	const Config *cfg = cfg->GetInstance();

	viewport = new VideoServices::Viewport2D();

	statsFont = new FontSpec("Arial", 12);
	statsNumGlyphs = new NumericGlyphs(*statsFont, STATS_COLOR);

	fpsTxt = new MultipartText(*statsFont, statsNumGlyphs, STATS_COLOR);
	*fpsTxt << pgettext("HUD|Frames Per Second","FPS") << ": " <<
		boost::format("%0.2f", OS::locale);
}

HighObserver::~HighObserver()
{
	delete fpsTxt;

	delete statsNumGlyphs;
	delete statsFont;

	delete viewport;
}

/**
 * Render the stats layer.
 * @param session The current client session.
 */
void HighObserver::RenderStats(const ClientSession *session) const
{
	Config *cfg = Config::GetInstance();

	if (cfg->runtime.showFramerate)
		fpsTxt->Blt(2, 2, viewport, session->GetCurrentFramerate());
}

/**
 * Render into the video buffer.
 * @param dest The target video buffer.
 * @param session The current client session.
 */
void HighObserver::Render(VideoServices::VideoBuffer *dest, const ClientSession *session)
{
	viewport->Setup(dest, 0, 0, dest->GetXRes(), dest->GetYRes());

	RenderStats(session);
}

}  // namespace Client
}  // namespace HoverRace
