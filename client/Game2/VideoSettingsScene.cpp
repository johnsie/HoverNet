
// VideoSettingsScene.cpp
//
// Copyright (c) 2014 Michael Imamura.
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

#include "../StdAfx.h"

#include "../../engine/Display/Label.h"
#include "../../engine/Display/Slider.h"
#include "../../engine/Util/Log.h"

#include "VideoSettingsScene.h"

using namespace HoverRace::Util;

namespace HoverRace {
namespace Client {

VideoSettingsScene::VideoSettingsScene(Display::Display &display,
	GameDirector &director) :
	SUPER(display, director, _("Video"), "Video Settings"),
	videoCfg(Config::GetInstance()->video), origVideoCfg(videoCfg)
{
	using namespace Display;
	const auto &s = display.styles;

	textScaleSlider = AddSetting(_("Text Scale"),
		new Slider(display, 0.5, 1.5, 0.1));
	textScaleSlider->SetSize(SLIDER_SIZE);
	textScaleConn = textScaleSlider->GetValueChangedSignal().connect([&](double val) {
		this->textScalePreviewLbl->SetScale(val / 2.0);
		videoCfg.textScale = val;
	});

	// Use a 2x scaled font so the >1.x preview doesn't get blurred / pixelated.
	auto scaledFont = s.bodyFont;
	scaledFont.size *= 2;

	textScalePreviewLbl = AddSetting("",
		new Label(_("This is sample text."), scaledFont, s.bodyFg));
	textScalePreviewLbl->SetFixedScale(true);
	textScalePreviewLbl->SetScale(videoCfg.textScale / 2.0);
}

void VideoSettingsScene::LoadFromConfig()
{
	origVideoCfg = videoCfg;
	textScaleSlider->SetValue(videoCfg.textScale);
}

void VideoSettingsScene::ResetToDefaults()
{
	videoCfg.ResetToDefaults();
}

void VideoSettingsScene::OnOk()
{
	bool needsMainMenu =
		origVideoCfg.textScale != videoCfg.textScale;

	Config::GetInstance()->Save();
	SUPER::OnOk();

	if (needsMainMenu) {
		director.RequestMainMenu();
	}
}

void VideoSettingsScene::OnCancel()
{
	videoCfg = origVideoCfg;
	SUPER::OnCancel();
}

}  // namespace Client
}  // namespace HoverRace
