// RenderGoldenRun -- the classic renderer's output for fixed scenes, as hashes (the "classic preset" reference of docs/render-golden.md).
//
//   HoverNetRenderGolden <track> <scene> [bench <frames>]
//
// It plays a scripted two-craft race on the track, stops at a fixed tick, draws the whole race view (3D scene, craft, HUD) the way the game
// does (MR_Observer::RenderNormalDisplay into the 8-bit framebuffer) and prints one line:
//
//   <track> <scene> <width>x<height> <hash of the framebuffer bytes> rgb=<hash of the picture after the palette> pixels=<non-background pixels>
//
// The simulation is fixed-step and identical on every platform (docs/determinism.md) and the simulation time is frozen at the scene's tick,
// so the line must be identical on every platform and every run. scripts/test-render-golden.sh compares it with docs/render-golden.txt.
// With HOVERNET_RENDER_DUMP=<directory> the frame is also written there as a PGM (the indices as grey levels) for looking at.
//
// Scenes: grid640 (tick 0, 640x480), drive640 (tick 600), wide1280 (tick 600, 1280x720), cockpit640 (tick 600, cockpit view, 640x480).
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
void HoverNetInitializeWebFactory();   // WebFactory.cpp: the object factory compiled into the program (a browser cannot dlopen ObjFac1.so)
#endif

namespace
{
// The same scripted inputs the determinism and replay tools use (SimHarness::ScriptedInput).
std::uint16_t ScriptedInput(int player, std::uint32_t tick)
{
    int c = MR_MainCharacter::eMotorOn;
    const int period = 37 + 16 * player;
    const int phase = static_cast<int>(tick / period) % 3;
    if (phase == 0) c |= MR_MainCharacter::eLeft;
    else if (phase == 1) c |= MR_MainCharacter::eRight;
    const std::uint32_t jumpPeriod = 190 + 40 * player;
    if (tick % jumpPeriod >= 150 && tick % jumpPeriod < 160) c |= MR_MainCharacter::eJump;
    if (tick % (70 + 20 * player) == 30) c |= MR_MainCharacter::eFire;
    if (tick % (410 + 30 * player) == 350) c |= MR_MainCharacter::eSelectWeapon;
    return static_cast<std::uint16_t>(c);
}

std::uint64_t Fnv1a(const std::uint8_t* data, std::size_t size)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) { h ^= data[i]; h *= 1099511628211ULL; }
    return h;
}
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <track> <grid640|drive640|wide1280|cockpit640>\n", argv[0]); return 2; }
    const std::string trackName = argv[1], scene = argv[2];
    int width = 640, height = 480;
    std::uint32_t ticks = 600;
    bool cockpit = false;
    if (scene == "grid640") ticks = 0;
    else if (scene == "drive640") {}
    else if (scene == "wide1280") { width = 1280; height = 720; }
    else if (scene == "cockpit640") cockpit = true;
    else if (scene == "bench1920") { width = 1920; height = 1080; }   // not a golden scene: only for HOVERNET_RENDER_BENCH
    else { std::fprintf(stderr, "unknown scene %s\n", scene.c_str()); return 2; }

    std::setvbuf(stderr, nullptr, _IONBF, 0);
    const bool trace = std::getenv("HOVERNET_RENDER_TRACE") != nullptr;   // stage markers on stderr, for finding where a platform stops
    auto stage = [&](const char* what) { if (trace) std::fprintf(stderr, "stage: %s\n", what); };
    MR_InitTrigoTables();
#ifdef __EMSCRIPTEN__
    HoverNetInitializeWebFactory();
