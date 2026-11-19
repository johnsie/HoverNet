#include "TrackBackdrop.h"

#include "../MainCharacter/StdAfx.h"
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/RecordFile.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <algorithm>
#include "TrackPlan.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
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
    std::lock_guard<std::mutex> guard(HoverNetPlan::LevelLock());
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
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0;   // the part of the picture kept: the middle, where the track is at any angle of its turn (the rest is empty)
    int scaleDown = 1;   // the level is shrunk by this factor for the picture (see Open)
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
int Flyover::Width() const { return mImpl->cropW; }
int Flyover::Height() const { return mImpl->cropH; }

constexpr double kCropLeft = 0.10, kCropRight = 0.90, kCropTop = 0.06, kCropBottom = 0.97;   // (the track is framed so that it stays inside this at every angle)

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
    MR_Level* level = const_cast<MR_Level*>(m.session->GetCurrentLevel());   // (this session is a throwaway of ours: it is only drawn, never raced)
    // The renderer's arithmetic (32-bit fixed point) is safe for a scene up to about 260 m deep and 130 m of eye height, which cannot hold a whole track that is 500 m or several kilometres across. So the
    // level in this session is shrunk (all its coordinates divided by one number) until the track is about 140 m across, and drawn from a normal distance: the real floors, walls and textures drawn
    // by the game's own renderer, at a smaller scale. (The pictures keep their own size, so on a big track they look larger; at this distance that is what reads best.)
    auto extent = [&](double& x0, double& x1, double& y0, double& y1, double& zLow, double& zHigh) {
        x0 = y0 = zLow = 1e18; x1 = y1 = zHigh = -1e18;
        for (int r = 0; r < level->GetRoomCount(); ++r) {
            for (int v = 0; v < level->GetRoomVertexCount(r); ++v) {
                const MR_2DCoordinate& c = level->GetRoomVertex(r, v);
                x0 = std::min<double>(x0, c.mX); x1 = std::max<double>(x1, c.mX); y0 = std::min<double>(y0, c.mY); y1 = std::max<double>(y1, c.mY);
            }
            zLow = std::min<double>(zLow, level->GetRoomBottomLevel(r)); zHigh = std::max<double>(zHigh, level->GetRoomBottomLevel(r));
        }
    };
    double x0, x1, y0, y1, zLow, zHigh;
    extent(x0, x1, y0, y1, zLow, zHigh);
    if (!(x1 > x0) || !(y1 > y0)) { m.Close(); return false; }
    constexpr double kTargetSpan = 140000.0;
    m.scaleDown = std::max(1, static_cast<int>(std::ceil(std::max(x1 - x0, y1 - y0) / kTargetSpan)));
    if (const char* forced = std::getenv("HOVERNET_FLYOVER_SCALE")) m.scaleDown = std::max(1, std::atoi(forced));   // test switch
    if (m.scaleDown > 1) { level->ScaleGeometryDown(m.scaleDown); extent(x0, x1, y0, y1, zLow, zHigh); }
    m.cx = (x0 + x1) * 0.5; m.cy = (y0 + y1) * 0.5;
    const double span = std::max(x1 - x0, y1 - y0);
    // The renderer tilts by shifting its horizon (a shear), in steps of an eighth of the picture's height: here half the picture. The eye is placed so the window it shows on the ground
    // holds the whole extent: at height H the picture's middle row meets the ground at H * focal / shift, the bottom row at 0.8 * H and the top row far out.
    const double aperture = 90.0 * 3.14159265 / 180.0;
    const double focal = (width * 0.5) / std::tan(aperture * 0.5);
    m.scroll = -4;
    const double shift = static_cast<double>(-m.scroll) * height / 8.0;
    // How far the track reaches from its centre (so it stays in the picture whichever way it faces as it turns): the eye is placed so that circle is about 0.52 of the width that is kept (the near edge is magnified by the perspective).
    double reach = 1.0;
    for (int r = 0; r < level->GetRoomCount(); ++r) for (int v = 0; v < level->GetRoomVertexCount(r); ++v) {
        const MR_2DCoordinate& c = level->GetRoomVertex(r, v);
        reach = std::max(reach, std::hypot(static_cast<double>(c.mX) - m.cx, static_cast<double>(c.mY) - m.cy));
    }
    const double keptWidth = width * (kCropRight - kCropLeft);
    const double eyeHeight = std::min(0.8 * span, 2.0 * reach * focal / (0.52 * keptWidth) * std::sin(std::atan(shift / focal))) * (std::getenv("HOVERNET_FLYOVER_ZOOM") ? std::atof(std::getenv("HOVERNET_FLYOVER_ZOOM")) : 1.0);   // (the zoom is a test switch)
    const double tilt = std::atan(shift / focal);
    const double slant = eyeHeight / std::sin(tilt);
    m.radius = eyeHeight * focal / shift;
    m.lookZ = (zLow + zHigh) * 0.5;
    m.eyeZ = m.lookZ + eyeHeight;
    if (std::getenv("HOVERNET_FLYOVER_DEBUG")) std::fprintf(stderr, "flyover: shrunk %dx, span %.0f mm, centre %.0f,%.0f, slant %.0f, radius %.0f, eyeZ %.0f, lookZ %.0f, tilt %.1f deg\n", m.scaleDown, span, m.cx, m.cy, slant, m.radius, m.eyeZ, m.lookZ, tilt * 180.0 / 3.14159265);
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
    m.width = width; m.height = height;
    m.cropX = static_cast<int>(width * kCropLeft); m.cropW = static_cast<int>(width * (kCropRight - kCropLeft));
    m.cropY = static_cast<int>(height * kCropTop); m.cropH = static_cast<int>(height * (kCropBottom - kCropTop));
    m.ready = true;
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
    m.observer->RenderFlyover(m.buffer, m.session, eye, yaw, m.scroll, 0, nullptr);
    // (no sky of its own: the picture is transparent round the track, so the menu's grid shows behind it)
    const std::size_t pixels = static_cast<std::size_t>(m.cropW) * m.cropH;
    rgba.resize(pixels * 4);
    const std::uint8_t* frame = m.buffer->GetBuffer();
    const std::size_t lineLen = static_cast<std::size_t>(m.buffer->GetLineLen());
    for (int y = 0; y < m.cropH; ++y) {
        const std::uint8_t* row = frame + static_cast<std::size_t>(y + m.cropY) * lineLen + m.cropX;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * m.cropW * 4;
        for (int x = 0; x < m.cropW; ++x) { out[x * 4] = m.palette[row[x] * 3]; out[x * 4 + 1] = m.palette[row[x] * 3 + 1]; out[x * 4 + 2] = m.palette[row[x] * 3 + 2]; out[x * 4 + 3] = row[x] == 0 ? 0 : 255; }   // colour 0 is never a surface: where nothing was drawn it is left see-through, so the menu's own backdrop shows instead of a black hole
    }
    if (const char* dump = std::getenv("HOVERNET_FLYOVER_DUMP")) { static bool once = false; if (!once) { once = true; if (FILE* f = std::fopen(dump, "wb")) { std::fprintf(f, "P6 %d %d 255\n", m.cropW, m.cropH); for (std::size_t i = 0; i < pixels; ++i) { const std::uint8_t px[3] = {rgba[i * 4 + 3] ? rgba[i * 4] : std::uint8_t(40), rgba[i * 4 + 3] ? rgba[i * 4 + 1] : std::uint8_t(0), rgba[i * 4 + 3] ? rgba[i * 4 + 2] : std::uint8_t(60)}; std::fwrite(px, 1, 3, f); } std::fclose(f); } } }   // test switch: the first picture, as a PPM
    if (std::getenv("HOVERNET_FLYOVER_DEBUG")) { std::size_t drawn = 0; for (std::size_t i = 3; i < rgba.size(); i += 4) if (rgba[i] != 0) ++drawn; std::fprintf(stderr, "flyover: %zu of %zu pixels drawn (shrunk %dx)\n", drawn, pixels, m.scaleDown); }
    return true;
}

