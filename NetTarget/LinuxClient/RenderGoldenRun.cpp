// RenderGoldenRun -- the software rasteriser's output for fixed scenes, as hashes (its regression reference, docs/render-golden.md).
//
//   HoverNetRenderGolden <track> <scene> [bench <frames> | modern]
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
#include "../MainCharacter/StdAfx.h"   // first: on Windows it brings the MFC compatibility layer (CList, CMap) the headers below use
#include "../Game2/ClientSession.h"
#include "../Game2/Observer.h"
#include "../MainCharacter/MainCharacter.h"
#include "../ObjFacTools/ResBitmap.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/RecordFile.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/ModernGraphics.h"
#include "../VideoServices/VideoBuffer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
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
    if (trace) {   // the trigonometry tables every platform must build identically (the rasteriser and the simulation use them)
        std::uint64_t h = 1469598103934665603ULL;
        for (int i = 0; i < MR_2PI; ++i) { h ^= static_cast<std::uint16_t>(MR_Sin[i]); h *= 1099511628211ULL; h ^= static_cast<std::uint16_t>(MR_Cos[i]); h *= 1099511628211ULL; }
        std::fprintf(stderr, "trig tables %016llx\n", static_cast<unsigned long long>(h));
    }
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

    // "modern": the same scene with the modern look (docs/roadmap-3.0.md): craft shadows drawn in the scene, then the picture passes and effects. The
    // classic line above is printed first and is what the committed goldens check; this line is checked by scripts/test-modern-graphics.sh for
    // properties (the classic frame is unchanged without the look, the look changes the picture, it is repeatable) -- not against committed hashes,
    // because its floating-point effects are only promised to repeat on the same machine.
    if ((argc >= 4 && std::strcmp(argv[3], "modern") == 0) || std::getenv("HOVERNET_RENDER_MODERN") != nullptr) {
        const char* presetName = std::getenv("HOVERNET_RENDER_PRESET");   // modern (default), high or lite
        const HoverNetGfx::Preset preset = presetName == nullptr ? HoverNetGfx::Preset::eModern : std::strcmp(presetName, "high") == 0 ? HoverNetGfx::Preset::eModernHigh
                                           : std::strcmp(presetName, "lite") == 0 ? HoverNetGfx::Preset::eModernLite : HoverNetGfx::Preset::eModern;
        HoverNetGfx::Options options = HoverNetGfx::OptionsForPreset(preset);
        if (std::getenv("HOVERNET_RENDER_NOLIGHT") != nullptr) options.lighting = false;   // to time the lighting by itself
        std::array<std::uint8_t, MR_NB_COLORS * 3> gradedPalette{};   // the client expands the frame through the graded palette (SDL2Graphics.cpp)
        HoverNetGfx::Presenter::GradePalette(palette.data(), gradedPalette.data(), options.brightness);
        const std::array<std::uint8_t, MR_NB_COLORS * 3>& usedPalette = options.grade ? gradedPalette : palette;
        std::uint8_t lut[256];
        HoverNetGfx::BuildShadowLut(palette.data(), 87, lut);
        observer->SetModernTextures(options.trueColour ? TRUE : FALSE, usedPalette.data());
        if (options.trueColour && std::getenv("HOVERNET_RENDER_NOTEXTURES") == nullptr) {   // true-colour textures, in this program and in the object library
            MR_ResBitmap::SetColourPalette(usedPalette.data());
            MR_DllObjectFactory::SetTexturePalette(usedPalette.data());
        }
        observer->SetModernLighting(TRUE);
        observer->SetModernEffects(TRUE, TRUE, FALSE, lut);
        for (std::uint32_t k = 0; k < 48; ++k) {   // some race time, so the trail has history
            for (int slot = 0; slot < players; ++slot) craft[static_cast<std::size_t>(slot)]->SetControlState(ScriptedInput(slot, ticks + k), session.GetSimulationTime());
            session.StepTicks(1, nullptr, nullptr);
            observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        }
        for (int settle = 0; settle < 12; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());   // the smoothed chase camera settles when the time stands still
        const std::vector<std::uint8_t> withLook(frame, frame + pixels);
        observer->SetModernEffects(TRUE, TRUE, TRUE, lut);   // back to the state withLook was drawn in, so the hints (and the depth they point at) match it
        for (int settle = 0; settle < 48; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        const HoverNetGfx::FrameHints hints = observer->GetFrameHints();
        observer->SetModernEffects(FALSE, FALSE, FALSE, nullptr);
        for (int settle = 0; settle < 12; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        std::size_t shadowed = 0;
        for (std::size_t i = 0; i < pixels; ++i) if (frame[i] != withLook[i]) ++shadowed;
        // The camera is never changed by the look: with the shadows off too, the 8-bit scene must be exactly the one drawn without the look.
        observer->SetModernEffects(TRUE, FALSE, FALSE, lut);
        for (int settle = 0; settle < 12; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        std::size_t cameraDiff = 0;
        {
            const std::vector<std::uint8_t> noShadows(frame, frame + pixels);
            observer->SetModernEffects(FALSE, FALSE, FALSE, nullptr);
            for (int settle = 0; settle < 12; ++settle) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
            for (std::size_t i = 0; i < pixels; ++i) if (frame[i] != noShadows[i]) ++cameraDiff;
        }
        std::size_t tagCount[8] = {};   // how many pixels the rasteriser tagged as floor / ceiling / wall / object / craft
        if (hints.tags != nullptr) for (std::size_t i = 0; i < pixels && i < static_cast<std::size_t>(hints.depthWidth) * hints.depthHeight; ++i) if (hints.tags[i] < 8) ++tagCount[hints.tags[i]];
        std::vector<std::uint8_t> picture(pixels * 3);
        for (std::size_t i = 0; i < pixels; ++i) for (int c = 0; c < 3; ++c) picture[i * 3 + c] = usedPalette[withLook[i] * 3 + c];
        // The rasteriser's own true-colour picture (filtered textures), laid over the palette expansion as the client does.
        const std::size_t trueColourPixels = HoverNetGfx::Presenter::ApplyTrueColour(picture.data(), withLook.data(), buffer.GetXRes(), buffer.GetYRes(), hints);
        const std::vector<std::uint8_t> plain = picture;
        HoverNetGfx::Presenter presenter;
        presenter.Process(picture.data(), buffer.GetXRes(), buffer.GetYRes(), options, hints);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < pixels * 3; ++i) if (picture[i] != plain[i]) ++changed;
        // Repeatable: a second presenter given the same inputs makes the same picture.
        std::vector<std::uint8_t> again = plain;
        HoverNetGfx::Presenter second;
        second.Process(again.data(), buffer.GetXRes(), buffer.GetYRes(), options, hints);
        std::printf("MODERN %s %s %dx%d rgb=%016llx shadowed=%zu cameradiff=%zu changed=%zu repeat=%s hints=%d craft=%d trail=%d speed=%.2f lights=%d depth=%d tags=%zu/%zu/%zu/%zu/%zu truecolour=%zu\n", trackName.c_str(), scene.c_str(),
                    buffer.GetXRes(), buffer.GetYRes(), static_cast<unsigned long long>(Fnv1a(picture.data(), picture.size())), shadowed, cameraDiff, changed,
                    again == picture ? "yes" : "NO", hints.valid ? 1 : 0, hints.craftVisible ? 1 : 0, hints.trailCount, hints.speed, hints.lightCount, hints.depth != nullptr ? 1 : 0, tagCount[1], tagCount[2], tagCount[3], tagCount[4], tagCount[5], trueColourPixels);
        // Cost of the passes: the same frame processed repeatedly (a fresh copy each time; this machine, one thread).
        const int runs = 20;
        double total = 0;
        for (int i = 0; i < runs; ++i) {
            std::vector<std::uint8_t> copy = plain;
            const auto a = std::chrono::steady_clock::now();
            presenter.Process(copy.data(), buffer.GetXRes(), buffer.GetYRes(), options, hints);
            total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        }
        std::printf("MODERNTIME %s %s %dx%d: mean %.2f ms over %d frames\n", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes(), total / runs, runs);
        // The same, pass by pass (HoverNetGfx::Presenter::SetProfiling): where the time goes.
        presenter.SetProfiling(true);
        std::vector<std::pair<std::string, double>> perPass;
        for (int i = 0; i < runs; ++i) {
            std::vector<std::uint8_t> copy = plain;
            presenter.Process(copy.data(), buffer.GetXRes(), buffer.GetYRes(), options, hints);
            const auto& times = presenter.LastPassTimes();
            if (perPass.empty()) for (const auto& t : times) perPass.emplace_back(t.name, 0.0);
            for (std::size_t k = 0; k < times.size() && k < perPass.size(); ++k) perPass[k].second += times[k].milliseconds;
        }
        std::printf("MODERNPASSES %s %s %dx%d:", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes());
        for (const auto& p : perPass) std::printf(" %s=%.2fms", p.first.c_str(), p.second / runs);
        std::printf("\n");
        // What the true colour costs the rasteriser: the same scene drawn with and without it (this machine, one thread).
        {
            const int frames = 30;
            double textured = 0, plainMs = 0;
            for (int pass = 0; pass < 2; ++pass) {
                observer->SetModernTextures(pass == 0 && options.trueColour ? TRUE : FALSE, usedPalette.data());
                observer->SetModernEffects(TRUE, TRUE, TRUE, lut);
                observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
                const auto a = std::chrono::steady_clock::now();
                for (int i = 0; i < frames; ++i) observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
                (pass == 0 ? textured : plainMs) = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count() / frames;
            }
            std::printf("MODERNRASTER %s %s %dx%d: true colour with filtered textures %.2f ms, without %.2f ms\n", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes(), textured, plainMs);
        }
        if (const char* dump = std::getenv("HOVERNET_RENDER_DUMP")) {
            std::string name = trackName;
            for (char& ch : name) if (ch == ' ') ch = '_';
            for (int which = 0; which < 2; ++which) {
                const std::string path = std::string(dump) + "/" + name + "-" + scene + (which == 0 ? "-plain.ppm" : "-modern.ppm");
                if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%d %d\n255\n", buffer.GetXRes(), buffer.GetYRes());
                    const std::vector<std::uint8_t>& image = which == 0 ? plain : picture;
                    std::fwrite(image.data(), 1, image.size(), f);
                    std::fclose(f);
                }
            }
        }
    }

    // "camerarate": the chase camera must follow the craft the same way whatever the frame rate. Two observers watch the same race; one is drawn on every
    // tick of simulated time (a high frame rate), the other on every fourth (a low one); at the end both draw the same moment. The pictures should match.
    if ((argc >= 4 && std::strcmp(argv[3], "camerarate") == 0) || std::getenv("HOVERNET_RENDER_CAMERARATE") != nullptr) {
        MR_Observer* slow = MR_Observer::New();
        if (slow == nullptr) { std::fprintf(stderr, "Could not create the second observer\n"); return 1; }
        std::size_t different = 0;
        const int span = 160;
        for (int k = 0; k < span; ++k) {
            for (int slot = 0; slot < players; ++slot) craft[static_cast<std::size_t>(slot)]->SetControlState(ScriptedInput(slot, ticks + static_cast<std::uint32_t>(k)), session.GetSimulationTime());
            session.StepTicks(1, nullptr, nullptr);
            observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
            if (k % 4 == 0 && k < span - 1) slow->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        }
        observer->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        const std::vector<std::uint8_t> fast(frame, frame + pixels);
        slow->RenderNormalDisplay(&buffer, &session, craft[0], session.GetSimulationTime(), session.GetBackImage());
        for (std::size_t i = 0; i < pixels; ++i) if (frame[i] != fast[i]) ++different;
        std::printf("CAMERARATE %s %s %dx%d: %zu of %zu pixels differ between a camera drawn every tick and one drawn every fourth tick\n", trackName.c_str(), scene.c_str(), buffer.GetXRes(), buffer.GetYRes(), different, pixels);
        slow->Delete();
    }

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
