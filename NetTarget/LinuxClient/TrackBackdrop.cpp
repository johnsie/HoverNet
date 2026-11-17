#include "TrackBackdrop.h"

#include "../MainCharacter/StdAfx.h"
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <array>

namespace HoverNetBackdrop
{
namespace
{
// The scripted driving the render-golden frames use (motor on, a few turns), so the craft is moving and the view is a real one, not a standing start.
std::uint16_t Drive(std::uint32_t tick)
{
    int c = MR_MainCharacter::eMotorOn;
    const int phase = static_cast<int>(tick / 37) % 3;
    if (phase == 0) c |= MR_MainCharacter::eLeft; else if (phase == 1) c |= MR_MainCharacter::eRight;
    return static_cast<std::uint16_t>(c);
}
}

int Render(const std::string& path, const std::string& name, int width, int height, int frames, int ticksBetween, std::vector<Picture>& out)
{
    out.clear();
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(path.c_str())) { delete track; return 0; }
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(width, height) || !buffer.Lock()) { delete track; return 0; }
    MR_ClientSession session;
    if (!session.LoadNew(name.c_str(), track, 1, FALSE, &buffer) || !session.CreateMainCharacter() || session.GetMainCharacter() == nullptr) return 0;
    MR_Level* level = session.Level();
    MR_MainCharacter* craft = session.GetMainCharacter();
    const int room = level->GetStartingRoom(0);
    if (room < 0 || room >= level->GetRoomCount()) return 0;
    craft->mPosition = level->GetStartingPos(0);
    craft->SetOrientation(level->GetStartingOrientation(0));
    craft->mRoom = room;
    craft->SetHoverId(0);
    craft->SetHoverModel(0);
    session.SetSimulationTime(0);
    MR_Observer* observer = MR_Observer::New();
    if (observer == nullptr) return 0;

    std::array<std::uint8_t, MR_NB_COLORS * 3> palette{};
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int at = MR_RESERVED_COLORS_BEGINNING + index;
            palette[at * 3] = colors[index].peRed; palette[at * 3 + 1] = colors[index].peGreen; palette[at * 3 + 2] = colors[index].peBlue;
        }
        delete [] colors;
        if (const MR_UInt8* back = buffer.GetBackPalette()) {
            for (int index = 0; index < MR_BACK_COLORS; ++index) {
                const PALETTEENTRY& color = MR_ConvertColor(back[index * 3], back[index * 3 + 1], back[index * 3 + 2], 0.75, 0.75, 0.05);
                const int at = MR_RESERVED_COLORS_BEGINNING + MR_BASIC_COLORS + index;
                palette[at * 3] = color.peRed; palette[at * 3 + 1] = color.peGreen; palette[at * 3 + 2] = color.peBlue;
            }
        }
    }
    std::uint32_t tick = 0;
    for (int f = 0; f < frames; ++f) {
        if (f > 0) for (int t = 0; t < ticksBetween; ++t, ++tick) { craft->SetControlState(Drive(tick), session.GetSimulationTime()); session.StepTicks(1, nullptr, nullptr); }
        for (int settle = 0; settle < 14; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft, session.GetSimulationTime(), session.GetBackImage());   // the smoothed chase camera settles when time stands still
        // The renderer draws its race text (time, lap) into the picture at the top and bottom: those rows are left out, which also makes the picture wide.
        Picture picture;
        const int fullW = buffer.GetXRes(), fullH = buffer.GetYRes();
        const int y0 = fullH * 23 / 100, y1 = fullH * 84 / 100;
        picture.width = fullW; picture.height = y1 - y0;
        const std::uint8_t* frame = buffer.GetBuffer();
        const std::size_t lineLen = static_cast<std::size_t>(buffer.GetLineLen());
        picture.rgba.resize(static_cast<std::size_t>(picture.width) * picture.height * 4);
        for (int y = 0; y < picture.height; ++y) {
            const std::uint8_t* row = frame + static_cast<std::size_t>(y + y0) * lineLen;
            std::uint8_t* out4 = picture.rgba.data() + static_cast<std::size_t>(y) * picture.width * 4;
            for (int x = 0; x < picture.width; ++x) { out4[x * 4] = palette[row[x] * 3]; out4[x * 4 + 1] = palette[row[x] * 3 + 1]; out4[x * 4 + 2] = palette[row[x] * 3 + 2]; out4[x * 4 + 3] = 255; }
        }
        out.push_back(std::move(picture));
    }
    observer->Delete();
    return static_cast<int>(out.size());
}
}