// ---- the worker thread --------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct FlyoverWorker::Impl
{
    std::mutex mutex;                 // guards everything below
    std::condition_variable wake;
    std::thread thread;
    bool quit = false, still = false, failed = false, trackChanged = false;
    int generation = 0;
    std::string path, name;
    std::vector<Frame> done;          // finished pictures not yet taken
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();   // when the current track was asked for: the clock of the turn

    void Run()
    {
        Flyover fly;
        for (;;) {
            std::string openPath, openName;
            int gen;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return quit || trackChanged; });
                if (quit) return;
                openPath = path; openName = name; trackChanged = false; gen = generation;
            }
            bool ok;
            {
                std::lock_guard<std::mutex> level(HoverNetPlan::LevelLock());
                #ifdef __EMSCRIPTEN__
                ok = fly.Open(openPath, openName, 640, 400);   // smaller on the page (memory, and the picture is drawn by the CPU there); smoothed when shown
#else
                ok = fly.Open(openPath, openName, 960, 600);   // drawn at this size, the middle kept, and smoothed when shown
#endif
            }
            { std::lock_guard<std::mutex> lock(mutex); failed = !ok; }
            if (!ok) continue;
            // Pictures are drawn one after another for where the eye will be when each is shown (one every degree or so), the menu blends between the last two by the real angle, and the turn is
            // continuous: the track turns at exactly the pace of the grid behind it. A modest share of one core: the pause between pictures is what is left of a third of a second.
            double lastMs = 100.0;
            for (int count = 0;; ++count) {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (quit) return;
                    if (trackChanged) break;                       // another track was asked for: start that one
                    if (still && count > 0) {                      // Reduced motion: the first picture only; wait for another track
                        wake.wait(lock, [&] { return quit || trackChanged; });
                        break;
                    }
                }
                const auto started = std::chrono::steady_clock::now();
                Frame frame; frame.index = count;
                frame.degrees = still ? Degrees(0.0) : Degrees(Seconds() + lastMs / 1000.0 + 0.33);
                if (const char* start = std::getenv("HOVERNET_FLYOVER_START")) frame.degrees = static_cast<float>(std::atof(start)) + (frame.degrees - Degrees(0.0));   // test switch
                bool drawn;
                {
                    std::lock_guard<std::mutex> level(HoverNetPlan::LevelLock());
                    drawn = fly.Draw(frame.degrees, frame.rgba);
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                lastMs = ms;
                if (drawn) {
                    frame.width = fly.Width(); frame.height = fly.Height();
                    std::lock_guard<std::mutex> lock(mutex);
                    if (generation == gen) done.push_back(std::move(frame));
                }
                const double rest = std::max(20.0, 330.0 - ms);
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(static_cast<int>(rest)), [&] { return quit || trackChanged; });
            }
        }
    }
    double Seconds()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    }
};