#endif
    MR_MainCharacter::RegisterFactory();
    MR_DllObjectFactory::MR_DllObjectFactoryCleanup factoryCleanup;

    stage("factory registered");
    MR_RecordFile* track = new MR_RecordFile;
    if (!track->OpenForRead(("NetTarget/Tracks/" + trackName + ".trk").c_str())) { std::fprintf(stderr, "Could not open %s\n", trackName.c_str()); delete track; return 1; }
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(width, height) || !buffer.Lock()) { std::fprintf(stderr, "Could not create a framebuffer\n"); return 1; }
    stage("track opened, framebuffer ready");
    MR_ClientSession session;
    if (!session.LoadNew(trackName.c_str(), track, 3, TRUE, &buffer) || !session.CreateMainCharacter() || session.GetMainCharacter() == nullptr) {
        std::fprintf(stderr, "Could not load %s\n", trackName.c_str());
        return 1;
    }
    stage("session loaded");
    MR_Level* level = session.Level();
    const int players = 2;
    std::vector<MR_MainCharacter*> craft(players, nullptr);
    for (int slot = 0; slot < players; ++slot) {
        const int room = level->GetStartingRoom(slot);
        if (room < 0 || room >= level->GetRoomCount()) { std::fprintf(stderr, "%s has no second start position\n", trackName.c_str()); return 1; }
        MR_MainCharacter* c = slot == 0 ? session.GetMainCharacter() : MR_MainCharacter::New(3, TRUE);
        if (c == nullptr) return 1;
        c->mPosition = level->GetStartingPos(slot);
        c->SetOrientation(level->GetStartingOrientation(slot));
        c->mRoom = room;
        c->SetHoverId(slot);
        c->SetHoverModel(0);
        craft[static_cast<std::size_t>(slot)] = c;
        if (slot != 0 && session.InsertSimulatedCharacter(c, room) == nullptr) { std::fprintf(stderr, "Could not place a craft\n"); return 1; }
    }
    for (int slot = 0; slot < players; ++slot) {
        for (int roomIndex = 0; roomIndex < level->GetRoomCount(); ++roomIndex) {
            for (MR_FreeElementHandle handle = level->GetFirstFreeElement(roomIndex); handle != nullptr; handle = MR_Level::GetNextFreeElement(handle)) {
                if (MR_Level::GetFreeElement(handle) == craft[static_cast<std::size_t>(slot)]) { level->MoveElement(handle, roomIndex); break; }
            }
        }
    }
    stage("craft placed");
    session.SetSimulationTime(0);
    for (std::uint32_t t = 0; t < ticks; ++t) {
        for (int slot = 0; slot < players; ++slot) craft[static_cast<std::size_t>(slot)]->SetControlState(ScriptedInput(slot, t), session.GetSimulationTime());
        session.StepTicks(1, nullptr, nullptr);
    }

    stage("race stepped");
    MR_Observer* observer = MR_Observer::New();
    if (observer == nullptr) { std::fprintf(stderr, "Could not create the observer\n"); return 1; }
    if (cockpit) observer->SetCockpitView(TRUE);
    stage("observer created");
    observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
    const std::size_t pixels = static_cast<std::size_t>(buffer.GetXRes()) * static_cast<std::size_t>(buffer.GetYRes());
    const std::uint8_t* frame = buffer.GetBuffer();
    std::size_t lit = 0;
    for (std::size_t i = 0; i < pixels; ++i) if (frame[i] != 0) ++lit;
    const char* bench = std::getenv("HOVERNET_RENDER_BENCH");
    if (argc >= 5 && std::strcmp(argv[3], "bench") == 0) bench = argv[4];   // also as arguments: a WebAssembly program under Node does not see the environment
    if (bench != nullptr) {
        // Frame-time measurement: draw the same scene N times and report the mean and the slowest frame (wall clock, this machine).
        const int n = std::max(1, std::atoi(bench));
        double total = 0, worst = 0;
        for (int i = 0; i < n; ++i) {
            const auto a = std::chrono::steady_clock::now();
            observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            total += ms; worst = std::max(worst, ms);
        }
        std::printf("BENCH %s %s %dx%d: mean %.2f ms, slowest %.2f ms over %d frames\n", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes(), total / n, worst, n);
    }
    // The picture as the player sees it: the same palette the game builds (gamma, intensity, the track's background colours), applied to the
    // indices. Hashed separately, because the palette is computed with floating point (docs/render-golden.md).
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
    std::vector<std::uint8_t> rgb(pixels * 3);
    for (std::size_t i = 0; i < pixels; ++i) { rgb[i * 3] = palette[frame[i] * 3]; rgb[i * 3 + 1] = palette[frame[i] * 3 + 1]; rgb[i * 3 + 2] = palette[frame[i] * 3 + 2]; }
    std::printf("%s %s %dx%d %016llx rgb=%016llx pixels=%zu\n", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes(),
                static_cast<unsigned long long>(Fnv1a(frame, pixels)), static_cast<unsigned long long>(Fnv1a(rgb.data(), rgb.size())), lit);

    if (const char* dump = std::getenv("HOVERNET_RENDER_DUMP")) {
        std::string name = trackName;
        for (char& ch : name) if (ch == ' ') ch = '_';
        const std::string path = std::string(dump) + "/" + name + "-" + scene + ".pgm";
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P5\n%d %d\n255\n", buffer.GetXRes(), buffer.GetYRes());
            std::fwrite(frame, 1, pixels, f);
            std::fclose(f);
        }
    }
    observer->Delete();
    return lit > 0 ? 0 : 1;
}
