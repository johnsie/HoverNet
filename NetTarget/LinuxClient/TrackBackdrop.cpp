#include "TrackBackdrop.h"

#include "../MainCharacter/StdAfx.h"
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// ---- the orbit ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Flyover::Impl
{
    MR_VideoBuffer* buffer = nullptr;
    MR_ClientSession* session = nullptr;
    MR_Observer* observer = nullptr;
    std::array<std::uint8_t, MR_NB_COLORS * 3> palette{};
    int width = 0, height = 0, scroll = 0;
    double cx = 0, cy = 0, radius = 0, eyeZ = 0, lookZ = 0;
    bool ready = false;
    void Close()
    {
        if (observer != nullptr) { observer->Delete(); observer = nullptr; }
        delete session; session = nullptr;
        delete buffer; buffer = nullptr;
        ready = false;
    }
};

Flyover::Flyover() : mImpl(new Impl) {}
Flyover::~Flyover() { mImpl->Close(); delete mImpl; }
bool Flyover::Ready() const { return mImpl->ready; }
int Flyover::Width() const { return mImpl->width; }
int Flyover::Height() const { return mImpl->height; }

bool Flyover::Open(const std::string& path, const std::string& name, int width, int height)
{
    Impl& m = *mImpl;
    m.Close();
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(path.c_str())) { delete track; return false; }
    m.buffer = new MR_VideoBuffer(nullptr, 1.0, 0.5, 0.5);
    if (!m.buffer->SetVideoMode(width, height) || !m.buffer->Lock()) { delete track; m.Close(); return false; }
    m.session = new MR_ClientSession;
    if (!m.session->LoadNew(name.c_str(), track, 1, FALSE, m.buffer) || m.session->GetCurrentLevel() == nullptr) { m.Close(); return false; }
    const MR_Level* level = m.session->GetCurrentLevel();
    // the track's extent and floor height, to place the orbit
    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18, zLow = 1e18, zHigh = -1e18;
    for (int r = 0; r < level->GetRoomCount(); ++r) {
        for (int v = 0; v < level->GetRoomVertexCount(r); ++v) {
            const MR_2DCoordinate& c = level->GetRoomVertex(r, v);
            x0 = std::min<double>(x0, c.mX); x1 = std::max<double>(x1, c.mX); y0 = std::min<double>(y0, c.mY); y1 = std::max<double>(y1, c.mY);
        }
        zLow = std::min<double>(zLow, level->GetRoomBottomLevel(r)); zHigh = std::max<double>(zHigh, level->GetRoomBottomLevel(r));
    }
    if (!(x1 > x0) || !(y1 > y0)) { m.Close(); return false; }
    m.cx = (x0 + x1) * 0.5; m.cy = (y0 + y1) * 0.5;
    double span = std::max(x1 - x0, y1 - y0);
    // The renderer's depth buffer reaches about 260 m: a small track is seen whole, a big one (a few kilometres) is circled around its start line from as far as the buffer allows.
    const double kReach = 125000.0;
    if (span > kReach) {
        const MR_3DCoordinate start = level->GetStartingPos(0);
        m.cx = start.mX; m.cy = start.mY; zLow = zHigh = start.mZ; span = kReach;
    }
    // The renderer tilts by shifting its horizon (a shear), in steps of an eighth of the picture's height: here half the picture. The eye is placed so the window it shows on the ground
    // holds the whole extent: at height H the picture's middle row meets the ground at H * focal / shift, the bottom row at 0.8 * H and the top row far out.
    const double aperture = 90.0 * 3.14159265 / 180.0;
    const double focal = (width * 0.5) / std::tan(aperture * 0.5);
    m.scroll = -4;
    const double shift = static_cast<double>(-m.scroll) * height / 8.0;
    const double eyeHeight = 0.8 * span;
    const double tilt = std::atan(shift / focal);
    const double slant = eyeHeight / std::sin(tilt);
    m.radius = eyeHeight * focal / shift;
    m.lookZ = (zLow + zHigh) * 0.5;
    m.eyeZ = m.lookZ + eyeHeight;
    if (std::getenv("HOVERNET_FLYOVER_DEBUG")) std::fprintf(stderr, "flyover: span %.0f mm, centre %.0f,%.0f, slant %.0f, radius %.0f, eyeZ %.0f, lookZ %.0f, tilt %.1f deg\n", span, m.cx, m.cy, slant, m.radius, m.eyeZ, m.lookZ, tilt * 57.2958);
    m.palette.fill(0);
    {
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int at = MR_RESERVED_COLORS_BEGINNING + index;
            m.palette[at * 3] = colors[index].peRed; m.palette[at * 3 + 1] = colors[index].peGreen; m.palette[at * 3 + 2] = colors[index].peBlue;
        }
        delete [] colors;
        if (const MR_UInt8* back = m.buffer->GetBackPalette()) {
            for (int index = 0; index < MR_BACK_COLORS; ++index) {
                const PALETTEENTRY& color = MR_ConvertColor(back[index * 3], back[index * 3 + 1], back[index * 3 + 2], 0.75, 0.75, 0.05);
                const int at = MR_RESERVED_COLORS_BEGINNING + MR_BASIC_COLORS + index;
                m.palette[at * 3] = color.peRed; m.palette[at * 3 + 1] = color.peGreen; m.palette[at * 3 + 2] = color.peBlue;
            }
        }
    }
    m.observer = MR_Observer::New();
    if (m.observer == nullptr) { m.Close(); return false; }
    m.width = width; m.height = height; m.ready = true;
    return true;
}

bool Flyover::Draw(float degrees, std::vector<std::uint8_t>& rgba)
{
    Impl& m = *mImpl;
    if (!m.ready) return false;
    const double a = degrees * 3.14159265 / 180.0;
    MR_3DCoordinate eye;
    eye.mX = static_cast<MR_Int32>(m.cx + std::cos(a) * m.radius);
    eye.mY = static_cast<MR_Int32>(m.cy + std::sin(a) * m.radius);
    eye.mZ = static_cast<MR_Int32>(m.eyeZ);
    // looking at the centre: the heading from the eye to it, in the renderer's angle units (4096 to the turn)
    const double heading = std::atan2(m.cy - eye.mY, m.cx - eye.mX);
    MR_Angle yaw = static_cast<MR_Angle>(std::lround(heading / (2.0 * 3.14159265) * 4096.0));
    yaw = MR_NORMALIZE_ANGLE(yaw);
    m.observer->RenderFlyover(m.buffer, m.session, eye, yaw, m.scroll, 0, m.session->GetBackImage());
    const std::size_t pixels = static_cast<std::size_t>(m.width) * m.height;
    rgba.resize(pixels * 4);
    const std::uint8_t* frame = m.buffer->GetBuffer();
    const std::size_t lineLen = static_cast<std::size_t>(m.buffer->GetLineLen());
    for (int y = 0; y < m.height; ++y) {
        const std::uint8_t* row = frame + static_cast<std::size_t>(y) * lineLen;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * m.width * 4;
        for (int x = 0; x < m.width; ++x) { out[x * 4] = m.palette[row[x] * 3]; out[x * 4 + 1] = m.palette[row[x] * 3 + 1]; out[x * 4 + 2] = m.palette[row[x] * 3 + 2]; out[x * 4 + 3] = 255; }
    }
    return true;
}
}