FlyoverWorker::FlyoverWorker() : mImpl(new Impl) { mImpl->thread = std::thread([this]() { mImpl->Run(); }); }
FlyoverWorker::~FlyoverWorker()
{
    { std::lock_guard<std::mutex> lock(mImpl->mutex); mImpl->quit = true; }
    mImpl->wake.notify_all();
    if (mImpl->thread.joinable()) mImpl->thread.join();
    delete mImpl;
}
void FlyoverWorker::SetTrack(const std::string& path, const std::string& name)
{
    {
        std::lock_guard<std::mutex> lock(mImpl->mutex);
        mImpl->path = path; mImpl->name = name; mImpl->trackChanged = true; mImpl->failed = false; mImpl->startedAt = std::chrono::steady_clock::now();
        ++mImpl->generation; mImpl->done.clear();
    }
    mImpl->wake.notify_all();
}
void FlyoverWorker::SetStill(bool still) { std::lock_guard<std::mutex> lock(mImpl->mutex); mImpl->still = still; }
double FlyoverWorker::Seconds() const { return mImpl->Seconds(); }
int FlyoverWorker::Generation() const { std::lock_guard<std::mutex> lock(mImpl->mutex); return mImpl->generation; }
bool FlyoverWorker::Failed() const { std::lock_guard<std::mutex> lock(mImpl->mutex); return mImpl->failed; }
int FlyoverWorker::TakeFrames(std::vector<Frame>& out)
{
    std::lock_guard<std::mutex> lock(mImpl->mutex);
    const int n = static_cast<int>(mImpl->done.size());
    for (Frame& f : mImpl->done) out.push_back(std::move(f));
    mImpl->done.clear();
    return n;
}
}
