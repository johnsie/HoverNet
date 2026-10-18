#ifdef _WIN32
#include <afxwin.h>
#include <afxtempl.h>
#endif

#include "TrackCatalog.h"
#include "TrackDownloader.h"
#include "Sha256.h"
#include "PersonalBests.h"
#include <functional>
#include <random>
#include "../Authority/CraftNames.h"
#include "GhostRun.h"
#include "ReplayLibrary.h"
#include "ReplayRecorder.h"
#include "../Authority/ReplayFormat.h"
#include "../Model/StateHash.h"
#include "../Model/StateSnapshot.h"
// Playing on a HoverNet 3.0 server: over TCP in the desktop client, over a WebSocket in the browser build (HOVERNET_WEB_AUTH).
#if !defined(HOVERNET_WEB) || defined(HOVERNET_WEB_AUTH)
#define HOVERNET_AUTH 1
#endif
#if defined(HOVERNET_AUTH)
#include "AuthConnection.h"
#include "OnlineLobby.h"
#include "../Authority/PredictingClient.h"
#include "../Authority/Protocol3.h"
#include "../Authority/Crypto.h"
#endif
#include "../GraphicsSDL2/SDL2Graphics.h"
#include "../Game2/ClientSession.h"
#ifdef HOVERNET_GAME2_PLAYER
#include "../Game2/Observer.h"
#include "../VideoServices/SoundServer.h"
#include "RaceServerClient.h"
#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/imgui_internal.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"
#endif
#include "../Model/GameSession.h"
#include "../MazeCompiler/TrackCommonStuff.h"
#include "../ObjFac1/ObjFac1Res.h"
#include "../ObjFacTools/ResActor.h"
#include "../ObjFacTools/ResourceLib.h"
#include "../ObjFacTools/SpriteHandle.h"
#include "../VideoServices/Sprite.h"
#include "../Util/DllObjectFactory.h"
#include "../Util/WorldCoordinates.h"
#include "../VideoServices/3DViewport.h"
#include "../VideoServices/ColorPalette.h"
#include "../VideoServices/VideoBuffer.h"

#include <SDL.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <map>
#include <sstream>
#include <vector>

#ifdef HOVERNET_WEB
void HoverNetInitializeWebFactory();
void HoverNetCleanWebFactory();

#include <emscripten.h>

// Compiled into the page's script rather than emscripten_run_script_int(), which would eval()
// a string -- forbidden by the site's Content-Security-Policy.
EM_JS(int, hovernet_js_min_viewport, (), {
    return Math.min(window.innerWidth, window.innerHeight);
});
// Game size (desktop): packed (width << 16 | height), 0 = fit the browser window. The page owns
// the canvas size (shell.html hnSetGameSize); SDL follows its resize events.
EM_JS(int, hovernet_js_get_game_size, (), {
    var m = /^(\d+)x(\d+)$/.exec((function() { try { return localStorage.getItem('hn-gamesize') || ''; } catch (e) { return ''; } })());
    return m ? ((parseInt(m[1], 10) << 16) | parseInt(m[2], 10)) : 0;
});
EM_JS(void, hovernet_js_set_game_size, (int w, int h), {
    if (window.hnSetGameSize) window.hnSetGameSize(w, h);
});
EM_JS(int, hovernet_js_window_size, (), {
    return (window.innerWidth << 16) | window.innerHeight;
});
// This page's address for a HoverNet server, e.g. wss://hovernet.outiva.com/auth/, written into `out` (at most cap bytes). Compiled into the
// page's script: the production page's Content-Security-Policy forbids eval(), which emscripten_run_script_string() uses (that froze the game
// when the HoverNet Servers screen opened in 2.1.0).
EM_JS(int, hovernet_js_default_server_url, (char* out, int cap), {
    var url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/auth/';
    return stringToUTF8(url, out, cap);
});
EM_JS(int, hovernet_js_is_touch, (), {
    return (window.matchMedia && matchMedia('(pointer: coarse)').matches) ? 1 : 0;
});
// Long side / short side of the screen times 1000, whatever the current orientation (the game is
// played in landscape, but may start while the phone is still in portrait).
EM_JS(int, hovernet_js_landscape_aspect_x1000, (), {
    var w = Math.max(window.innerWidth, window.innerHeight), h = Math.max(1, Math.min(window.innerWidth, window.innerHeight));
    return Math.round(1000 * w / h);
});
// Resume on the next display frame (or after 100 ms if the tab is hidden and rAF is paused, so
// network polling keeps running). Pacing the loop to the display avoids tearing and flicker.
EM_ASYNC_JS(void, hovernet_js_wait_frame, (), {
    await new Promise(function(resolve) {
        var done = false;
        function finish() { if (!done) { done = true; resolve(); } }
        requestAnimationFrame(finish);
        setTimeout(finish, 100);
    });
});

// ---- VR (Meta Quest browser, WebXR) -- see shell.html "VR" and docs. All of this is inert unless
// the player pressed Enter VR; desktop and phone browsers never take these paths.
EM_JS(int, hovernet_js_is_quest, (), {
    return /OculusBrowser|Quest/i.test(navigator.userAgent) ? 1 : 0;
});
EM_JS(int, hovernet_js_vr_active, (), {
    return (window.hnVR && window.hnVR.active) ? 1 : 0;
});
// Inside a headset the page's requestAnimationFrame is paused; pace to the XR frame instead.
EM_ASYNC_JS(void, hovernet_js_vr_wait_frame, (), {
    await new Promise(function(resolve) {
        var done = false;
        function finish() { if (!done) { done = true; resolve(); } }
        if (window.hnVR && window.hnVR.waiters) window.hnVR.waiters.push(finish);
        setTimeout(finish, 40);
    });
});
// A 2D screen (menus) shown on the virtual screen: copy the canvas now, while it still holds the
// frame that was just presented.
EM_JS(void, hovernet_js_vr_submit_mono, (), {
    if (window.hnVR && window.hnVR.submitMono) window.hnVR.submitMono();
});
// A stereo race frame: two RGBA images, left then right.
EM_JS(void, hovernet_js_vr_submit_stereo, (const unsigned char* left, const unsigned char* right, int w, int h), {
    if (window.hnVR && window.hnVR.submitStereo) {
        window.hnVR.submitStereo(HEAPU8.subarray(left, left + w * h * 4), HEAPU8.subarray(right, right + w * h * 4), w, h);
    }
});

// Bitmask of on-screen touch controls currently held, written by the page's
// touch pad (packaging/web/shell.html) and OR-ed into the same control state
// the keyboard and gamepad feed. Going through the game rather than synthetic
// key events means remapped keyboard bindings never affect touch play.
enum HoverNetTouchBits {
    eTouchLeft = 1, eTouchRight = 2, eTouchAccelerate = 4, eTouchBrake = 8,
    eTouchFire = 16, eTouchJump = 32, eTouchSelectWeapon = 64, eTouchToggleView = 128
};
static int gHoverNetTouchMask = 0;
extern "C" EMSCRIPTEN_KEEPALIVE void hovernet_set_touch_mask(int mask)
{
    gHoverNetTouchMask = mask;
}

// Frames completed by any menu or race loop; the page keeps its loader up until the
// first one so a slow start never shows a blank canvas. (Counted in HoverNetFrameDelay.)
static int gHoverNetFrames = 0;
extern "C" EMSCRIPTEN_KEEPALIVE int hovernet_frames(void)
{
    return gHoverNetFrames;
}

// True while the race loop is running; the page shows the on-screen race
// controls only then, so they never cover the menus.
static int gHoverNetInRace = 0;
extern "C" EMSCRIPTEN_KEEPALIVE int hovernet_in_race(void)
{
    return gHoverNetInRace;
}

// Test hook for the browser smoke test: show the results screen without driving a whole race (the screen and its buttons are what
// is under test; finishing a race is covered by the replay tests).
static int gHoverNetTestResults = 0;
extern "C" EMSCRIPTEN_KEEPALIVE void hovernet_test_show_results(void)
{
    gHoverNetTestResults = 1;
}

// Test hook for the browser tests: set one of the HOVERNET_TEST_ switches the native tests pass in the environment (only those).
extern "C" EMSCRIPTEN_KEEPALIVE void hovernet_test_setenv(const char* name, const char* value)
{
    if (name != nullptr && value != nullptr && std::strncmp(name, "HOVERNET_TEST_", 14) == 0) setenv(name, value, 1);
}

// Soft-keyboard bridge. Menu text fields are drawn by ImGui inside the canvas,
// so iOS/Android never show a keyboard for them. The page keeps a real (hidden)
// <input> for that, asks here whether a field is active, and feeds what the
// player types back as ordinary SDL events so ImGui and in-race chat see the
// same input as a hardware keyboard.
extern "C" EMSCRIPTEN_KEEPALIVE int hovernet_wants_text(void)
{
#ifdef HOVERNET_GAME2_PLAYER
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput ? 1 : 0;
#else
    return 0;
#endif
}

extern "C" EMSCRIPTEN_KEEPALIVE void hovernet_text_input(const char* utf8)
{
    if (utf8 == nullptr || utf8[0] == '\0') return;
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_TEXTINPUT;
    std::snprintf(event.text.text, sizeof(event.text.text), "%s", utf8);
    SDL_PushEvent(&event);
}

// key: 1 Backspace, 2 Enter, 3 Tab, 4 Left, 5 Right, 6 Up, 7 Down, 8 Delete, 9 Home, 10 End
extern "C" EMSCRIPTEN_KEEPALIVE void hovernet_text_key(int key)
{
    static const SDL_Keycode kCodes[] = {
        SDLK_UNKNOWN, SDLK_BACKSPACE, SDLK_RETURN, SDLK_TAB, SDLK_LEFT, SDLK_RIGHT,
        SDLK_UP, SDLK_DOWN, SDLK_DELETE, SDLK_HOME, SDLK_END
    };
    if (key < 1 || key >= static_cast<int>(sizeof(kCodes) / sizeof(kCodes[0]))) return;
    for (const Uint32 type : { static_cast<Uint32>(SDL_KEYDOWN), static_cast<Uint32>(SDL_KEYUP) }) {
        SDL_Event event;
        SDL_zero(event);
        event.type = type;
        event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.sym = kCodes[key];
        event.key.keysym.scancode = SDL_GetScancodeFromKey(kCodes[key]);
        SDL_PushEvent(&event);
    }
}
#endif

namespace
{
// True on phones/tablets (browser build only): screens show on-screen touch controls, not keys.
inline bool HoverNetIsTouchDevice()
{
#ifdef HOVERNET_WEB
    static const bool touch = hovernet_js_is_touch() != 0;
    return touch;
#else
    return false;
#endif
}

// End-of-frame pause shared by every menu and race loop. Natively it is the original fixed
// 16 ms sleep. In the browser a fixed sleep stacks on top of the (software) render and canvas
// copy, so a slow frame took 16 ms longer than it needed to; sleep only what is left of a
// 16 ms frame budget (always yielding once so the page can process input and paint).
// VR mode: true only inside a headset session on web. Natively, HOVERNET_TEST_VR=<dir> pretends
// VR is on and writes each eye's frame there as a PPM, so the stereo camera can be checked
// without a headset.
static bool gHoverNetVrStereoSubmitted = false;
inline bool HoverNetVrActive()
{
#ifdef HOVERNET_WEB
    return hovernet_js_vr_active() != 0;
#else
    static const bool testVr = std::getenv("HOVERNET_TEST_VR") != nullptr;
    return testVr;
#endif
}

inline void HoverNetVrSubmitStereo(const uint8_t* pLeft, const uint8_t* pRight, int pWidth, int pHeight)
{
    gHoverNetVrStereoSubmitted = true;
#ifdef HOVERNET_WEB
    hovernet_js_vr_submit_stereo(pLeft, pRight, pWidth, pHeight);
#else
    static int frame = 0;
    if (frame < 3) {
        const char* dir = std::getenv("HOVERNET_TEST_VR");
        for (int eye = 0; eye < 2; ++eye) {
            const std::string path = std::string(dir) + "/eye" + (eye == 0 ? "L" : "R") + std::to_string(frame) + ".ppm";
            if (FILE* out = std::fopen(path.c_str(), "wb")) {
                std::fprintf(out, "P6\n%d %d\n255\n", pWidth, pHeight);
                const uint8_t* rgba = eye == 0 ? pLeft : pRight;
                for (int i = 0; i < pWidth * pHeight; ++i) std::fwrite(rgba + i * 4, 1, 3, out);
                std::fclose(out);
            }
        }
    }
    ++frame;
#endif
}

inline void HoverNetFrameDelay()
{
#ifdef HOVERNET_WEB
    ++gHoverNetFrames;
    const bool vr = HoverNetVrActive();
    // Browser mouse/touch events are pushed into SDL while we wait here. Loops restore the
    // renderer's logical size before this call, and SDL rescales every event queued while a
    // logical size is set -- but the ImGui menus draw 1:1 in real window pixels, so clicks and
    // slider drags landed in the wrong place unless the window was exactly the logical size. Keep
    // the logical size off while events can arrive; put it back before the next frame polls.
    SDL_Window* const window = SDL_GetWindowFromID(1);
    SDL_Renderer* const renderer = window != nullptr ? SDL_GetRenderer(window) : nullptr;
    int logicalW = 0, logicalH = 0;
    if (renderer != nullptr) {
        SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
        SDL_RenderSetLogicalSize(renderer, 0, 0);
    }
    if (vr) {
        // In a headset: menus go to the virtual screen as a copy of the canvas (a race frame has
        // already been sent as two eye images), and the loop paces to the headset's frames.
        if (!gHoverNetVrStereoSubmitted) hovernet_js_vr_submit_mono();
        gHoverNetVrStereoSubmitted = false;
        hovernet_js_vr_wait_frame();
    }
    else {
        hovernet_js_wait_frame();
    }
    if (renderer != nullptr) SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
#else
    SDL_Delay(16);
#endif
}
}

namespace
{
// Game framebuffer size. 1024x768 everywhere except phones in the browser, which use a widescreen
// size matching the screen so the race fills it instead of being letterboxed (see below).
int kWidth = 1024;
int kHeight = 768;

#ifdef HOVERNET_GAME2_PLAYER
void ApplyRaceViewSetting(int& width, int& height);   // the player's "Race view" setting (defined with the other saved settings)
#endif

void ConfigureGameResolution()
{
    int testW = 0, testH = 0;
    const char* testRes = std::getenv("HOVERNET_TEST_RES");
    if (testRes != nullptr && std::sscanf(testRes, "%dx%d", &testW, &testH) == 2 && testW >= 320 && testH >= 240) {
        kWidth = testW & ~1;
        kHeight = testH & ~1;
        return;
    }
#ifdef HOVERNET_WEB
    if (hovernet_js_is_quest() != 0) {
        // Quest browser: the race is drawn twice per frame (one image per eye) by the CPU
        // rasteriser, so use a smaller frame. Only this headset browser takes this branch.
        kWidth = 640;
        kHeight = 480;
        return;
    }
    if (HoverNetIsTouchDevice()) {
        // Phones are about 2.2:1 in landscape. Keep the width modest (the CPU rasteriser's cost
        // scales with pixels, and this is fewer than 1024x768) and match the screen's shape,
        // clamped between 4:3 and 2.4:1.
        const double aspect = std::max(4.0 / 3.0, std::min(2.4, hovernet_js_landscape_aspect_x1000() / 1000.0));
        kWidth = 960;
        kHeight = static_cast<int>(kWidth / aspect + 0.5) & ~1;
        return;
    }
#endif
#ifdef HOVERNET_GAME2_PLAYER
    ApplyRaceViewSetting(kWidth, kHeight);
#endif
}

// Every interactive menu now renders 1:1 in real window coordinates through
// ImGui. The renderer's fixed 1024x768 logical size is used only for the game
// framebuffer and restored after each menu frame, so menus need no separate
// letterbox mouse-coordinate translation path.

// Set once the player has actually confirmed "yes, quit HoverNet" (see
// ConfirmQuit, defined further down) from whichever screen they were on when
// they clicked the window's close button. Every screen's own loop already
// exits on its own "running" flag; this is what lets that exit propagate all
// the way out to main() instead of just closing the current screen and
// falling through to whatever came next (the main menu, a race, ...). Shared
// by both the HoverNetTrack3DViewer and HoverNetGame2Player targets (the
// latter's menus set it; the former's plain race loop only reads it).
bool g_QuitConfirmed = false;

#ifdef HOVERNET_GAME2_PLAYER
// The public production RaceServer. --lobby overrides this.
#ifdef HOVERNET_WEB
constexpr const char* kDefaultLobbyHost = "hovernet.outiva.com";
#else
constexpr const char* kDefaultLobbyHost = "outiva.com";
#endif
constexpr unsigned kDefaultLobbyPort = 9600;
#endif

#ifndef HOVERNET_SOURCE_DIR
#define HOVERNET_SOURCE_DIR "."
#endif

std::string SourcePath(const char* relativePath)
{
    const char* dataDirectory = std::getenv("HOVERNET_DATA_DIR");
    if (dataDirectory != nullptr && dataDirectory[0] != '\0') {
        return std::string(dataDirectory) + "/" + relativePath;
    }

    // Installed/portable builds keep their data tree beside the executable.
    // Prefer it over the compile-time checkout, which exists only on the build
    // machine and made a copied Windows executable unable to load any assets.
    char* basePath = SDL_GetBasePath();
    if (basePath != nullptr) {
        const std::string installedPath = std::string(basePath) + relativePath;
        SDL_free(basePath);
        std::ifstream installedFile(installedPath.c_str(), std::ios::binary);
        if (installedFile.good()) return installedPath;
    }
    return std::string(HOVERNET_SOURCE_DIR) + "/" + relativePath;
}

struct RenderStats
{
    int surfacesRendered;
    int actorsRendered;
};

void ClampCameraHeight(const MR_Level& level, int room, MR_3DCoordinate& camera)
{
    const MR_Int32 floor = level.GetRoomBottomLevel(room);
    const MR_Int32 ceiling = level.GetRoomTopLevel(room);
    constexpr MR_Int32 kClearance = 200;
    if (ceiling - floor <= kClearance * 2) {
        camera.mZ = (floor + ceiling) / 2;
        return;
    }
    camera.mZ = std::max(floor + kClearance, std::min(camera.mZ, ceiling - kClearance));
}

int ParseFrameCount(int argc, char** argv)
{
    for (int argument = 1; argument + 1 < argc; ++argument) {
        if (std::strcmp(argv[argument], "--frames") == 0) {
            return std::max(1, std::atoi(argv[argument + 1]));
        }
    }
    return -1;
}

bool HasArgument(int argc, char** argv, const char* argument)
{
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], argument) == 0) {
            return true;
        }
    }
    return false;
}

// --memory-report <path>: for the frame-time/memory baseline soak test (see
// docs/roadmap-2.0.md's Phase 1 "Establish a repeatable frame-time and memory
// baseline" item) -- writes a CSV of periodic frame/timing/RSS samples over a
// long --frames run, so a regression that leaks memory or degrades frame time
// shows up as a trend instead of needing someone to notice a 2-hour session
// getting slower by hand.
std::string ParseMemoryReportPath(int argc, char** argv)
{
    for (int argument = 1; argument + 1 < argc; ++argument) {
        if (std::strcmp(argv[argument], "--memory-report") == 0) {
            return argv[argument + 1];
        }
    }
    return "";
}

// Reads resident memory where the host exposes the Linux /proc interface.
// The diagnostic is optional on other platforms: returning -1 records
// "unavailable" rather than preventing the shared client from building or
// inventing a misleading zero measurement.
long ReadResidentMemoryKB()
{
#ifdef _WIN32
    return -1;
#else
    std::ifstream lStatus("/proc/self/status");
    std::string lLine;
    while (std::getline(lStatus, lLine)) {
        if (lLine.rfind("VmRSS:", 0) == 0) {
            return std::atol(lLine.c_str() + 6);
        }
    }
    return -1;
#endif
}

std::string ParseTrackArg(int argc, char** argv)
{
    for (int argument = 1; argument + 1 < argc; ++argument) {
        if (std::strcmp(argv[argument], "--track") == 0) {
            return argv[argument + 1];
        }
    }
    return "ClassicH";
}

#ifdef HOVERNET_GAME2_PLAYER
bool ParseLobbyArg(int argc, char** argv, std::string& outHost, unsigned& outPort)
{
    for (int argument = 1; argument + 2 < argc; ++argument) {
        if (std::strcmp(argv[argument], "--lobby") == 0) {
            outHost = argv[argument + 1];
            outPort = static_cast<unsigned>(std::atoi(argv[argument + 2]));
            return true;
        }
    }
    return false;
}
#endif

MR_ResBitmap* BitmapForSurface(const MR_SurfaceElement* surface, MR_ResourceLib& resources)
{
    if (surface == nullptr || surface->GetTypeId().mDllId != 1) {
        return nullptr;
    }

    switch (surface->GetTypeId().mClassId) {
    case 51: return resources.GetBitmap(MR_STD_FLOOR);
    case 52: return resources.GetBitmap(MR_STD_RIGHT_WALL);
    case 53: return resources.GetBitmap(MR_STD_LEFT_WALL);
    case 54: return resources.GetBitmap(MR_RED_RIGHT_WALL_OFF);
    case 55: return resources.GetBitmap(MR_RED_LEFT_WALL_OFF);
    case 56: return resources.GetBitmap(MR_GREEN_RIGHT_WALL_OFF);
    case 57: return resources.GetBitmap(MR_GREEN_LEFT_WALL_OFF);
    case 58: return resources.GetBitmap(MR_STEP_WALL);
    case 59: return resources.GetBitmap(MR_PASS_RIGHT_WALL);
    case 60: return resources.GetBitmap(MR_PASS_LEFT_WALL);
    case 61: return resources.GetBitmap(MR_DO_NOT_ENTER_WALL1);
    case 62: return resources.GetBitmap(MR_DO_NOT_ENTER_WALL2);
    case 63: return resources.GetBitmap(MR_BLUE_BUBBLE_FLOOR);
    case 64: return resources.GetBitmap(MR_SPEED_ZONE);
    case 65: return resources.GetBitmap(MR_FUEL_ZONE);
    case 66: return resources.GetBitmap(MR_YELLOW_STEP);
    case 67: return resources.GetBitmap(MR_CHECKER);
    case 68: return resources.GetBitmap(MR_PIT_WORD);
    case 69: return resources.GetBitmap(MR_FINISH_WORD);
    case 70:
    case 71: return resources.GetBitmap(MR_YELLOW_NEON);
    case 72: return resources.GetBitmap(MR_STD_WALL);
    case 73: return resources.GetBitmap(MR_STD_WALL_TOP);
    default: return nullptr;
    }
}

int RenderRoomWalls(const MR_Level& level, int roomId, MR_3DViewPort& viewport, MR_ResourceLib& resources)
{
    int surfacesRendered = 0;
    const int vertexCount = level.GetRoomVertexCount(roomId);
    const MR_Int32 floorLevel = level.GetRoomBottomLevel(roomId);
    const MR_Int32 ceilingLevel = level.GetRoomTopLevel(roomId);

    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const int next = (vertex + 1) % vertexCount;
        MR_ResBitmap* bitmap = BitmapForSurface(level.GetRoomWallElement(roomId, vertex), resources);
        if (bitmap == nullptr) {
            continue;
        }

        const MR_2DCoordinate& first = level.GetRoomVertex(roomId, vertex);
        const MR_2DCoordinate& second = level.GetRoomVertex(roomId, next);
        const int neighbor = level.GetNeighbor(roomId, vertex);
        if (neighbor == -1) {
            viewport.RenderWallSurface(MR_3DCoordinate(first.mX, first.mY, ceilingLevel),
                                      MR_3DCoordinate(second.mX, second.mY, floorLevel),
                                      level.GetRoomWallLen(roomId, vertex), bitmap);
            ++surfacesRendered;
            continue;
        }

        const MR_Int32 neighborFloor = level.GetRoomBottomLevel(neighbor);
        const MR_Int32 neighborCeiling = level.GetRoomTopLevel(neighbor);
        if (floorLevel < neighborFloor) {
            viewport.RenderWallSurface(MR_3DCoordinate(first.mX, first.mY, neighborFloor),
                                      MR_3DCoordinate(second.mX, second.mY, floorLevel),
                                      level.GetRoomWallLen(roomId, vertex), bitmap);
            ++surfacesRendered;
        }
        if (ceilingLevel > neighborCeiling) {
            viewport.RenderWallSurface(MR_3DCoordinate(first.mX, first.mY, ceilingLevel),
                                      MR_3DCoordinate(second.mX, second.mY, neighborCeiling),
                                      level.GetRoomWallLen(roomId, vertex), bitmap);
            ++surfacesRendered;
        }
    }
    return surfacesRendered;
}

int RenderFloorOrCeiling(const MR_Level& level, const MR_SectionId& section,
                         bool floor, MR_3DViewPort& viewport, MR_ResourceLib& resources)
{
    MR_PolygonShape* shape = nullptr;
    MR_SurfaceElement* surface = nullptr;
    MR_Int32 surfaceLevel = 0;

    if (section.mType == MR_SectionId::eRoom) {
        shape = level.GetRoomShape(section.mId);
        surfaceLevel = floor ? shape->ZMin() : shape->ZMax();
        surface = floor ? level.GetRoomBottomElement(section.mId)
                        : level.GetRoomTopElement(section.mId);
    }
    else {
        shape = level.GetFeatureShape(section.mId);
        surfaceLevel = floor ? shape->ZMax() : shape->ZMin();
        surface = floor ? level.GetFeatureTopElement(section.mId)
                        : level.GetFeatureBottomElement(section.mId);
    }

    MR_ResBitmap* bitmap = BitmapForSurface(surface, resources);
    if (bitmap == nullptr) {
        delete shape;
        return 0;
    }

    std::vector<MR_2DCoordinate> vertices(shape->VertexCount());
    for (int vertex = 0; vertex < shape->VertexCount(); ++vertex) {
        vertices[vertex].mX = shape->X(vertex);
        vertices[vertex].mY = shape->Y(vertex);
    }

    viewport.RenderHorizontalSurface(shape->VertexCount(), vertices.data(), surfaceLevel, !floor, bitmap);
    delete shape;
    return 1;
}

int RenderFeatureWalls(const MR_Level& level, int featureId, MR_3DViewPort& viewport,
                       MR_ResourceLib& resources)
{
    int surfacesRendered = 0;
    MR_PolygonShape* shape = level.GetFeatureShape(featureId);
    const int vertexCount = shape->VertexCount();
    for (int vertex = 0; vertex < vertexCount; ++vertex) {
        const int next = (vertex + 1) % vertexCount;
        MR_ResBitmap* bitmap = BitmapForSurface(level.GetFeatureWallElement(featureId, vertex), resources);
        if (bitmap == nullptr) {
            continue;
        }

        viewport.RenderWallSurface(MR_3DCoordinate(shape->X(next), shape->Y(next), shape->ZMax()),
                                  MR_3DCoordinate(shape->X(vertex), shape->Y(vertex), shape->ZMin()),
                                  level.GetFeatureWallLen(featureId, vertex), bitmap);
        ++surfacesRendered;
    }
    delete shape;
    return surfacesRendered;
}

int RenderFreeElements(const MR_Level& level, int room, MR_3DViewPort& viewport,
                       MR_ResourceLib& resources, MR_SimulationTime simulationTime)
{
    int actorsRendered = 0;
    MR_FreeElementHandle handle = level.GetFirstFreeElement(room);
    while (handle != nullptr) {
        MR_FreeElement* element = level.GetFreeElement(handle);
        if (element != nullptr) {
            element->Render(&viewport, simulationTime);
            ++actorsRendered;
        }
        handle = level.GetNextFreeElement(handle);
    }
    return actorsRendered;
}

bool ContainsFreeElementType(const MR_Level& level, MR_UInt16 dllId, MR_UInt16 classId)
{
    for (int room = 0; room < level.GetRoomCount(); ++room) {
        MR_FreeElementHandle handle = level.GetFirstFreeElement(room);
        while (handle != nullptr) {
            MR_FreeElement* element = level.GetFreeElement(handle);
            if (element != nullptr && element->GetTypeId().mDllId == dllId &&
                element->GetTypeId().mClassId == classId) {
                return true;
            }
            handle = level.GetNextFreeElement(handle);
        }
    }
    return false;
}

#ifdef HOVERNET_GAME2_PLAYER
// The font sprite indexes glyphs as (ascii - 32 + 1), not raw ASCII -- every other
// StrBlt call site in the game (see Observer.cpp) wraps its text in Ascii2Simple()
// for this reason; skipping it renders scrambled/wrong glyphs.
void DrawUiText(const MR_Sprite& font, int x, int y, const char* text, MR_3DViewPort* dest,
                MR_Sprite::eAlignment hAlign = MR_Sprite::eLeft, MR_Sprite::eAlignment vAlign = MR_Sprite::eTop,
                int scaling = 1)
{
    font.StrBlt(x, y, Ascii2Simple(text), dest, hAlign, vAlign, scaling);
}

// Loads the same bitmap font sprite MR_Observer uses for its HUD text, for the menu
// and lobby screens to share. Caller owns the returned handle (may be null on
// failure, e.g. if the resource pack couldn't provide it).
MR_SpriteHandle* LoadUiFont()
{
    try {
        MR_ObjectFromFactoryId baseFontId = {1, 1000};
        MR_SpriteHandle* handle = (MR_SpriteHandle*)MR_DllObjectFactory::CreateObject(baseFontId);
        if (handle != nullptr && handle->GetSprite() == nullptr) {
            delete handle;
            return nullptr;
        }
        return handle;
    } catch (...) {
        return nullptr;
    }
}

// In-game lobby screen: connect to a RaceServer, list its open races, and let the
// player pick one (or type a new name to host it) using the keyboard, rendered
// with the game's own bitmap font -- no separate CLI tool needed. Returns true and
// sets outJoinedName when the player joined a race; false if they cancelled or no
// server was reachable (in which case the caller should fall back to local play).
// pFrameLimit bounds how many screen refreshes the lobby will wait through before
// giving up and returning false, exactly like the main loop's --frames: without it
// (pFrameLimit < 0) this blocks indefinitely for a human, as a lobby screen should.
// With it, an automated/headless run (e.g. under ctest, with no real keyboard input
// ever arriving) can't hang here forever.
// The bundled (official) tracks, in the order players see them. They always sit
// at the top of every track selector; community tracks follow (see TrackCatalog).
const char* const kOfficialTracks[] = {"ClassicH", "Steeplechase", "Switchback", "The Alley2", "The River", "Tidal Causeway", "Metro Spiral"};
constexpr int kOfficialTrackCount = sizeof(kOfficialTracks) / sizeof(kOfficialTracks[0]);

// The catalog of every track this install can play (defined after SourcePath and
// ConfigDirPath, which it needs to find the track folders).
TrackCatalog& Tracks();
TrackDownloadSettings CommunityDownloadSettings();

struct TrackGuide
{
    const char* mCharacter;
    const char* mDifficulty;
    const char* mDescription;
    int mSuggestedLaps;
};

// Short, player-facing guidance for the bundled tracks. The setup screen used
// to expose only internal filenames, leaving a first-time player no basis for
// choosing a course or sensible race length.
const TrackGuide kTrackGuides[] = {
    {"Balanced", "Beginner", "The classic all-round HoverNet circuit: a good place to learn handling and weapons.", 5},
    {"Obstacles", "Intermediate", "A longer obstacle course where clean lines and controlled jumps matter.", 3},
    {"Winding", "Intermediate", "A long, twisting route with jumps and water features breaking up its straights.", 3},
    {"Technical", "Advanced", "A compact corridor course that rewards precise steering and quick reactions.", 5},
    {"Fast", "Intermediate", "A flowing waterside course suited to sustained speed and close racing.", 4},
    {"Water", "Intermediate", "A broad tidal circuit with offset chicanes, water gardens, and twin jumps.", 3},
    {"Technical", "Advanced", "A rotated skyline route with rapid direction changes and tactical pickup lines.", 3},
};
static_assert(sizeof(kTrackGuides) / sizeof(kTrackGuides[0]) == kOfficialTrackCount,
              "Every official track needs setup guidance");

// What the setup screens show under "Course briefing". Official tracks carry
// hand-written guidance; community tracks get a generated one from the catalog.
struct TrackBriefing
{
    std::string mCharacter;
    std::string mDifficulty;
    std::string mDescription;
    int mSuggestedLaps = 3;
    bool mFreePlay = false;
};

TrackBriefing BriefingFor(int index)
{
    TrackBriefing briefing;
    const TrackEntry* entry = Tracks().Get(index);
    if (entry == nullptr) return briefing;
    if (entry->mOfficial && index < kOfficialTrackCount) {
        const TrackGuide& guide = kTrackGuides[index];
        briefing.mCharacter = guide.mCharacter;
        briefing.mDifficulty = guide.mDifficulty;
        briefing.mDescription = guide.mDescription;
        briefing.mSuggestedLaps = guide.mSuggestedLaps;
        return briefing;
    }
    briefing.mFreePlay = entry->mFreePlay;
    briefing.mCharacter = entry->mFreePlay ? "Free play" : "Race";
    briefing.mDifficulty = "Unrated";
    briefing.mDescription = "A community-made track from the classic HoverRace library.";
    if (entry->mFreePlay) {
        briefing.mDescription += " It has no finish line, so there are no laps to complete: "
                                 "drive, fight and explore for as long as you like.";
    }
    if (entry->mStarts > 0) {
        briefing.mDescription += " Starts for " + std::to_string(entry->mStarts) + " players.";
    }
    return briefing;
}

// The server limits a race's display name to 32 bytes while track names may run
// to 63, so a long track name is shortened for the race label (cut at a UTF-8
// character boundary; the full name still travels separately as the track).
std::string RaceNameForTrack(const std::string& trackName)
{
    const std::size_t kMaxRaceNameBytes = 32;
    if (trackName.size() <= kMaxRaceNameBytes) return trackName;
    std::size_t length = kMaxRaceNameBytes;
    while (length > 0 && (static_cast<unsigned char>(trackName[length]) & 0xC0) == 0x80) --length;
    return trackName.substr(0, length);
}

int FindHostableTrack(const std::string& trackName)
{
    return Tracks().Find(trackName);
}

// Name of the track at a catalog index (falls back to the first official track).
std::string TrackNameAt(int index)
{
    const TrackEntry* entry = Tracks().Get(index);
    return entry != nullptr ? entry->mName : std::string(kOfficialTracks[0]);
}

void HoverNetSectionHeading(const char* label);
bool HoverNetButton(const char* label, const ImVec2& size);

struct TrackPreview
{
    SDL_Texture* mTexture = nullptr;
    int mWidth = 0;
    int mHeight = 0;
    bool mLoaded = false;
    bool mWaitingForDownload = false; // the track is not on this computer yet
};

TrackPreview LoadTrackPreview(SDL_Renderer* renderer, const char* trackName)
{
    TrackPreview preview;
    preview.mLoaded = true;
    if (renderer == nullptr || trackName == nullptr) return preview;
    {
        // A downloadable track that is not installed yet gets its map once it is
        // (mLoaded stays false so the caller asks again after the download).
        const TrackEntry* entry = Tracks().Get(Tracks().Find(trackName));
        if (entry != nullptr && !entry->mInstalled) {
            preview.mLoaded = false;
            preview.mWaitingForDownload = true;
            return preview;
        }
    }

    try {
        MR_RecordFile track;
        const std::string path = Tracks().PathFor(trackName);
        if (path.empty()) return preview;
        if (!track.OpenForRead(path.c_str()) || track.GetNbRecords() < 4) return preview;

        std::array<MR_UInt8, MR_NB_COLORS * 3> palette{};
        PALETTEENTRY* basicColors = MR_GetColors(0.75, 0.75, 0.05);
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int paletteIndex = MR_RESERVED_COLORS_BEGINNING + index;
            palette[paletteIndex * 3] = basicColors[index].peRed;
            palette[paletteIndex * 3 + 1] = basicColors[index].peGreen;
            palette[paletteIndex * 3 + 2] = basicColors[index].peBlue;
        }
        delete [] basicColors;

        track.SelectRecord(2);
        {
            CArchive backgroundArchive(&track, CArchive::load | CArchive::bNoFlushOnDelete);
            int imageType = 0;
            backgroundArchive >> imageType;
            if (imageType == MR_RAWBITMAP) {
                std::array<MR_UInt8, MR_BACK_COLORS * 3> backgroundPalette{};
                backgroundArchive.Read(backgroundPalette.data(), static_cast<UINT>(backgroundPalette.size()));
                for (int index = 0; index < MR_BACK_COLORS; ++index) {
                    const PALETTEENTRY& color = MR_ConvertColor(
                        backgroundPalette[index * 3], backgroundPalette[index * 3 + 1],
                        backgroundPalette[index * 3 + 2], 0.75, 0.75, 0.05);
                    const int paletteIndex = MR_RESERVED_COLORS_BEGINNING + MR_BASIC_COLORS + index;
                    palette[paletteIndex * 3] = color.peRed;
                    palette[paletteIndex * 3 + 1] = color.peGreen;
                    palette[paletteIndex * 3 + 2] = color.peBlue;
                }
            }
        }

        track.SelectRecord(3);
        CArchive mapArchive(&track, CArchive::load | CArchive::bNoFlushOnDelete);
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        mapArchive >> x0 >> x1 >> y0 >> y1;
        MR_Sprite map;
        map.Serialize(mapArchive);
        preview.mWidth = map.GetItemWidth();
        preview.mHeight = map.GetItemHeight();
        if (preview.mWidth <= 0 || preview.mHeight <= 0 || map.GetNbItem() <= 0) return preview;

        MR_VideoBuffer indexed(nullptr, 1.0, 0.5, 0.5);
        if (!indexed.SetVideoMode(preview.mWidth, preview.mHeight) || !indexed.Lock()) return preview;
        indexed.Clear(0);
        MR_2DViewPort viewport;
        viewport.Setup(&indexed, 0, 0, preview.mWidth, preview.mHeight);
        map.Blt(0, 0, &viewport);

        std::vector<MR_UInt8> pixels(static_cast<std::size_t>(preview.mWidth) * preview.mHeight * 4);
        const MR_UInt8* source = indexed.GetBuffer();
        for (int pixel = 0; pixel < preview.mWidth * preview.mHeight; ++pixel) {
            const int colorIndex = source[pixel];
            pixels[pixel * 4] = palette[colorIndex * 3];
            pixels[pixel * 4 + 1] = palette[colorIndex * 3 + 1];
            pixels[pixel * 4 + 2] = palette[colorIndex * 3 + 2];
            pixels[pixel * 4 + 3] = colorIndex == 0 ? 0 : 255;
        }

        preview.mTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STATIC,
                                              preview.mWidth, preview.mHeight);
        if (preview.mTexture != nullptr) {
            SDL_UpdateTexture(preview.mTexture, nullptr, pixels.data(), preview.mWidth * 4);
            SDL_SetTextureBlendMode(preview.mTexture, SDL_BLENDMODE_BLEND);
        }
    }
    catch (...) {
        if (preview.mTexture != nullptr) SDL_DestroyTexture(preview.mTexture);
        preview.mTexture = nullptr;
        preview.mWidth = 0;
        preview.mHeight = 0;
    }
    return preview;
}

void DrawTrackPreview(const TrackPreview& preview, float minimumHeight = 180.0f,
                      bool showHeading = true)
{
    if (showHeading) HoverNetSectionHeading("Track map");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 frameSize(std::max(1.0f, available.x),
                           std::max(minimumHeight, available.y - 4.0f));
    const ImVec2 frameMin = ImGui::GetCursorScreenPos();
    const ImVec2 frameMax(frameMin.x + frameSize.x, frameMin.y + frameSize.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(frameMin, frameMax, IM_COL32(13, 13, 18, 255), 8.0f);
    draw->AddRect(frameMin, frameMax, IM_COL32(105, 105, 120, 255), 8.0f);

    if (preview.mTexture != nullptr && preview.mWidth > 0 && preview.mHeight > 0) {
        const float inset = 18.0f;
        const float scale = std::min((frameSize.x - 2.0f * inset) / preview.mWidth,
                                     (frameSize.y - 2.0f * inset) / preview.mHeight);
        const ImVec2 imageSize(preview.mWidth * scale, preview.mHeight * scale);
        const ImVec2 imageMin(frameMin.x + (frameSize.x - imageSize.x) * 0.5f,
                              frameMin.y + (frameSize.y - imageSize.y) * 0.5f);
        draw->AddImage(reinterpret_cast<ImTextureID>(preview.mTexture), imageMin,
                       ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
    }
    else {
        const char* unavailable = preview.mWaitingForDownload ? "Map appears after the track is downloaded"
                                                              : "Map preview unavailable";
        const ImVec2 textSize = ImGui::CalcTextSize(unavailable);
        draw->AddText(ImVec2(frameMin.x + (frameSize.x - textSize.x) * 0.5f,
                             frameMin.y + (frameSize.y - textSize.y) * 0.5f),
                      IM_COL32(180, 180, 190, 255), unavailable);
    }
    ImGui::Dummy(frameSize);
}

// Per-user settings directory, following the XDG Base Directory spec (the
// documented, conventional location on Linux) instead of the four separate
// dotfiles (.hovernet_host_prefs, .hovernet_local_race_prefs,
// .hovernet_username, .hovernet_server_url) this used to scatter directly in
// $HOME -- see docs/roadmap-2.0.md's Phase 3 "persist all settings in a
// documented per-user location" item. Falls back to "." (today's effective
// behavior when $HOME is unset) rather than failing outright.
std::string ConfigDirPath()
{
    std::string base;
    std::string dir;
#ifdef _WIN32
    // APPDATA is the conventional per-user roaming configuration root and is
    // available without tying the shared client to MFC or registry settings.
    const char* appData = std::getenv("APPDATA");
    base = appData != nullptr && appData[0] != '\0' ? appData : ".";
    dir = base + "/HoverNet";
    _mkdir(dir.c_str());
#else
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome != nullptr && xdgConfigHome[0] != '\0') {
        base = xdgConfigHome;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.config";
    }
    dir = base + "/hovernet";
    // mkdir -p in two steps: base ($XDG_CONFIG_HOME or ~/.config) may not exist
    // either on a fresh account. EEXIST is the expected/common case, not a
    // failure; anything else just means the later fopen calls will fail too,
    // which they already handle (LoadX returns defaults, SaveX silently no-ops).
    mkdir(base.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
#endif
    return dir;
}

// The track folders a catalog searches. Official tracks ship with the game; the
// community pack is a separate download, so it is looked for beside the game
// (NetTarget/CommunityTracks) and in the per-user config folder.
// HOVERNET_COMMUNITY_TRACKS_DIR and HOVERNET_COMMUNITY_MANIFEST can point at any
// other folder / manifest (used by tests).
TrackCatalog& Tracks()
{
    static TrackCatalog catalog;
    static bool built = false;
    if (!built) {
        built = true;
        auto directoryOf = [](const std::string& file) {
            const std::size_t slash = file.find_last_of("/\\");
            return slash == std::string::npos ? std::string("./") : file.substr(0, slash + 1);
        };
        TrackCatalogOptions options;
        for (const char* name : kOfficialTracks) options.mOfficialNames.push_back(name);
        options.mOfficialDirectories.push_back(directoryOf(SourcePath("NetTarget/Tracks/ClassicH.trk")));
        options.mManifestPath = SourcePath("NetTarget/CommunityTracks.tsv");
        const char* manifestOverride = std::getenv("HOVERNET_COMMUNITY_MANIFEST");
        if (manifestOverride != nullptr && manifestOverride[0] != '\0') {
            options.mManifestPath = manifestOverride;
        }
        const char* extra = std::getenv("HOVERNET_COMMUNITY_TRACKS_DIR");
        if (extra != nullptr && extra[0] != '\0') {
            options.mCommunityDirectories.push_back(std::string(extra) + "/");
        }
        options.mCommunityDirectories.push_back(directoryOf(options.mManifestPath) + "CommunityTracks/");
        options.mCommunityDirectories.push_back(ConfigDirPath() + "/CommunityTracks/");
        // Downloads go to the per-user folder (writable without administrator
        // rights); tests can redirect them with HOVERNET_COMMUNITY_TRACKS_DIR.
        options.mDownloadDirectory = (extra != nullptr && extra[0] != '\0')
            ? std::string(extra) + "/"
            : ConfigDirPath() + "/CommunityTracks/";
        catalog.Build(options);
    }
    return catalog;
}

TrackDownloadSettings CommunityDownloadSettings()
{
    TrackDownloadSettings settings;
    settings.mBaseUrl = DefaultTrackDownloadBaseUrl();
    // The first community directory is HOVERNET_COMMUNITY_TRACKS_DIR when set,
    // otherwise the per-user folder under NetTarget/.. -- but the install folder
    // may be read-only, so prefer the per-user folder unless overridden.
    const char* override = std::getenv("HOVERNET_COMMUNITY_TRACKS_DIR");
    settings.mDestination = (override != nullptr && override[0] != '\0')
        ? std::string(override) + "/"
        : ConfigDirPath() + "/CommunityTracks/";
    return settings;
}

// One-time upgrade path: if oldDotfilePath (this setting's old location directly
// in $HOME) exists but newPath (under ConfigDirPath()) doesn't yet, copy it over
// so upgrading players don't silently lose settings the first time they run a
// build with this change. Safe to call unconditionally on every load -- a no-op
// once the new file exists or the old one never did.
void MigrateLegacyDotfile(const std::string& oldDotfilePath, const std::string& newPath)
{
    std::ifstream newCheck(newPath);
    if (newCheck.good()) {
        return;
    }
    std::ifstream oldFile(oldDotfilePath, std::ios::binary);
    if (!oldFile.good()) {
        return;
    }
    std::ofstream newFile(newPath, std::ios::binary);
    if (!newFile.good()) {
        return;
    }
    newFile << oldFile.rdbuf();
}

struct HostPrefs
{
    int mTrackIndex = 0;   // index into the track catalog (official tracks first)
    int mLaps = 5;
    bool mWeapons = false;
};

// Remembered choices are stored by track NAME, not catalog position: the catalog
// grows and reorders as the community pack is installed or updated, so an index
// would silently point at a different track. Line 1 is the track name, line 2 is
// "laps weapons". Older builds wrote "trackIndex laps weapons" on one line; that
// index meant the old fixed list of official tracks, which is still the start of
// the catalog, so it is read as-is.
void ReadRacePrefsFile(const std::string& path, HostPrefs& prefs)
{
    std::ifstream in(path);
    std::string first;
    if (!std::getline(in, first)) return;
    while (!first.empty() && (first.back() == '\r' || first.back() == '\n')) first.pop_back();

    int track = -1, laps = 0, weapons = 0;
    {
        std::istringstream legacy(first);
        std::string extra;
        int legacyTrack = 0, legacyLaps = 0, legacyWeapons = 0;
        if ((legacy >> legacyTrack >> legacyLaps >> legacyWeapons) && !(legacy >> extra)) {
            if (legacyTrack >= 0 && legacyTrack < kOfficialTrackCount) { prefs.mTrackIndex = legacyTrack; }
            if (legacyLaps >= 1 && legacyLaps <= 20) { prefs.mLaps = legacyLaps; }
            prefs.mWeapons = legacyWeapons != 0;
            return;
        }
    }
    track = Tracks().Find(first);
    if (track >= 0) { prefs.mTrackIndex = track; }
    if (in >> laps >> weapons) {
        if (laps >= 1 && laps <= 20) { prefs.mLaps = laps; }
        prefs.mWeapons = weapons != 0;
    }
}

void WriteRacePrefsFile(const std::string& path, const HostPrefs& prefs)
{
    std::ofstream out(path);
    out << TrackNameAt(prefs.mTrackIndex) << '\n'
        << prefs.mLaps << ' ' << (prefs.mWeapons ? 1 : 0) << '\n';
}

std::string HostPrefsPath()
{
    const char* home = std::getenv("HOME");
    const std::string newPath = ConfigDirPath() + "/host_prefs";
    MigrateLegacyDotfile(std::string(home != nullptr ? home : ".") + "/.hovernet_host_prefs", newPath);
    return newPath;
}

// "Remember last choice" for hosting settings, across process runs, per the user's
// own machine -- deliberately simple (one line, three ints) rather than a general
// config format, since this is the only thing it stores.
HostPrefs LoadHostPrefs()
{
    HostPrefs prefs;
    ReadRacePrefsFile(HostPrefsPath(), prefs);
    return prefs;
}

void SaveHostPrefs(const HostPrefs& prefs)
{
    WriteRacePrefsFile(HostPrefsPath(), prefs);
}

std::string LocalRacePrefsPath()
{
    const char* home = std::getenv("HOME");
    const std::string newPath = ConfigDirPath() + "/local_race_prefs";
    MigrateLegacyDotfile(std::string(home != nullptr ? home : ".") + "/.hovernet_local_race_prefs", newPath);
    return newPath;
}

// Local play's own remembered track/laps/weapons choice, deliberately kept
// separate from the online host dialog's ~/.hovernet_host_prefs above: before
// RunLocalRaceSetup existed, local play always used ClassicH at 1 lap with
// weapons forced on (see main()'s startup LoadNew call), which every ctest
// smoke test that drives local play headlessly (--fire, --combat, etc.) still
// expects by default. Reusing LoadHostPrefs' defaults here (5 laps, weapons
// off) would silently change that the moment this screen's timeout-confirms
// under a frame-limited run with no prefs file yet on a fresh machine -- which
// is exactly what broke HoverNetGame2PlayerFire in CI (it passed locally only
// because this sandbox already had a leftover ~/.hovernet_host_prefs with
// weapons on, from unrelated interactive testing).
HostPrefs LoadLocalRacePrefs()
{
    HostPrefs prefs;
    prefs.mLaps = 1;
    prefs.mWeapons = true;
    ReadRacePrefsFile(LocalRacePrefsPath(), prefs);
    return prefs;
}

void SaveLocalRacePrefs(const HostPrefs& prefs)
{
    WriteRacePrefsFile(LocalRacePrefsPath(), prefs);
}


// Test hook for headless UI regression runs: when HOVERNET_CAPTURE_FRAME names a
// file, the last frame of a frame-limited screen is saved there as a BMP. Call it
// after the draw data is rendered and before SDL_RenderPresent.
void CaptureFrameIfRequested(SDL_Renderer* renderer, int framesShown, int frameLimit, const char* variable = "HOVERNET_CAPTURE_FRAME")
{
    const char* path = std::getenv(variable);
    if (path == nullptr || path[0] == '\0' || renderer == nullptr || frameLimit < 0 ||
        framesShown != frameLimit) {
        return;
    }
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0) return;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) return;
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0) {
        SDL_SaveBMP(surface, path);
    }
    SDL_FreeSurface(surface);
}

// A download size for display ("114 KB", "2.4 MB", "151 MB").
std::string FormatMegabytes(long long bytes)
{
    char buffer[32];
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes < 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%lld KB", std::max<long long>(1, (bytes + 512) / 1024));
    }
    else {
        std::snprintf(buffer, sizeof(buffer), megabytes < 10.0 ? "%.1f MB" : "%.0f MB", megabytes);
    }
    return buffer;
}

// Wrapped note in the accent colour (used for "downloads when you start").
void AccentNote(const std::string& text);

// The "Downloading..." dialog shared by every screen that can fetch tracks. A
// screen calls Begin() with what it needs, then Draw() every frame in the same
// ImGui scope (scope 0 = the screen's window, 1 = inside the host dialog).
// Draw() reports the moment a download finishes so the screen can carry on with
// what the player asked for (start the race, host it, join it).
class DownloadUi
{
public:
    enum class State { eIdle, eRunning, eDone, eFailed };

    bool Active() const { return mJob != nullptr; }

    void Begin(const std::vector<TrackEntry>& tracks, int scope)
    {
        if (tracks.empty() || mJob) return;
        mJob.reset(new TrackDownloadJob(CommunityDownloadSettings(), tracks));
        mScope = scope;
        mOpenRequested = true;
        mJob->Start();
    }

    State Draw(int scope)
    {
        if (!mJob || mScope != scope) return State::eIdle;
        if (mOpenRequested) {
            ImGui::OpenPopup("Downloading tracks");
            mOpenRequested = false;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter()), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        State result = State::eRunning;
        if (ImGui::BeginPopupModal("Downloading tracks", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            if (!mJob->Finished()) {
                const int total = mJob->TotalTracks();
                const int done = std::min(mJob->CompletedTracks() + 1, total);
                if (total > 1) {
                    ImGui::Text("Downloading track %d of %d", done, total);
                    ImGui::TextDisabled("%s", mJob->CurrentName().c_str());
                }
                else {
                    ImGui::TextUnformatted("Downloading track");
                    ImGui::TextDisabled("%s", mJob->CurrentName().c_str());
                }
                const long long totalBytes = std::max<long long>(1, mJob->TotalBytes());
                const float fraction = std::min(1.0f, static_cast<float>(mJob->BytesDone()) /
                                                          static_cast<float>(totalBytes));
                ImGui::ProgressBar(fraction, ImVec2(380.0f, 0.0f));
                ImGui::TextDisabled("%s of %s", FormatMegabytes(mJob->BytesDone()).c_str(),
                                    FormatMegabytes(totalBytes).c_str());
                ImGui::Spacing();
                if (HoverNetButton("Cancel", ImVec2(-FLT_MIN, 0))) {
                    mJob->Cancel();
                }
            }
            else {
                Tracks().Refresh();
                if (mJob->Succeeded()) {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    mJob.reset();
                    return State::eDone;
                }
                if (mJob->Cancelled() && mJob->Error().empty()) {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    mJob.reset();
                    return State::eFailed;
                }
                ImGui::TextUnformatted("The download did not finish");
                ImGui::Spacing();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
                ImGui::TextWrapped("%s", mJob->Error().c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (HoverNetButton("OK", ImVec2(-FLT_MIN, 0))) {
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                    mJob.reset();
                    return State::eFailed;
                }
            }
            ImGui::EndPopup();
        }
        return result;
    }

private:
    std::unique_ptr<TrackDownloadJob> mJob;
    int mScope = 0;
    bool mOpenRequested = false;
};

bool ContainsIgnoreCase(const std::string& text, const std::string& needle)
{
    if (needle.empty()) return true;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string haystack(text.size(), ' ');
    std::string wanted(needle.size(), ' ');
    for (std::size_t i = 0; i < text.size(); ++i) haystack[i] = lower(static_cast<unsigned char>(text[i]));
    for (std::size_t i = 0; i < needle.size(); ++i) wanted[i] = lower(static_cast<unsigned char>(needle[i]));
    return haystack.find(wanted) != std::string::npos;
}

// The track dropdown shared by every selector (local setup and the online host
// dialog). Official tracks are always listed first under their own heading; the
// community library follows under another, with a search box because it holds
// over a thousand names. 'slot' keeps each selector's search text separate.
// Returns true when the player picked a different track.
bool TrackSelector(const char* id, int slot, int& index)
{
    static std::string searches[4];
    std::string& search = searches[slot & 3];

    const TrackCatalog& catalog = Tracks();
    const TrackEntry* current = catalog.Get(index);
    bool changed = false;

    // Test hook: opens the dropdown on its first frame so a headless run can
    // capture the popup (see CaptureFrameIfRequested). Never set in normal play.
    static bool sOpenedForTest = false;
    if (!sOpenedForTest && std::getenv("HOVERNET_TEST_OPEN_TRACK_DROPDOWN") != nullptr) {
        sOpenedForTest = true;
        ImGui::OpenPopupEx(ImHashStr("##ComboPopup", 0, ImGui::GetID(id)));
    }

    if (!ImGui::BeginCombo(id, current != nullptr ? current->mName.c_str() : "", ImGuiComboFlags_HeightLarge)) {
        return false;
    }

    if (catalog.Count() > catalog.OfficialCount()) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s", search.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##tracksearch", "Search tracks...", buffer, sizeof(buffer))) {
            search = buffer;
        }
    }

    std::vector<int> official;
    std::vector<int> community;
    for (int i = 0; i < catalog.Count(); ++i) {
        const TrackEntry& entry = catalog.Entries()[static_cast<std::size_t>(i)];
        if (!ContainsIgnoreCase(entry.mName, search)) continue;
        (entry.mOfficial ? official : community).push_back(i);
    }

    auto drawEntry = [&](int i) {
        const TrackEntry& entry = catalog.Entries()[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const bool selected = i == index;
        if (ImGui::Selectable(entry.mName.c_str(), selected)) {
            if (i != index) changed = true;
            index = i;
        }
        if (selected && ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }
        std::string tag;
        if (entry.mFreePlay) tag = "free play";
        if (!entry.mInstalled) tag += std::string(tag.empty() ? "" : "  ") + "download";
        if (!tag.empty()) {
            const float tagWidth = ImGui::CalcTextSize(tag.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                     ImGui::GetWindowContentRegionMax().x - tagWidth));
            ImGui::TextDisabled("%s", tag.c_str());
        }
        ImGui::PopID();
    };

    if (!official.empty()) {
        HoverNetSectionHeading("Official tracks");
        for (int i : official) drawEntry(i);
    }
    if (!community.empty()) {
        ImGui::Spacing();
        HoverNetSectionHeading("Community tracks");
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(community.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                drawEntry(community[static_cast<std::size_t>(row)]);
            }
        }
    }
    if (official.empty() && community.empty()) {
        ImGui::TextDisabled("No tracks match \"%s\"", search.c_str());
    }

    ImGui::EndCombo();
    return changed;
}

enum class LobbyPhase
{
    eEnteringName, // First time in the lobby with no saved username yet
    eBrowsing,    // Picking or naming a race
    eWaitingRoom, // Joined a race, waiting for its creator to start it
};

std::string UsernamePath()
{
    const char* home = std::getenv("HOME");
    const std::string newPath = ConfigDirPath() + "/username";
    MigrateLegacyDotfile(std::string(home != nullptr ? home : ".") + "/.hovernet_username", newPath);
    return newPath;
}

// Empty return means no username has been chosen yet (RunLobbyScreen prompts for one).
std::string LoadUsername()
{
    std::ifstream in(UsernamePath());
    std::string name;
    std::getline(in, name);
    return name;
}

void SaveUsername(const std::string& name)
{
    std::ofstream out(UsernamePath());
    out << name << '\n';
}

std::string ServerUrlPath()
{
    const char* home = std::getenv("HOME");
    const std::string newPath = ConfigDirPath() + "/server_url";
    MigrateLegacyDotfile(std::string(home != nullptr ? home : ".") + "/.hovernet_server_url", newPath);
    return newPath;
}

// Returns false (leaving outHost/outPort untouched) if nothing's been saved
// yet -- callers already have the built-in default to fall back to.
bool LoadServerUrl(std::string& outHost, unsigned& outPort)
{
    std::ifstream in(ServerUrlPath());
    std::string host;
    unsigned port = 0;
    if (in >> host >> port && !host.empty() && port > 0 && port <= 65535) {
        outHost = host;
        outPort = port;
        return true;
    }
    return false;
}

void SaveServerUrl(const std::string& host, unsigned port)
{
    std::ofstream out(ServerUrlPath());
    out << host << ' ' << port << '\n';
}

std::string VolumePath()
{
    return ConfigDirPath() + "/volume";
}

// Returns 1.0 (full volume, matching the pre-existing hardcoded behavior) if
// nothing's been saved yet or the saved value is out of range.
double LoadVolume()
{
    std::ifstream in(VolumePath());
    double volume = 1.0;
    if (in >> volume && volume >= 0.0 && volume <= 1.0) {
        return volume;
    }
    return 1.0;
}

void SaveVolume(double volume)
{
    std::ofstream out(VolumePath());
    out << volume << '\n';
}

std::string MutePath()
{
    return ConfigDirPath() + "/mute";
}

bool LoadMute()
{
    std::ifstream in(MutePath());
    int value = 0;
    return (in >> value) && value != 0;
}

void SaveMute(bool muted)
{
    std::ofstream out(MutePath());
    out << (muted ? 1 : 0) << '\n';
}

std::string FullscreenPath()
{
    return ConfigDirPath() + "/fullscreen";
}

// Defaults to windowed (false) if nothing's been saved yet, matching the
// pre-existing hardcoded behavior.
bool LoadFullscreen()
{
    std::ifstream in(FullscreenPath());
    int value = 0;
    return (in >> value) && value != 0;
}

void SaveFullscreen(bool fullscreen)
{
    std::ofstream out(FullscreenPath());
    out << (fullscreen ? 1 : 0) << '\n';
}

struct WindowSize
{
    int width = 1024;
    int height = 768;
};

std::string WindowSizePath()
{
    return ConfigDirPath() + "/window_size";
}

WindowSize LoadWindowSize()
{
    WindowSize size;
    std::ifstream in(WindowSizePath());
    int width = 0;
    int height = 0;
    if (in >> width >> height &&
        width >= 640 && width <= 7680 && height >= 480 && height <= 4320) {
        size.width = width;
        size.height = height;
    }
    return size;
}

void SaveWindowSize(const WindowSize& size)
{
    std::ofstream out(WindowSizePath());
    out << size.width << ' ' << size.height << '\n';
}

// Race view: the shape of the picture the race is drawn in. The software renderer draws any shape (measured: 1920x1080 in about 6 ms; see
// docs/render-golden.md) and shows the same horizontal view with less above and below, so a wide window can be filled instead of
// pillarboxed. Applied when the game starts.
enum class RaceView { eClassic, eWidescreen, eMatchWindow };

std::string RaceViewPath()
{
    return ConfigDirPath() + "/race_view";
}

RaceView LoadRaceView()
{
    std::ifstream in(RaceViewPath());
    std::string word;
    if (in >> word) {
        if (word == "wide") return RaceView::eWidescreen;
        if (word == "window") return RaceView::eMatchWindow;
    }
    return RaceView::eClassic;
}

void SaveRaceView(RaceView view)
{
    std::ofstream out(RaceViewPath());
    out << (view == RaceView::eWidescreen ? "wide" : view == RaceView::eMatchWindow ? "window" : "classic") << '\n';
}

// The picture size for a window of the given shape: 1280 wide, as tall as the shape asks, between 4:3 and 2.4:1 (a very wide window gets
// bars rather than a sliver of a view). Close to 4:3 is the classic 1024x768, so such a window sees no change.
void RaceViewSizeForWindow(int windowWidth, int windowHeight, int& width, int& height)
{
    if (windowWidth <= 0 || windowHeight <= 0) return;
    const double aspect = std::max(4.0 / 3.0, std::min(2.4, static_cast<double>(windowWidth) / windowHeight));
    if (aspect < 1.4) return;   // keep the classic size
    width = 1280;
    height = static_cast<int>(width / aspect + 0.5) & ~1;
}

void ApplyRaceViewSetting(int& width, int& height)
{
    switch (LoadRaceView()) {
    case RaceView::eClassic:
        return;
    case RaceView::eWidescreen:
        width = 1280;
        height = 720;
        return;
    case RaceView::eMatchWindow: {
#ifdef HOVERNET_WEB
        const int browserWindow = hovernet_js_window_size();
        RaceViewSizeForWindow(browserWindow >> 16, browserWindow & 0xffff, width, height);
#else
        int windowWidth = LoadWindowSize().width, windowHeight = LoadWindowSize().height;
        if (LoadFullscreen() && SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
            SDL_DisplayMode mode;
            if (SDL_GetDesktopDisplayMode(0, &mode) == 0) { windowWidth = mode.w; windowHeight = mode.h; }
        }
        RaceViewSizeForWindow(windowWidth, windowHeight, width, height);
#endif
        return;
    }
    }
}

std::string UiScalePath()
{
    return ConfigDirPath() + "/ui_scale";
}

float LoadUiScale()
{
    std::ifstream in(UiScalePath());
    float scale = 1.0f;
    if (in >> scale && scale >= 0.75f && scale <= 1.5f) {
        return scale;
    }
#ifdef HOVERNET_WEB
    // No saved choice: phones get a tighter default so menus fit their short
    // landscape screens. The settings slider still overrides it.
    if (hovernet_js_min_viewport() < 600) {
        return 0.85f;
    }
#endif
    return 1.0f;
}

void SaveUiScale(float scale)
{
    std::ofstream out(UiScalePath());
    out << scale << '\n';
}

std::string LargeHudTextPath()
{
    return ConfigDirPath() + "/large_hud_text";
}

bool LoadLargeHudText()
{
    std::ifstream in(LargeHudTextPath());
    int enabled = 0;
    return (in >> enabled) && enabled != 0;
}

void SaveLargeHudText(bool enabled)
{
    std::ofstream out(LargeHudTextPath());
    out << (enabled ? 1 : 0) << '\n';
}

// "Show my best run as a ghost" (on unless the player switched it off): the player's best run on this track and lap count is drawn beside
// their craft in later races (GhostRun.h).
std::string ShowGhostPath()
{
    return ConfigDirPath() + "/ghost_run";
}

bool LoadShowGhost()
{
    std::ifstream in(ShowGhostPath());
    int enabled = 1;
    return !(in >> enabled) || enabled != 0;
}

void SaveShowGhost(bool enabled)
{
    std::ofstream out(ShowGhostPath());
    out << (enabled ? 1 : 0) << '\n';
}

#if defined(HOVERNET_AUTH)
// The device id of this game: 16 random bytes made the first time and kept in the settings folder (the browser keeps that folder in IndexedDB), sent to a
// HoverNet server in every Hello. The monthly league uses it to tell that two players in a race are on different devices (a player racing themselves from two
// tabs or two windows shares one). It is random, tells nothing about the player or the computer, and is never shown to anybody. HOVERNET_TEST_DEVICE_ID
// (32 hex digits) replaces it in tests.
void EnsureDeviceId()
{
    auto parse = [](const std::string& hex, std::uint64_t& high, std::uint64_t& low) {
        if (hex.size() != 32) return false;
        for (char c : hex) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        high = std::strtoull(hex.substr(0, 16).c_str(), nullptr, 16);
        low = std::strtoull(hex.substr(16).c_str(), nullptr, 16);
        return high != 0 || low != 0;
    };
    std::uint64_t high = 0, low = 0;
    if (const char* forced = std::getenv("HOVERNET_TEST_DEVICE_ID")) { if (parse(forced, high, low)) { OnlineLobby::SetDeviceId(high, low); return; } }
    const std::string path = ConfigDirPath() + "/device_id";
    { std::ifstream in(path); std::string text; if (in >> text && parse(text, high, low)) { OnlineLobby::SetDeviceId(high, low); return; } }
    std::random_device random;
    high = (static_cast<std::uint64_t>(random()) << 32) ^ random() ^ static_cast<std::uint64_t>(std::time(nullptr));
    low = (static_cast<std::uint64_t>(random()) << 32) ^ random() ^ static_cast<std::uint64_t>(SDL_GetPerformanceCounter());
    if (high == 0 && low == 0) low = 1;
    char text[40];
    std::snprintf(text, sizeof text, "%016llx%016llx", static_cast<unsigned long long>(high), static_cast<unsigned long long>(low));
    std::ofstream out(path);
    out << text << '\n';
    OnlineLobby::SetDeviceId(high, low);
}
#endif

std::string ReducedMotionPath()
{
    return ConfigDirPath() + "/reduced_motion";
}

bool LoadReducedMotion()
{
    std::ifstream in(ReducedMotionPath());
    int enabled = 0;
    return (in >> enabled) && enabled != 0;
}

void SaveReducedMotion(bool enabled)
{
    std::ofstream out(ReducedMotionPath());
    out << (enabled ? 1 : 0) << '\n';
}

std::string HighContrastPath()
{
    return ConfigDirPath() + "/high_contrast";
}

bool LoadHighContrast()
{
    std::ifstream in(HighContrastPath());
    int enabled = 0;
    return (in >> enabled) && enabled != 0;
}

void SaveHighContrast(bool enabled)
{
    std::ofstream out(HighContrastPath());
    out << (enabled ? 1 : 0) << '\n';
}

struct KeyboardBindings
{
    SDL_Scancode accelerate = SDL_SCANCODE_LSHIFT;
    SDL_Scancode brake = SDL_SCANCODE_DOWN;
    SDL_Scancode steerLeft = SDL_SCANCODE_LEFT;
    SDL_Scancode steerRight = SDL_SCANCODE_RIGHT;
    SDL_Scancode jump = SDL_SCANCODE_UP;
    SDL_Scancode fire = SDL_SCANCODE_LCTRL;
    SDL_Scancode selectWeapon = SDL_SCANCODE_TAB;
};

KeyboardBindings gKeyboardBindings;

std::string KeyboardBindingsPath()
{
    return ConfigDirPath() + "/keyboard_bindings";
}

bool IsValidBinding(int value)
{
    return value > SDL_SCANCODE_UNKNOWN && value < SDL_NUM_SCANCODES &&
           value != SDL_SCANCODE_ESCAPE;
}

KeyboardBindings LoadKeyboardBindings()
{
    KeyboardBindings bindings;
    std::ifstream in(KeyboardBindingsPath());
    int values[7] = {};
    if (in >> values[0] >> values[1] >> values[2] >> values[3] >>
              values[4] >> values[5] >> values[6]) {
        for (int value : values) {
            if (!IsValidBinding(value)) return KeyboardBindings{};
        }
        bindings.accelerate = static_cast<SDL_Scancode>(values[0]);
        bindings.brake = static_cast<SDL_Scancode>(values[1]);
        bindings.steerLeft = static_cast<SDL_Scancode>(values[2]);
        bindings.steerRight = static_cast<SDL_Scancode>(values[3]);
        bindings.jump = static_cast<SDL_Scancode>(values[4]);
        bindings.fire = static_cast<SDL_Scancode>(values[5]);
        bindings.selectWeapon = static_cast<SDL_Scancode>(values[6]);
    }
    return bindings;
}

void SaveKeyboardBindings(const KeyboardBindings& bindings)
{
    std::ofstream out(KeyboardBindingsPath());
    out << static_cast<int>(bindings.accelerate) << ' '
        << static_cast<int>(bindings.brake) << ' '
        << static_cast<int>(bindings.steerLeft) << ' '
        << static_cast<int>(bindings.steerRight) << ' '
        << static_cast<int>(bindings.jump) << ' '
        << static_cast<int>(bindings.fire) << ' '
        << static_cast<int>(bindings.selectWeapon) << '\n';
}

struct ControllerBindings
{
    SDL_GameControllerButton jump = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton fire = SDL_CONTROLLER_BUTTON_X;
    SDL_GameControllerButton selectWeapon = SDL_CONTROLLER_BUTTON_Y;
    SDL_GameControllerAxis steerAxis = SDL_CONTROLLER_AXIS_LEFTX;
    SDL_GameControllerAxis accelerateAxis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    SDL_GameControllerAxis brakeAxis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    bool invertSteering = false;
    bool invertAccelerate = false;
    bool invertBrake = false;
    int deadzone = 8000;
};

ControllerBindings gControllerBindings;

std::string ControllerBindingsPath()
{
    return ConfigDirPath() + "/controller_bindings";
}

bool IsValidControllerBinding(int value)
{
    return value >= SDL_CONTROLLER_BUTTON_A && value < SDL_CONTROLLER_BUTTON_MAX &&
           value != SDL_CONTROLLER_BUTTON_START;
}

bool IsValidControllerAxis(int value)
{
    return value >= SDL_CONTROLLER_AXIS_LEFTX && value < SDL_CONTROLLER_AXIS_MAX;
}

ControllerBindings LoadControllerBindings()
{
    ControllerBindings bindings;
    std::ifstream in(ControllerBindingsPath());
    int buttons[3] = {};
    if (!(in >> buttons[0] >> buttons[1] >> buttons[2])) return bindings;
    for (int button : buttons) {
        if (!IsValidControllerBinding(button)) return ControllerBindings{};
    }
    bindings.jump = static_cast<SDL_GameControllerButton>(buttons[0]);
    bindings.fire = static_cast<SDL_GameControllerButton>(buttons[1]);
    bindings.selectWeapon = static_cast<SDL_GameControllerButton>(buttons[2]);

    // A legacy file ends after the three button values. Preserve it and use
    // default axes; once saved again it is upgraded to the extended format.
    in >> std::ws;
    if (in.peek() == std::char_traits<char>::eof()) return bindings;

    int axes[3] = {};
    int inverted[3] = {};
    int deadzone = 0;
    if (!(in >> axes[0] >> axes[1] >> axes[2] >>
              inverted[0] >> inverted[1] >> inverted[2] >> deadzone)) {
        return ControllerBindings{};
    }
    for (int axis : axes) {
        if (!IsValidControllerAxis(axis)) return ControllerBindings{};
    }
    for (int value : inverted) {
        if (value != 0 && value != 1) return ControllerBindings{};
    }
    in >> std::ws;
    if (deadzone < 0 || deadzone > 30000 || in.peek() != std::char_traits<char>::eof()) {
        return ControllerBindings{};
    }
    bindings.steerAxis = static_cast<SDL_GameControllerAxis>(axes[0]);
    bindings.accelerateAxis = static_cast<SDL_GameControllerAxis>(axes[1]);
    bindings.brakeAxis = static_cast<SDL_GameControllerAxis>(axes[2]);
    bindings.invertSteering = inverted[0] != 0;
    bindings.invertAccelerate = inverted[1] != 0;
    bindings.invertBrake = inverted[2] != 0;
    bindings.deadzone = deadzone;
    return bindings;
}

void SaveControllerBindings(const ControllerBindings& bindings)
{
    std::ofstream out(ControllerBindingsPath());
    out << static_cast<int>(bindings.jump) << ' '
        << static_cast<int>(bindings.fire) << ' '
        << static_cast<int>(bindings.selectWeapon) << ' '
        << static_cast<int>(bindings.steerAxis) << ' '
        << static_cast<int>(bindings.accelerateAxis) << ' '
        << static_cast<int>(bindings.brakeAxis) << ' '
        << (bindings.invertSteering ? 1 : 0) << ' '
        << (bindings.invertAccelerate ? 1 : 0) << ' '
        << (bindings.invertBrake ? 1 : 0) << ' '
        << bindings.deadzone << '\n';
}

constexpr int kOnboardingVersion = 2;

std::string OnboardingCompletePath()
{
    return ConfigDirPath() + "/onboarding_complete";
}

bool HasCompletedOnboarding()
{
    std::ifstream in(OnboardingCompletePath());
    int completedVersion = 0;
    return (in >> completedVersion) && completedVersion == kOnboardingVersion;
}

void SaveOnboardingComplete()
{
    std::ofstream out(OnboardingCompletePath());
    out << kOnboardingVersion << '\n';
}

struct RemotePlayer
{
    MR_MainCharacter* mCharacter = nullptr;
    MR_FreeElementHandle mHandle = nullptr;
};
struct OnlineElementBroadcastContext
{
    RaceServerClient* mClient = nullptr;
};

void BroadcastCreatedElement(MR_FreeElement* pElement, int pRoom, void* pHookData)
{
    OnlineElementBroadcastContext* lContext = static_cast<OnlineElementBroadcastContext*>(pHookData);
    if (pElement == nullptr || lContext == nullptr || lContext->mClient == nullptr ||
        !lContext->mClient->IsConnected()) {
        return;
    }
    const MR_ObjectFromFactoryId lTypeId = pElement->GetTypeId();
    const MR_ElementNetState lState = pElement->GetNetState();
    lContext->mClient->SendAutoElement(lTypeId.mDllId, lTypeId.mClassId, pRoom,
                                       lState.mData, lState.mDataLen);
}

// Light theme with red/maroon accents instead of Dear ImGui's default dark-blue
// look, and closer to the Windows client's pale system-dialog appearance -- see
// RunLobbyScreen's layout, which mirrors IDD_INTERNET_MEETING's panel arrangement.
// Dark charcoal base (like Discord/Steam/most modern game UIs) with a crimson
// accent reserved for "look here" or "click this" -- headings, the header banner,
// buttons, selection -- so it reads as deliberate rather than everywhere.
constexpr ImVec4 kHoverNetRed{0.86f, 0.20f, 0.27f, 1.00f};
constexpr ImVec4 kHoverNetRedHover{0.95f, 0.32f, 0.38f, 1.00f};
constexpr ImVec4 kHoverNetRedActive{0.70f, 0.12f, 0.18f, 1.00f};
constexpr ImVec4 kHoverNetCoral{0.98f, 0.45f, 0.48f, 1.00f};
constexpr ImVec4 kHoverNetWhite{1.00f, 1.00f, 1.00f, 1.00f};
constexpr ImVec4 kHoverNetSurface{0.075f, 0.080f, 0.105f, 0.98f};
ImFont* gHoverNetDisplayFont = nullptr;

void LoadHoverNetMenuFonts(ImGuiIO& io)
{
    gHoverNetDisplayFont = nullptr;

    ImFontConfig bodyConfig;
    bodyConfig.SizePixels = 19.0f;
    bodyConfig.OversampleH = 2;
    bodyConfig.OversampleV = 2;
    bodyConfig.RasterizerMultiply = 1.08f;
    const std::string bodyPath = SourcePath("NetTarget/LinuxClient/assets/Inter-Variable.ttf");
    ImFont* bodyFont = io.Fonts->AddFontFromFileTTF(bodyPath.c_str(), bodyConfig.SizePixels, &bodyConfig);
    if (bodyFont == nullptr) {
        io.Fonts->AddFontDefault(&bodyConfig);
    }

    ImFontConfig displayConfig;
    displayConfig.SizePixels = 25.0f;
    displayConfig.OversampleH = 2;
    displayConfig.OversampleV = 2;
    displayConfig.RasterizerMultiply = 1.10f;
    const std::string displayPath = SourcePath("NetTarget/LinuxClient/assets/Oxanium-Variable.ttf");
    gHoverNetDisplayFont = io.Fonts->AddFontFromFileTTF(
        displayPath.c_str(), displayConfig.SizePixels, &displayConfig);
    if (gHoverNetDisplayFont == nullptr) {
        gHoverNetDisplayFont = bodyFont;
    }
}

void ApplyHoverNetLobbyStyle(float scale, bool highContrast)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(20.0f, 18.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 11.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.PopupRounding = 12.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = highContrast ? 1.0f : 0.0f;

    // High contrast removes subtle near-neighbour shades: surfaces become much
    // darker, secondary text and borders brighter, and action red darker under
    // white labels. The standard palette retains its softer layered appearance.
    const ImVec4 windowBg = highContrast ? ImVec4(0.01f, 0.01f, 0.01f, 1.00f)
                                        : ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    const ImVec4 panelBg = highContrast ? ImVec4(0.045f, 0.045f, 0.055f, 1.00f)
                                        : kHoverNetSurface;
    const ImVec4 fieldBg = highContrast ? ImVec4(0.00f, 0.00f, 0.00f, 1.00f)
                                        : ImVec4(0.045f, 0.050f, 0.070f, 1.00f);
    const ImVec4 softBorder = highContrast ? ImVec4(0.82f, 0.82f, 0.86f, 1.00f)
                                           : ImVec4(0.20f, 0.22f, 0.29f, 1.00f);
    const ImVec4 text = highContrast ? ImVec4(1.00f, 1.00f, 1.00f, 1.00f)
                                     : ImVec4(0.93f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textMuted = highContrast ? ImVec4(0.82f, 0.82f, 0.86f, 1.00f)
                                          : ImVec4(0.62f, 0.65f, 0.72f, 1.00f);
    const ImVec4 actionRed = highContrast ? ImVec4(0.58f, 0.03f, 0.10f, 1.00f) : kHoverNetRed;
    const ImVec4 actionHover = highContrast ? ImVec4(0.78f, 0.08f, 0.16f, 1.00f) : kHoverNetRedHover;
    const ImVec4 actionActive = highContrast ? ImVec4(0.42f, 0.01f, 0.06f, 1.00f) : kHoverNetRedActive;
    const ImVec4 selection = highContrast ? ImVec4(0.95f, 0.15f, 0.22f, 0.62f)
                                          : ImVec4(0.86f, 0.20f, 0.27f, 0.35f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_WindowBg] = windowBg;
    colors[ImGuiCol_ChildBg] = panelBg;
    colors[ImGuiCol_PopupBg] = panelBg;
    colors[ImGuiCol_Border] = softBorder;
    colors[ImGuiCol_FrameBg] = fieldBg;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_CheckMark] = highContrast ? kHoverNetWhite : kHoverNetCoral;
    colors[ImGuiCol_SliderGrab] = actionRed;
    colors[ImGuiCol_SliderGrabActive] = actionActive;
    colors[ImGuiCol_Button] = actionRed;
    colors[ImGuiCol_ButtonHovered] = actionHover;
    colors[ImGuiCol_ButtonActive] = actionActive;
    colors[ImGuiCol_Header] = selection;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.86f, 0.20f, 0.27f, 0.55f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.86f, 0.20f, 0.27f, 0.75f);
    colors[ImGuiCol_NavHighlight] = kHoverNetCoral;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, 0.01f, 0.02f, 0.78f);
    colors[ImGuiCol_Separator] = softBorder;
    colors[ImGuiCol_SeparatorHovered] = actionHover;
    colors[ImGuiCol_SeparatorActive] = actionActive;
    colors[ImGuiCol_ResizeGrip] = ImVec4(actionRed.x, actionRed.y, actionRed.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = actionHover;
    colors[ImGuiCol_ResizeGripActive] = actionActive;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kHoverNetRed.x, kHoverNetRed.y, kHoverNetRed.z, 0.40f);
    colors[ImGuiCol_ScrollbarBg] = windowBg;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = kHoverNetRedHover;
    colors[ImGuiCol_ScrollbarGrabActive] = kHoverNetRedActive;
    style.ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}

// Buttons use a solid red fill (see ApplyHoverNetLobbyStyle), so they need white
// label text -- the style's default text color, while readable on dark panels,
// has less contrast against the red fill than pure white.
bool HoverNetButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return pressed;
}

// A secondary action (Leave, Back, Host ...): a muted fill so the one primary
// action on a screen (Start Race, Join Game) is the only red button and reads as
// the thing to press.
bool HoverNetQuietButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.11f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.12f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.10f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.33f, 0.41f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool pressed = HoverNetButton(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

// Section titles ("Game list", "Chat section", ...) in a coral accent, legible on
// the dark panel background, distinguishing them from ordinary body text without
// another loud color block.
void AccentNote(const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void HoverNetSectionHeading(const char* label)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        cursor, ImVec2(cursor.x + 4.0f, cursor.y + height),
        ImGui::ColorConvertFloat4ToU32(kHoverNetRed), 2.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
    if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
    ImGui::TextUnformatted(label);
    if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
    ImGui::PopStyleColor();
}

void DrawHoverNetScreenBackdrop(const ImGuiViewport* viewport)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = viewport->WorkPos;
    const ImVec2 end(start.x + viewport->WorkSize.x, start.y + viewport->WorkSize.y);
    draw->AddRectFilledMultiColor(start, end,
        IM_COL32(9, 12, 22, 255), IM_COL32(31, 10, 22, 255),
        IM_COL32(5, 8, 15, 255), IM_COL32(13, 7, 15, 255));
    const float horizon = start.y + viewport->WorkSize.y * 0.72f;
    const ImVec2 vanish(start.x + viewport->WorkSize.x * 0.50f, horizon);
    for (int line = -8; line <= 8; ++line) {
        const float x = start.x + viewport->WorkSize.x * (0.5f + line * 0.14f);
        draw->AddLine(vanish, ImVec2(x, end.y), IM_COL32(224, 42, 67, 34), 1.0f);
    }
    for (int row = 1; row <= 4; ++row) {
        const float amount = static_cast<float>(row * row) / 16.0f;
        const float y = horizon + (end.y - horizon) * amount;
        draw->AddLine(ImVec2(start.x, y), ImVec2(end.x, y), IM_COL32(224, 42, 67, 28), 1.0f);
    }
}

void HoverNetScreenHeading(const char* eyebrow, const char* title, const char* detail)
{
    ImGui::TextColored(kHoverNetCoral, "%s", eyebrow);
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
    if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
    ImGui::TextUnformatted(title);
    if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
    ImGui::PopStyleColor();
    if (detail != nullptr && detail[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", detail);
    }
}

// A one-line coral-accented note (e.g. inline field validation) without the
// section rule HoverNetSectionHeading always adds after its text.
void HoverNetHint(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

// Forward-declared: defined below, but RunLobbyScreen (right below) already
// needs to call it from its own SDL_QUIT handling.
bool ConfirmQuit(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                 const MR_Sprite& font, int pFrameLimit = -1);

// PauseChoice/RunPauseMenu and SettingsResult/RunSettingsScreen are fully
// defined further below (where the pause menu and settings screen already
// live), but RunLobbyScreen's own Escape handling needs to call them too --
// pressing Escape while browsing/hosting/waiting in the lobby used to just
// close the lobby screen outright with no way back except relaunching it,
// unlike Escape everywhere else in the client.
enum class PauseChoice
{
    eResume,
    eRestartRace,
    eLeaveRace,
    eOnlineLobby,
    eWatchReplay,
    eMainMenu,
    eSettings,
    eControls,
    eQuit,
};

PauseChoice RunPauseMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                         MR_3DViewPort& viewport, const MR_Sprite& font, bool pIsOnline,
                         int pFrameLimit = -1, bool pOfferReplays = true, bool pCanRestart = false);

enum class PostRaceChoice
{
    eContinue,
    eNewLocalRace,
    eRestartRace,
    eOnlineLobby,
    eMainMenu,
    eQuit,
};

PostRaceChoice RunPostRaceScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                                 MR_3DViewPort& viewport, const MR_Sprite& font,
                                 MR_SimulationTime pFinishTime, MR_SimulationTime pBestLap,
                                 int pRank, int pPlayerCount, int pFrameLimit = -1,
                                 const PersonalBests::Outcome* pBests = nullptr, bool pCanRestart = false);

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
// The official HoverNet server players are sent to without typing anything. The key is the server's identity for an encrypted connection (64 hex
// characters) once the owner has made the server reachable from the internet with one; empty means a plain connection. The browser always
// uses its own site (wss://<site>/auth/, TLS at the web server) and ignores these.
constexpr const char* kOfficialAuthAddress = "hovernet.outiva.com:9610";
constexpr const char* kOfficialAuthKey = "";

struct AuthServerSettings
{
    std::string address = kOfficialAuthAddress;   // host:port
    std::string key = kOfficialAuthKey;           // 64 hex characters, or empty
    bool custom = false;                          // the player chose another server under "Use another server"; otherwise the official one
};

std::string AuthServerSettingsPath() { return ConfigDirPath() + "/hovernet_server"; }

AuthServerSettings LoadAuthServerSettings()
{
    AuthServerSettings s;
    std::ifstream in(AuthServerSettingsPath());
    std::string address, key, flag;
    std::getline(in, address);
    std::getline(in, key);
    std::getline(in, flag);
    if (flag == "custom" && !address.empty()) { s.address = address; s.key = key; s.custom = true; }
    return s;
}

void SaveAuthServerSettings(const AuthServerSettings& s)
{
    std::ofstream out(AuthServerSettingsPath());
    out << s.address << '\n' << s.key << '\n' << (s.custom ? "custom" : "official") << '\n';
}

// "host", "host:port" or "[v6]:port" -> host and port (default 9610). False when it does not look like an address.
bool SplitServerAddress(const std::string& text, std::string& host, int& port)
{
    std::string t = text;
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    port = 9610;
    if (t.empty() || t.size() > 255) return false;
    const std::size_t colon = t.rfind(':');
    if (colon != std::string::npos && t.find(':') == colon) {
        const int p = std::atoi(t.c_str() + colon + 1);
        if (p < 1 || p > 65535) return false;
        port = p;
        t = t.substr(0, colon);
    }
    if (t.empty()) return false;
    host = t;
    return true;
}

#endif

struct SettingsResult
{
    bool confirmed = false;
    std::string username;
    std::string serverHost;
    unsigned serverPort = 0;
    double volume = 1.0;
    bool muted = false;
    bool fullscreen = false;
    int windowWidth = 1024;
    int windowHeight = 768;
    float uiScale = 1.0f;
    bool largeHudText = false;
    bool reducedMotion = false;
    bool highContrast = false;
};

SettingsResult RunSettingsScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                                 const MR_Sprite& font, const std::string& currentUsername,
                                 const std::string& currentServerHost, unsigned currentServerPort,
                                 double currentVolume, bool currentMuted, bool currentFullscreen,
                                 int pFrameLimit);

bool RunOnboardingScreen(SDL2GraphicsBackend& graphics, int pFrameLimit, int pStartPage = 0);
void RunControlsScreen(SDL2GraphicsBackend& graphics, int pFrameLimit);

// pClient is caller-owned (not constructed here) and deliberately left connected
// when this returns true: a joined-and-started race needs to keep talking to the
// server afterward (player position sync), so the connection can't be scoped to
// just this function the way it could when all it did was browse/chat.
bool RunLobbyScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                    const MR_Sprite& font, RaceServerClient& pClient, std::string& host, unsigned& port,
                    std::string& outJoinedName, std::string& outTrackName, int& outNumLaps, bool& outWeapons,
                    int& outLocalClientId, std::vector<RaceServerPeer>& outPeers, int pFrameLimit,
                    bool pAutoHostStart = false, bool pAutoJoin = false, bool pWaitForPeer = false,
                    int pAutoTrackIndex = 0)
{
    // The lobby screen renders through Dear ImGui directly against the SDL_Renderer
    // graphics already owns, not the paletted MR_VideoBuffer the 3D game view uses --
    // buffer/viewport/font are also passed to Settings when recovery from a
    // failed connection needs a corrected server address.
    RaceServerClient& client = pClient;

    while (!client.Connect(host, port)) {
        const std::string failureReason = client.GetProtocolError();
        std::fprintf(stderr, "Lobby screen: could not connect to RaceServer at %s:%u%s%s\n",
                     host.c_str(), port, failureReason.empty() ? "" : ": ", failureReason.c_str());
        // Bounded smoke runs must never wait for interaction. Interactive players
        // get an actionable cross-platform prompt instead of a terminal-only
        // failure that looks like the Online Lobby button did nothing.
        if (pFrameLimit >= 0) return false;

        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Retry"},
            {0, 2, "Open Settings"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Back to main menu"}
        };
        char message[320];
        if (!failureReason.empty()) {
            std::snprintf(message, sizeof(message),
                          "The RaceServer at %s:%u rejected this client.\n\n%s\n\n"
                          "Install a compatible HoverNet release or choose another server in Settings.",
                          host.c_str(), port, failureReason.c_str());
        } else {
            std::snprintf(message, sizeof(message),
                          "Could not connect to the RaceServer at %s:%u.\n\n"
                          "Check your connection, or change the server address in Settings.",
                          host.c_str(), port);
        }
        const SDL_MessageBoxData messageBox = {
            SDL_MESSAGEBOX_ERROR, graphics.GetWindow(), "HoverNet - Online Lobby",
            message, 3, buttons, nullptr
        };
        int buttonId = 0;
        if (SDL_ShowMessageBox(&messageBox, &buttonId) != 0 || buttonId == 0) {
            return false;
        }
        if (buttonId == 2) {
            const SettingsResult settings = RunSettingsScreen(
                graphics, buffer, viewport, font, LoadUsername(), host, port,
                LoadVolume(), LoadMute(), LoadFullscreen(), -1);
            if (settings.confirmed) {
                SaveUsername(settings.username);
                host = settings.serverHost;
                port = settings.serverPort;
                SaveServerUrl(host, port);
                SaveVolume(settings.volume);
                SaveMute(settings.muted);
                SaveFullscreen(settings.fullscreen);
                SaveWindowSize(WindowSize{settings.windowWidth, settings.windowHeight});
                SaveUiScale(settings.uiScale);
                SaveLargeHudText(settings.largeHudText);
                SaveReducedMotion(settings.reducedMotion);
                SaveHighContrast(settings.highContrast);
            }
        }
    }

    std::vector<RaceServerGameInfo> games;
    std::vector<RaceServerGameInfo> gamesBeingListed;
    client.SendMessage(eRSMsgListGames, nullptr, 0);

    std::vector<std::string> chatLog;
    auto pushChat = [&chatLog](const std::string& line) {
        chatLog.push_back(line);
        // ImGui's chat pane scrolls, so it can keep far more history visible than
        // the old fixed-height hand-drawn panel could.
        constexpr std::size_t kMaxChatLines = 200;
        if (chatLog.size() > kMaxChatLines) {
            chatLog.erase(chatLog.begin());
        }
    };

    // Who else is browsing the lobby right now (not the race roster -- that's
    // raceMembers, populated once eWaitingRoom starts). The first batch of
    // eRSMsgLobbyUserPresent after ListLobbyUsers is people already there before
    // us, so it's populated silently; receivedInitialRoster (set by
    // eRSMsgLobbyUserListEnd) switches later eRSMsgLobbyUserPresent messages over
    // to "someone just joined" chat announcements instead.
    std::vector<RaceServerPeer> lobbyUsers;
    bool receivedInitialRoster = false;

    // Chat messages carry only the sender's client id (see ServerSocket.cpp's
    // MRNM_CHAT_MESSAGE relay), not a name -- resolve it against whichever roster
    // we've learned it from, lobby or race, so incoming lines can be attributed.
    std::map<int, std::string> playerNames;

    std::string username = LoadUsername();
    if (pAutoHostStart) username = pWaitForPeer ? "AcceptanceHost" : "AcceptancePlayer";
    else if (pAutoJoin) username = "AcceptanceJoiner";
    int selected = 0;
    LobbyPhase phase = LobbyPhase::eBrowsing;
    Uint32 lastRefresh = SDL_GetTicks();
    Uint32 lastPing = SDL_GetTicks();
    std::string statusText = "Connected to the shared HoverNet lobby.";
    bool running = true;
    bool joined = false;
    bool isHost = false;
    bool connectionLossReported = false;
    bool autoHostSent = false;
    bool autoStartSent = false;
    bool autoJoinSent = false;
    std::vector<std::string> raceMembers;
    int framesShown = 0;
    bool hostPopupOpen = false;
    bool requestOpenHostPopup = false;
    bool requestOpenRecoverySettings = false;
    // The old hand-drawn lobby let you start typing chat from anywhere by pressing
    // T; with a real text field there's no such affordance, so without this the
    // player has to notice and click the chat box before Enter does anything --
    // easy to read as "chat is broken". Focus it by default instead.
    bool focusChatInput = true;
    // The browser canvas owns keyboard input, but a newly displayed ImGui text
    // box isn't active until it receives focus. Focus the first-run username
    // field as soon as it appears so SDL_TEXTINPUT reaches it without requiring
    // a precisely positioned mouse click first.
    bool focusUsernameInput = username.empty();
    // The chat log follows the newest message unless the player scrolled up to read
    // older ones (see the ChatLog child); sending a message always jumps back down.
    bool chatForcePin = true;
    bool chatWasPinned = true;
    // Phones: the panel grid becomes tabs (0 games/race, 1 chat, 2 users) -- see the lobby layout.
    int lobbyTab = std::getenv("HOVERNET_TEST_LOBBY_TAB") != nullptr ? std::atoi(std::getenv("HOVERNET_TEST_LOBBY_TAB")) : 0;
    // Test hooks (screenshots only): HOVERNET_TEST_LOBBY_DEMO=browse|waiting fills the
    // lobby with sample races, users and chat once connected.
    const char* demoMode = std::getenv("HOVERNET_TEST_LOBBY_DEMO");

    char usernameBuf[24] = {0};
    char chatBuf[64] = {0};

    if (username.empty()) {
        phase = LobbyPhase::eEnteringName;
        statusText = "Choose a name to show other players";
    }
    else {
        std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", username.c_str());
        client.SetPlayerName(username);
        client.ListLobbyUsers();
    }

    HostPrefs hostPrefs = LoadHostPrefs();
    if (pAutoHostStart) {
        hostPrefs.mTrackIndex = std::max(0, std::min(pAutoTrackIndex, Tracks().Count() - 1));
        hostPrefs.mLaps = 1;
        hostPrefs.mWeapons = true;
    }
    std::vector<TrackPreview> lobbyTrackPreviews(static_cast<std::size_t>(Tracks().Count()));

    auto refreshGames = [&]() {
        gamesBeingListed.clear();
        client.SendMessage(eRSMsgListGames, nullptr, 0);
        lastRefresh = SDL_GetTicks();
        statusText = "Refreshing race list...";
    };
    auto reconnectToServer = [&]() {
        if (!client.Connect(host, port)) {
            const std::string reason = client.GetProtocolError();
            statusText = reason.empty() ? "Reconnect failed - check the server address in Settings" : reason;
            return false;
        }
        games.clear(); gamesBeingListed.clear(); lobbyUsers.clear(); raceMembers.clear();
        playerNames.clear(); outPeers.clear(); outJoinedName.clear(); outTrackName.clear();
        selected = 0; joined = false; isHost = false; receivedInitialRoster = false;
        connectionLossReported = false;
        phase = username.empty() ? LobbyPhase::eEnteringName : LobbyPhase::eBrowsing;
        statusText = "Reconnected to the shared HoverNet lobby.";
        if (!username.empty()) {
            client.SetPlayerName(username);
            client.ListLobbyUsers();
            refreshGames();
        }
        return true;
    };
    DownloadUi download;
    bool hostAfterDownload = false;
    bool joinAfterDownload = false;
    auto joinSelected = [&]() {
        if (!games.empty() && selected >= 0 && selected < static_cast<int>(games.size()) &&
            !games[static_cast<std::size_t>(selected)].mStarted) {
            const RaceServerGameInfo& game = games[static_cast<std::size_t>(selected)];
            const int joinTrackIndex = FindHostableTrack(game.mTrack);
            if (joinTrackIndex < 0) {
                statusText = "This race uses a track this version of HoverNet does not know ('" + game.mTrack +
                             "') - update the game to join it";
                return;
            }
            const TrackEntry* joinTrack = Tracks().Get(joinTrackIndex);
            if (joinTrack != nullptr && !joinTrack->mInstalled) {
                if (!joinTrack->mHasDownload) {
                    statusText = "Track '" + game.mTrack + "' is not installed and cannot be downloaded";
                    return;
                }
                if (!download.Active()) {
                    download.Begin({*joinTrack}, 0);
                    joinAfterDownload = true;
                }
                return;
            }
            if (client.JoinGameById(game.mRaceId)) {
                outJoinedName = game.mName;
                outTrackName = game.mTrack;
                outNumLaps = std::max(1, game.mNumLaps);
                outWeapons = game.mWeapons;
                phase = LobbyPhase::eWaitingRoom;
                raceMembers.clear();
                statusText = "Joined - waiting for the race to start";
            }
        }
    };
    auto hostRaceNow = [&]() {
        // The race name is just the track name -- no reason to make the host type
        // one. Two people can host the same track at once (races are joined by id,
        // see JoinGameById), so this doesn't collide.
        const std::string trackName = TrackNameAt(hostPrefs.mTrackIndex);
        const std::string raceName = RaceNameForTrack(trackName);
        outTrackName = trackName;
        outNumLaps = hostPrefs.mLaps;
        outWeapons = hostPrefs.mWeapons;
        if (client.HostRace(raceName, trackName,
                             hostPrefs.mLaps, hostPrefs.mWeapons)) {
            outJoinedName = raceName;
            phase = LobbyPhase::eWaitingRoom;
            raceMembers.clear();
            if (!pAutoHostStart) SaveHostPrefs(hostPrefs);
            statusText = "Race created - waiting for players";
        }
        else {
            // HostRace() only fails a client-side size check (or the socket being
            // down); the server's own verdict (e.g. an unknown track) always
            // arrives later as an async eRSMsgJoinedRace(raceId=-1), handled below.
            statusText = "Could not send host request";
        }
    };
    // Leaves the current hosted/joined race and goes back to browsing, without
    // tearing down the whole lobby screen the way Quit does. There's no
    // explicit "leave race" wire message -- disconnecting and reconnecting is
    // what actually removes this client's ClientConnection server-side (which
    // is what reaps a hosted race immediately now, see RaceManager::LeaveRace),
    // so just do that and re-announce as browsing, same as a fresh connect.
    auto cancelWaitingRoom = [&]() {
        client.Disconnect();
        isHost = false;
        raceMembers.clear();
        lobbyUsers.clear();
        receivedInitialRoster = false;
        phase = LobbyPhase::eBrowsing;
        statusText = "Left the race.";
        if (client.Connect(host, port)) {
            client.SetPlayerName(username);
            client.ListLobbyUsers();
            refreshGames();
        }
        else {
            statusText = "Left the race, but could not reconnect to the lobby.";
        }
    };

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // This is a fixed-purpose screen, not a user-arranged workspace -- don't leave
    // an imgui.ini behind in whatever directory the game happens to run from.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadHoverNetMenuFonts(io);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    SDL_StartTextInput();
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        if (pAutoHostStart && !autoHostSent) {
            hostRaceNow();
            autoHostSent = true;
        }
        if (pAutoJoin && !autoJoinSent && phase == LobbyPhase::eBrowsing && !games.empty()) {
            joinSelected();
            autoJoinSent = true;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    g_QuitConfirmed = true;
                    running = false;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                // Used to just close the lobby screen outright -- the same
                // pause menu Escape brings up everywhere else in the client,
                // so there's a way back out of an accidental press, and a way
                // to reach Settings without leaving first.
                const PauseChoice pauseChoice = RunPauseMenu(graphics, buffer, viewport, font, false, pFrameLimit, false);
                if (pauseChoice == PauseChoice::eQuit) {
                    g_QuitConfirmed = true;
                    running = false;
                }
                else if (pauseChoice == PauseChoice::eLeaveRace) {
                    // Labeled "New Local Race" here (pIsOnline=false) -- leaves
                    // the lobby exactly like Escape used to unconditionally,
                    // which already falls back to local play (and from there,
                    // the local race setup screen) in the caller.
                    running = false;
                }
                else if (pauseChoice == PauseChoice::eSettings) {
                    const SettingsResult settings = RunSettingsScreen(
                        graphics, buffer, viewport, font, username, host, port,
                        LoadVolume(), LoadMute(), LoadFullscreen(), pFrameLimit);
                    if (settings.confirmed) {
                        SaveUsername(settings.username);
                        if (settings.username != username) {
                            username = settings.username;
                            std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", username.c_str());
                            if (client.IsConnected()) {
                                client.SetPlayerName(username);
                            }
                        }
                        // host/port here are this screen's own parameters, already
                        // connected to whatever they were at entry -- a changed
                        // server address can't retroactively apply to that live
                        // connection, so just persist it for the next time the
                        // lobby is (re)opened.
                        SaveServerUrl(settings.serverHost, settings.serverPort);
                        SaveVolume(settings.volume);
                        SaveMute(settings.muted);
                        SaveFullscreen(settings.fullscreen);
                        WindowSize savedSize;
                        savedSize.width = settings.windowWidth;
                        savedSize.height = settings.windowHeight;
                        SaveWindowSize(savedSize);
                        SaveUiScale(settings.uiScale);
                        SaveLargeHudText(settings.largeHudText);
                        SaveReducedMotion(settings.reducedMotion);
                        SaveHighContrast(settings.highContrast);
                    }
                }
                else if (pauseChoice == PauseChoice::eControls) {
                    RunControlsScreen(graphics, pFrameLimit);
                    if (g_QuitConfirmed) {
                        running = false;
                    }
                }
                // eResume / eOnlineLobby: no-op -- already right here browsing.
            }
        }

        if (running && phase == LobbyPhase::eBrowsing && SDL_GetTicks() - lastRefresh > 3000) {
            gamesBeingListed.clear();
            client.SendMessage(eRSMsgListGames, nullptr, 0);
            lastRefresh = SDL_GetTicks();
            lastPing = SDL_GetTicks();
        }

        // The race list refresh above already doubles as a keep-alive while
        // browsing, but the waiting room (and eBrowsing between refreshes) can
        // otherwise sit silent past the server's idle timeout -- ping so the
        // server doesn't decide this connection went stale and drop it.
        if (running && phase != LobbyPhase::eEnteringName && SDL_GetTicks() - lastPing > 10000) {
            client.Ping();
            lastPing = SDL_GetTicks();
        }

        // Drain every message currently waiting rather than blocking for a response
        // to one specific request -- lobby listings and chat share this connection
        // and can arrive interleaved.
        RaceServerMessage message;
        while (client.PollMessage(message, 0)) {
            if (client.HandlePingReply(message)) continue;
            RaceServerGameInfo info;
            if (message.mType == eRSMsgGameInfo && RaceServerClient::ParseGameInfo(message, info)) {
                gamesBeingListed.push_back(info);
            }
            else if (message.mType == eRSMsgGameListEnd) {
                games = gamesBeingListed;
                if (!games.empty()) {
                    selected = std::min(selected, static_cast<int>(games.size()) - 1);
                    if (phase == LobbyPhase::eBrowsing) {
                        statusText = std::to_string(games.size()) +
                            (games.size() == 1 ? " open race" : " open races");
                    }
                }
                else {
                    selected = 0;
                    if (phase == LobbyPhase::eBrowsing) {
                        statusText = "No open races - host one to get started";
                    }
                }
            }
            else if (message.mType == eRSMsgChatMessage) {
                int senderClientId = -1;
                std::string text;
                if (RaceServerClient::ParseChatMessage(message, senderClientId, text)) {
                    const auto nameIt = playerNames.find(senderClientId);
                    const std::string senderName =
                        (nameIt != playerNames.end()) ? nameIt->second : "Player " + std::to_string(senderClientId);
                    pushChat(senderName + ": " + text);
                }
            }
            else if (message.mType == eRSMsgJoinedRace) {
                RaceServerJoinAck ack;
                if (RaceServerClient::ParseJoinedRace(message, ack)) {
                    if (ack.mRaceId < 0) {
                        // The server rejected the pending host request (unknown
                        // track) or the joined race no longer exists. The UI
                        // already optimistically moved to the waiting room on
                        // send; undo that.
                        phase = LobbyPhase::eBrowsing;
                        statusText = "Could not create race";
                        pushChat("* Could not host or join '" + outJoinedName + "'");
                    }
                    else {
                        isHost = ack.mIsHost;
                        outLocalClientId = ack.mClientId;
                        playerNames[ack.mClientId] = username;
                        statusText = ack.mIsHost ? "Race created - waiting for players" : "Joined - waiting for host";
                    }
                }
            }
            else if (message.mType == eRSMsgConnNameSet) {
                RaceServerPeer peer;
                if (RaceServerClient::ParsePeer(message, peer)) {
                    raceMembers.push_back(peer.mName);
                    outPeers.push_back(peer);
                    playerNames[peer.mClientId] = peer.mName;
                    pushChat("* " + peer.mName + " joined");
                    statusText = peer.mName + " joined the race";
                }
            }
            else if (message.mType == eRSMsgRaceStarted) {
                statusText = "Race starting...";
                joined = true;
                running = false;
            }
            else if (message.mType == eRSMsgLobbyUserPresent) {
                RaceServerPeer peer;
                if (RaceServerClient::ParsePeer(message, peer) &&
                    std::none_of(lobbyUsers.begin(), lobbyUsers.end(),
                                 [&](const RaceServerPeer& u) { return u.mClientId == peer.mClientId; })) {
                    lobbyUsers.push_back(peer);
                    playerNames[peer.mClientId] = peer.mName;
                    if (receivedInitialRoster) {
                        pushChat("* " + peer.mName + " entered the lobby");
                    }
                }
            }
            else if (message.mType == eRSMsgLobbyUserListEnd) {
                receivedInitialRoster = true;
            }
            else if (message.mType == eRSMsgLobbyUserLeft) {
                int leftClientId = -1;
                if (RaceServerClient::ParseLobbyUserLeft(message, leftClientId)) {
                    const auto it = std::find_if(lobbyUsers.begin(), lobbyUsers.end(),
                                                 [&](const RaceServerPeer& u) { return u.mClientId == leftClientId; });
                    if (it != lobbyUsers.end()) {
                        pushChat("* " + it->mName + " left the lobby");
                        lobbyUsers.erase(it);
                    }
                }
            }
            else if (message.mType == eRSMsgPlayerNameAssigned) {
                // The name we asked for may already be taken by someone else
                // currently connected -- the server disambiguates it (see
                // ServerSocket.cpp's MRNM_SET_PLAYER_NAME handler) and tells us
                // the real one back here, which we adopt as our own.
                std::string assignedName;
                if (RaceServerClient::ParsePlayerNameAssigned(message, assignedName) &&
                    assignedName != username) {
                    username = assignedName;
                    if (!pAutoHostStart && !pAutoJoin) SaveUsername(username);
                    std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", username.c_str());
                    statusText = "That name was taken -- you're now '" + username + "'";
                }
            }
        }

        if (pAutoHostStart && isHost && !autoStartSent && (!pWaitForPeer || !outPeers.empty())) {
            client.StartRace();
            autoStartSent = true;
        }

        if (demoMode != nullptr && framesShown == 9) {
            pushChat("Banshee: just joined, hello!");
            pushChat("* Banshee entered the lobby");
            pushChat("Speedy-Gonzales: welcome Banshee, this is the newest message");
        }
        // Screenshot fixture: sample races, users and chat so the layout can be checked
        // headlessly with a populated lobby. Never set in normal play.
        if (demoMode != nullptr && framesShown == 3 && phase == LobbyPhase::eBrowsing) {
            const std::string mode = demoMode;
            for (int i = 0; i < 4; ++i) {
                static const char* const kNames[] = {"Speedy-Gonzales", "NightRider", "zoom_zoom", "Skeletor-Linux1"};
                RaceServerPeer peer; peer.mClientId = 200 + i; peer.mName = kNames[i];
                lobbyUsers.push_back(peer);
            }
            const char* const kLines[] = {
                "* NightRider entered the lobby", "Speedy-Gonzales: anyone up for Steeplechase?",
                "NightRider: yes! 3 laps, weapons on", "zoom_zoom: give me a minute, downloading a track",
                "* zoom_zoom entered the lobby", "Speedy-Gonzales: no rush", "NightRider: hosting now",
                "zoom_zoom: ok joined", "Skeletor-Linux1: hi all", "Speedy-Gonzales: Windows or Linux?",
                "zoom_zoom: windows x64", "NightRider: linux here, works great", "Speedy-Gonzales: nice",
                "zoom_zoom: ready when you are", "NightRider: starting in a sec", "Skeletor-Linux1: who is host?",
                "NightRider: me", "Speedy-Gonzales: gl hf everyone"};
            for (const char* line : kLines) pushChat(line);
            if (mode == "waiting") {
                phase = LobbyPhase::eWaitingRoom;
                outTrackName = "Steeplechase"; outNumLaps = 3; outWeapons = true; isHost = true;
                raceMembers = {"NightRider", "zoom_zoom", "Speedy-Gonzales", "Banshee", "Q-Ball", "Turbo-Tess", "Lil_Hover"};
                statusText = "Race created - waiting for players";
            }
            else {
                RaceServerGameInfo a; a.mRaceId = 1; a.mName = "Steeplechase"; a.mTrack = "Steeplechase";
                a.mNumLaps = 3; a.mNumPlayers = 2; a.mWeapons = true;
                RaceServerGameInfo b; b.mRaceId = 2; b.mName = "Tidal Causeway"; b.mTrack = "Tidal Causeway";
                b.mNumLaps = 5; b.mNumPlayers = 1; b.mWeapons = false;
                games = {a, b};
                selected = 0;
            }
        }

        // Disabling the renderer's logical size for the duration of this ImGui
        // frame (rather than forcing ImGui to draw at the fixed kWidth x
        // kHeight logical size main.cpp's legacy framebuffer needs, and then
        // reverse-mapping mouse coordinates back through that letterbox) is
        // the fix that actually holds up: with logical size disabled, ImGui
        // draws 1:1 against the real window and every mouse coordinate SDL
        // reports -- real events and ImGui_ImplSDL2_NewFrame()'s own
        // "global mouse state" fallback alike -- is already in that same
        // space, so no coordinate translation is needed anywhere, in any
        // frame, regardless of window size or aspect ratio. Three earlier
        // attempts (v0.1.73 through v0.1.76) instead tried to keep ImGui
        // drawing at the fixed logical size and correct for the mismatch
        // after the fact; each shipped looking right and wasn't. Restored
        // to kWidth x kHeight below, before this loop's *next* iteration's
        // event polling can reach a raw-framebuffer screen like ConfirmQuit
        // that still needs it (see HoverNetImGuiLogicalMouseSmoke).
        // PollMessage closes the client when recv() reports that the server has
        // gone away. Record that transition once so a useful reconnect failure
        // reason is not overwritten on every subsequent frame.
        if (!client.IsConnected() && !connectionLossReported) {
            statusText = "Connection to RaceServer lost";
            connectionLossReported = true;
        }

        SDL_RenderSetLogicalSize(graphics.GetRenderer(), 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Lobby", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        DrawHoverNetScreenBackdrop(mainViewport);
        // The status line is repeated in the actions panel, so drop it from the heading on narrow screens.
        HoverNetScreenHeading("ONLINE MULTIPLAYER", "INTERNET MEETING ROOM",
                              ImGui::GetMainViewport()->WorkSize.x < 640.0f ? "" : statusText.c_str());
        ImGui::Spacing();

        if (!client.IsConnected()) {
            const float panelWidth = std::min(520.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 std::max(0.0f, (ImGui::GetContentRegionAvail().x - panelWidth) * 0.5f));
            ImGui::BeginChild("ConnectionLost", ImVec2(panelWidth, 275), true);
            HoverNetSectionHeading("CONNECTION LOST");
            ImGui::TextWrapped("The RaceServer connection closed. Race and player details shown before the disconnect may no longer be valid.");
            if (!statusText.empty()) {
                ImGui::Spacing();
                AccentNote(statusText);
            }
            ImGui::Spacing();
            if (HoverNetButton("Reconnect", ImVec2(-1, 42))) {
                reconnectToServer();
            }
            if (HoverNetButton("Open Settings", ImVec2(-1, 42))) {
                requestOpenRecoverySettings = true;
            }
            if (HoverNetButton("Back to main menu", ImVec2(-1, 42))) {
                running = false;
            }
            ImGui::EndChild();
        }
        else if (phase == LobbyPhase::eEnteringName) {
            const float cardWidth = std::min(560.0f, ImGui::GetContentRegionAvail().x);
            const float cardHeight = 230.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - cardWidth) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                std::max(20.0f, (ImGui::GetContentRegionAvail().y - cardHeight) * 0.36f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetSurface);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
            ImGui::BeginChild("ChooseRacerName", ImVec2(cardWidth, cardHeight), true);
            HoverNetSectionHeading("WELCOME, RACER");
            ImGui::Spacing();
            ImGui::TextDisabled("Choose the name other drivers will see in the lobby.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1.0f);
            if (focusUsernameInput) {
                SDL_StartTextInput();
                ImGui::SetKeyboardFocusHere();
                focusUsernameInput = false;
            }
            const bool submitted = ImGui::InputText("##username", usernameBuf, sizeof(usernameBuf),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            const bool clicked = HoverNetButton("ENTER THE LOBBY", ImVec2(-FLT_MIN, 46.0f));
            if ((submitted || clicked) && usernameBuf[0] != '\0') {
                username = usernameBuf;
                SaveUsername(username);
                client.SetPlayerName(username);
                client.ListLobbyUsers();
                phase = LobbyPhase::eBrowsing;
                statusText = "Connected to the shared HoverNet lobby.";
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else {
            // Panel grid (after the Windows client's IDD_INTERNET_MEETING dialog): race
            // list | race details | actions on top, then the lobby roster and chat
            // below. While you are in a race the race list is hidden and its room goes
            // to the details, so the player list never needs to scroll.
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const bool inWaitingRoomEarly = (phase == LobbyPhase::eWaitingRoom);
            const float availWidth = ImGui::GetContentRegionAvail().x;
            // Small screens cannot hold the three-column grid: show one panel group at a time behind a
            // tab bar. Narrow (portrait) screens also stack the race list, details and actions.
            const bool tabbed = availWidth < 640.0f || ImGui::GetContentRegionAvail().y < 560.0f;
            const bool stacked = availWidth < 640.0f;
            if (tabbed) {
                const char* tabNames[3] = {inWaitingRoomEarly ? "RACE" : "GAMES", "CHAT", "USERS"};
                const int tabCount = inWaitingRoomEarly ? 2 : 3;
                if (lobbyTab >= tabCount) lobbyTab = 0;
                const float tabWidth = (availWidth - spacing * (tabCount - 1)) / tabCount;
                for (int tab = 0; tab < tabCount; ++tab) {
                    if (tab > 0) ImGui::SameLine();
                    const bool active = lobbyTab == tab;
                    const bool clicked = active ? HoverNetButton(tabNames[tab], ImVec2(tabWidth, 40.0f))
                                                : HoverNetQuietButton(tabNames[tab], ImVec2(tabWidth, 40.0f));
                    if (clicked) lobbyTab = tab;
                }
            }
            const float availHeight = ImGui::GetContentRegionAvail().y;
            // The top row needs about 250 px to show a race and its players without
            // scrolling; the chat keeps at least ~260 px (around ten lines) and is
            // scrollable beyond that. Anything in between is split evenly.
            // Browsing needs less (no player list); a waiting room has to fit all eight
            // players beside the race info without scrolling.
            const float topHeight = tabbed ? availHeight
                : inWaitingRoomEarly
                ? std::max(305.0f, std::min(availHeight * 0.44f, availHeight - 250.0f))
                : std::max(298.0f, std::min(availHeight * 0.43f, availHeight - 260.0f));
            // Portrait stack: each panel gets a share of the height and the full width.
            const float stackGap = ImGui::GetStyle().ItemSpacing.y;
            const float listHeight = stacked ? (inWaitingRoomEarly ? 0.0f : std::max(96.0f, availHeight * 0.24f))
                                             : topHeight;
            const float detailsHeight = stacked ? std::max(150.0f, availHeight * (inWaitingRoomEarly ? 0.60f : 0.42f))
                                                : topHeight;
            const float actionsHeight = stacked
                ? std::max(120.0f, availHeight - listHeight - detailsHeight - stackGap * (inWaitingRoomEarly ? 1.0f : 2.0f))
                : topHeight;
            const float actionsWidth = std::max(176.0f, availWidth * 0.17f);
            const float leftColWidth = availWidth * 0.29f;
            const bool inWaitingRoom = (phase == LobbyPhase::eWaitingRoom);
            const float detailsColWidth = inWaitingRoom
                ? availWidth - actionsWidth - spacing
                : availWidth - leftColWidth - actionsWidth - 2.0f * spacing;
            constexpr int kMaxPlayersPerRace = 8; // the RaceServer's per-race limit

            const RaceServerGameInfo* selectedGame =
                (phase == LobbyPhase::eBrowsing && !games.empty() && selected >= 0 &&
                 selected < static_cast<int>(games.size()))
                    ? &games[static_cast<std::size_t>(selected)]
                    : nullptr;

            if (!tabbed || lobbyTab == 0) {
            if (!inWaitingRoom) {
                ImGui::BeginChild("GameListPanel", ImVec2(stacked ? 0.0f : leftColWidth, listHeight), true);
                HoverNetSectionHeading("Game list");
                if (ImGui::BeginListBox("##races", ImVec2(-FLT_MIN, -FLT_MIN))) {
                    if (games.empty()) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("No open races - host one to get started");
                        ImGui::PopTextWrapPos();
                    }
                    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
                        const RaceServerGameInfo& game = games[static_cast<std::size_t>(i)];
                        char label[256];
                        std::snprintf(label, sizeof(label), "%s%s", game.mName.c_str(),
                                      game.mStarted ? "  (racing)" : "");
                        const bool isSelected = (i == selected);
                        if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            selected = i;
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                joinSelected();
                            }
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
            }

            ImGui::BeginChild("GameDetailsPanel", ImVec2(stacked ? 0.0f : detailsColWidth, detailsHeight), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            HoverNetSectionHeading(inWaitingRoom ? "Your race" : "Game details");
            // One consistent "label  value" row style for both modes (labels dimmed).
            const float infoLabelWidth = ImGui::CalcTextSize("Weapons:").x + 16.0f;
            auto infoRow = [&](const char* label, const std::string& value) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.69f, 1.0f));
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
                ImGui::SameLine(infoLabelWidth);
                ImGui::TextWrapped("%s", value.c_str());
            };
            if (inWaitingRoom) {
                // Race info on the left, the whole player list on the right at full
                // height, so all eight players are visible without scrolling.
                const float infoWidth = std::max(250.0f, ImGui::GetContentRegionAvail().x * 0.46f);
                // Narrow screens: race info on top, the player list underneath at full width.
                ImGui::BeginChild("RaceInfoColumn",
                                  stacked ? ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.44f) : ImVec2(infoWidth, 0),
                                  false, ImGuiWindowFlags_NoScrollbar);
                const int waitingTrackIndex = FindHostableTrack(outTrackName);
                const TrackBriefing waitingBriefing = BriefingFor(waitingTrackIndex);
                infoRow("Track:", outTrackName.empty() ? "-" : outTrackName);
                infoRow("Laps:", waitingBriefing.mFreePlay ? "none (free play)" : std::to_string(outNumLaps));
                infoRow("Weapons:", outWeapons ? "On" : "Off");
                if (!waitingBriefing.mCharacter.empty()) {
                    infoRow("Style:", waitingBriefing.mCharacter + " - " + waitingBriefing.mDifficulty);
                }
                ImGui::Spacing();
                if (waitingTrackIndex >= 0) {
                    TrackPreview& preview = lobbyTrackPreviews[waitingTrackIndex];
                    if (!preview.mLoaded) {
                        preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(waitingTrackIndex).c_str());
                    }
                    ImGui::BeginChild("SelectedRaceMap", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                    DrawTrackPreview(preview, 80.0f, false);
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
                ImGui::BeginChild("PlayersColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::Text("Players (%d / %d)", 1 + static_cast<int>(raceMembers.size()), kMaxPlayersPerRace);
                ImGui::BeginChild("PlayersListBox", ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y), true);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f)); // compact rows
                ImGui::BulletText("%s (you)%s", username.c_str(), isHost ? " - host" : "");
                for (const std::string& member : raceMembers) {
                    ImGui::BulletText("%s", member.c_str());
                }
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::EndChild();
            }
            else {
                const bool haveSelection = selectedGame != nullptr;
                const std::string trackName = haveSelection ? selectedGame->mTrack : std::string();
                const int trackIndex = FindHostableTrack(trackName);
                const TrackBriefing briefing = BriefingFor(trackIndex);
                const float mapWidth = std::min(190.0f, ImGui::GetContentRegionAvail().x * 0.36f);
                const float infoHeight = 178.0f;
                ImGui::BeginChild("SelectedRaceMap", ImVec2(mapWidth, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
                if (trackIndex >= 0) {
                    TrackPreview& preview = lobbyTrackPreviews[trackIndex];
                    if (!preview.mLoaded) {
                        preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(trackIndex).c_str());
                    }
                    DrawTrackPreview(preview, 96.0f, false);
                }
                else {
                    DrawTrackPreview(TrackPreview(), 96.0f, false);
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("SelectedRaceInfo", ImVec2(0, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f)); // compact rows
                if (haveSelection) {
                    infoRow("Track:", trackName);
                    infoRow("Laps:", briefing.mFreePlay ? "none (free play)" : std::to_string(selectedGame->mNumLaps));
                    infoRow("Weapons:", selectedGame->mWeapons ? "On" : "Off");
                    infoRow("Players:", std::to_string(selectedGame->mNumPlayers) + " / " + std::to_string(kMaxPlayersPerRace));
                    if (trackIndex >= 0) {
                        infoRow("Style:", briefing.mCharacter + " - " + briefing.mDifficulty);
                    }
                    infoRow("Status:", selectedGame->mStarted ? "Race in progress" : "Waiting for players");
                }
                else {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("Select a race in the list to see its track, laps, weapons and players, "
                                        "or host your own.");
                    ImGui::PopTextWrapPos();
                }
                ImGui::PopStyleVar();
                ImGui::EndChild();
                if (haveSelection && trackIndex >= 0 && !briefing.mDescription.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.69f, 1.0f));
                    ImGui::TextWrapped("%s", briefing.mDescription.c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndChild();

            if (!stacked) ImGui::SameLine();
            ImGui::BeginChild("ActionsPanel", ImVec2(0, actionsHeight), true);
            if (inWaitingRoom) {
                if (isHost) {
                    // The one primary action: the only filled red button on the screen.
                    if (HoverNetButton("Start Race", ImVec2(-FLT_MIN, 40))) {
                        client.StartRace();
                        statusText = "Starting race...";
                    }
                }
                else {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("Waiting for the host to start the race...");
                    ImGui::PopTextWrapPos();
                }
                ImGui::Spacing();
                if (HoverNetQuietButton("Leave Race", ImVec2(-FLT_MIN, 0))) {
                    cancelWaitingRoom();
                }
            }
            else {
                const int selectedTrackIndex = selectedGame == nullptr ? -1 : FindHostableTrack(selectedGame->mTrack);
                const TrackEntry* selectedTrack = Tracks().Get(selectedTrackIndex);
                const bool trackKnown = selectedGame == nullptr || selectedTrack != nullptr;
                const bool trackNeedsDownload = selectedTrack != nullptr && !selectedTrack->mInstalled;
                const bool trackDownloadable = trackNeedsDownload && selectedTrack->mHasDownload;
                const bool canJoin = selectedGame != nullptr && !selectedGame->mStarted && trackKnown &&
                                     (!trackNeedsDownload || trackDownloadable);
                ImGui::BeginDisabled(!canJoin);
                if (HoverNetButton(trackNeedsDownload ? "Download & Join" : "Join Game", ImVec2(-FLT_MIN, 40))) {
                    joinSelected();
                }
                ImGui::EndDisabled();
                ImGui::PushTextWrapPos(0.0f);
                if (!trackKnown) {
                    ImGui::TextWrapped("This race uses a track this version of HoverNet does not have. "
                                       "Update the game to join it.");
                }
                else if (trackNeedsDownload) {
                    ImGui::TextWrapped("You don't have this community track yet (%s). It downloads when you join.",
                                       FormatMegabytes(selectedTrack->mDownloadBytes).c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (HoverNetButton("HOST A RACE", ImVec2(-FLT_MIN, 44.0f))) {
                    requestOpenHostPopup = true;
                }
            }
            // What just happened ("Race created", "Joined - waiting for host", errors...)
            // lives here, in the column's spare space, instead of a banner of its own.
            if (!statusText.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
                ImGui::TextWrapped("%s", statusText.c_str());
                ImGui::PopStyleColor();
            }
            // Leaving the lobby sits apart at the bottom so it is not hit by mistake.
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
                actionsHeight - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y));
            if (HoverNetQuietButton("Back to Menu", ImVec2(-FLT_MIN, 0))) {
                running = false;
            }
            ImGui::EndChild();
            }

            // ---- bottom row: who is here, and the chat ----
            if (!tabbed || lobbyTab == 2) {
            ImGui::BeginChild("UsersListPanel", ImVec2(tabbed ? 0.0f : leftColWidth, 0), true);
            // You are always in the list, then everyone else who is in the lobby (the
            // server announces only the *other* users, so you have to add yourself).
            std::vector<std::string> otherUsers;
            for (const RaceServerPeer& user : lobbyUsers) {
                if (user.mName != username) otherUsers.push_back(user.mName);
            }
            std::sort(otherUsers.begin(), otherUsers.end(), [](const std::string& a, const std::string& b) {
                std::string la = a, lb = b;
                for (char& c : la) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (char& c : lb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return la != lb ? la < lb : a < b;
            });
            {
                char heading[48];
                std::snprintf(heading, sizeof(heading), "Users list (%d)", 1 + static_cast<int>(otherUsers.size()));
                HoverNetSectionHeading(heading);
            }
            ImGui::BeginChild("UsersListBox", ImVec2(-FLT_MIN, -FLT_MIN), true);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f)); // compact rows
            ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
            ImGui::Text("%s (you)", username.c_str());
            ImGui::PopStyleColor();
            for (const std::string& name : otherUsers) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::EndChild();
            }

            if (!tabbed) ImGui::SameLine();
            if (!tabbed || lobbyTab == 1) {
            ImGui::BeginChild("ChatPanel", ImVec2(0, 0), true);
            HoverNetSectionHeading("Chat");
            const float chatInputRow = ImGui::GetFrameHeightWithSpacing();
            ImGui::BeginChild("ChatLog", ImVec2(0, ImGui::GetContentRegionAvail().y - chatInputRow), true);
            {
                // Scroll positions read here are last frame's, i.e. *before* any line
                // added this frame: if the log was at the bottom (or a message was just
                // sent) it stays pinned to the newest message; if the player scrolled up
                // to read history it is left alone.
                const bool pinned = chatForcePin || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
                for (const std::string& chatLine : chatLog) {
                    const bool systemLine = chatLine.size() > 2 && chatLine[0] == '*' && chatLine[1] == ' ';
                    if (systemLine) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.69f, 1.0f));
                    ImGui::TextWrapped("%s", chatLine.c_str());
                    if (systemLine) ImGui::PopStyleColor();
                }
                if (pinned) {
                    ImGui::SetScrollHereY(1.0f);
                }
                chatForcePin = false;
                chatWasPinned = pinned;
            }
            ImGui::EndChild();
            if (focusChatInput) {
                ImGui::SetKeyboardFocusHere();
                focusChatInput = false;
            }
            const float jumpWidth = chatWasPinned ? 0.0f : 132.0f;
            ImGui::SetNextItemWidth(-(90.0f + jumpWidth + (chatWasPinned ? 0.0f : spacing)));
            bool sendChat = ImGui::InputText("##chat", chatBuf, sizeof(chatBuf),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
            if (!chatWasPinned) {
                ImGui::SameLine();
                if (HoverNetQuietButton("Jump to latest", ImVec2(jumpWidth, 0))) {
                    chatForcePin = true;
                }
            }
            ImGui::SameLine();
            sendChat = HoverNetButton("Send", ImVec2(-FLT_MIN, 0)) || sendChat;
            if (sendChat && chatBuf[0] != '\0') {
                const std::string outgoing(chatBuf);
                if (client.SendMessage(eRSMsgChatMessage, outgoing.data(), outgoing.size())) {
                    pushChat(username + ": " + outgoing);
                    chatForcePin = true;
                }
                chatBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::EndChild();
            }
        }

        // OpenPopup/BeginPopupModal must be called at the same ID-stack depth --
        // calling OpenPopup from inside one of the child panels above registered a
        // popup ID scoped to that child, which BeginPopupModal here (at the outer
        // window's scope) could never match, so the dialog never appeared. Doing
        // the OpenPopup call here, at the same scope as BeginPopupModal, fixes it.
        if (requestOpenHostPopup) {
            hostPopupOpen = true;
            ImGui::OpenPopup("HostRace");
            requestOpenHostPopup = false;
        }
        const ImGuiViewport* popupViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(
            ImVec2(std::min(860.0f, popupViewport->WorkSize.x - 40.0f),
                   std::min(560.0f, popupViewport->WorkSize.y - 40.0f)),
            ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("HostRace", &hostPopupOpen)) {
            HoverNetSectionHeading("Host race");
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float selectorHeight = ImGui::GetContentRegionAvail().y -
                ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
            const bool sideBySide = contentWidth >= 610.0f;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float controlsWidth = sideBySide ? contentWidth * 0.52f : contentWidth;

            ImGui::BeginChild("HostRaceOptions",
                              ImVec2(controlsWidth, sideBySide ? selectorHeight : 315.0f), false);
            ImGui::TextUnformatted("Track");
            ImGui::SetNextItemWidth(-1.0f);
            TrackSelector("##HostTrack", 0, hostPrefs.mTrackIndex);
            const TrackBriefing guide = BriefingFor(hostPrefs.mTrackIndex);
            const TrackEntry* hostTrack = Tracks().Get(hostPrefs.mTrackIndex);
            const bool hostNeedsDownload = hostTrack != nullptr && !hostTrack->mInstalled;
            ImGui::Spacing();
            HoverNetSectionHeading("Course briefing");
            ImGui::Text("Style: %s", guide.mCharacter.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("  Difficulty: %s", guide.mDifficulty.c_str());
            ImGui::TextWrapped("%s", guide.mDescription.c_str());
            if (hostNeedsDownload) {
                AccentNote("Not on this computer yet - it downloads (" + FormatMegabytes(hostTrack->mDownloadBytes) +
                           ") when you create the race.");
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(guide.mFreePlay);
            ImGui::TextUnformatted("Laps");
            ImGui::SameLine();
            if (guide.mFreePlay) {
                ImGui::TextDisabled("Not used on a free-play track");
            }
            else {
                ImGui::TextDisabled("Suggested: %d", guide.mSuggestedLaps);
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("##HostLaps", &hostPrefs.mLaps, 1, 20);
            ImGui::EndDisabled();
            ImGui::Checkbox("Weapons enabled", &hostPrefs.mWeapons);
            ImGui::EndChild();

            TrackPreview& preview = lobbyTrackPreviews[hostPrefs.mTrackIndex];
            if (!preview.mLoaded) {
                preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(hostPrefs.mTrackIndex).c_str());
            }
            if (sideBySide) {
                ImGui::SameLine(0.0f, gap);
                ImGui::BeginChild("HostTrackPreview", ImVec2(0, selectorHeight), false);
                DrawTrackPreview(preview);
                ImGui::EndChild();
            }
            else {
                ImGui::Spacing();
                ImGui::BeginChild("HostTrackPreview", ImVec2(0, 190.0f), false);
                DrawTrackPreview(preview);
                ImGui::EndChild();
            }

            if (HoverNetButton(hostNeedsDownload ? "Download & Create" : "Create Race", ImVec2(160, 36)) &&
                !download.Active()) {
                if (hostNeedsDownload) {
                    download.Begin({*hostTrack}, 1);
                    hostAfterDownload = true;
                }
                else {
                    hostRaceNow();
                    hostPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (HoverNetButton("Cancel", ImVec2(120, 36)) && !download.Active()) {
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            {
                const DownloadUi::State downloadState = download.Draw(1);
                if (downloadState == DownloadUi::State::eDone && hostAfterDownload) {
                    hostAfterDownload = false;
                    hostRaceNow();
                    hostPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }
                else if (downloadState == DownloadUi::State::eFailed) {
                    hostAfterDownload = false;
                }
            }
            ImGui::EndPopup();
        }

        {
            const DownloadUi::State downloadState = download.Draw(0);
            if (downloadState == DownloadUi::State::eDone && joinAfterDownload) {
                joinAfterDownload = false;
                joinSelected();
            }
            else if (downloadState == DownloadUi::State::eFailed) {
                joinAfterDownload = false;
            }
        }

        ImGui::End();
        ImGui::Render();

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        if (requestOpenRecoverySettings) {
            requestOpenRecoverySettings = false;
            const SettingsResult settings = RunSettingsScreen(
                graphics, buffer, viewport, font, username, host, port,
                LoadVolume(), LoadMute(), LoadFullscreen(), pFrameLimit);
            if (settings.confirmed) {
                username = settings.username;
                std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", username.c_str());
                host = settings.serverHost;
                port = settings.serverPort;
                SaveUsername(username); SaveServerUrl(host, port);
                SaveVolume(settings.volume); SaveMute(settings.muted);
                SaveFullscreen(settings.fullscreen);
                SaveWindowSize(WindowSize{settings.windowWidth, settings.windowHeight});
                SaveUiScale(settings.uiScale); SaveLargeHudText(settings.largeHudText);
                SaveReducedMotion(settings.reducedMotion); SaveHighContrast(settings.highContrast);
                reconnectToServer();
            }
        }
        HoverNetFrameDelay();
    }
    SDL_StopTextInput();

    for (TrackPreview& preview : lobbyTrackPreviews) {
        if (preview.mTexture != nullptr) SDL_DestroyTexture(preview.mTexture);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    return joined;
}

// Clicking the window's close (X) button opens the same shared ImGui visual
// language as the rest of the Phase 3 menus. It borrows an active context when
// invoked from another ImGui screen and owns one when invoked directly from a
// race. Returns true only after an explicit destructive confirmation.
bool ConfirmQuit(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                 const MR_Sprite& font, int pFrameLimit)
{
    (void)buffer;
    (void)viewport;
    (void)font;
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    bool confirmed = false;
    bool running = true;
    bool focusSafeChoice = true;
    int framesShown = 0;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                confirmed = true; // A second close request is deliberate.
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                running = false;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("Quit HoverNet", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 workSize = mainViewport->WorkSize;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 workEnd(mainViewport->WorkPos.x + workSize.x,
                             mainViewport->WorkPos.y + workSize.y);
        draw->AddRectFilledMultiColor(mainViewport->WorkPos, workEnd,
            IM_COL32(12, 12, 18, 255), IM_COL32(55, 10, 20, 255),
            IM_COL32(7, 7, 11, 255), IM_COL32(24, 7, 13, 255));

        const float panelWidth = std::min(520.0f, workSize.x - 48.0f);
        const float panelHeight = 310.0f;
        ImGui::SetCursorPos(ImVec2((workSize.x - panelWidth) * 0.5f,
                                   (workSize.y - panelHeight) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        ImGui::BeginChild("QuitConfirmationPanel", ImVec2(panelWidth, panelHeight), true);
        HoverNetSectionHeading("LEAVE THE RACE?");
        ImGui::Spacing();
        ImGui::TextWrapped("Quit HoverNet and return to your desktop?");
        ImGui::TextDisabled("Unsaved race progress will be lost.");
        ImGui::Spacing();
        ImGui::Spacing();

        if (focusSafeChoice) {
            ImGui::SetKeyboardFocusHere();
            focusSafeChoice = false;
        }
        if (HoverNetButton("KEEP PLAYING", ImVec2(-FLT_MIN, 54.0f))) {
            running = false;
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.08f, 0.12f, 1.0f));
        if (HoverNetButton("QUIT HOVERNET", ImVec2(-FLT_MIN, 48.0f))) {
            confirmed = true;
            running = false;
        }
        ImGui::PopStyleColor();
        ImGui::Spacing();
        HoverNetHint("Esc / B: keep playing");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return confirmed;
}

// pIsOnline only changes the middle option's label -- callers already know
// their own context (onlineClient.IsConnected()) and interpret eLeaveRace as
// "leave this race" when online or "set up a new local race" when offline,
// so the enum itself doesn't need a second value for the same button slot.
// eOnlineLobby is always available (whether currently racing online or off)
// as a shortcut to the online lobby -- callers do exactly what eLeaveRace
// already does when online (reset and rejoin the lobby).
PauseChoice RunPauseMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                         MR_3DViewPort& viewport, const MR_Sprite& font, bool pIsOnline,
                         int pFrameLimit, bool pOfferReplays, bool pCanRestart)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

#ifdef HOVERNET_WEB
    // The page's on-screen race controls (top-right buttons, touch pad) float above the canvas
    // and swallowed clicks on the menu buttons beneath them; hide them while paused.
    struct PausedFlag {
        int mSaved = gHoverNetInRace;
        PausedFlag() { gHoverNetInRace = 0; }
        ~PausedFlag() { gHoverNetInRace = mSaved; }
    } pausedFlag;
#endif

    struct PauseOption { const char* label; PauseChoice choice; };
    std::vector<PauseOption> options = {
        {"RESUME RACE", PauseChoice::eResume},
    };
    if (pCanRestart) options.push_back({"RESTART RACE", PauseChoice::eRestartRace});   // a local race: the same track, laps and weapons again
    options.push_back({pIsOnline ? "LEAVE RACE" : "NEW LOCAL RACE", PauseChoice::eLeaveRace});
    options.insert(options.end(), {
        {"ONLINE MULTIPLAYER", PauseChoice::eOnlineLobby},
    });
    if (pOfferReplays) options.push_back({"WATCH A REPLAY", PauseChoice::eWatchReplay});
    const int primaryCount = static_cast<int>(options.size());   // the race options above the "GAME & ACCESSIBILITY" heading
    if (pOfferReplays) options.push_back({"MAIN MENU", PauseChoice::eMainMenu});   // (not in the classic lobby, which has its own way out)
    options.push_back({"SETTINGS", PauseChoice::eSettings});
    options.push_back({"CONTROLS", PauseChoice::eControls});
#ifndef __EMSCRIPTEN__   // a browser tab has nothing to quit to; the tab is closed instead
    options.push_back({"QUIT HOVERNET", PauseChoice::eQuit});
#endif
    const int optionCount = static_cast<int>(options.size());
    PauseChoice choice = PauseChoice::eResume;
    bool focusFirstButton = true;
    bool running = true;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        if (framesShown == 3 && pOfferReplays && std::getenv("HOVERNET_TEST_PAUSE_CHOICE") != nullptr) {   // test hook (scripts/test-main-menu.sh, test-restart-race.sh)
            const std::string pick = std::getenv("HOVERNET_TEST_PAUSE_CHOICE");
            if (pick == "main") { choice = PauseChoice::eMainMenu; running = false; break; }
            if (pick == "restart" && pCanRestart) { choice = PauseChoice::eRestartRace; running = false; break; }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    choice = PauseChoice::eQuit;
                    running = false;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                      event.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
                running = false;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Pause Menu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 workPos = mainViewport->WorkPos;
        const ImVec2 workSize = mainViewport->WorkSize;
        const ImVec2 workEnd(workPos.x + workSize.x, workPos.y + workSize.y);
        ImDrawList* background = ImGui::GetWindowDrawList();
        background->AddRectFilledMultiColor(
            workPos, workEnd,
            IM_COL32(13, 13, 19, 255), IM_COL32(42, 10, 20, 255),
            IM_COL32(7, 7, 11, 255), IM_COL32(17, 7, 12, 255));

        const float horizonY = workPos.y + workSize.y * 0.60f;
        const ImVec2 vanishingPoint(workPos.x + workSize.x * 0.28f, horizonY);
        for (int line = -5; line <= 10; ++line) {
            const float bottomX = workPos.x + workSize.x * (static_cast<float>(line) / 7.0f);
            background->AddLine(vanishingPoint, ImVec2(bottomX, workEnd.y),
                                IM_COL32(227, 43, 66, 55), 1.5f);
        }
        const float gridRows[] = {0.10f, 0.24f, 0.43f, 0.68f, 0.94f};
        for (float row : gridRows) {
            const float y = horizonY + (workEnd.y - horizonY) * row;
            background->AddLine(ImVec2(workPos.x, y), ImVec2(workEnd.x, y),
                                IM_COL32(240, 53, 75, 48), 1.0f);
        }

        ImGui::SetCursorPos(ImVec2(54.0f, 64.0f));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
        ImGui::TextUnformatted("GAME PAUSED");
        if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::TextColored(kHoverNetCoral, "%s", pIsOnline ? "ONLINE RACE" : "LOCAL RACE");
        ImGui::Spacing();
        ImGui::TextDisabled("Take a breath. The track will wait.");
        ImGui::EndGroup();

        const float panelWidth = std::min(430.0f, workSize.x * 0.48f);
        const float panelHeight = std::min(590.0f, workSize.y - 72.0f);
        ImGui::SetCursorPos(ImVec2(workSize.x - panelWidth - 38.0f, 36.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.96f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 20.0f));
        ImGui::BeginChild("PauseMenuPanel", ImVec2(panelWidth, panelHeight), true);
        HoverNetSectionHeading("RACE OPTIONS");
        ImGui::Spacing();

        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        for (int index = 0; index < optionCount; ++index) {
            const float height = index < primaryCount ? 54.0f : 42.0f;
            if (HoverNetButton(options[index].label, ImVec2(-FLT_MIN, height))) {
                choice = options[index].choice;
                running = false;
            }
            if (index == primaryCount - 1) {
                ImGui::Spacing();
                ImGui::TextDisabled("GAME & ACCESSIBILITY");
                ImGui::Separator();
            }
            ImGui::Spacing();
        }
        HoverNetHint("Esc / B / Start: resume");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return choice;
}


// What the player may do when a replay has played to its end.
enum class ReplayEndChoice { eStay, eWatchAgain, eAnotherReplay, eLeaderboards, eMainMenu, eQuit };

ReplayEndChoice RunReplayEndScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport, const MR_Sprite& font,
                                   const std::string& track, int players, int laps, long long durationMs, const std::vector<std::string>& finishers, int pFrameLimit)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }
#ifdef HOVERNET_WEB
    struct PausedFlag {
        int mSaved = gHoverNetInRace;
        PausedFlag() { gHoverNetInRace = 0; }
        ~PausedFlag() { gHoverNetInRace = mSaved; }
    } pausedFlag;
#endif
    ReplayEndChoice choice = ReplayEndChoice::eStay;
    bool focusFirst = true, running = true;
    int framesShown = 0;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { if (ConfirmQuit(graphics, buffer, viewport, font)) { choice = ReplayEndChoice::eQuit; running = false; } }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && (event.cbutton.button == SDL_CONTROLLER_BUTTON_B || event.cbutton.button == SDL_CONTROLLER_BUTTON_START)) running = false;
        }
        if (framesShown == 3) {
            // Test hook (scripts/test-replay-end.sh): a comma-separated list of choices (again, another, main, quit, stay), one for each time the screen is shown.
            static int shown = 0;
            ++shown;
            if (const char* script = std::getenv("HOVERNET_TEST_REPLAY_END")) {
                std::string list = script, word;
                std::vector<std::string> words;
                for (char c : list + ",") { if (c == ',') { words.push_back(word); word.clear(); } else word += c; }
                const std::string pick = static_cast<std::size_t>(shown) <= words.size() ? words[static_cast<std::size_t>(shown) - 1] : std::string("quit");
                std::printf("REPLAYEND shown %d: %s\n", shown, pick.c_str());
                std::fflush(stdout);
                choice = pick == "again" ? ReplayEndChoice::eWatchAgain : pick == "another" ? ReplayEndChoice::eAnotherReplay : pick == "leaderboards" ? ReplayEndChoice::eLeaderboards : pick == "main" ? ReplayEndChoice::eMainMenu : pick == "stay" ? ReplayEndChoice::eStay : ReplayEndChoice::eQuit;
                running = false;
                continue;
            }
        }
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Replay Finished", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);
        const ImVec2 workSize = mainViewport->WorkSize;
        ImGui::SetCursorPos(ImVec2(54.0f, 64.0f));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
        ImGui::TextUnformatted("REPLAY FINISHED");
        if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::TextColored(kHoverNetCoral, "%s", track.c_str());
        ImGui::TextDisabled("%d player%s, %d lap%s, %lld:%02lld", players, players == 1 ? "" : "s", laps, laps == 1 ? "" : "s", durationMs / 60000, (durationMs % 60000) / 1000);
        ImGui::Spacing();
        for (const std::string& line : finishers) ImGui::TextUnformatted(line.c_str());
        ImGui::EndGroup();

        const float panelWidth = std::min(430.0f, workSize.x * 0.48f);
        const float panelHeight = std::min(470.0f, workSize.y - 72.0f);
        ImGui::SetCursorPos(ImVec2(workSize.x - panelWidth - 38.0f, 36.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.96f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 20.0f));
        ImGui::BeginChild("ReplayEndPanel", ImVec2(panelWidth, panelHeight), true);
        HoverNetSectionHeading("WHAT NEXT?");
        ImGui::Spacing();
        struct Action { const char* label; ReplayEndChoice choice; };
        std::vector<Action> actions = {
            {"WATCH AGAIN", ReplayEndChoice::eWatchAgain},
            {"WATCH ANOTHER REPLAY", ReplayEndChoice::eAnotherReplay},
        };
#if defined(HOVERNET_AUTH)
        actions.push_back({"LEADERBOARDS", ReplayEndChoice::eLeaderboards});
#endif
        actions.push_back({"MAIN MENU", ReplayEndChoice::eMainMenu});
#ifndef __EMSCRIPTEN__
        actions.push_back({"QUIT HOVERNET", ReplayEndChoice::eQuit});
#endif
        for (std::size_t i = 0; i < actions.size(); ++i) {
            if (focusFirst) { ImGui::SetKeyboardFocusHere(); focusFirst = false; }
            if (HoverNetButton(actions[i].label, ImVec2(-FLT_MIN, i == 0 ? 54.0f : 44.0f))) { choice = actions[i].choice; running = false; }
            ImGui::Spacing();
        }
        HoverNetHint("Esc / B: stay and look around (Left or Home rewinds)");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return choice;
}

void FormatRaceTime(MR_SimulationTime pTime, char* pBuffer, std::size_t pBufferSize)
{
    const MR_SimulationTime safeTime = std::max<MR_SimulationTime>(0, pTime);
    std::snprintf(pBuffer, pBufferSize, "%d:%02d.%02d",
                  safeTime / 60000, (safeTime % 60000) / 1000, (safeTime % 1000) / 10);
}

PostRaceChoice RunPostRaceScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                                 MR_3DViewPort& viewport, const MR_Sprite& font,
                                 MR_SimulationTime pFinishTime, MR_SimulationTime pBestLap,
                                 int pRank, int pPlayerCount, int pFrameLimit,
                                 const PersonalBests::Outcome* pBests, bool pCanRestart)
{
    if (std::getenv("HOVERNET_TEST_FINISH") != nullptr) { std::printf("FINISHTEST results screen opened\n"); std::fflush(stdout); }
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    char finishTime[32];
    char bestLap[32];
    FormatRaceTime(pFinishTime, finishTime, sizeof(finishTime));
    FormatRaceTime(pBestLap, bestLap, sizeof(bestLap));
    PostRaceChoice choice = PostRaceChoice::eContinue;
    bool focusFirstButton = true;
    bool running = true;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        if (framesShown == 2 && std::getenv("HOVERNET_TEST_RESULTS_CHOICE") != nullptr) {   // test hook (scripts/test-main-menu.sh, test-restart-race.sh)
            const std::string pick = std::getenv("HOVERNET_TEST_RESULTS_CHOICE");
            if (pick == "main") { choice = PostRaceChoice::eMainMenu; running = false; break; }
            if (pick == "restart" && pCanRestart) { choice = PostRaceChoice::eRestartRace; running = false; break; }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    choice = PostRaceChoice::eQuit;
                    running = false;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                running = false;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Race Results", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 workPos = mainViewport->WorkPos;
        const ImVec2 workSize = mainViewport->WorkSize;
        const ImVec2 workEnd(workPos.x + workSize.x, workPos.y + workSize.y);
        ImDrawList* background = ImGui::GetWindowDrawList();
        background->AddRectFilledMultiColor(
            workPos, workEnd,
            IM_COL32(18, 12, 20, 255), IM_COL32(69, 13, 28, 255),
            IM_COL32(8, 8, 13, 255), IM_COL32(23, 8, 14, 255));
        const float stripeWidth = std::max(32.0f, workSize.x / 18.0f);
        for (float x = workPos.x - workSize.y; x < workEnd.x; x += stripeWidth * 2.0f) {
            background->AddQuadFilled(
                ImVec2(x, workEnd.y), ImVec2(x + stripeWidth, workEnd.y),
                ImVec2(x + workSize.y * 0.36f + stripeWidth, workPos.y),
                ImVec2(x + workSize.y * 0.36f, workPos.y), IM_COL32(255, 255, 255, 10));
        }

        const float cardWidth = std::min(680.0f, workSize.x - 48.0f);
        const float cardHeight = std::min((pBests != nullptr && pBests->Any() ? 690.0f : 620.0f), workSize.y - 48.0f);
        ImGui::SetCursorPos(ImVec2((workSize.x - cardWidth) * 0.5f,
                                   (workSize.y - cardHeight) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.065f, 0.085f, 0.97f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        ImGui::BeginChild("ResultsCard", ImVec2(cardWidth, cardHeight), true);

        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(2.0f);
        const char* heading = pRank == 1 ? "VICTORY!" : "RACE COMPLETE";
        const float headingWidth = ImGui::CalcTextSize(heading).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - headingWidth) * 0.5f);
        ImGui::TextUnformatted(heading);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        const char* subtitle = pRank == 1 ? "You owned the track." : "Finish line crossed.";
        const float subtitleWidth = ImGui::CalcTextSize(subtitle).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - subtitleWidth) * 0.5f);
        ImGui::TextColored(kHoverNetCoral, "%s", subtitle);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTable("RaceResultStats", 3,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            const char* labels[] = {"POSITION", "FINISH TIME", "BEST LAP"};
            char position[32];
            std::snprintf(position, sizeof(position), "%d / %d", pRank, std::max(1, pPlayerCount));
            const char* values[] = {position, finishTime, bestLap};
            for (int column = 0; column < 3; ++column) {
                ImGui::TableNextColumn();
                const float labelWidth = ImGui::CalcTextSize(labels[column]).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    std::max(0.0f, (ImGui::GetColumnWidth() - labelWidth) * 0.5f));
                ImGui::TextDisabled("%s", labels[column]);
                ImGui::SetWindowFontScale(1.35f);
                const float valueWidth = ImGui::CalcTextSize(values[column]).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    std::max(0.0f, (ImGui::GetColumnWidth() - valueWidth) * 0.5f));
                ImGui::TextColored(kHoverNetWhite, "%s", values[column]);
                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::EndTable();
        }

        if (pBests != nullptr && pBests->Any()) {
            // The player's own records on this track (kept on this machine only).
            char text[160], previous[32];
            auto centred = [&](const char* line, bool highlight) {
                const float width = ImGui::CalcTextSize(line).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
                if (highlight) ImGui::TextColored(kHoverNetCoral, "%s", line); else ImGui::TextDisabled("%s", line);
            };
            ImGui::Spacing();
            if (pBests->newRaceBest) {
                if (pBests->hadRaceBest) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->previousRace), previous, sizeof(previous)); std::snprintf(text, sizeof(text), "NEW PERSONAL BEST!  (was %s)", previous); }
                else std::snprintf(text, sizeof(text), "NEW PERSONAL BEST!  (your first finish on this track)");
                centred(text, true);
            }
            else if (pBests->raceBest > 0) {
                FormatRaceTime(static_cast<MR_SimulationTime>(pBests->raceBest), previous, sizeof(previous));
                std::snprintf(text, sizeof(text), "Your best finish here: %s", previous);
                centred(text, false);
            }
            if (pBests->newLapBest) {
                if (pBests->hadLapBest) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->previousLap), previous, sizeof(previous)); std::snprintf(text, sizeof(text), "New best lap!  (was %s)", previous); }
                else std::snprintf(text, sizeof(text), "New best lap!");
                centred(text, true);
            }
            else if (pBests->lapBest > 0) {
                FormatRaceTime(static_cast<MR_SimulationTime>(pBests->lapBest), previous, sizeof(previous));
                std::snprintf(text, sizeof(text), "Your best lap here: %s", previous);
                centred(text, false);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        struct PostRaceAction { const char* label; PostRaceChoice choice; };
        std::vector<PostRaceAction> actions = {{"KEEP DRIVING", PostRaceChoice::eContinue}};
        if (pCanRestart) actions.push_back({"RESTART RACE", PostRaceChoice::eRestartRace});   // the same track, laps and weapons again
        actions.insert(actions.end(), {{"NEW LOCAL RACE", PostRaceChoice::eNewLocalRace},
                                       {"ONLINE MULTIPLAYER", PostRaceChoice::eOnlineLobby},
                                       {"MAIN MENU", PostRaceChoice::eMainMenu}});
#ifndef __EMSCRIPTEN__
        actions.push_back({"QUIT HOVERNET", PostRaceChoice::eQuit});
#endif
        for (std::size_t index = 0; index < actions.size(); ++index) {
            if (HoverNetButton(actions[index].label, ImVec2(-FLT_MIN, index == 0 ? 54.0f : 44.0f))) {
                choice = actions[index].choice;
                running = false;
            }
            ImGui::Spacing();
        }
        HoverNetHint("Esc / B: keep driving");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return choice;
}

struct LocalRaceSetup
{
    bool confirmed = false;
    std::string trackName = "ClassicH";
    int laps = 5;
    bool weapons = false;
};

// Lets the player pick track/laps/weapons for an offline race instead of
// silently reusing whatever was already loaded (always ClassicH, 1 lap,
// weapons on -- see main()'s startup load) -- reachable from the main menu's
// "Local Play" and the in-race pause menu's "New Local Race". Shares the same
// persisted defaults as the online host dialog (~/.hovernet_host_prefs); under
// an automated/headless run (pFrameLimit bounded, no real input arriving),
// falls through to confirmed with whatever prefs were already loaded, the
// same way RunMainMenu defaults to Local Play, so bounded test runs still
// terminate deterministically instead of bouncing between this and the main
// menu forever.
LocalRaceSetup RunLocalRaceSetup(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                                 const MR_Sprite& font, int pFrameLimit)
{
    // Renders through Dear ImGui against the SDL_Renderer directly, exactly
    // like RunLobbyScreen -- buffer/viewport/font are unused now, kept only so
    // callers (which still pass them for the pre-ImGui menus) don't change.
    (void)buffer;
    (void)viewport;
    (void)font;

    HostPrefs prefs = LoadLocalRacePrefs();
    PersonalBests personalBests;   // the player's own records, shown under the laps while choosing a track
    personalBests.Load(ConfigDirPath() + "/personal_bests");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadHoverNetMenuFonts(io);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    bool running = true;
    bool cancelled = false;
    int framesShown = 0;
    std::vector<TrackPreview> trackPreviews(static_cast<std::size_t>(Tracks().Count()));
    DownloadUi download;
    bool startAfterDownload = false;
    // Test hook: behave as if Start Race was pressed on the first frame.
    bool autoStartForTest = std::getenv("HOVERNET_TEST_AUTO_START") != nullptr;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                cancelled = true;
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    g_QuitConfirmed = true;
                }
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                cancelled = true;
                running = false;
            }
        }

        // Disabling the renderer's logical size for the duration of this ImGui
        // frame (rather than forcing ImGui to draw at the fixed kWidth x
        // kHeight logical size main.cpp's legacy framebuffer needs, and then
        // reverse-mapping mouse coordinates back through that letterbox) is
        // the fix that actually holds up: with logical size disabled, ImGui
        // draws 1:1 against the real window and every mouse coordinate SDL
        // reports -- real events and ImGui_ImplSDL2_NewFrame()'s own
        // "global mouse state" fallback alike -- is already in that same
        // space, so no coordinate translation is needed anywhere, in any
        // frame, regardless of window size or aspect ratio. Three earlier
        // attempts (v0.1.73 through v0.1.76) instead tried to keep ImGui
        // drawing at the fixed logical size and correct for the mismatch
        // after the fact; each shipped looking right and wasn't. Restored
        // to kWidth x kHeight below, before this loop's *next* iteration's
        // event polling can reach a raw-framebuffer screen like ConfirmQuit
        // that still needs it (see HoverNetImGuiLogicalMouseSmoke).
        SDL_RenderSetLogicalSize(graphics.GetRenderer(), 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Local Race", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        DrawHoverNetScreenBackdrop(mainViewport);

        // Phones (narrow or short windows): slim header bar and no wasted vertical space.
        const bool phoneLayout = mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phoneLayout ? ImVec2(14.0f, 8.0f) : ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, phoneLayout ? 40.0f : 76.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(phoneLayout ? 1.1f : 1.35f);
        ImGui::TextUnformatted("LOCAL RACE SETUP");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (!phoneLayout) ImGui::Spacing();

        const float panelWidth = std::min(900.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
        ImGui::BeginChild("LocalRaceSetupPanel", ImVec2(panelWidth, 0), true);

        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const bool sideBySide = contentWidth >= 610.0f;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float controlsWidth = sideBySide ? contentWidth * 0.52f : contentWidth;
        // Stacked (narrow) layout: size the options to their content so Cancel is never clipped.
        ImGui::BeginChild("RaceOptions", ImVec2(controlsWidth, sideBySide ? 0.0f : 0.0f),
                          sideBySide ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY,
                          sideBySide ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar);
        // Short landscape: keep Start Race and Cancel on screen by dropping the course-briefing
        // heading and description (the style and difficulty line stays).
        const bool tightHeight = phoneLayout && sideBySide;

        ImGui::PushItemWidth(-1.0f);
        ImGui::TextUnformatted("Track");
        TrackSelector("##Track", 1, prefs.mTrackIndex);
        ImGui::PopItemWidth();

        const TrackBriefing guide = BriefingFor(prefs.mTrackIndex);
        const TrackEntry* selectedTrack = Tracks().Get(prefs.mTrackIndex);
        const bool needsDownload = selectedTrack != nullptr && !selectedTrack->mInstalled;
        ImGui::Spacing();
        if (!tightHeight) HoverNetSectionHeading("Course briefing");
        ImGui::Text("Style: %s", guide.mCharacter.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("  Difficulty: %s", guide.mDifficulty.c_str());
        if (!tightHeight) ImGui::TextWrapped("%s", guide.mDescription.c_str());
        if (needsDownload) {
            AccentNote("Not on this computer yet - it downloads (" + FormatMegabytes(selectedTrack->mDownloadBytes) +
                       ") when you start.");
        }
        ImGui::Spacing();

        ImGui::BeginDisabled(guide.mFreePlay);
        ImGui::TextUnformatted("Laps");
        ImGui::SameLine();
        if (guide.mFreePlay) {
            ImGui::TextDisabled("Not used on a free-play track");
        }
        else {
            ImGui::TextDisabled("Suggested: %d", guide.mSuggestedLaps);
        }
        ImGui::PushItemWidth(-1.0f);
        ImGui::SliderInt("##Laps", &prefs.mLaps, 1, 20);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
        if (!tightHeight && !guide.mFreePlay) {
            const std::string trackName = TrackNameAt(prefs.mTrackIndex);
            const long long raceBest = personalBests.RaceBest(trackName, prefs.mLaps), lapBest = personalBests.LapBest(trackName);
            char best[32], text[96];
            if (raceBest > 0) {
                FormatRaceTime(static_cast<MR_SimulationTime>(raceBest), best, sizeof(best));
                std::snprintf(text, sizeof(text), "Your best over %d lap%s: %s", prefs.mLaps, prefs.mLaps == 1 ? "" : "s", best);
                ImGui::TextDisabled("%s", text);
            }
            if (lapBest > 0) {
                FormatRaceTime(static_cast<MR_SimulationTime>(lapBest), best, sizeof(best));
                std::snprintf(text, sizeof(text), "Your best lap on this track: %s", best);
                ImGui::TextDisabled("%s", text);
            }
        }
        ImGui::Spacing();
        ImGui::Checkbox("Weapons enabled", &prefs.mWeapons);
        ImGui::Spacing();
        ImGui::Spacing();

        const float halfButton = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const bool startPressed =
            HoverNetButton(needsDownload ? "Download & Start Race" : "Start Race",
                           ImVec2(tightHeight ? halfButton : -FLT_MIN, 40)) ||
            (autoStartForTest && framesShown == 1);
        if (startPressed && !download.Active()) {
            if (needsDownload) {
                download.Begin({*selectedTrack}, 0);
                startAfterDownload = true;
            }
            else {
                running = false;
            }
        }
        if (tightHeight) ImGui::SameLine(); else ImGui::Spacing();
        if (HoverNetButton("Cancel", ImVec2(-FLT_MIN, tightHeight ? 40.0f : 36.0f))) {
            cancelled = true;
            running = false;
        }
        const std::vector<TrackEntry> missing = Tracks().MissingDownloads();
        if (!missing.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            char label[96];
            std::snprintf(label, sizeof(label), "Download all %d community tracks (%s)",
                          static_cast<int>(missing.size()),
                          FormatMegabytes(Tracks().MissingDownloadBytes()).c_str());
            if (HoverNetButton(label, ImVec2(-FLT_MIN, 0)) && !download.Active()) {
                download.Begin(missing, 0);
                startAfterDownload = false;
            }
        }
        ImGui::EndChild();

        TrackPreview& preview = trackPreviews[prefs.mTrackIndex];
        if (!preview.mLoaded) {
            preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(prefs.mTrackIndex).c_str());
        }
        if (sideBySide) {
            ImGui::SameLine(0.0f, gap);
            ImGui::BeginChild("TrackPreview", ImVec2(0, 0), false);
            DrawTrackPreview(preview);
            ImGui::EndChild();
        }
        else {
            ImGui::Spacing();
            ImGui::BeginChild("TrackPreview", ImVec2(0, phoneLayout ? 200.0f : 240.0f), false);
            DrawTrackPreview(preview);
            ImGui::EndChild();
        }

        ImGui::EndChild();
        {
            const DownloadUi::State downloadState = download.Draw(0);
            if (downloadState == DownloadUi::State::eDone && startAfterDownload) {
                startAfterDownload = false;
                running = false;
            }
            else if (downloadState == DownloadUi::State::eFailed) {
                startAfterDownload = false;
            }
        }
        ImGui::End();

        ImGui::Render();
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    for (TrackPreview& preview : trackPreviews) {
        if (preview.mTexture != nullptr) SDL_DestroyTexture(preview.mTexture);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    LocalRaceSetup result;
    result.confirmed = !cancelled;
    result.trackName = TrackNameAt(prefs.mTrackIndex);
    result.laps = prefs.mLaps;
    result.weapons = prefs.mWeapons;
    if (result.confirmed) {
        SaveLocalRacePrefs(prefs);
    }
    return result;
}

bool IsBlank(const char* text)
{
    for (const char* p = text; *p != '\0'; ++p) {
        if (!std::isspace(static_cast<unsigned char>(*p))) {
            return false;
        }
    }
    return true;
}

// Lets the player change their display name and which RaceServer they connect
// to, instead of the only options being a --lobby command-line flag (for the
// server) or waiting to be prompted the very first time the lobby ever opens
// (for the name, and even then never again afterward). UX choices worth
// calling out: fields are pre-filled with the CURRENT values, not blank
// placeholders, so opening this to tweak one thing doesn't require
// re-entering the other; Save is disabled (with an inline reason, not just a
// silently inert button) whenever the name or host is blank or the port is
// out of range, instead of accepting bad input that only fails later when
// something tries to connect; a Reset button restores the built-in default
// server without the player needing to know or retype it; and Escape/Cancel
// discards edits entirely rather than applying whatever was typed so far.
SettingsResult RunSettingsScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                                 const MR_Sprite& font, const std::string& currentUsername,
                                 const std::string& currentServerHost, unsigned currentServerPort,
                                 double currentVolume, bool currentMuted, bool currentFullscreen,
                                 int pFrameLimit)
{
    (void)buffer;
    (void)viewport;
    (void)font;

    char usernameBuf[24] = {0};
    std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", currentUsername.c_str());
    char hostBuf[128] = {0};
    std::snprintf(hostBuf, sizeof(hostBuf), "%s", currentServerHost.c_str());
    int port = static_cast<int>(currentServerPort);
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    // The HoverNet server for online multiplayer: empty means the official one. Another server (a friend's, a club's) is typed here.
    char authAddressBuf[256] = {0};
    char authKeyBuf[80] = {0};
    {
        const AuthServerSettings savedAuth = LoadAuthServerSettings();
        if (savedAuth.custom) {
            std::snprintf(authAddressBuf, sizeof(authAddressBuf), "%s", savedAuth.address.c_str());
            std::snprintf(authKeyBuf, sizeof(authKeyBuf), "%s", savedAuth.key.c_str());
        }
    }
#endif
    float volumePercent = static_cast<float>(currentVolume * 100.0);
    bool muted = currentMuted;
    const float currentUiScale = LoadUiScale();
    float uiScalePercent = currentUiScale * 100.0f;
    bool applyUiScale = false;
    bool largeHudText = LoadLargeHudText();
    RaceView raceView = LoadRaceView();
    bool showGhost = LoadShowGhost();
    bool reducedMotion = LoadReducedMotion();
    const bool currentHighContrast = LoadHighContrast();
    bool highContrast = currentHighContrast;
    bool fullscreen = currentFullscreen;
    int windowWidth = 0;
    int windowHeight = 0;
    if (currentFullscreen) {
        const WindowSize savedSize = LoadWindowSize();
        windowWidth = savedSize.width;
        windowHeight = savedSize.height;
    }
    else {
        SDL_GetWindowSize(graphics.GetWindow(), &windowWidth, &windowHeight);
    }
    const int currentWindowWidth = windowWidth;
    const int currentWindowHeight = windowHeight;
#ifdef HOVERNET_WEB
    const int entryGameSize = hovernet_js_get_game_size();  // restored on Cancel
#endif
    struct ResolutionOption { int width; int height; };
    constexpr ResolutionOption resolutions[] = {
        {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}
    };
    int selectedResolution = -1;
    for (int index = 0; index < static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0])); ++index) {
        if (resolutions[index].width == windowWidth && resolutions[index].height == windowHeight) {
            selectedResolution = index;
        }
    }

    // Settings can be opened both from the standalone in-race pause menu (which
    // owns its ImGui context) and from the ImGui lobby.  Creating a second context
    // in the latter case overwrote the SDL backend's context pointer; destroying
    // it on return then left the lobby using freed backend state and the process
    // exited on its next frame.  Borrow the caller's context when there is one,
    // and only own backend/context lifetime for legacy callers.
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }
    const bool textInputWasActive = SDL_IsTextInputActive() == SDL_TRUE;
    if (!textInputWasActive) {
        SDL_StartTextInput();
    }

    bool running = true;
    bool cancelled = false;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                cancelled = true;
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    g_QuitConfirmed = true;
                }
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                cancelled = true;
                running = false;
            }
        }

        // Disabling the renderer's logical size for the duration of this ImGui
        // frame (rather than forcing ImGui to draw at the fixed kWidth x
        // kHeight logical size main.cpp's legacy framebuffer needs, and then
        // reverse-mapping mouse coordinates back through that letterbox) is
        // the fix that actually holds up: with logical size disabled, ImGui
        // draws 1:1 against the real window and every mouse coordinate SDL
        // reports -- real events and ImGui_ImplSDL2_NewFrame()'s own
        // "global mouse state" fallback alike -- is already in that same
        // space, so no coordinate translation is needed anywhere, in any
        // frame, regardless of window size or aspect ratio. Three earlier
        // attempts (v0.1.73 through v0.1.76) instead tried to keep ImGui
        // drawing at the fixed logical size and correct for the mismatch
        // after the fact; each shipped looking right and wasn't. Restored
        // to kWidth x kHeight below, before this loop's *next* iteration's
        // event polling can reach a raw-framebuffer screen like ConfirmQuit
        // that still needs it (see HoverNetImGuiLogicalMouseSmoke).
        SDL_RenderSetLogicalSize(graphics.GetRenderer(), 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Settings", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        DrawHoverNetScreenBackdrop(mainViewport);

        const bool phoneHeader = mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phoneHeader ? ImVec2(14.0f, 8.0f) : ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, phoneHeader ? 40.0f : 76.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(phoneHeader ? 1.1f : 1.35f);
        ImGui::TextUnformatted("SETTINGS");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Spacing();

        const float centerWidth = std::min(460.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        // Fill the available height so added accessibility/audio controls do not
        // force scrolling while the screen still has unused space below.
        // Phones: the Save/Cancel row is pinned under the scrolling panel so it never needs a scroll.
        ImGui::BeginChild("SettingsPanel", ImVec2(centerWidth, phoneHeader ? -52.0f : 0.0f), true);
        if (const char* testScroll = std::getenv("HOVERNET_TEST_SETTINGS_SCROLL")) { if (framesShown >= 2) ImGui::SetScrollY(static_cast<float>(std::atoi(testScroll))); }   // screenshot tests: look further down

        ImGui::TextUnformatted("Display Name");
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##Username", usernameBuf, sizeof(usernameBuf));
        ImGui::PopItemWidth();
        const bool usernameBlank = IsBlank(usernameBuf);
        if (usernameBlank) {
            HoverNetHint("Enter a display name");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        bool authAddressValid = true;
        const char* authAddressProblem = "";
        if (authAddressBuf[0] != '\0') {
            const std::string typedAddress = authAddressBuf;
#ifdef HOVERNET_WEB
            authAddressValid = typedAddress.compare(0, 6, "wss://") == 0 || typedAddress.compare(0, 5, "ws://") == 0;
            authAddressProblem = "A web address starts with wss://";
#else
            std::string checkHost;
            int checkPort = 0;
            authAddressValid = SplitServerAddress(typedAddress, checkHost, checkPort);
            authAddressProblem = "Type it as name:port, for example race.example.org:9610";
            Crypto::Bytes checkKey;
            if (authAddressValid && authKeyBuf[0] != '\0' && (!Crypto::FromHex(authKeyBuf, checkKey) || checkKey.size() != 32)) {
                authAddressValid = false;
                authAddressProblem = "The server key is 64 characters (0 to 9, a to f)";
            }
#endif
        }
#else
        const bool authAddressValid = true;
#endif
        ImGui::TextUnformatted("RaceServer Address");
        ImGui::SetNextItemWidth(-96.0f);
        ImGui::InputText("##ServerHost", hostBuf, sizeof(hostBuf));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("##ServerPort", &port, 0);
        const bool hostBlank = IsBlank(hostBuf);
        const bool portValid = port > 0 && port <= 65535;
        if (hostBlank) {
            HoverNetHint("Enter a server address");
        }
        else if (!portValid) {
            HoverNetHint("Port must be between 1 and 65535");
        }
        ImGui::Spacing();
        if (HoverNetButton("Reset to default server", ImVec2(-FLT_MIN, 0))) {
            std::snprintf(hostBuf, sizeof(hostBuf), "%s", kDefaultLobbyHost);
            port = static_cast<int>(kDefaultLobbyPort);
        }

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("HoverNet server (online multiplayer)");
        ImGui::TextDisabled("Leave empty to use the official HoverNet server. For a friend's or a club's server, type its address.");
        ImGui::SetNextItemWidth(-1.0f);
#ifdef HOVERNET_WEB
        ImGui::InputTextWithHint("##AuthAddress", "official HoverNet server (wss://race.example.org/auth/ for another)", authAddressBuf, sizeof(authAddressBuf));
#else
        ImGui::InputTextWithHint("##AuthAddress", "official HoverNet server (race.example.org:9610 for another)", authAddressBuf, sizeof(authAddressBuf));
        if (authAddressBuf[0] != '\0') {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##AuthKey", "server key, 64 characters (optional)", authKeyBuf, sizeof(authKeyBuf));
        }
#endif
        if (authAddressBuf[0] != '\0' && !authAddressValid) HoverNetHint(authAddressProblem);
        ImGui::Spacing();
        if (HoverNetButton("Use the official HoverNet server", ImVec2(-FLT_MIN, 0))) {
            authAddressBuf[0] = '\0';
            authKeyBuf[0] = '\0';
        }
#endif

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Audio");
        if (ImGui::Checkbox("Mute all sound", &muted)) {
            MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0);
        }
        ImGui::PushItemWidth(-1.0f);
        // Keep the chosen level independently of mute so unmuting restores it
        // instead of forgetting the player's setting. Both controls preview live.
        if (ImGui::SliderFloat("##Volume", &volumePercent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0);
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::TextUnformatted("UI Scale");
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::SliderFloat("##UiScale", &uiScalePercent, 75.0f, 150.0f, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp)) {
            applyUiScale = true;
        }
        ImGui::PopItemWidth();
        ImGui::Spacing();
        ImGui::Checkbox("Large HUD text", &largeHudText);
        ImGui::TextDisabled("Keeps race information and chat at the font's native size.");
        ImGui::Checkbox("Reduced motion", &reducedMotion);
        ImGui::TextDisabled("Disables animated menu speed streaks.");
        ImGui::Checkbox("Show my best run as a ghost", &showGhost);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Draws your fastest run on this track beside your craft, so you race your own best time. It never touches the race.");
        ImGui::PopTextWrapPos();
        if (ImGui::Checkbox("High contrast menus", &highContrast)) {
            applyUiScale = true;
        }
        ImGui::TextDisabled("Strengthens text, borders, fields, and focus states.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!HoverNetIsTouchDevice()) {   // phones and tablets always use a picture shaped like their screen
            ImGui::TextUnformatted("Race view");
            static const char* const raceViewLabels[] = {"Classic (4:3)", "Widescreen (16:9)", "Match my window"};
            ImGui::PushItemWidth(-1.0f);
            if (ImGui::BeginCombo("##RaceView", raceViewLabels[static_cast<int>(raceView)])) {
                for (int index = 0; index < 3; ++index) {
                    if (ImGui::Selectable(raceViewLabels[index], static_cast<int>(raceView) == index)) raceView = static_cast<RaceView>(index);
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("The shape of the picture while racing. Takes effect the next time you start HoverNet.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }

#ifdef HOVERNET_WEB
        // Browser: size the gameplay area inside the page (the browser window itself is not resized).
        if (!HoverNetIsTouchDevice()) {
            struct GameSizeOption { int width; int height; const char* label; };
            static const GameSizeOption gameSizes[] = {
                {0, 0, "Fit browser window"}, {800, 600, "800 x 600"}, {1024, 768, "1024 x 768"},
                {1280, 720, "1280 x 720"}, {1600, 900, "1600 x 900"}, {1920, 1080, "1920 x 1080"}
            };
            const int browserWindow = hovernet_js_window_size();
            const int browserW = browserWindow >> 16, browserH = browserWindow & 0xffff;
            const int currentSize = hovernet_js_get_game_size();
            char sizeLabel[32] = "Fit browser window";
            if (currentSize != 0) std::snprintf(sizeLabel, sizeof(sizeLabel), "%d x %d", currentSize >> 16, currentSize & 0xffff);
            ImGui::TextUnformatted("Game Size");
            ImGui::PushItemWidth(-1.0f);
            if (ImGui::BeginCombo("##GameSize", sizeLabel)) {
                for (const GameSizeOption& option : gameSizes) {
                    if (option.width > browserW || option.height > browserH) continue;  // would not fit
                    const bool selected = (option.width << 16 | option.height) == currentSize;
                    if (ImGui::Selectable(option.label, selected)) hovernet_js_set_game_size(option.width, option.height);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Sizes only the game area; the browser window stays as it is.");
        }
#else
        ImGui::TextUnformatted("Window Resolution");
        char resolutionLabel[32];
        std::snprintf(resolutionLabel, sizeof(resolutionLabel), "%d x %d", windowWidth, windowHeight);
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::BeginCombo("##WindowResolution", resolutionLabel)) {
            for (int index = 0; index < static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0])); ++index) {
                char optionLabel[32];
                std::snprintf(optionLabel, sizeof(optionLabel), "%d x %d",
                              resolutions[index].width, resolutions[index].height);
                const bool selected = selectedResolution == index;
                if (ImGui::Selectable(optionLabel, selected)) {
                    selectedResolution = index;
                    windowWidth = resolutions[index].width;
                    windowHeight = resolutions[index].height;
                    if (!fullscreen) {
                        SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
                        SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
#endif
        ImGui::Spacing();

        // Applied live, same as Volume above. The fixed-resolution framebuffer
        // is scaled by SDL_RenderSetLogicalSize; leaving fullscreen restores the
        // selected window size without changing gameplay rendering.
        if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
            SDL_SetWindowFullscreen(graphics.GetWindow(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            if (!fullscreen) {
                SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
                SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        if (HoverNetButton("Restore defaults", ImVec2(-FLT_MIN, 34))) {
            std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", "Player");
            std::snprintf(hostBuf, sizeof(hostBuf), "%s", kDefaultLobbyHost);
            port = static_cast<int>(kDefaultLobbyPort);
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
            authAddressBuf[0] = '\0';
            authKeyBuf[0] = '\0';
#endif
            volumePercent = 100.0f;
            muted = false;
            uiScalePercent = 100.0f;
            largeHudText = false;
            raceView = RaceView::eClassic;
            showGhost = true;
            reducedMotion = false;
            highContrast = false;
            selectedResolution = 0;
            windowWidth = resolutions[0].width;
            windowHeight = resolutions[0].height;
            fullscreen = false;
#ifdef HOVERNET_WEB
            hovernet_js_set_game_size(0, 0);
#endif

            // Defaults are a live preview, just like changing these controls
            // individually. Cancel below still restores the exact entry state.
            MR_SoundServer::SetMasterVolume(1.0);
            SDL_SetWindowFullscreen(graphics.GetWindow(), 0);
            SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
            SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED);
            applyUiScale = true;
        }
        ImGui::TextDisabled("Preview only until Save is selected.");
        ImGui::Spacing();

        const bool canSave = !usernameBlank && !hostBlank && portValid && authAddressValid;
        if (!phoneHeader) {
            ImGui::BeginDisabled(!canSave);
            if (HoverNetButton("Save", ImVec2(-FLT_MIN, 40))) {
                running = false;
            }
            ImGui::EndDisabled();
            ImGui::Spacing();
            if (HoverNetButton("Cancel", ImVec2(-FLT_MIN, 36))) {
                cancelled = true;
                running = false;
            }
        }

        ImGui::EndChild();
        if (phoneHeader) {
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
            const float half = (centerWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::BeginDisabled(!canSave);
            if (HoverNetButton("Save", ImVec2(half, 40))) {
                running = false;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (HoverNetButton("Cancel", ImVec2(half, 40))) {
                cancelled = true;
                running = false;
            }
        }
        ImGui::End();

        ImGui::Render();
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        if (applyUiScale) {
            ApplyHoverNetLobbyStyle(uiScalePercent / 100.0f, highContrast);
            applyUiScale = false;
        }
        HoverNetFrameDelay();
    }

    if (!textInputWasActive) {
        SDL_StopTextInput();
    }
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    if (cancelled) {
        // Unlike username/host, display and audio changes preview live. Cancel
        // restores the exact mode, window size, and volume from entry.
        MR_SoundServer::SetMasterVolume(currentMuted ? 0.0 : currentVolume);
        SDL_SetWindowFullscreen(graphics.GetWindow(), currentFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifdef HOVERNET_WEB
        hovernet_js_set_game_size(entryGameSize >> 16, entryGameSize & 0xffff);
#endif
        if (!currentFullscreen) {
            SDL_SetWindowSize(graphics.GetWindow(), currentWindowWidth, currentWindowHeight);
            SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        if (!ownsImGuiContext) {
            ApplyHoverNetLobbyStyle(currentUiScale, currentHighContrast);
        }
    }

    if (!cancelled) { SaveRaceView(raceView); SaveShowGhost(showGhost); }
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    if (!cancelled) {
        AuthServerSettings chosen;   // the official server unless an address was typed
        if (authAddressBuf[0] != '\0') { chosen.address = authAddressBuf; chosen.key = authKeyBuf; chosen.custom = true; }
        SaveAuthServerSettings(chosen);
    }
#endif
    SettingsResult result;
    result.confirmed = !cancelled;
    result.username = usernameBuf;
    result.serverHost = hostBuf;
    result.serverPort = static_cast<unsigned>(port);
    result.volume = volumePercent / 100.0;
    result.muted = muted;
    result.fullscreen = fullscreen;
    result.windowWidth = windowWidth;
    result.windowHeight = windowHeight;
    result.uiScale = uiScalePercent / 100.0f;
    result.largeHudText = largeHudText;
    result.reducedMotion = reducedMotion;
    result.highContrast = highContrast;
    return result;
}

bool RunOnboardingScreen(SDL2GraphicsBackend& graphics, int pFrameLimit, int pStartPage)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadHoverNetMenuFonts(io);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    SDL_Texture* tutorialRaceTexture = nullptr;
    const std::string tutorialRacePath =
        SourcePath("NetTarget/LinuxClient/assets/tutorial-race.bmp");
    if (SDL_Surface* tutorialRaceSurface = SDL_LoadBMP(tutorialRacePath.c_str())) {
        tutorialRaceTexture = SDL_CreateTextureFromSurface(
            graphics.GetRenderer(), tutorialRaceSurface);
        SDL_FreeSurface(tutorialRaceSurface);
    }

    const char* titles[] = {
        "CHOOSE A RACE", "DRIVE YOUR HOVERCRAFT", "READ THE RACE", "MAKE IT YOURS"
    };
    constexpr int pageCount = sizeof(titles) / sizeof(titles[0]);
    int page = std::max(0, std::min(pStartPage, pageCount - 1));
    int framesShown = 0;
    bool completed = false;
    bool running = true;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                g_QuitConfirmed = true;
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("HoverNet How to Play", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        DrawHoverNetScreenBackdrop(viewport);

        // Phones: small and/or short screens drop the fixed desktop sizes so each page fits
        // without scrolling (ImGui child windows cannot be touch-dragged).
        const bool compact = viewport->WorkSize.x < 760.0f || viewport->WorkSize.y < 560.0f;
        const bool narrow = viewport->WorkSize.x < 560.0f;
        const bool touchControls = HoverNetIsTouchDevice();

        HoverNetScreenHeading("DRIVER BRIEFING", "HOW TO PLAY",
                              compact ? nullptr : "Everything you need before the lights go green");
        if (!compact) ImGui::Spacing();

        const float panelWidth = std::min(900.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetSurface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compact ? ImVec2(14.0f, 10.0f) : ImVec2(28.0f, 24.0f));
        ImGui::BeginChild("OnboardingPanel", ImVec2(panelWidth, compact ? -6.0f : -20.0f), true);

        const float progressWidth = ImGui::GetContentRegionAvail().x;
        const float segmentGap = 8.0f;
        const float segmentWidth = (progressWidth - segmentGap * (pageCount - 1)) / pageCount;
        ImDrawList* panelDraw = ImGui::GetWindowDrawList();
        ImVec2 progressPos = ImGui::GetCursorScreenPos();
        for (int step = 0; step < pageCount; ++step) {
            const ImU32 color = step <= page ? IM_COL32(220, 50, 70, 255) : IM_COL32(58, 62, 78, 255);
            panelDraw->AddRectFilled(
                ImVec2(progressPos.x + step * (segmentWidth + segmentGap), progressPos.y),
                ImVec2(progressPos.x + step * (segmentWidth + segmentGap) + segmentWidth, progressPos.y + 5.0f),
                color, 3.0f);
        }
        ImGui::Dummy(ImVec2(0, compact ? 6.0f : 14.0f));
        ImGui::TextColored(kHoverNetCoral, "BRIEFING %02d / %02d", page + 1, pageCount);
        // Short landscape screens: page label and title share one line to save height.
        if (viewport->WorkSize.y < 460.0f) ImGui::SameLine(0.0f, 14.0f);
        ImGui::SetWindowFontScale(compact ? 1.2f : 1.45f);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::TextUnformatted(titles[page]);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();

        const float contentHeight = std::max(compact ? 90.0f : 300.0f,
                                             ImGui::GetContentRegionAvail().y - (compact ? 76.0f : 72.0f));
        // Compact cards size themselves to their text (no clipping or scrollbars); desktop keeps fixed sizes.
        const ImGuiChildFlags cardFlags = compact ? (ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY)
                                                  : ImGuiChildFlags_Border;
        const float contentAvailHeight = contentHeight;
        ImGui::BeginChild("BriefingContent", ImVec2(0, contentHeight), false);
        if (page == 0) {
            const float gap = compact ? 10.0f : 18.0f;
            const float cardWidth = narrow ? ImGui::GetContentRegionAvail().x
                                           : (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
            const float cardHeight = compact ? 0.0f : 245.0f;
            auto modeCard = [&](const char* number, const char* heading, const char* copy, bool primary) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, primary ? ImVec4(0.20f, 0.055f, 0.085f, 1.0f)
                                                                : ImVec4(0.065f, 0.075f, 0.105f, 1.0f));
                ImGui::BeginChild(heading, ImVec2(cardWidth, cardHeight), cardFlags, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(kHoverNetCoral, "%s", number);
                ImGui::SetWindowFontScale(1.25f);
                ImGui::TextUnformatted(heading);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Spacing();
                ImGui::TextWrapped("%s", copy);
                ImGui::Spacing();
                ImGui::TextDisabled("%s", primary ? "RACE WITH THE WORLD" : "RACE ON YOUR TERMS");
                ImGui::EndChild();
                ImGui::PopStyleColor();
            };
            modeCard("01", "LOCAL RACE", "Choose a track, set the laps and decide whether weapons are enabled.", false);
            if (!narrow) ImGui::SameLine(0.0f, gap);
            modeCard("02", "ONLINE MULTIPLAYER", "Join an open race or host a new event in the shared HoverNet lobby.", true);
            ImGui::Spacing();
            if (!compact || contentAvailHeight > 200.0f)
                AccentNote("Start with Local Race to learn a track, then take your hovercraft online.");
        }
        else if (page == 1 && touchControls) {
            // On a phone the race uses the on-screen buttons, so describe those instead of keys.
            const float cardGap = 8.0f;
            const int columns = narrow ? 2 : 4;
            const float cardWidth = (ImGui::GetContentRegionAvail().x - cardGap * (columns - 1)) / columns;
            const float cardHeight = compact ? 0.0f : 98.0f;
            const char* touchPads[][2] = {
                {"< >", "Steer"}, {"GO", "Accelerate"}, {"BRAKE", "Brake / reverse"}, {"FIRE", "Fire weapon"},
                {"JUMP", "Jump"}, {"WEAPON", "Select weapon"}, {"MENU", "Pause"}, {"CONTROLS", "Hide / show pad"}
            };
            for (int pad = 0; pad < 8; ++pad) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.09f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
                ImGui::BeginChild(touchPads[pad][0], ImVec2(cardWidth, cardHeight), cardFlags, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(kHoverNetCoral, "%s", touchPads[pad][0]);
                ImGui::TextUnformatted(touchPads[pad][1]);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if ((pad + 1) % columns != 0) ImGui::SameLine(0.0f, cardGap);
            }
            if (!compact || contentAvailHeight > 230.0f)
                ImGui::TextDisabled("Hold buttons to drive. Tap the gamepad icon to hide the pad.");
        }
        else if (page == 1) {
            const float cardGap = compact ? 8.0f : 14.0f;
            const int columns = narrow ? 2 : 3;
            const float cardWidth = (ImGui::GetContentRegionAvail().x - cardGap * (columns - 1)) / columns;
            const char* keys[][2] = {
                {"SHIFT", "Accelerate"}, {"DOWN", "Brake / reverse"}, {"LEFT / RIGHT", "Steer"},
                {"UP", "Jump"}, {"CTRL", "Fire weapon"}, {"TAB", "Select weapon"}
            };
            for (int key = 0; key < 6; ++key) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.09f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compact ? ImVec2(10.0f, 6.0f) : ImVec2(18.0f, 14.0f));
                ImGui::BeginChild(keys[key][0], ImVec2(cardWidth, compact ? 0.0f : 98.0f), cardFlags, ImGuiWindowFlags_NoScrollbar);
                if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
                ImGui::TextColored(kHoverNetCoral, "%s", keys[key][0]);
                if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
                if (!compact) ImGui::Spacing();
                ImGui::TextUnformatted(keys[key][1]);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                if ((key + 1) % columns != 0) ImGui::SameLine(0.0f, cardGap);
            }
            ImGui::Spacing();
            if (compact && contentAvailHeight < 260.0f) {
                // No room for the controller card on a short screen (it is listed under Controls).
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.085f, 0.055f, 0.075f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
                ImGui::BeginChild("ControllerSummary", ImVec2(0.0f, compact ? 0.0f : 112.0f), cardFlags, ImGuiWindowFlags_NoScrollbar);
                HoverNetSectionHeading("CONTROLLER");
                ImGui::TextWrapped("LEFT STICK  Steer    |    TRIGGERS  Accelerate / brake    |    A  Jump    |    X  Fire    |    Y  Select weapon");
                ImGui::TextDisabled("Review and remap every binding from Controls.");
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
        }
        else if (page == 2) {
            const ImVec2 diagramPos = ImGui::GetCursorScreenPos();
            const ImVec2 diagramSize(ImGui::GetContentRegionAvail().x,
                                     compact ? std::min(265.0f, std::max(90.0f, contentAvailHeight - 70.0f)) : 265.0f);
            if (tutorialRaceTexture != nullptr) {
                ImGui::Image(reinterpret_cast<ImTextureID>(tutorialRaceTexture), diagramSize);
            }
            else {
                ImGui::InvisibleButton("RaceScreenPreview", diagramSize);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(diagramPos.x + 20.0f, diagramPos.y + 20.0f),
                    IM_COL32(255, 112, 122, 255), "RACE PREVIEW UNAVAILABLE");
            }
            ImGui::Spacing();
            ImGui::TextWrapped("The race view keeps your position, speed, fuel and weapon state across the top. The course and rival craft remain visible ahead of you.");
        }
        else {
            const char* features[][2] = {
                {"IDENTITY", "Set your display name and RaceServer."},
                {"DISPLAY", "Choose window mode, resolution and UI scale."},
                {"ACCESSIBILITY", "Enable large HUD text, reduced motion and high contrast."},
                {"AUDIO", "Adjust volume or mute the game."}
            };
            const int featureColumns = (compact && !narrow) ? 2 : 1;
            const float featureGap = 10.0f;
            const float featureWidth = (ImGui::GetContentRegionAvail().x - featureGap * (featureColumns - 1)) / featureColumns;
            for (int feature = 0; feature < 4; ++feature) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.09f, 1.0f));
                ImGui::BeginChild(features[feature][0], ImVec2(featureColumns > 1 ? featureWidth : 0.0f, compact ? 0.0f : 68.0f),
                                  cardFlags, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextColored(kHoverNetCoral, "%s", features[feature][0]);
                if (narrow || featureColumns > 1) {
                    ImGui::TextWrapped("%s", features[feature][1]);
                }
                else {
                    ImGui::SameLine(compact ? 150.0f : 180.0f);
                    ImGui::TextUnformatted(features[feature][1]);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                if (featureColumns > 1 && feature % 2 == 0) ImGui::SameLine(0.0f, featureGap);
            }
            if (!compact) ImGui::TextDisabled("Settings preview live. Save keeps them; Cancel restores the previous setup.");
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();
        if (page > 0) {
            if (HoverNetQuietButton("BACK", ImVec2(compact ? 96.0f : 130.0f, 44))) --page;
            ImGui::SameLine();
        }
        const char* nextLabel = compact ? (page == pageCount - 1 ? "FINISH" : "NEXT")
                                        : (page == pageCount - 1 ? "FINISH BRIEFING" : "NEXT BRIEFING");
        const float nextWidth = compact ? 130.0f : (page == pageCount - 1 ? 190.0f : 175.0f);
        if (HoverNetButton(nextLabel, ImVec2(nextWidth, 44))) {
            if (page == pageCount - 1) {
                completed = true;
                running = false;
            }
            else {
                ++page;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (tutorialRaceTexture != nullptr) SDL_DestroyTexture(tutorialRaceTexture);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    return completed;
}

void RunControlsScreen(SDL2GraphicsBackend& graphics, int pFrameLimit)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    SDL_Scancode* remappableBindings[] = {
        &gKeyboardBindings.accelerate, &gKeyboardBindings.brake,
        &gKeyboardBindings.steerLeft, &gKeyboardBindings.steerRight,
        &gKeyboardBindings.jump, &gKeyboardBindings.fire,
        &gKeyboardBindings.selectWeapon,
    };
    SDL_GameControllerButton* remappableControllerBindings[] = {
        &gControllerBindings.jump, &gControllerBindings.fire,
        &gControllerBindings.selectWeapon,
    };
    int listeningForBinding = -1;
    int listeningForControllerBinding = -1;
    std::string bindingStatus;
    bool running = true;
    int framesShown = 0;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                g_QuitConfirmed = true;
                running = false;
            }
            else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE &&
                    (listeningForBinding >= 0 || listeningForControllerBinding >= 0)) {
                    // Escape always cancels capture first. Controller capture used
                    // to fall through and close the entire Controls screen.
                    listeningForBinding = -1;
                    listeningForControllerBinding = -1;
                }
                else if (listeningForBinding >= 0 &&
                         IsValidBinding(event.key.keysym.scancode)) {
                    const int target = listeningForBinding;
                    const SDL_Scancode previous = *remappableBindings[target];
                    bool swapped = false;
                    for (int index = 0; index < static_cast<int>(sizeof(remappableBindings) /
                                                                  sizeof(remappableBindings[0])); ++index) {
                        if (index != target && *remappableBindings[index] == event.key.keysym.scancode) {
                            *remappableBindings[index] = previous;
                            swapped = true;
                            break;
                        }
                    }
                    *remappableBindings[target] = event.key.keysym.scancode;
                    SaveKeyboardBindings(gKeyboardBindings);
                    bindingStatus = swapped ? "Keyboard bindings swapped to keep each action unique."
                                            : "Keyboard binding updated.";
                    listeningForBinding = -1;
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     listeningForControllerBinding >= 0 &&
                     IsValidControllerBinding(event.cbutton.button)) {
                const int target = listeningForControllerBinding;
                const SDL_GameControllerButton requested =
                    static_cast<SDL_GameControllerButton>(event.cbutton.button);
                const SDL_GameControllerButton previous = *remappableControllerBindings[target];
                bool swapped = false;
                for (int index = 0; index < static_cast<int>(sizeof(remappableControllerBindings) /
                                                              sizeof(remappableControllerBindings[0])); ++index) {
                    if (index != target && *remappableControllerBindings[index] == requested) {
                        *remappableControllerBindings[index] = previous;
                        swapped = true;
                        break;
                    }
                }
                *remappableControllerBindings[target] = requested;
                SaveControllerBindings(gControllerBindings);
                bindingStatus = swapped ? "Controller bindings swapped to keep each action unique."
                                        : "Controller binding updated.";
                listeningForControllerBinding = -1;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Controls", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        DrawHoverNetScreenBackdrop(mainViewport);

        const bool phoneHeader = mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phoneHeader ? ImVec2(14.0f, 8.0f) : ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, phoneHeader ? 40.0f : 76.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(phoneHeader ? 1.1f : 1.35f);
        ImGui::TextUnformatted("CONTROLS");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const float centerWidth = std::min(700.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        ImGui::BeginChild("ControlsPanel", ImVec2(centerWidth, -52.0f), true);
        int recognizedControllers = 0;
        const char* firstControllerName = nullptr;
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (!SDL_IsGameController(index)) continue;
            ++recognizedControllers;
            if (firstControllerName == nullptr) firstControllerName = SDL_GameControllerNameForIndex(index);
        }
        if (recognizedControllers > 0) {
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.65f, 1.0f), "Controller ready: %s%s",
                               firstControllerName != nullptr ? firstControllerName : "SDL controller",
                               recognizedControllers > 1 ? " (+ more connected)" : "");
        }
        else {
            ImGui::TextDisabled("No SDL-compatible controller detected; keyboard controls remain available.");
        }
        ImGui::TextDisabled("Start is reserved for pause. Escape cancels binding capture.");
        ImGui::Spacing();
        if (ImGui::BeginTable("ControlBindings", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableHeadersRow();
            const char* bindings[][3] = {
                {"Accelerate", nullptr, "Right trigger"},
                {"Brake / Reverse", nullptr, "Left trigger"},
                {"Steer left", nullptr, "Left stick"},
                {"Steer right", nullptr, "Left stick"},
                {"Jump", nullptr, "A"},
                {"Fire weapon", nullptr, "X"},
                {"Select weapon", nullptr, "Y"},
                {"External / Cockpit view", "F3 / F4", "-"},
                {"Player list / More messages", "F5 / F6", "-"},
                {"HUD margin", "+ / -", "-"},
                {"Scroll HUD", "Page Up / Page Down", "-"},
                {"HUD zoom", "Insert / Delete", "-"},
                {"Reset HUD", "Home", "-"},
                {"Camera distance (replays, spectating)", "C", "-"},
                {"Pause menu", "Escape", "Start"},
            };
            for (int index = 0; index < static_cast<int>(sizeof(bindings) / sizeof(bindings[0])); ++index) {
                const auto& binding = bindings[index];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(binding[0]);
                ImGui::TableSetColumnIndex(1);
                if (index < static_cast<int>(sizeof(remappableBindings) / sizeof(remappableBindings[0]))) {
                    const char* keyName = listeningForBinding == index
                        ? "Press a key (Escape cancels)"
                        : SDL_GetScancodeName(*remappableBindings[index]);
                    char buttonLabel[96];
                    std::snprintf(buttonLabel, sizeof(buttonLabel), "%s##Binding%d", keyName, index);
                    if (HoverNetButton(buttonLabel, ImVec2(-FLT_MIN, 0))) {
                        listeningForBinding = index;
                        listeningForControllerBinding = -1;
                    }
                }
                else {
                    ImGui::TextUnformatted(binding[1]);
                }
                ImGui::TableSetColumnIndex(2);
                if (index >= 4 && index <= 6) {
                    const int controllerIndex = index - 4;
                    const char* buttonName = listeningForControllerBinding == controllerIndex
                        ? "Press a button"
                        : SDL_GameControllerGetStringForButton(
                            *remappableControllerBindings[controllerIndex]);
                    if (buttonName == nullptr || buttonName[0] == '\0') buttonName = "Unknown";
                    char controllerLabel[96];
                    std::snprintf(controllerLabel, sizeof(controllerLabel), "%s##ControllerBinding%d",
                                  buttonName, controllerIndex);
                    if (HoverNetButton(controllerLabel, ImVec2(-FLT_MIN, 0))) {
                        listeningForControllerBinding = controllerIndex;
                        listeningForBinding = -1;
                    }
                }
                else {
                    ImGui::TextUnformatted(binding[2]);
                }
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        HoverNetSectionHeading("CONTROLLER AXES");
        const char* axisNames[] = {"Left stick X", "Left stick Y", "Right stick X",
                                   "Right stick Y", "Left trigger", "Right trigger"};
        int steerAxis = static_cast<int>(gControllerBindings.steerAxis);
        int accelerateAxis = static_cast<int>(gControllerBindings.accelerateAxis);
        int brakeAxis = static_cast<int>(gControllerBindings.brakeAxis);
        bool controllerAxesChanged = false;
        ImGui::SetNextItemWidth(-1.0f);
        controllerAxesChanged |= ImGui::Combo("Steering axis", &steerAxis, axisNames,
                                              static_cast<int>(SDL_CONTROLLER_AXIS_MAX));
        controllerAxesChanged |= ImGui::Checkbox("Invert steering", &gControllerBindings.invertSteering);
        ImGui::SetNextItemWidth(-1.0f);
        controllerAxesChanged |= ImGui::Combo("Accelerate axis", &accelerateAxis, axisNames,
                                              static_cast<int>(SDL_CONTROLLER_AXIS_MAX));
        controllerAxesChanged |= ImGui::Checkbox("Invert accelerate direction",
                                                  &gControllerBindings.invertAccelerate);
        ImGui::SetNextItemWidth(-1.0f);
        controllerAxesChanged |= ImGui::Combo("Brake / reverse axis", &brakeAxis, axisNames,
                                              static_cast<int>(SDL_CONTROLLER_AXIS_MAX));
        controllerAxesChanged |= ImGui::Checkbox("Invert brake direction",
                                                  &gControllerBindings.invertBrake);
        ImGui::SetNextItemWidth(-1.0f);
        controllerAxesChanged |= ImGui::SliderInt("Axis deadzone", &gControllerBindings.deadzone,
                                                   0, 24000, "%d");
        if (controllerAxesChanged) {
            gControllerBindings.steerAxis = static_cast<SDL_GameControllerAxis>(steerAxis);
            gControllerBindings.accelerateAxis = static_cast<SDL_GameControllerAxis>(accelerateAxis);
            gControllerBindings.brakeAxis = static_cast<SDL_GameControllerAxis>(brakeAxis);
            SaveControllerBindings(gControllerBindings);
        }
        ImGui::Spacing();
        if (HoverNetButton("Reset all control defaults", ImVec2(-FLT_MIN, 34))) {
            gKeyboardBindings = KeyboardBindings{};
            gControllerBindings = ControllerBindings{};
            SaveKeyboardBindings(gKeyboardBindings);
            SaveControllerBindings(gControllerBindings);
            listeningForBinding = -1;
            listeningForControllerBinding = -1;
            bindingStatus = "Default controls restored.";
        }
        ImGui::Spacing();
        if (!bindingStatus.empty()) {
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.65f, 1.0f), "%s", bindingStatus.c_str());
        }
        HoverNetHint("Remaps, axis direction, and deadzone changes apply immediately. Reusing an input swaps bindings.");
        ImGui::EndChild();
        ImGui::Spacing();
        if (HoverNetButton("Back", ImVec2(-FLT_MIN, 40))) {
            running = false;
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
}


// A time as people write it in the UK: "18 Oct 2026, 21:11" (the device's own time zone).
std::string UkDateTime(std::uint32_t unixSeconds)
{
    static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &t);
#else
    localtime_r(&t, &parts);
#endif
    char text[48];
    std::snprintf(text, sizeof text, "%d %s %d, %02d:%02d", parts.tm_mday, months[parts.tm_mon % 12], parts.tm_year + 1900, parts.tm_hour, parts.tm_min);
    return text;
}

// ---- Watch a replay: the player's own saved races, the server's replays (everyone's), and the track records ---------------------------------
// "My races" are the files of ReplayLibrary.h on this device. "Server replays" and "Track records" ask the HoverNet server the player plays on
// (the lobby's replay messages, OnlineLobby): the newest replays of every race it ran, whether or not the player was in it, and the fastest
// finishes and laps per track and lap count, each with the replay it came from. Returns true when a replay was started (the caller then
// drops into the race loop); false when the player went back. `start(path, slot, note, fatal)` plays a file following the craft of `slot` (the note
// says why that craft, e.g. "the winner") and returns an error text, which is shown here without leaving the screen.
#if defined(HOVERNET_AUTH)
// Connects an OnlineLobby to the server the player uses (the official one unless another was chosen in the HoverNet servers screen).
bool ConnectLobbyForReplays(OnlineLobby& lobby)
{
    AuthServerSettings settings = LoadAuthServerSettings();
    std::string address = kOfficialAuthAddress, key = kOfficialAuthKey;
#ifdef HOVERNET_WEB
    {
        char url[256];
        url[0] = '\0';
        hovernet_js_default_server_url(url, sizeof(url));
        address = url[0] != '\0' ? std::string(url) : std::string("wss://localhost/auth/");
        key.clear();
        if (settings.custom && (settings.address.compare(0, 3, "ws:") == 0 || settings.address.compare(0, 4, "wss:") == 0)) address = settings.address;
    }
#else
    if (settings.custom) { address = settings.address; key = settings.key; }
#endif
    if (const char* testServer = std::getenv("HOVERNET_TEST_AUTH_SERVER")) { address = testServer; key.clear(); }
    std::string host;
    int port = 0;
    std::vector<std::uint8_t> serverKey;
#ifdef HOVERNET_WEB
    host = address;
#else
    if (!SplitServerAddress(address, host, port)) return false;
    if (!key.empty() && (!Crypto::FromHex(key, serverKey) || serverKey.size() != 32)) return false;
#endif
    std::string name = LoadUsername();
    if (name.empty() || !Protocol3::IsValidText(name, 32)) name = "Viewer";
    return lobby.Connect(host, port, name, serverKey);
}
#endif

bool RunReplayBrowser(SDL2GraphicsBackend& graphics, int pFrameLimit, const std::function<std::string(const std::string&, int, const std::string&, bool&)>& start)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    ReplayLibrary library(ConfigDirPath() + "/replays");
    std::vector<ReplayLibrary::Entry> entries = library.List();
    if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYSCREEN %zu replays\n", entries.size()); std::fflush(stdout); }
    std::string message;
    int confirmDelete = -1;
    // Which craft to follow: asked before a replay with more than one player starts (the file holds slots, not names).
    struct CraftChoice { bool active = false, opened = false, server = false; std::string path, title; std::uint64_t id = 0; int players = 0, mySlot = -1, winnerSlot = -1; long long winnerMs = 0; std::string winnerName; std::vector<std::string> slotNames; } craftChoice;   // slotNames: who drove each slot (server replays, from the results table)
    bool started = false;
    bool focusFirst = true;
    bool running = true;
    int framesShown = 0;
    // The server tabs share one connection, opened the first time one of them is shown.
#if defined(HOVERNET_AUTH)
    OnlineLobby server;
    bool serverTried = false;
    int serverTrack = 0;                 // index into the track filter (0 = all tracks, else official track serverTrack - 1)
    bool needReplays = true;
    // The question "which craft do you want to follow?" for a server replay: the likely one is the winner; each slot is named from the results table.
    auto askCraftFor = [&](const Protocol3::ReplayInfo& r) {
        craftChoice = CraftChoice();
        craftChoice.active = true; craftChoice.server = true; craftChoice.id = r.id; craftChoice.title = r.track; craftChoice.players = r.players;
        craftChoice.winnerSlot = r.winnerSlot < 8 ? r.winnerSlot : -1; craftChoice.winnerMs = r.winnerMs; craftChoice.winnerName = r.winnerName;
        craftChoice.slotNames.assign(8, std::string());
        for (const Protocol3::ReplayFinisher& f : r.finishers) {
            std::string who = f.name;
            if (f.craft < Authority::kCraftCount) who += std::string(who.empty() ? "" : " ") + "(" + Authority::CraftName(f.craft) + ")";
            craftChoice.slotNames[f.slot] = who;
        }
    };
    std::uint64_t downloadSlotId = 0;    // the replay being fetched, and the craft to follow in it
    int downloadSlot = 0;
    std::string downloadNote;
    const std::string cachePath = ConfigDirPath() + "/replays/shared_replay.hnr.cache";   // the last downloaded replay (".cache": not listed in My races)
    const int officialTracks = Tracks().OfficialCount();
    int wantTab = 0;
    if (const char* tabEnv = std::getenv("HOVERNET_TEST_REPLAY_TAB")) wantTab = std::atoi(tabEnv);   // screenshot/test hook: open on this tab
#endif
    auto formatMs = [](long long ms, char* out, std::size_t n) { std::snprintf(out, n, "%lld:%02lld.%02lld", ms / 60000, (ms % 60000) / 1000, (ms % 1000) / 10); };
    auto formatDate = [](std::uint32_t unixSeconds, char* out, std::size_t n) { std::snprintf(out, n, "%s", UkDateTime(unixSeconds).c_str()); };

    while (running && !started && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { g_QuitConfirmed = true; running = false; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) running = false;
        }
        std::string startPath;   // set by a WATCH button (or the test hook) and acted on after the frame is drawn
        int startSlot = 0;
        std::string startNote;
        if (framesShown == 3 && std::getenv("HOVERNET_TEST_REPLAY_PICK") != nullptr) {
            for (const ReplayLibrary::Entry& e : entries) if (e.valid) {
                if (std::getenv("HOVERNET_TEST_REPLAY_PICK_CRAFT") != nullptr && e.players > 1) { craftChoice = CraftChoice(); craftChoice.active = true; craftChoice.path = e.path; craftChoice.title = e.track; craftChoice.players = e.players; craftChoice.mySlot = e.mySlot; }   // through the question
                else startPath = e.path;
                break;
            }
        }
#if defined(HOVERNET_AUTH)
        server.Pump();
        if (server.GetState() == OnlineLobby::State::eBrowsing) {
            if (needReplays) { server.RefreshReplays(serverTrack == 0 ? std::string() : Tracks().Get(serverTrack - 1)->mName); needReplays = false; }
        }
        std::vector<std::uint8_t> downloaded;
        if (downloadSlotId != 0 && server.TakeReplay(downloaded)) {
            // The checked file goes to the cache path and is played from there.
            library.MakeDirectory();
            std::FILE* f = std::fopen(cachePath.c_str(), "wb");
            const bool written = f != nullptr && std::fwrite(downloaded.data(), 1, downloaded.size(), f) == downloaded.size();
            if (f != nullptr) std::fclose(f);
            if (written) { startPath = cachePath; startSlot = downloadSlot; startNote = downloadNote; }
            else message = "Could not save the replay on this device.";
            downloadSlotId = 0;
        }
        else if (downloadSlotId != 0 && !server.Downloading()) {
            message = server.ReplayNotice().empty() ? "The replay could not be downloaded." : server.ReplayNotice();
            downloadSlotId = 0;
        }
        if (std::getenv("HOVERNET_TEST_REPLAY_SERVER_PICK") != nullptr && framesShown > 3 && downloadSlotId == 0 && startPath.empty() && server.ReplaysLoaded() && !server.Replays().empty()) {
            static bool picked = false;   // test hook (scripts/test-shared-replays.sh): download and play the newest server replay
            if (!picked) {
                picked = true;
                const Protocol3::ReplayInfo& r = server.Replays()[0];
                if (std::getenv("HOVERNET_TEST_REPLAY_PICK_CRAFT") != nullptr && r.players > 1) askCraftFor(r);
                else { downloadSlotId = r.id; downloadSlot = 0; downloadNote.clear(); server.FetchReplay(downloadSlotId); }
            }
        }
#endif

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Replays", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);
        const bool phoneHeader = mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phoneHeader ? ImVec2(14.0f, 8.0f) : ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, phoneHeader ? 40.0f : 76.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(phoneHeader ? 1.1f : 1.35f);
        ImGui::TextUnformatted("WATCH A REPLAY");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const float centerWidth = std::min(760.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        ImGui::BeginChild("ReplayPanel", ImVec2(centerWidth, -52.0f), true);
        const ImVec4 warn(1.0f, 0.55f, 0.45f, 1.0f);
        if (ImGui::BeginTabBar("ReplayTabs")) {
            // ---- my races
            if (ImGui::BeginTabItem("MY RACES")) {
                if (entries.empty()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted("No saved races yet.");
                    ImGui::TextDisabled("A race on a HoverNet server that you take part in, or watch from its first moment, is saved here when it ends (the newest %zu are kept). "
                                        "Nothing is sent anywhere: the files stay on this device. The Server replays tab has everyone's races.", ReplayLibrary::kKeep);
                    ImGui::PopTextWrapPos();
                }
                for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
                    const ReplayLibrary::Entry& e = entries[static_cast<std::size_t>(index)];
                    ImGui::PushID(index);
                    if (e.valid) {
                        const long long seconds = e.durationMs() / 1000;
                        ImGui::Text("%s", e.track.c_str());
                        ImGui::TextDisabled("%s   %d player%s, %d lap%s, %lld:%02lld", e.when.c_str(), e.players, e.players == 1 ? "" : "s", e.laps, e.laps == 1 ? "" : "s", seconds / 60, seconds % 60);
                    }
                    else {
                        ImGui::TextColored(warn, "Damaged file");
                        ImGui::TextDisabled("%s", e.name.c_str());
                    }
                    const float buttonWidth = std::max(90.0f, (ImGui::GetContentRegionAvail().x - 14.0f) * 0.5f);
                    if (e.valid) {
                        if (focusFirst) { ImGui::SetKeyboardFocusHere(); focusFirst = false; }
                        if (HoverNetButton("WATCH", ImVec2(buttonWidth, 34))) {
                            if (e.players > 1) { craftChoice = CraftChoice(); craftChoice.active = true; craftChoice.path = e.path; craftChoice.title = e.track; craftChoice.players = e.players; craftChoice.mySlot = e.mySlot; }
                            else { startPath = e.path; startSlot = 0; }
                        }
                        ImGui::SameLine();
                    }
                    if (HoverNetButton(confirmDelete == index ? "SURE? DELETE" : "DELETE", ImVec2(e.valid ? buttonWidth : -FLT_MIN, 34))) {
                        if (confirmDelete == index) { library.Remove(e.name); entries = library.List(); confirmDelete = -1; ImGui::PopID(); break; }
                        confirmDelete = index;
                    }
                    ImGui::PopID();
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
#if defined(HOVERNET_AUTH)
            // ---- the server's replays, and the track records
            for (int tab = 1; tab <= 1; ++tab) {
                ImGuiTabItemFlags flags = (wantTab == tab) ? ImGuiTabItemFlags_SetSelected : 0;
                if (!ImGui::BeginTabItem("SERVER REPLAYS", nullptr, flags)) continue;
                if (wantTab == tab) wantTab = 0;
                if (!serverTried) { serverTried = true; if (!ConnectLobbyForReplays(server)) message = "The server address is not usable. Check it in the HoverNet servers screen."; }
                if (server.GetState() == OnlineLobby::State::eFailed) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(warn, "%s", server.Message().empty() ? "Could not reach the server." : server.Message().c_str());
                    ImGui::PopTextWrapPos();
                    if (HoverNetButton("TRY AGAIN", ImVec2(-FLT_MIN, 38))) { serverTried = false; needReplays = true; server.Close(); }
                }
                else if (server.GetState() != OnlineLobby::State::eBrowsing) {
                    ImGui::TextDisabled("Connecting to the server...");
                }
                else if (tab == 1) {
                    // Filter by track.
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char* current = serverTrack == 0 ? "All tracks" : Tracks().Get(serverTrack - 1)->mName.c_str();
                    if (ImGui::BeginCombo("##ServerTrack", current)) {
                        if (ImGui::Selectable("All tracks", serverTrack == 0)) { serverTrack = 0; needReplays = true; }
                        for (int t = 0; t < officialTracks; ++t) if (ImGui::Selectable(Tracks().Get(t)->mName.c_str(), serverTrack == t + 1)) { serverTrack = t + 1; needReplays = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::Spacing();
                    if (server.ListsUnreadable() && !server.ReplaysLoaded()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "The server answered in a form this version of the game cannot read: the game and the server are on different versions. Reload the page (or update the game), or try again after the server has been updated."); ImGui::PopTextWrapPos(); }
                    else if (!server.ReplaysLoaded()) ImGui::TextDisabled("Loading the list...");
                    else if (server.Replays().empty()) ImGui::TextDisabled("The server has no replays%s yet.", serverTrack == 0 ? "" : " of this track");
                    for (std::size_t i = 0; i < server.Replays().size(); ++i) {
                        const Protocol3::ReplayInfo& r = server.Replays()[i];
                        ImGui::PushID(static_cast<int>(i));
                        char when[32], best[32];
                        formatDate(r.savedAt, when, sizeof when);
                        ImGui::Text("%s", r.track.c_str());
                        const long long seconds = static_cast<long long>(r.ticks) * ReplayFormat::kFrameMs / 1000;
                        ImGui::TextDisabled("%s   %d player%s, %d lap%s, %lld:%02lld", when, r.players, r.players == 1 ? "" : "s", r.laps, r.laps == 1 ? "" : "s", seconds / 60, seconds % 60);
                        if (r.finishers.empty()) ImGui::TextDisabled("No results recorded");
                        for (std::size_t k = 0; k < r.finishers.size(); ++k) {
                            const Protocol3::ReplayFinisher& f = r.finishers[k];
                            char who[96], time[32] = "did not finish";
                            if (f.ms > 0) formatMs(f.ms, time, sizeof time);
                            std::snprintf(who, sizeof who, "%s", f.name.empty() ? (std::string("Player ") + std::to_string(f.slot + 1)).c_str() : f.name.c_str());
                            ImGui::TextDisabled("%zu. %s%s%s%s   %s", k + 1, who, f.craft < Authority::kCraftCount ? " (" : "", f.craft < Authority::kCraftCount ? Authority::CraftName(f.craft) : "", f.craft < Authority::kCraftCount ? ")" : "", time);
                        }
                        const bool busy = downloadSlotId != 0;
                        char label[48];
                        if (busy && downloadSlotId == r.id) std::snprintf(label, sizeof label, "DOWNLOADING %d%%", server.DownloadPercent());
                        else std::snprintf(label, sizeof label, "WATCH");
                        if (HoverNetButton(label, ImVec2(-FLT_MIN, 34)) && !busy) {
                            message.clear();
                            if (r.players > 1) askCraftFor(r);
                            else { downloadSlotId = r.id; downloadSlot = 0; downloadNote.clear(); server.FetchReplay(r.id); }
                        }
                        ImGui::PopID();
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    }
                }
                ImGui::EndTabItem();
            }
#endif
            ImGui::EndTabBar();
        }
        if (craftChoice.active && !craftChoice.opened) { ImGui::OpenPopup("Follow which craft?"); craftChoice.opened = true; }
        if (craftChoice.active) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            bool open = true;
            if (ImGui::BeginPopupModal("Follow which craft?", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%s: which craft do you want to follow?", craftChoice.title.c_str());
                ImGui::TextDisabled("You can switch craft while it plays with [ and ].");
                ImGui::Spacing();
                int chosen = -1;
                const int defaultSlot = craftChoice.mySlot >= 0 ? craftChoice.mySlot : (craftChoice.winnerSlot >= 0 ? craftChoice.winnerSlot : 0);
                if (const char* pickCraft = std::getenv("HOVERNET_TEST_REPLAY_PICK_CRAFT")) if (std::string(pickCraft) != "show") chosen = std::string(pickCraft) == "default" ? defaultSlot : std::atoi(pickCraft);   // test hook: answer the question ("show" leaves it open for a screenshot)
                for (int slot = 0; slot < craftChoice.players; ++slot) {
                    char label[200];
                    std::string suffix;
                    if (slot == craftChoice.mySlot) suffix += "  (you)";
                    if (slot == craftChoice.winnerSlot) {
                        char t[32]; formatMs(craftChoice.winnerMs, t, sizeof t);
                        suffix += std::string("  (winner") + (craftChoice.winnerName.empty() ? std::string() : " " + craftChoice.winnerName) + ", " + t + ")";
                    }
                    const std::string who = slot < static_cast<int>(craftChoice.slotNames.size()) && !craftChoice.slotNames[static_cast<std::size_t>(slot)].empty() ? " - " + craftChoice.slotNames[static_cast<std::size_t>(slot)] : std::string();
                    std::snprintf(label, sizeof label, "PLAYER %d%s%s##craft%d", slot + 1, who.c_str(), suffix.c_str(), slot);
                    if (slot == defaultSlot && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                    if (HoverNetButton(label, ImVec2(360, 38))) chosen = slot;
                }
                ImGui::Spacing();
                if (HoverNetButton("CANCEL", ImVec2(360, 34))) { craftChoice.active = false; ImGui::CloseCurrentPopup(); }
                if (chosen >= 0) {
                    std::string note;
                    if (chosen == craftChoice.mySlot) note = "you";
                    else if (chosen == craftChoice.winnerSlot) note = craftChoice.winnerName.empty() ? std::string("the winner") : "the winner, " + craftChoice.winnerName;
                    if (craftChoice.server) { downloadSlotId = craftChoice.id; downloadSlot = chosen; downloadNote = note; server.FetchReplay(craftChoice.id); }
                    else { startPath = craftChoice.path; startSlot = chosen; startNote = note; }
                    craftChoice.active = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            else craftChoice.active = false;
            if (!open) craftChoice.active = false;
        }
        if (!message.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(warn, "%s", message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (HoverNetButton("Back", ImVec2(-FLT_MIN, 40))) running = false;
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();

        if (!startPath.empty()) {
            bool fatal = false;
            message = start(startPath, startSlot, startNote, fatal);
            if (message.empty()) started = true;
            else if (fatal) { g_QuitConfirmed = true; running = false; }
            confirmDelete = -1;
        }
    }

#if defined(HOVERNET_AUTH)
    if (!started) server.Close();
#endif
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return started;
}


// ---- Leaderboards: the community's records, per track and per craft ---------------------------------------------------------------------------
// From the server the player plays on (the whole online community, not this device): for a track, the fastest single laps or the fastest races of a
// number of laps, with a board for each craft (the best five), because the craft change how fast a hovercraft is. Each time has the player's name and a
// WATCH button that plays the run and follows that craft. Returns true when a replay was started.
#if defined(HOVERNET_AUTH)
bool RunLeaderboards(SDL2GraphicsBackend& graphics, int pFrameLimit, const std::function<std::string(const std::string&, int, const std::string&, bool&)>& start)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }
    ReplayLibrary cacheFolder(ConfigDirPath() + "/replays");
    const std::string cachePath = ConfigDirPath() + "/replays/shared_replay.hnr.cache";
    PersonalBests myBests;   // the player's own bests (on this device), shown beside the community's
    myBests.Load(ConfigDirPath() + "/personal_bests");
    OnlineLobby server;
    bool serverTried = false;
    const int officialTracks = Tracks().OfficialCount();
    int track = 0;                       // official track index
    int laps = 0;                        // 0: fastest laps; n: fastest races of n laps
    bool need = true, needWinners = true, needLeague = true;
    std::string leagueMonth;             // the month shown in the league tab (empty: the current month)
    int wantTab = -1;                    // test hook: open on this tab
    std::uint64_t downloadId = 0;
    int downloadSlot = 0;
    std::string downloadNote, message, startPath, startNote;
    int startSlot = 0;
    bool started = false, running = true;
    int framesShown = 0;
    if (const char* trackEnv = std::getenv("HOVERNET_TEST_RECORDS_TRACK")) { const int found = Tracks().Find(trackEnv); if (found >= 0 && found < officialTracks) track = found; }   // screenshot/test hook
    if (const char* lapsEnv = std::getenv("HOVERNET_TEST_RECORDS_LAPS")) laps = std::atoi(lapsEnv);
    if (const char* tabEnv = std::getenv("HOVERNET_TEST_LEADERBOARD_TAB")) wantTab = std::atoi(tabEnv);
    auto formatMs = [](long long ms, char* out, std::size_t n) { std::snprintf(out, n, "%lld:%02lld.%02lld", ms / 60000, (ms % 60000) / 1000, (ms % 1000) / 10); };
    auto formatDate = [](std::uint32_t unixSeconds, char* out, std::size_t n) { std::snprintf(out, n, "%s", UkDateTime(unixSeconds).c_str()); };
    bool printedReport = false;

    while (running && !started && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { g_QuitConfirmed = true; running = false; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) running = false;
        }
        if (!serverTried) { serverTried = true; if (!ConnectLobbyForReplays(server)) message = "The server address is not usable. Check it in the HoverNet servers screen."; }
        server.Pump();
        if (server.GetState() == OnlineLobby::State::eBrowsing) {
            if (need && officialTracks > 0) { server.RefreshRecords(Tracks().Get(track)->mName, laps); need = false; }
            if (needWinners) { server.RefreshWinners(); needWinners = false; }
            if (needLeague) { server.RefreshLeague(leagueMonth); needLeague = false; }
        }
        std::vector<std::uint8_t> downloaded;
        if (downloadId != 0 && server.TakeReplay(downloaded)) {
            cacheFolder.MakeDirectory();
            std::FILE* f = std::fopen(cachePath.c_str(), "wb");
            const bool written = f != nullptr && std::fwrite(downloaded.data(), 1, downloaded.size(), f) == downloaded.size();
            if (f != nullptr) std::fclose(f);
            if (written) { startPath = cachePath; startSlot = downloadSlot; startNote = downloadNote; }
            else message = "Could not save the replay on this device.";
            downloadId = 0;
        }
        else if (downloadId != 0 && !server.Downloading()) {
            message = server.ReplayNotice().empty() ? "The replay could not be downloaded." : server.ReplayNotice();
            downloadId = 0;
        }
        if (server.RecordsLoaded() && !printedReport && std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {
            printedReport = true;
            const Protocol3::RecordList& r = server.Records();
            std::printf("LEADERBOARDS %s laps %d boards %zu", r.track.c_str(), r.laps, r.boards.size());
            for (const Protocol3::RecordBoard& b : r.boards) std::printf(" | %s: %s %d", Authority::CraftName(b.craft), b.entries.empty() ? "-" : b.entries[0].name.c_str(), b.entries.empty() ? 0 : b.entries[0].ms);
            std::printf("\n");
            std::fflush(stdout);
            if (std::getenv("HOVERNET_TEST_LEADERBOARD_PICK") != nullptr && !r.boards.empty() && !r.boards[0].entries.empty()) {   // test hook: watch the first record
                downloadId = r.boards[0].entries[0].replay; downloadSlot = r.boards[0].entries[0].slot; downloadNote = laps == 0 ? "the fastest lap" : "the record holder"; server.FetchReplay(downloadId);
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Leaderboards", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);
        const bool phoneHeader = mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phoneHeader ? ImVec2(14.0f, 8.0f) : ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, phoneHeader ? 40.0f : 76.0f), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(phoneHeader ? 1.1f : 1.35f);
        ImGui::TextUnformatted("LEADERBOARDS");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        const float centerWidth = std::min(760.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        ImGui::BeginChild("LeaderboardPanel", ImVec2(centerWidth, -52.0f), true);
        const ImVec4 warn(1.0f, 0.55f, 0.45f, 1.0f);
        if (server.GetState() == OnlineLobby::State::eFailed) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(warn, "%s", server.Message().empty() ? "Could not reach the server." : server.Message().c_str());
            ImGui::PopTextWrapPos();
            if (HoverNetButton("TRY AGAIN", ImVec2(-FLT_MIN, 38))) { serverTried = false; need = needWinners = needLeague = true; server.Close(); }
        }
        else if (server.GetState() != OnlineLobby::State::eBrowsing) {
            ImGui::TextDisabled("Connecting to the server...");
        }
        else if (ImGui::BeginTabBar("LeaderboardTabs")) {
            // ---- the records, per craft
            if (ImGui::BeginTabItem("RECORDS", nullptr, wantTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::TextDisabled("The best times of everyone who races on this server, for each craft.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (officialTracks > 0 && ImGui::BeginCombo("##Track", Tracks().Get(track)->mName.c_str())) {
                for (int t = 0; t < officialTracks; ++t) if (ImGui::Selectable(Tracks().Get(t)->mName.c_str(), track == t)) { track = t; need = true; }
                ImGui::EndCombo();
            }
            char current[48];
            if (laps == 0) std::snprintf(current, sizeof current, "Fastest laps");
            else std::snprintf(current, sizeof current, "Fastest races: %d lap%s", laps, laps == 1 ? "" : "s");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##Kind", current)) {
                if (ImGui::Selectable("Fastest laps", laps == 0)) { laps = 0; need = true; }
                if (server.RecordsLoaded()) for (std::uint8_t l : server.Records().availableLaps) {
                    char label[48];
                    std::snprintf(label, sizeof label, "Fastest races: %d lap%s", l, l == 1 ? "" : "s");
                    if (ImGui::Selectable(label, laps == l)) { laps = l; need = true; }
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            if (server.ListsUnreadable() && !server.RecordsLoaded()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "The server answered in a form this version of the game cannot read: the game and the server are on different versions. Reload the page (or update the game), or try again after the server has been updated."); ImGui::PopTextWrapPos(); }
            else if (!server.RecordsLoaded()) ImGui::TextDisabled("Loading the leaderboard...");
            else {
                const std::string& trackName = Tracks().Get(track)->mName;
                const long long mine = laps == 0 ? myBests.LapBest(trackName) : myBests.RaceBest(trackName, laps);
                if (mine) { char t[32]; formatMs(mine, t, sizeof t); ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.65f, 1.0f), "Your best on this device: %s (any craft)", t); ImGui::Spacing(); }
                if (server.Records().boards.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("No times for this yet. Times come from races on the server; races against the clock on your own device count toward your personal bests, not here."); ImGui::PopTextWrapPos(); }
                int idCounter = 0;
                for (int craft = 0; craft < Authority::kCraftCount; ++craft) {
                    const Protocol3::RecordBoard* board = nullptr;
                    for (const Protocol3::RecordBoard& b : server.Records().boards) if (b.craft == craft) board = &b;
                    HoverNetSectionHeading(Authority::CraftName(craft));
                    if (board == nullptr || board->entries.empty()) { ImGui::TextDisabled("No times yet"); ImGui::Spacing(); continue; }
                    for (std::size_t i = 0; i < board->entries.size(); ++i) {
                        const Protocol3::RecordEntry& e = board->entries[i];
                        ImGui::PushID(++idCounter);
                        char time[32], when[32];
                        formatMs(e.ms, time, sizeof time);
                        formatDate(e.savedAt, when, sizeof when);
                        char who[48];
                        if (e.name.empty()) std::snprintf(who, sizeof who, "player %d", e.slot + 1); else std::snprintf(who, sizeof who, "%s", e.name.c_str());
                        ImGui::Text("%zu.  %s   %s   %s", i + 1, who, time, when);
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - 120.0f);
                        const bool busy = downloadId != 0;
                        if (HoverNetButton(busy && downloadId == e.replay ? "..." : "WATCH", ImVec2(110, 0)) && !busy) {
                            downloadId = e.replay; downloadSlot = e.slot; downloadNote = laps == 0 ? "the fastest lap" : "the record holder"; message.clear(); server.FetchReplay(e.replay);
                        }
                        ImGui::PopID();
                    }
                    ImGui::Spacing();
                }
            }
                ImGui::EndTabItem();
            }
            // ---- the recent winners of races with two or more players
            if (ImGui::BeginTabItem("RECENT WINNERS", nullptr, wantTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Who won the latest races. A race is listed only if two or more different players were in it.");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (server.ListsUnreadable() && !server.WinnersLoaded()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "The server answered in a form this version of the game cannot read: the game and the server are on different versions. Reload the page (or update the game)."); ImGui::PopTextWrapPos(); }
                else if (!server.WinnersLoaded()) ImGui::TextDisabled("Loading the winners...");
                else if (server.Winners().empty()) ImGui::TextDisabled("No races with two or more players have been won yet.");
                for (std::size_t i = 0; i < server.Winners().size(); ++i) {
                    const Protocol3::WinnerEntry& w = server.Winners()[i];
                    ImGui::PushID(1000 + static_cast<int>(i));
                    char time[32], when[48];
                    formatMs(w.ms, time, sizeof time);
                    formatDate(w.savedAt, when, sizeof when);
                    ImGui::Text("%s", w.track.c_str());
                    ImGui::TextDisabled("%s   %d lap%s, %d players", when, w.laps, w.laps == 1 ? "" : "s", w.players);
                    ImGui::Text("Winner: %s%s%s%s   %s", w.name.empty() ? "player" : w.name.c_str(), w.craft < Authority::kCraftCount ? " (" : "", w.craft < Authority::kCraftCount ? Authority::CraftName(w.craft) : "", w.craft < Authority::kCraftCount ? ")" : "", time);
                    const bool busy = downloadId != 0;
                    if (HoverNetButton(busy && downloadId == w.replay ? "DOWNLOADING..." : "WATCH THE RACE", ImVec2(-FLT_MIN, 34)) && !busy) {
                        downloadId = w.replay; downloadSlot = w.slot; downloadNote = "the winner"; message.clear(); server.FetchReplay(w.replay);
                    }
                    ImGui::PopID();
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            // ---- the monthly league
            if (ImGui::BeginTabItem("MONTHLY LEAGUE", nullptr, wantTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
                static const char* const monthNames[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
                auto monthLabel = [&](const std::string& key) -> std::string {   // "2026-10" -> "October 2026"
                    const int m = key.size() == 7 ? std::atoi(key.substr(5, 2).c_str()) : 0;
                    return m >= 1 && m <= 12 ? std::string(monthNames[m - 1]) + " " + key.substr(0, 4) : key;
                };
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Win a race: 3 points. Second: 2. Third: 1. Only races where two or more players on different devices finished count. It starts again on the 1st of every month (UTC).");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (server.ListsUnreadable() && !server.LeagueLoaded()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "The server answered in a form this version of the game cannot read: the game and the server are on different versions. Reload the page (or update the game)."); ImGui::PopTextWrapPos(); }
                else if (!server.LeagueLoaded()) ImGui::TextDisabled("Loading the league...");
                else {
                    const Protocol3::LeagueData& lg = server.League();
                    std::vector<std::string> months;
                    months.push_back(lg.currentMonth);
                    for (const Protocol3::LeagueMonthWinner& w : lg.pastWinners) months.push_back(w.month);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##LeagueMonth", (monthLabel(lg.month) + (lg.month == lg.currentMonth ? "  (this month)" : "")).c_str())) {
                        for (const std::string& m : months) if (ImGui::Selectable((monthLabel(m) + (m == lg.currentMonth ? "  (this month)" : "")).c_str(), m == lg.month)) { leagueMonth = m == lg.currentMonth ? std::string() : m; needLeague = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::Spacing();
                    HoverNetSectionHeading((monthLabel(lg.month) + (lg.month == lg.currentMonth ? " so far" : "")).c_str());
                    if (lg.standings.empty()) ImGui::TextDisabled("Nobody has scored yet.");
                    for (std::size_t i = 0; i < lg.standings.size(); ++i) {
                        const Protocol3::LeagueRow& r = lg.standings[i];
                        ImGui::Text("%zu.  %s   %d point%s", i + 1, r.name.c_str(), r.points, r.points == 1 ? "" : "s");
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - 190.0f);
                        ImGui::TextDisabled("%d win%s, %d race%s", r.wins, r.wins == 1 ? "" : "s", r.races, r.races == 1 ? "" : "s");
                    }
                    ImGui::Spacing();
                    HoverNetSectionHeading("Winners of earlier months");
                    if (lg.pastWinners.empty()) ImGui::TextDisabled("No month has finished yet.");
                    for (const Protocol3::LeagueMonthWinner& w : lg.pastWinners) {
                        if (w.name.empty()) ImGui::Text("%s:  nobody scored", monthLabel(w.month).c_str());
                        else ImGui::Text("%s:  %s  (%d points)", monthLabel(w.month).c_str(), w.name.c_str(), w.points);
                    }
                    ImGui::Spacing();
                    char yearHeading[48];
                    std::snprintf(yearHeading, sizeof yearHeading, "%d so far", lg.currentYear);
                    HoverNetSectionHeading(yearHeading);
                    if (lg.yearStandings.empty()) ImGui::TextDisabled("Nobody has scored yet this year.");
                    for (std::size_t i = 0; i < lg.yearStandings.size() && i < 5; ++i) ImGui::Text("%zu.  %s   %d points", i + 1, lg.yearStandings[i].name.c_str(), lg.yearStandings[i].points);
                    ImGui::Spacing();
                    HoverNetSectionHeading("Winners of earlier years");
                    if (lg.yearWinners.empty()) ImGui::TextDisabled("The winner of each year is announced here when the year ends.");
                    for (const Protocol3::LeagueYearWinner& w : lg.yearWinners) {
                        if (w.name.empty()) ImGui::Text("%d:  nobody scored", w.year);
                        else ImGui::Text("%d:  %s  (%d points)", w.year, w.name.c_str(), w.points);
                    }
                }
                ImGui::EndTabItem();
            }
            wantTab = -1;
            ImGui::EndTabBar();
        }
        if (!message.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "%s", message.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::EndChild();
        ImGui::Spacing();
        if (HoverNetButton("Back", ImVec2(-FLT_MIN, 40))) running = false;
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
        if (!startPath.empty()) {
            bool fatal = false;
            message = start(startPath, startSlot, startNote, fatal);
            startPath.clear();
            if (message.empty()) started = true;
            else if (fatal) { g_QuitConfirmed = true; running = false; }
        }
    }
    if (!started) server.Close();
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return started;
}
#endif

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
// ---- Play on a HoverNet server (protocol 3.0): choose a server, then host, join or watch a race ---------------------------------------
// The lobby logic is OnlineLobby (tested against a real server by HoverNetOnlineLobbyTest); this is only its screen. The browser build
// has no transport for these servers yet, so the screen is native-only (docs/web-parity-matrix.md).
// True when the code may be used for a private race (empty = public).
bool PrivateCodeOk(const std::string& code) { return code.empty() || (code.size() >= 6 && code.size() <= 16); }

// How the HoverNet server screen ended: a race is ready (lobby.GetState() == eReady, the connection is open), the player went back, or the
// official server could not be reached and the caller should offer the classic lobby instead.
enum class AuthScreenResult { eRace, eBack, eFallback };

// Opens on the official server by itself: players type nothing but, the first time, their name. The technical fields (an address, a key) are
// behind "Use another server".
AuthScreenResult RunAuthServerScreen(SDL2GraphicsBackend& graphics, OnlineLobby& lobby, int pFrameLimit)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    AuthServerSettings settings = LoadAuthServerSettings();
    // The official server: the player's own site in the browser, the built-in address elsewhere. Test hooks may point it at a test server.
    const char* testServer = std::getenv("HOVERNET_TEST_AUTH_SERVER");
    const char* clickTest = std::getenv("HOVERNET_TEST_AUTH_CLICKS");
    std::string officialAddress = kOfficialAuthAddress, officialKey = kOfficialAuthKey;
#ifdef HOVERNET_WEB
    {
        char url[256];
        url[0] = '\0';
        hovernet_js_default_server_url(url, sizeof(url));
        officialAddress = url[0] != '\0' ? std::string(url) : std::string("wss://localhost/auth/");
        officialKey.clear();
        // A remembered custom address must be a WebSocket address to be usable here.
        if (settings.custom && settings.address.compare(0, 3, "ws:") != 0 && settings.address.compare(0, 4, "wss:") != 0) settings.custom = false;
        if (!settings.custom) settings.key.clear();
    }
#endif
    if (testServer != nullptr) { officialAddress = testServer; officialKey.clear(); settings.custom = false; }
    if (clickTest != nullptr) { officialAddress = clickTest; officialKey.clear(); settings.custom = false; }
    if (!settings.custom) { settings.address = officialAddress; settings.key = officialKey; }
    char address[256], key[80], hostCode[32], joinCode[32], raceNumber[16];
    std::snprintf(address, sizeof(address), "%s", settings.address.c_str());
    std::snprintf(key, sizeof(key), "%s", settings.key.c_str());
    hostCode[0] = joinCode[0] = raceNumber[0] = '\0';
    HostPrefs prefs = LoadLocalRacePrefs();
    const int players = 8;   // a race always has seats for 8; the host starts it with whoever is there
    int laps = std::max(1, std::min(10, prefs.mLaps));
    bool weapons = prefs.mWeapons;
    int selectedRace = -1;
    // The name: asked once ("Welcome, racer"), remembered. The tests have no saved name and are not asked.
    char nameBuffer[40];
    nameBuffer[0] = '\0';
    {
        std::string saved = LoadUsername();
        if (saved.empty() && (testServer != nullptr || clickTest != nullptr)) saved = "Player";
        std::snprintf(nameBuffer, 33, "%s", saved.c_str());   // at most 32 bytes: the protocol's limit for a name
    }
    std::string name = nameBuffer;
    bool nameKnown = !name.empty();
    bool showCard = false;        // the technical card behind "Use another server"
    bool autoPending = true;      // connect by itself as soon as the name is known
    bool autoAttempt = false;     // an automatic attempt on the official server is under way: if it fails, the classic lobby is offered
    int connectingFrames = 0;
    AuthScreenResult result = AuthScreenResult::eBack;
    std::string hostedCode;              // the code of the private race this player is hosting (empty: a public race)
    std::string problem;                 // a mistake in what the player typed, shown under the button that found it
    bool ready = false, running = true;
    int framesShown = 0;
    // Test hook: with HOVERNET_TEST_AUTH_SERVER=host:port the screen hosts a one-player race by itself once connected; never set in normal play.
    int testStep = testServer != nullptr ? 2 : 0;

    // Test hook (scripts/test-online-screen-clicks.sh): real SDL mouse events are pushed at the on-screen position of the HOST A RACE button and
    // the dialog's create button, so the test proves they answer an actual click (HOVERNET_TEST_AUTH_CLICKS=host:port); never set in normal play.
    int clickStep = 0, clickFrame = 0, clickHold = 0;
    ImVec2 connectCenter(-1, -1), hostOpenCenter(-1, -1), createCenter(-1, -1), startRaceCenter(-1, -1);
    std::vector<TrackPreview> previews(static_cast<std::size_t>(Tracks().Count()));
    bool hostPopupOpen = false, requestHostPopup = false, privatePopupOpen = false, requestPrivatePopup = false;
    int lobbyTab = 0;                   // small screens: 0 games, 1 chat, 2 users
    char chatInput[128];
    chatInput[0] = '\0';
    std::size_t lastChatCount = 0;
    bool focusChat = false, chatFocusedOnce = false;
    std::uint32_t selectedRaceId = 0;   // the selected race, by number: the list refreshes by itself and its rows move
    Uint32 lastListRefresh = 0;
    auto pushClick = [](const ImVec2& at, int step) {
        SDL_Event e;
        SDL_memset(&e, 0, sizeof(e));
        if (step == 0) {
            e.type = SDL_MOUSEMOTION; e.motion.x = static_cast<Sint32>(at.x); e.motion.y = static_cast<Sint32>(at.y);
        }
        else {
            e.type = step == 1 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.button = SDL_BUTTON_LEFT; e.button.state = step == 1 ? SDL_PRESSED : SDL_RELEASED; e.button.clicks = 1;
            e.button.x = static_cast<Sint32>(at.x); e.button.y = static_cast<Sint32>(at.y);
        }
        SDL_PushEvent(&e);
    };

    auto connect = [&](bool custom) {
        std::string host;
        int port = 0;
        std::vector<std::uint8_t> serverKey;
        const std::string keyText(key);
#ifdef HOVERNET_WEB
        host = address;
        while (!host.empty() && host.back() == ' ') host.pop_back();
        while (!host.empty() && host.front() == ' ') host.erase(host.begin());
        if (host.compare(0, 6, "wss://") != 0 && host.compare(0, 5, "ws://") != 0) { problem = "Type the server as a wss:// address, for example wss://race.example.org/auth/."; return; }
#else
        if (!SplitServerAddress(address, host, port)) { problem = "Type the server as name:port, for example race.example.org:9610."; return; }
        if (!keyText.empty() && (!Crypto::FromHex(keyText, serverKey) || serverKey.size() != 32)) { problem = "The server key is 64 characters (0 to 9, a to f), or leave it empty."; return; }
#endif
        {
            std::string typed = nameBuffer;
            while (!typed.empty() && typed.back() == ' ') typed.pop_back();
            while (!typed.empty() && typed.front() == ' ') typed.erase(typed.begin());
            if (typed.empty() || !Protocol3::IsValidText(typed, 32)) { problem = "Type the name other players will see (1 to 32 letters, no control characters)."; return; }
            name = typed;
            SaveUsername(name);
        }
        problem.clear();
        settings.address = address;
        settings.key = keyText;
        settings.custom = custom;
        if (custom) SaveAuthServerSettings(settings);   // only another server is remembered; the official one needs no memory
        else if (!AuthServerSettingsPath().empty()) { AuthServerSettings official; SaveAuthServerSettings(official); }
        lobby.Connect(host, port, name, serverKey);
    };
    auto useOfficialServer = [&]() {
        std::snprintf(address, sizeof(address), "%s", officialAddress.c_str());
        std::snprintf(key, sizeof(key), "%s", officialKey.c_str());
    };

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
                g_QuitConfirmed = true;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (lobby.GetState() == OnlineLobby::State::eWaiting) lobby.Cancel();
                else if (showCard && lobby.GetState() != OnlineLobby::State::eBrowsing) running = false;
                else running = false;
            }
        }
        lobby.Pump();
        if (lobby.GetState() == OnlineLobby::State::eReady) { ready = true; break; }
        if (lobby.GetState() == OnlineLobby::State::eBrowsing && !chatFocusedOnce) { chatFocusedOnce = true; focusChat = true; }   // ready to type
        if (std::getenv("HOVERNET_TEST_AUTH_STARTCLICK") != nullptr && lobby.GetState() == OnlineLobby::State::eWaiting && lobby.IsHost()) {
            // Test hook (scripts/test-start-race.sh): wait for a second player, show that the race does not start by itself, then press
            // Start Race with real mouse events.
            static bool sAnnounced = false;
            static int sWaitFrames = 0;
            if (!sAnnounced) { sAnnounced = true; std::printf("STARTTEST waiting for a second player\n"); std::fflush(stdout); }
            if (lobby.GetRoster().members.size() >= 2) {
                ++sWaitFrames;
                if (sWaitFrames == 40) { std::printf("STARTTEST two players are in and the race has not started by itself\n"); std::fflush(stdout); }
                if (sWaitFrames >= 46 && startRaceCenter.x >= 0) {
                    const int phase = sWaitFrames - 46;
                    if (phase == 0) pushClick(startRaceCenter, 0);
                    else if (phase == 1) pushClick(startRaceCenter, 1);
                    else if (phase == 2) { pushClick(startRaceCenter, 2); std::printf("STARTTEST Start Race clicked\n"); std::fflush(stdout); }
                }
            }
        }
        if (std::getenv("HOVERNET_TEST_AUTH_AUTOSTART") != nullptr && lobby.GetState() == OnlineLobby::State::eWaiting && lobby.IsHost() && lobby.GetRoster().members.size() >= 2) lobby.StartRace();   // test hook: the host presses Start Race when someone has joined
        if (std::getenv("HOVERNET_TEST_AUTH_AUTOREFRESH") != nullptr && lobby.GetState() == OnlineLobby::State::eBrowsing) {
            // Test hook (scripts/test-lobby-autorefresh.sh): nothing is pressed; a race hosted by someone else must appear in the list by itself.
            static bool sAnnounced = false;
            if (!sAnnounced) { sAnnounced = true; std::printf("AUTOREFRESHTEST waiting for a race to appear\n"); std::fflush(stdout); }
            if (!lobby.Races().empty()) { std::printf("AUTOREFRESHTEST ok: a race hosted by someone else appeared on its own\n"); std::fflush(stdout); running = false; g_QuitConfirmed = true; }
        }
        // The race list keeps itself current: ask again every two seconds while browsing (the server limits how often, and this stays well under).
        if (lobby.GetState() == OnlineLobby::State::eBrowsing && SDL_GetTicks() - lastListRefresh > 2000) { lastListRefresh = SDL_GetTicks(); lobby.Refresh(); }
        if (std::getenv("HOVERNET_TEST_AUTH_LOBBYCHAT") != nullptr && lobby.GetState() == OnlineLobby::State::eBrowsing) {
            // Test hook (scripts/test-lobby-chat.sh): type a message with real key events and press Enter; it must come back from the server.
            static int sFrames = 0;
            ++sFrames;
            if (sFrames == 20) {
                SDL_Event text;
                SDL_memset(&text, 0, sizeof(text));
                text.type = SDL_TEXTINPUT;
                SDL_strlcpy(text.text.text, "hello lobby", sizeof(text.text.text));
                SDL_PushEvent(&text);
            }
            if (sFrames == 24 || sFrames == 26) {
                SDL_Event key;
                SDL_memset(&key, 0, sizeof(key));
                key.type = sFrames == 24 ? SDL_KEYDOWN : SDL_KEYUP; key.key.state = sFrames == 24 ? SDL_PRESSED : SDL_RELEASED;
                key.key.keysym.sym = SDLK_RETURN; key.key.keysym.scancode = SDL_SCANCODE_RETURN;
                SDL_PushEvent(&key);
            }
            bool heard = false;
            for (const std::string& line : lobby.ChatLines()) if (line == name + ": hello lobby") heard = true;
            if (heard) { std::printf("LOBBYCHATTEST ok: the message typed in the lobby came back from the server\n"); std::fflush(stdout); running = false; g_QuitConfirmed = true; }
            if (sFrames > 300) { std::printf("LOBBYCHATTEST FAIL: no echo (%zu chat lines)\n", lobby.ChatLines().size()); std::fflush(stdout); running = false; g_QuitConfirmed = true; }
        }
        // Connect by itself once the name is known: one frame shows "Connecting", then the connection is made.
        if (autoPending && nameKnown && !showCard) {
            if (++connectingFrames >= 2) {
                autoPending = false;
                if (settings.custom) { /* the saved custom server: its address and key are already in the fields */ }
                else useOfficialServer();
                autoAttempt = !settings.custom;
                connect(settings.custom);
                if (!problem.empty()) { showCard = true; autoAttempt = false; }
            }
        }
        // The official server could not be reached (or is not running): the caller offers the classic lobby. A custom server's failure is
        // shown on the "Use another server" card instead.
        if (autoAttempt && lobby.GetState() == OnlineLobby::State::eFailed) { result = AuthScreenResult::eFallback; running = false; break; }
        if (!autoAttempt && !showCard && nameKnown && lobby.GetState() == OnlineLobby::State::eFailed) showCard = true;
        if (clickTest != nullptr) {
            ++clickFrame;
            const OnlineLobby::State st = lobby.GetState();
            // click sequence: move, press, release on consecutive frames, aimed at the button's centre as drawn last frame
            auto click = [&](const ImVec2& at, int nextStep) {
                if (at.x < 0) return;
                if (clickHold == 0) pushClick(at, 0);
                else if (clickHold == 1) pushClick(at, 1);
                else if (clickHold == 2) { pushClick(at, 2); clickStep = nextStep; clickHold = -1; clickFrame = 0; }
                ++clickHold;
            };
            // Connected on its own (nothing typed); then HOST A RACE (which opens the dialog), then the dialog's create button: two real clicks.
            if (clickStep == 0 && st == OnlineLobby::State::eBrowsing && clickFrame > 8) { clickStep = 2; clickHold = 0; clickFrame = 0; std::printf("CLICKTEST connected by itself: no address was typed\n"); }
            else if (clickStep == 2 && clickFrame > 5) click(hostOpenCenter, 3);
            else if (clickStep == 3 && hostPopupOpen && clickFrame > 8) { clickStep = 4; clickHold = 0; clickFrame = 0; std::printf("CLICKTEST host dialog opened by a click\n"); }
            else if (clickStep == 4 && clickFrame > 5) click(createCenter, 5);
            else if (clickStep == 5 && st == OnlineLobby::State::eWaiting) { std::printf("CLICKTEST ok: Host clicked, the race is being set up\n"); std::fflush(stdout); running = false; g_QuitConfirmed = true; }
            if (clickFrame > 600) { std::printf("CLICKTEST FAIL at step %d (state %d)\n", clickStep, static_cast<int>(st)); std::fflush(stdout); running = false; g_QuitConfirmed = true; }
        }
        if (testStep == 2 && std::getenv("HOVERNET_TEST_AUTH_SPECTATE") != nullptr && lobby.GetState() == OnlineLobby::State::eBrowsing) {
            // Test hook: wait for a running race and watch it.
            static Uint32 sLastRefresh = 0;
            static bool sAnnounced = false;
            if (!sAnnounced) { sAnnounced = true; std::printf("SERVERSTEST waiting for a race to watch\n"); std::fflush(stdout); }
            if (SDL_GetTicks() - sLastRefresh > 400) {
                sLastRefresh = SDL_GetTicks();
                lobby.Refresh();
                if (std::getenv("HOVERNET_TEST_AUTH_WATCHQUEUE") != nullptr) {
                    // Watch Race on a race that has not started: the lobby waits and lets the browser in when the host starts it.
                    for (const auto& r : lobby.Races()) if (!r.started) {
                        lobby.WatchWhenStarted(r.id, ""); testStep = 3;
                        std::printf("SERVERSTEST queued a watch on a race that has not started\n"); std::fflush(stdout);
                        break;
                    }
                }
                else for (const auto& r : lobby.Races()) if (r.started) { lobby.Spectate(r.id, ""); testStep = 3; break; }
            }
        }
        if (testStep == 2 && std::getenv("HOVERNET_TEST_AUTH_POPUP") != nullptr && lobby.GetState() == OnlineLobby::State::eBrowsing) {
            // Test hook for screenshots: open the "Host a race" or the "Join a private race" dialog.
            static bool sOpened = false;
            if (!sOpened) { sOpened = true; (std::string(std::getenv("HOVERNET_TEST_AUTH_POPUP")) == "private" ? requestPrivatePopup : requestHostPopup) = true; }
        }
        if (testStep == 2 && std::getenv("HOVERNET_TEST_AUTH_BROWSE") == nullptr && std::getenv("HOVERNET_TEST_AUTH_SPECTATE") == nullptr && lobby.GetState() == OnlineLobby::State::eBrowsing) { lobby.Host(Tracks().Get(0) != nullptr ? Tracks().Get(0)->mName : "ClassicH", std::getenv("HOVERNET_TEST_AUTH_PLAYERS") != nullptr ? std::max(1, std::min(8, std::atoi(std::getenv("HOVERNET_TEST_AUTH_PLAYERS")))) : 1, 1, false, ""); testStep = 3; }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Servers", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);

        const OnlineLobby::State state = lobby.GetState();
        const bool waitingRoom = state == OnlineLobby::State::eWaiting;
        const bool connected = state == OnlineLobby::State::eBrowsing || waitingRoom;
        const bool narrow = mainViewport->WorkSize.x < 640.0f;
        // The same heading as the online lobby: a small eyebrow, the title, and the status beside it.
        // Players are told "the HoverNet server", never an address, unless they chose another server themselves.
        const std::string headerStatus = narrow ? std::string() : (connected ? (settings.custom ? "Connected to " + settings.address : std::string("Connected to the HoverNet server")) + " as " + name : std::string());
        HoverNetScreenHeading("ONLINE MULTIPLAYER", "HOVERNET LOBBY", headerStatus.c_str());
        ImGui::Spacing();

        auto infoRowLabelWidth = ImGui::CalcTextSize("Weapons:").x + 16.0f;
        auto infoRow = [&](const char* label, const std::string& value) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.69f, 1.0f));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::SameLine(infoRowLabelWidth);
            ImGui::TextWrapped("%s", value.c_str());
        };
        auto previewFor = [&](int trackIndex) -> TrackPreview& {
            TrackPreview& preview = previews[static_cast<std::size_t>(trackIndex)];
            if (!preview.mLoaded) preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(trackIndex).c_str());
            return preview;
        };
        auto centreOfLastItem = [&]() {
            return ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        };

        if (!connected) {
            const float cardWidth = std::min(560.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - cardWidth) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(10.0f, (ImGui::GetContentRegionAvail().y - 300.0f) * 0.36f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetSurface);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
            ImGui::BeginChild("EntryCard", ImVec2(cardWidth, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            if (!nameKnown) {
                // ---- the first time: only a name, as in the classic lobby ----
                HoverNetSectionHeading("WELCOME, RACER");
                ImGui::Spacing();
                ImGui::TextDisabled("Choose the name other drivers will see in the lobby.");
                ImGui::Spacing();
                ImGui::SetNextItemWidth(-1.0f);
                static bool focusName = true;
                if (focusName) { SDL_StartTextInput(); ImGui::SetKeyboardFocusHere(); focusName = false; }
                const bool submitted = ImGui::InputText("##yourname", nameBuffer, 33, ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::Spacing();
                const bool clicked = HoverNetButton("ENTER THE LOBBY", ImVec2(-FLT_MIN, 46.0f));
                std::string typed = nameBuffer;
                while (!typed.empty() && typed.back() == ' ') typed.pop_back();
                while (!typed.empty() && typed.front() == ' ') typed.erase(typed.begin());
                if ((submitted || clicked) && !typed.empty() && Protocol3::IsValidText(typed, 32)) {
                    name = typed;
                    SaveUsername(name);
                    nameKnown = true;
                }
                else if ((submitted || clicked)) AccentNote("Type a name (1 to 32 letters, no control characters).");
                ImGui::Spacing();
                if (HoverNetQuietButton("Back to Menu", ImVec2(-FLT_MIN, 0))) running = false;
            }
            else if (showCard) {
                // ---- a server chosen in Settings could not be reached: say so, and where to change it ----
                HoverNetSectionHeading("CAN'T REACH THE SERVER");
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("This is the HoverNet server you chose in Settings. Check it there, or go back to the official HoverNet server.");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (state == OnlineLobby::State::eFailed || (state == OnlineLobby::State::eIdle && !lobby.Message().empty())) AccentNote(lobby.Message());
                if (!problem.empty()) AccentNote(problem);
                ImGui::Spacing();
                const bool clicked = HoverNetButton("TRY AGAIN", ImVec2(-FLT_MIN, 46.0f));
                if (clicked) { showCard = false; autoPending = true; autoAttempt = false; connectingFrames = 0; problem.clear(); lobby.Close(); }
                ImGui::Spacing();
                if (HoverNetQuietButton("Use the official HoverNet server", ImVec2(-FLT_MIN, 0))) {
                    settings = AuthServerSettings();
                    AuthServerSettings official;
                    SaveAuthServerSettings(official);
                    showCard = false; autoPending = true; autoAttempt = false; connectingFrames = 0; problem.clear();
                    lobby.Close();
                }
                ImGui::Spacing();
                if (HoverNetQuietButton("Back to Menu", ImVec2(-FLT_MIN, 0))) running = false;
            }
            else {
                // ---- the normal way in: nothing to type ----
                HoverNetSectionHeading("CONNECTING");
                ImGui::Spacing();
                ImGui::TextDisabled("Looking for the HoverNet server...");
                ImGui::Spacing();
                if (HoverNetQuietButton("Back to Menu", ImVec2(-FLT_MIN, 0))) running = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else {
            // ---- the panel grid of the lobby: race list | race details | actions ----
            if (!waitingRoom) hostedCode.clear();   // set again below if this frame's Create press starts a private race
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float availWidth = ImGui::GetContentRegionAvail().x;
            // Small screens show one panel group at a time behind tabs, as the classic lobby does.
            const bool tabbed = availWidth < 640.0f || ImGui::GetContentRegionAvail().y < 560.0f;
            const bool stacked = availWidth < 640.0f;
            if (tabbed) {
                const char* tabNames[3] = {waitingRoom ? "RACE" : "GAMES", "CHAT", "USERS"};
                const int tabCount = waitingRoom ? 2 : 3;
                if (lobbyTab >= tabCount) lobbyTab = 0;
                const float tabWidth = (availWidth - spacing * (tabCount - 1)) / tabCount;
                for (int tab = 0; tab < tabCount; ++tab) {
                    if (tab > 0) ImGui::SameLine();
                    const bool active = lobbyTab == tab;
                    const bool clickedTab = active ? HoverNetButton(tabNames[tab], ImVec2(tabWidth, 40.0f)) : HoverNetQuietButton(tabNames[tab], ImVec2(tabWidth, 40.0f));
                    if (clickedTab) lobbyTab = tab;
                }
            }
            const float availHeight = ImGui::GetContentRegionAvail().y;
            // The top row needs about 360 px for a race and the seven actions; the chat keeps the rest (at least ~190 px).
            const float topHeight = tabbed ? availHeight : std::max(360.0f, std::min(availHeight * 0.58f, availHeight - 190.0f));
            const float stackGap = ImGui::GetStyle().ItemSpacing.y;
            const float listHeight = stacked ? (waitingRoom ? 0.0f : std::max(96.0f, availHeight * 0.26f)) : topHeight;
            const float detailsHeight = stacked ? std::max(150.0f, availHeight * (waitingRoom ? 0.55f : 0.38f)) : topHeight;
            const float actionsHeight = stacked ? std::max(150.0f, availHeight - listHeight - detailsHeight - stackGap * (waitingRoom ? 1.0f : 2.0f)) : topHeight;
            const float actionsWidth = std::max(232.0f, availWidth * 0.21f);
            const float listWidth = availWidth * 0.28f;
            const float detailsWidth = waitingRoom ? availWidth - actionsWidth - spacing : availWidth - listWidth - actionsWidth - 2.0f * spacing;
            const auto& races = lobby.Races();
            selectedRace = -1;
            for (int i = 0; i < static_cast<int>(races.size()); ++i) if (races[static_cast<std::size_t>(i)].id == selectedRaceId) selectedRace = i;
            if (selectedRace < 0 && !races.empty() && std::getenv("HOVERNET_TEST_AUTH_SELECT") != nullptr) { selectedRace = 0; selectedRaceId = races[0].id; }   // test hook for screenshots
            const Protocol3::RaceInfo* selectedInfo = (!waitingRoom && selectedRace >= 0) ? &races[static_cast<std::size_t>(selectedRace)] : nullptr;
            const bool canJoin = selectedInfo != nullptr && !selectedInfo->started && selectedInfo->joined < selectedInfo->players;

            if (!tabbed || lobbyTab == 0) {
            if (!waitingRoom) {
                ImGui::BeginChild("GameListPanel", ImVec2(stacked ? 0.0f : listWidth, listHeight), true);
                HoverNetSectionHeading("Game list");
                if (ImGui::BeginListBox("##races", ImVec2(-FLT_MIN, -FLT_MIN))) {
                    if (races.empty()) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("No open races - host one to get started");
                        ImGui::PopTextWrapPos();
                    }
                    for (int i = 0; i < static_cast<int>(races.size()); ++i) {
                        const Protocol3::RaceInfo& r = races[static_cast<std::size_t>(i)];
                        char label[200];
                        std::snprintf(label, sizeof(label), "%s  (%d/%d)%s", r.track.c_str(), r.joined, r.players, r.started ? "  (racing)" : "");
                        if (ImGui::Selectable(label, i == selectedRace, ImGuiSelectableFlags_AllowDoubleClick)) {
                            selectedRace = i;
                            selectedRaceId = r.id;
                            const Protocol3::RaceInfo& chosen = races[static_cast<std::size_t>(i)];
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !chosen.started && chosen.joined < chosen.players) lobby.Join(chosen.id, "");
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
            }

            ImGui::BeginChild("GameDetailsPanel", ImVec2(stacked ? 0.0f : detailsWidth, detailsHeight), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            HoverNetSectionHeading(waitingRoom ? "Your race" : "Game details");
            if (waitingRoom) {
                const Protocol3::HelloAck& ack = lobby.Ack();
                const Protocol3::Roster& roster = lobby.GetRoster();
                const int trackIndex = FindHostableTrack(ack.track);
                const TrackBriefing briefing = BriefingFor(trackIndex);
                // Race info and the map on the left, the player list on the right at full height, as in the classic waiting room.
                const float infoWidth = stacked ? 0.0f : std::max(250.0f, ImGui::GetContentRegionAvail().x * 0.50f);
                ImGui::BeginChild("RaceInfoColumn", stacked ? ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.55f) : ImVec2(infoWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
                infoRow("Track:", ack.track.empty() ? "-" : ack.track);
                if (!ack.track.empty()) {
                    infoRow("Laps:", briefing.mFreePlay ? "none (free play)" : std::to_string(ack.laps));
                    infoRow("Weapons:", ack.weapons ? "On" : "Off");
                    if (ack.raceId != 0) infoRow("Race:", "number " + std::to_string(ack.raceId));
                }
                if (ack.raceId != 0 && !hostedCode.empty()) {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
                    ImGui::TextWrapped("Private race: tell your friends number %u and code %s. They type both under \"Join a private race\".", ack.raceId, hostedCode.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                if (trackIndex >= 0) {
                    ImGui::BeginChild("RaceMapColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                    DrawTrackPreview(previewFor(trackIndex), 80.0f, false);
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
                ImGui::BeginChild("PlayersColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::Text("Players (%d / %d)", static_cast<int>(roster.members.size()), roster.seats != 0 ? static_cast<int>(roster.seats) : static_cast<int>(ack.players));
                ImGui::BeginChild("PlayersListBox", ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y), true);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 2.0f));
                for (const auto& member : roster.members) {
                    const bool isYou = member.first == ack.player;
                    ImGui::BulletText("%s%s%s", member.second.c_str(), isYou ? " (you)" : "", member.first == 0 ? " - host" : "");
                }
                if (roster.members.empty()) ImGui::BulletText("%s (you)%s", name.c_str(), lobby.IsHost() ? " - host" : "");
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::EndChild();
            }
            else if (selectedInfo != nullptr) {
                const int trackIndex = FindHostableTrack(selectedInfo->track);
                const TrackBriefing briefing = BriefingFor(trackIndex);
                const float mapWidth = std::min(190.0f, ImGui::GetContentRegionAvail().x * 0.36f);
                const float infoHeight = 178.0f;
                ImGui::BeginChild("SelectedRaceMap", ImVec2(mapWidth, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
                DrawTrackPreview(trackIndex >= 0 ? previewFor(trackIndex) : TrackPreview(), 96.0f, false);
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("SelectedRaceInfo", ImVec2(0, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
                infoRow("Track:", selectedInfo->track);
                infoRow("Laps:", briefing.mFreePlay ? "none (free play)" : std::to_string(selectedInfo->laps));
                infoRow("Weapons:", selectedInfo->weapons ? "On" : "Off");
                infoRow("Players:", std::to_string(selectedInfo->joined) + " / " + std::to_string(selectedInfo->players));
                if (trackIndex >= 0) infoRow("Style:", briefing.mCharacter + " - " + briefing.mDifficulty);
                infoRow("Status:", selectedInfo->started ? "Race in progress" : "Waiting for players");
                infoRow("Race:", "number " + std::to_string(selectedInfo->id));
                ImGui::PopStyleVar();
                ImGui::EndChild();
                if (trackIndex >= 0 && !briefing.mDescription.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.69f, 1.0f));
                    ImGui::TextWrapped("%s", briefing.mDescription.c_str());
                    ImGui::PopStyleColor();
                }
            }
            else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Select a race in the list to see its track, laps, weapons and players, or host your own.");
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();

            if (!stacked) ImGui::SameLine();
            ImGui::BeginChild("ActionsPanel", ImVec2(0, actionsHeight), true);
            if (waitingRoom) {
                const bool alone = lobby.GetRoster().members.size() < 2;
                if (lobby.IsHost()) {
                    // The one primary action: the only filled red button on the screen. The race starts when the host presses it, with whoever is
                    // there, even nobody else (to practise).
                    if (HoverNetButton("Start Race", ImVec2(-FLT_MIN, 40))) lobby.StartRace();
                    startRaceCenter = centreOfLastItem();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled(alone ? "Others can still join until you start." : "Start the race when you are ready.");
                    ImGui::PopTextWrapPos();
                }
                else {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("Waiting for the host to start the race...");
                    ImGui::PopTextWrapPos();
                }
#ifdef HOVERNET_WEB
                // A browser stops running a tab that is not in front (including its network handling), so a race that starts while the player
                // is looking at another tab starts without them.
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Keep this tab in front: your browser pauses a tab you cannot see.");
                ImGui::PopTextWrapPos();
#endif
                ImGui::Spacing();
                if (HoverNetQuietButton("Leave Race", ImVec2(-FLT_MIN, 0))) lobby.Cancel();
            }
            else {
                ImGui::BeginDisabled(!canJoin);
                if (HoverNetButton("Join Game", ImVec2(-FLT_MIN, 40))) lobby.Join(selectedInfo->id, "");
                ImGui::EndDisabled();
                // A race that has not started can be chosen too: the client waits in the list and starts watching when the race starts.
                const bool waitingToWatch = lobby.QueuedWatchId() != 0;
                ImGui::BeginDisabled(selectedInfo == nullptr && !waitingToWatch);
                if (waitingToWatch) {
                    if (HoverNetQuietButton("Waiting for the race to start (click to cancel)", ImVec2(-FLT_MIN, 0))) lobby.CancelQueuedWatch();
                }
                else if (HoverNetQuietButton("Watch Race", ImVec2(-FLT_MIN, 0))) lobby.WatchWhenStarted(selectedInfo->id, "");
                ImGui::EndDisabled();
                ImGui::Spacing();
                if (HoverNetButton("HOST A RACE", ImVec2(-FLT_MIN, 44.0f))) requestHostPopup = true;
                hostOpenCenter = centreOfLastItem();
                if (HoverNetQuietButton("Join a private race", ImVec2(-FLT_MIN, 0))) requestPrivatePopup = true;
            }
            if (!lobby.Notice().empty() || !problem.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
                if (!lobby.Notice().empty()) ImGui::TextWrapped("%s", lobby.Notice().c_str());
                if (!problem.empty()) ImGui::TextWrapped("%s", problem.c_str());
                ImGui::PopStyleColor();
            }
            // Leaving sits apart at the bottom so it is not hit by mistake.
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), actionsHeight - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y));
            if (HoverNetQuietButton("Back to Menu", ImVec2(-FLT_MIN, 0))) { lobby.Close(); running = false; }
            ImGui::EndChild();
            }

            // ---- the bottom row: who is here, and the chat, as in the classic lobby ----
            if (!waitingRoom && (!tabbed || lobbyTab == 2)) {
                ImGui::BeginChild("UsersListPanel", ImVec2(tabbed ? 0.0f : listWidth, 0), true);
                char heading[48];
                std::snprintf(heading, sizeof(heading), "In the lobby (%d)", lobby.UsersTotal() > 0 ? lobby.UsersTotal() : 1);
                HoverNetSectionHeading(heading);
                ImGui::BeginChild("UsersListBox", ImVec2(-FLT_MIN, -FLT_MIN), true);
                bool listedYou = false;
                for (const std::string& user : lobby.Users()) {
                    if (!listedYou && user == name) { ImGui::BulletText("%s (you)", user.c_str()); listedYou = true; }
                    else ImGui::BulletText("%s", user.c_str());
                }
                if (lobby.Users().empty()) ImGui::BulletText("%s (you)", name.c_str());
                if (lobby.UsersTotal() > static_cast<int>(lobby.Users().size())) ImGui::TextDisabled("+ %d more", lobby.UsersTotal() - static_cast<int>(lobby.Users().size()));
                ImGui::EndChild();
                ImGui::EndChild();
                if (!tabbed) ImGui::SameLine();
            }
            if (!tabbed || lobbyTab == 1) {
                ImGui::BeginChild("ChatPanel", ImVec2(0, 0), true);
                HoverNetSectionHeading(waitingRoom ? "Race chat" : "Lobby chat");
                const float inputRow = ImGui::GetFrameHeightWithSpacing() + 4.0f;
                ImGui::BeginChild("ChatLog", ImVec2(0, std::max(40.0f, ImGui::GetContentRegionAvail().y - inputRow)), true);
                for (const std::string& line : lobby.ChatLines()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (lobby.ChatLines().size() != lastChatCount) { ImGui::SetScrollHereY(1.0f); lastChatCount = lobby.ChatLines().size(); }
                ImGui::EndChild();
                ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 84.0f));
                if (focusChat) { SDL_StartTextInput(); ImGui::SetKeyboardFocusHere(); focusChat = false; }
                bool sendChat = ImGui::InputTextWithHint("##lobbychat", "Say something...", chatInput, sizeof(chatInput), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                sendChat = HoverNetButton("Send", ImVec2(-FLT_MIN, 0)) || sendChat;
                if (sendChat && chatInput[0] != '\0') { lobby.SendChat(chatInput); chatInput[0] = '\0'; focusChat = true; }
                ImGui::EndChild();
            }
        }

        // Popups are opened at the window's own scope (the same ID-stack depth as BeginPopupModal), as in the lobby.
        if (requestHostPopup) { hostPopupOpen = true; ImGui::OpenPopup("HostRace"); requestHostPopup = false; problem.clear(); }
        if (requestPrivatePopup) { privatePopupOpen = true; ImGui::OpenPopup("PrivateRace"); requestPrivatePopup = false; problem.clear(); }
        const ImGuiViewport* popupViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(std::min(860.0f, popupViewport->WorkSize.x - 40.0f), std::min(690.0f, popupViewport->WorkSize.y - 30.0f)), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("HostRace", &hostPopupOpen)) {
            HoverNetSectionHeading("Host a race");
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float selectorHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y;
            const bool sideBySide = contentWidth >= 610.0f;
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float controlsWidth = sideBySide ? contentWidth * 0.52f : contentWidth;
            ImGui::BeginChild("HostRaceOptions", ImVec2(controlsWidth, sideBySide ? selectorHeight : 330.0f), false);
            ImGui::TextUnformatted("Track");
            ImGui::SetNextItemWidth(-1.0f);
            TrackSelector("##HostTrack", 2, prefs.mTrackIndex);
            const TrackBriefing guide = BriefingFor(prefs.mTrackIndex);
            const TrackEntry* hostTrack = Tracks().Get(prefs.mTrackIndex);
            ImGui::Spacing();
            HoverNetSectionHeading("Course briefing");
            ImGui::Text("Style: %s", guide.mCharacter.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("  Difficulty: %s", guide.mDifficulty.c_str());
            ImGui::TextWrapped("%s", guide.mDescription.c_str());
            const bool trackOk = hostTrack != nullptr && hostTrack->mOfficial;
            if (!trackOk) AccentNote("Servers host the official tracks unless their owner added others: pick one of the first seven.");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Up to 8 players can join. You start the race when you are ready, with whoever is there (alone, it is a time trial).");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::BeginDisabled(guide.mFreePlay);
            ImGui::TextUnformatted("Laps");
            ImGui::SameLine();
            ImGui::TextDisabled(guide.mFreePlay ? "Not used on a free-play track" : "Suggested: %d", guide.mSuggestedLaps);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("##HostLaps", &laps, 1, 10);
            ImGui::EndDisabled();
            ImGui::Checkbox("Weapons enabled", &weapons);
            ImGui::TextUnformatted("Private code");
            ImGui::SameLine();
            ImGui::TextDisabled("6 to 16 characters; empty for a public race");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##hostcode", "e.g. kestrel7", hostCode, sizeof(hostCode));
            const bool codeOk = PrivateCodeOk(hostCode);
            if (!codeOk) AccentNote("A private code needs 6 to 16 characters.");
            ImGui::EndChild();
            if (sideBySide) {
                ImGui::SameLine(0.0f, gap);
                ImGui::BeginChild("HostTrackPreview", ImVec2(0, selectorHeight), false);
                DrawTrackPreview(previewFor(prefs.mTrackIndex));
                ImGui::EndChild();
            }
            else {
                ImGui::Spacing();
                ImGui::BeginChild("HostTrackPreview", ImVec2(0, 190.0f), false);
                DrawTrackPreview(previewFor(prefs.mTrackIndex));
                ImGui::EndChild();
            }
            ImGui::BeginDisabled(!codeOk || !trackOk);
            if (HoverNetButton(hostCode[0] != '\0' ? "Create Private Race" : "Create Race", ImVec2(190, 36))) {
                lobby.Host(hostTrack->mName, players, laps, weapons, hostCode);
                hostedCode = hostCode;
                prefs.mLaps = laps;
                prefs.mWeapons = weapons;
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            createCenter = centreOfLastItem();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (HoverNetButton("Cancel", ImVec2(120, 36))) { hostPopupOpen = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, popupViewport->WorkSize.x - 40.0f), 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("PrivateRace", &privatePopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            HoverNetSectionHeading("Join a private race");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Private races are not listed. Type the race number and the code your friend gave you.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::TextUnformatted("Race number");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##racenumber", "number", raceNumber, sizeof(raceNumber), ImGuiInputTextFlags_CharsDecimal);
            ImGui::TextUnformatted("Code");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##joincode", "code", joinCode, sizeof(joinCode));
            ImGui::Spacing();
            const std::uint32_t number = static_cast<std::uint32_t>(std::strtoul(raceNumber, nullptr, 10));
            ImGui::BeginDisabled(number == 0 || joinCode[0] == '\0');
            if (HoverNetButton("Join", ImVec2(120, 36))) { lobby.Join(number, joinCode); privatePopupOpen = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (HoverNetQuietButton("Watch", ImVec2(120, 36))) { lobby.Spectate(number, joinCode); privatePopupOpen = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (HoverNetQuietButton("Cancel", ImVec2(120, 36))) { privatePopupOpen = false; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit, "HOVERNET_CAPTURE_SERVERS");
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (!ready) lobby.Close();
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    if (ready) return AuthScreenResult::eRace;
    return result;
}
#endif

enum class MenuChoice
{
    eLocalPlay,
    eOnlineLobby,
    eWatchReplay,
    eLeaderboards,
    eSettings,
    eControls,
    eHowToPlay,
};

// The first player screen. A bounded/headless run still defaults to Local Play
// if no button is activated, preserving the automated gameplay contract.
MenuChoice RunMainMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                       const MR_Sprite& font, int pFrameLimit)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadHoverNetMenuFonts(io);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    SDL_Texture* menuHovercraftTexture = nullptr;
    const std::string menuHovercraftPath =
        SourcePath("NetTarget/LinuxClient/assets/menu-hovercraft.bmp");
    if (SDL_Surface* menuHovercraftSurface = SDL_LoadBMP(menuHovercraftPath.c_str())) {
        SDL_SetSurfaceBlendMode(menuHovercraftSurface, SDL_BLENDMODE_BLEND);
        menuHovercraftTexture =
            SDL_CreateTextureFromSurface(graphics.GetRenderer(), menuHovercraftSurface);
        SDL_FreeSurface(menuHovercraftSurface);
        if (menuHovercraftTexture != nullptr) {
            SDL_SetTextureBlendMode(menuHovercraftTexture, SDL_BLENDMODE_BLEND);
        }
    }

    const char* options[] = {"Local Play", "Online Lobby", "Watch a Replay", "Leaderboards", "Settings", "Controls", "How to Play"};
    constexpr int optionCount = sizeof(options) / sizeof(options[0]);
    MenuChoice choice = MenuChoice::eLocalPlay;
#if defined(HOVERNET_AUTH)
    // Test hook: a bounded run picks ONLINE MULTIPLAYER (scripts/test-online-menu.sh); never set in normal play.
    const char* testMenu = std::getenv("HOVERNET_TEST_MENU");
    static bool sPickedForTest = false;   // once: after the screen is left without a race, a bounded run falls back to Local Play
    const bool pickAuthServerForTest = !sPickedForTest && (pFrameLimit >= 0 || std::getenv("HOVERNET_TEST_MENU_FORCE") != nullptr) && testMenu != nullptr && std::string(testMenu) == "authserver";
    if (pickAuthServerForTest) { choice = MenuChoice::eOnlineLobby; sPickedForTest = true; }
#else
    const bool pickAuthServerForTest = false;
#endif
    // Test hook: a bounded run opens Watch a Replay (scripts/test-replay-library.sh); never set in normal play.
    static bool sPickedReplays = false;
    static int sMenuCalls = 0;           // HOVERNET_TEST_MENU_AFTER=n: the first n times the menu is shown it behaves normally
    ++sMenuCalls;
    bool pickReplaysForTest = false;
    if (const char* replaysMenu = std::getenv("HOVERNET_TEST_MENU")) {
        if (!sPickedReplays && sMenuCalls > (std::getenv("HOVERNET_TEST_MENU_AFTER") ? std::atoi(std::getenv("HOVERNET_TEST_MENU_AFTER")) : 0) && (std::string(replaysMenu) == "replays" || std::string(replaysMenu) == "leaderboards") && (pFrameLimit >= 0 || std::getenv("HOVERNET_TEST_MENU_FORCE") != nullptr)) {
            choice = std::string(replaysMenu) == "leaderboards" ? MenuChoice::eLeaderboards : MenuChoice::eWatchReplay; sPickedReplays = true; pickReplaysForTest = true;
        }
    }
    bool focusFirstButton = true;
    bool running = !pickAuthServerForTest && !pickReplaysForTest;
    int framesShown = 0;
    const bool reducedMotion = LoadReducedMotion();
    // --frames bounds gameplay, not time spent idling at a menu with no human
    // input. One rendered frame verifies this screen in automation, then the
    // documented Local Play default continues into the actual bounded race.
    const int menuFrameLimit = pFrameLimit < 0 ? -1 : 1;

    while (running && (menuFrameLimit < 0 || framesShown < menuFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    g_QuitConfirmed = true;
                    running = false;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                running = false;
            }
        }

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Main Menu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

        const ImVec2 workPos = mainViewport->WorkPos;
        const ImVec2 workSize = mainViewport->WorkSize;
        ImDrawList* background = ImGui::GetWindowDrawList();
        const ImVec2 workEnd(workPos.x + workSize.x, workPos.y + workSize.y);

        // A lightweight animated race scene built from ImGui primitives keeps the
        // menu fast and resolution-independent without pretending it is a desktop
        // form. The vanishing grid remains static; speed streaks move unless the
        // player's Reduced motion accessibility setting freezes them.
        background->AddRectFilledMultiColor(
            workPos, workEnd,
            IM_COL32(13, 13, 19, 255), IM_COL32(25, 10, 18, 255),
            IM_COL32(7, 7, 11, 255), IM_COL32(12, 6, 10, 255));
        const float horizonY = workPos.y + workSize.y * 0.54f;
        const ImVec2 vanishingPoint(workPos.x + workSize.x * 0.35f, horizonY);
        background->AddRectFilled(
            ImVec2(workPos.x, horizonY), workEnd, IM_COL32(9, 9, 14, 235));
        for (int line = -8; line <= 12; ++line) {
            const float bottomX = workPos.x + workSize.x * (static_cast<float>(line) / 10.0f);
            background->AddLine(vanishingPoint, ImVec2(bottomX, workEnd.y),
                                IM_COL32(202, 32, 57, 72), 1.5f);
        }
        const float gridRows[] = {0.04f, 0.10f, 0.19f, 0.31f, 0.47f, 0.67f, 0.88f};
        for (float row : gridRows) {
            const float y = horizonY + (workEnd.y - horizonY) * row;
            background->AddLine(ImVec2(workPos.x, y), ImVec2(workEnd.x, y),
                                IM_COL32(240, 53, 75, 52), 1.0f);
        }
        const Uint32 animationTick = reducedMotion ? 0 : SDL_GetTicks() / 8;
        for (int streak = 0; streak < 18; ++streak) {
            const float x = workPos.x + static_cast<float>((streak * 193 + animationTick) %
                static_cast<Uint32>(std::max(1.0f, workSize.x)));
            const float y = workPos.y + 125.0f + static_cast<float>((streak * 47) %
                static_cast<int>(std::max(1.0f, workSize.y * 0.38f)));
            const float length = 35.0f + static_cast<float>((streak * 17) % 90);
            background->AddLine(ImVec2(x, y), ImVec2(x + length, y - 8.0f),
                                IM_COL32(255, 91, 108, 70), 2.0f);
        }

        // Phones: portrait puts the menu panel across the bottom with the craft above it; short
        // landscape keeps two columns but with a compact, full-height panel.
        const bool phonePortrait = workSize.x < 640.0f;
        const bool phoneShort = !phonePortrait && workSize.y < 460.0f;
        const bool phone = phonePortrait || phoneShort;
        const float panelWidth = phonePortrait ? workSize.x - 32.0f
                               : phoneShort    ? std::min(380.0f, workSize.x * 0.46f)
                                               : std::min(430.0f, workSize.x * 0.42f);
        const float panelHeight = phonePortrait ? std::min(workSize.y * 0.62f, 430.0f)
                                : phoneShort    ? workSize.y - 24.0f
                                                : std::min(580.0f, workSize.y - 84.0f);
        const ImVec2 panelPos = phonePortrait ? ImVec2(16.0f, workSize.y - panelHeight - 16.0f)
                              : phoneShort    ? ImVec2(workSize.x - panelWidth - 12.0f, 12.0f)
                                              : ImVec2(workSize.x - panelWidth - 42.0f, 42.0f);
        const float craftX = workPos.x + workSize.x * 0.30f;
        const float craftY = workPos.y + workSize.y * 0.68f;
        const float craftScale = std::min(workSize.x / 1200.0f, workSize.y / 760.0f);
        if (menuHovercraftTexture != nullptr) {
            // This hero render is derived from HoverNet's original round-skirt
            // craft: oversized turbines and a visible pilot give the menu a
            // stronger arcade-racing identity without under-body lighting.
            float availableWidth = std::max(220.0f, workSize.x - panelWidth - 66.0f);
            float craftSize = std::min(availableWidth, workSize.y * 0.72f);
            float imageX = workPos.x + std::max(8.0f, (availableWidth - craftSize) * 0.5f);
            float imageY = workPos.y + std::max(155.0f, workSize.y - craftSize - 18.0f);
            if (phonePortrait) {
                // Centred in the gap between the title block and the panel.
                const float gapTop = 112.0f, gapBottom = panelPos.y - 6.0f;
                craftSize = std::max(100.0f, std::min(workSize.x * 0.78f, gapBottom - gapTop));
                imageX = workPos.x + (workSize.x - craftSize) * 0.5f;
                imageY = workPos.y + gapTop + std::max(0.0f, (gapBottom - gapTop - craftSize) * 0.5f);
            }
            else if (phoneShort) {
                availableWidth = std::max(120.0f, workSize.x - panelWidth - 36.0f);
                craftSize = std::min(availableWidth, workSize.y * 0.64f);
                imageX = workPos.x + std::max(8.0f, (availableWidth - craftSize) * 0.5f);
                imageY = workPos.y + std::max(112.0f, workSize.y - craftSize - 8.0f);
            }
            background->AddImage(
                reinterpret_cast<ImTextureID>(menuHovercraftTexture),
                ImVec2(imageX, imageY), ImVec2(imageX + craftSize, imageY + craftSize));
        }
        else {
            // Keep the menu usable in development if its packaged artwork is
            // missing by falling back to the original geometric silhouette.
            ImVec2 craftBody[] = {
                ImVec2(craftX - 180*craftScale, craftY + 28*craftScale),
                ImVec2(craftX - 92*craftScale, craftY - 38*craftScale),
                ImVec2(craftX + 72*craftScale, craftY - 52*craftScale),
                ImVec2(craftX + 190*craftScale, craftY + 18*craftScale),
                ImVec2(craftX + 105*craftScale, craftY + 62*craftScale),
                ImVec2(craftX - 122*craftScale, craftY + 70*craftScale),
            };
            background->AddConvexPolyFilled(craftBody, 6, IM_COL32(218, 36, 63, 245));
            background->AddTriangleFilled(
                ImVec2(craftX - 52*craftScale, craftY - 42*craftScale),
                ImVec2(craftX + 22*craftScale, craftY - 112*craftScale),
                ImVec2(craftX + 74*craftScale, craftY - 48*craftScale),
                IM_COL32(245, 224, 226, 235));
            background->AddCircleFilled(ImVec2(craftX - 118*craftScale, craftY + 66*craftScale),
                                        43*craftScale, IM_COL32(35, 35, 42, 255));
            background->AddCircleFilled(ImVec2(craftX + 112*craftScale, craftY + 58*craftScale),
                                        43*craftScale, IM_COL32(35, 35, 42, 255));
        }

        ImGui::SetCursorPos(phone ? ImVec2(20.0f, 16.0f) : ImVec2(48.0f, 48.0f));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("HOVERNET");
        ImGui::SetWindowFontScale(1.0f);
        if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
        ImGui::TextColored(kHoverNetCoral, "HIGH-SPEED COMBAT RACING");
        ImGui::Spacing();
        ImGui::TextDisabled("RACE THE LINE.  OWN THE TRACK.");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::SetCursorPos(panelPos);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.94f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phone ? ImVec2(14.0f, 12.0f) : ImVec2(22.0f, 20.0f));
        ImGui::BeginChild("MainMenuPanel", ImVec2(panelWidth, panelHeight), true, phone ? 0 : ImGuiWindowFlags_NoScrollbar);   // a phone in landscape cannot show every button: the panel scrolls
        ImGui::TextColored(kHoverNetCoral, "READY TO RACE?");
        if (!phone) ImGui::TextDisabled("Choose your mode");
        if (!phone) ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        const float primaryHeight = phonePortrait ? 54.0f : phoneShort ? 42.0f : 62.0f;
        const float secondaryHeight = phonePortrait ? 42.0f : phoneShort ? 34.0f : 42.0f;
        if (HoverNetButton("LOCAL RACE", ImVec2(-FLT_MIN, primaryHeight))) {
            choice = MenuChoice::eLocalPlay;
            running = false;
        }
        ImGui::Spacing();
        if (HoverNetButton("ONLINE MULTIPLAYER", ImVec2(-FLT_MIN, primaryHeight))) {
            choice = MenuChoice::eOnlineLobby;
            running = false;
        }
        ImGui::PopStyleVar();

        ImGui::Spacing();
        if (!phoneShort) {
            ImGui::TextDisabled("GARAGE & SUPPORT");
            ImGui::Separator();
            ImGui::Spacing();
        }
        for (int index = 2; index < optionCount; ++index) {
            if (HoverNetButton(options[index], ImVec2(-FLT_MIN, secondaryHeight))) {
                choice = static_cast<MenuChoice>(index);
                running = false;
            }
            ImGui::Spacing();
        }
        if (!phone) {
            ImGui::PushTextWrapPos(0.0f);
            HoverNetHint("Navigate with arrow keys or D-pad. Select with Enter or A.");
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        if (!phonePortrait) {
            ImGui::SetCursorPos(ImVec2(22.0f, workSize.y - 30.0f));
            // HOVERNET_VERSION already starts with "v".
            ImGui::TextDisabled("%s%s", HOVERNET_VERSION[0] == 'v' ? "" : "v", HOVERNET_VERSION);
            if (!phone) { ImGui::SameLine(); ImGui::TextDisabled("  |   ESC / B: quit"); }
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (menuHovercraftTexture != nullptr) {
        SDL_DestroyTexture(menuHovercraftTexture);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    return choice;
}
#endif

#ifndef HOVERNET_GAME2_PLAYER
// The diagnostic viewer shares the input loop but not the player settings UI.
// Keep its historical fixed keyboard layout without creating per-user config.
struct KeyboardBindings
{
    SDL_Scancode accelerate = SDL_SCANCODE_LSHIFT;
    SDL_Scancode brake = SDL_SCANCODE_DOWN;
    SDL_Scancode steerLeft = SDL_SCANCODE_LEFT;
    SDL_Scancode steerRight = SDL_SCANCODE_RIGHT;
    SDL_Scancode jump = SDL_SCANCODE_UP;
    SDL_Scancode fire = SDL_SCANCODE_LCTRL;
    SDL_Scancode selectWeapon = SDL_SCANCODE_TAB;
};
KeyboardBindings gKeyboardBindings;
struct ControllerBindings
{
    SDL_GameControllerButton jump = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton fire = SDL_CONTROLLER_BUTTON_X;
    SDL_GameControllerButton selectWeapon = SDL_CONTROLLER_BUTTON_Y;
    SDL_GameControllerAxis steerAxis = SDL_CONTROLLER_AXIS_LEFTX;
    SDL_GameControllerAxis accelerateAxis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    SDL_GameControllerAxis brakeAxis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    bool invertSteering = false;
    bool invertAccelerate = false;
    bool invertBrake = false;
    int deadzone = 8000;
};
ControllerBindings gControllerBindings;
#endif

RenderStats RenderScene(const MR_Level& level, int room, const MR_3DCoordinate& camera,
                        MR_Angle orientation, MR_3DViewPort& viewport, MR_ResourceLib& resources,
                        MR_SimulationTime simulationTime, MR_MainCharacter* mainCharacter = nullptr)
{
    viewport.SetupCameraPosition(camera, orientation, 0);
    viewport.Clear(0);
    viewport.ClearZ();

    RenderStats stats = {0, 0};
    const int visibleSurfaceCount = level.GetNbVisibleSurface(room);
    const MR_SectionId* floorSections = level.GetVisibleFloorList(room);
    const MR_SectionId* ceilingSections = level.GetVisibleCeilingList(room);
    for (int surface = 0; surface < visibleSurfaceCount; ++surface) {
        stats.surfacesRendered += RenderFloorOrCeiling(level, floorSections[surface], true, viewport, resources);
        stats.surfacesRendered += RenderFloorOrCeiling(level, ceilingSections[surface], false, viewport, resources);
    }

    int visibleRoomCount = 0;
    const int* visibleRooms = level.GetVisibleZones(room, visibleRoomCount);
    for (int visibleIndex = -1; visibleIndex < visibleRoomCount; ++visibleIndex) {
        const int visibleRoom = visibleIndex == -1 ? room : visibleRooms[visibleIndex];
        const int featureCount = level.GetFeatureCount(visibleRoom);
        for (int feature = 0; feature < featureCount; ++feature) {
            stats.surfacesRendered += RenderFeatureWalls(level, level.GetFeature(visibleRoom, feature), viewport, resources);
        }
        stats.surfacesRendered += RenderRoomWalls(level, visibleRoom, viewport, resources);
        stats.actorsRendered += RenderFreeElements(level, visibleRoom, viewport, resources, simulationTime);
    }

    if (mainCharacter != nullptr && mainCharacter->mRoom == room) {
        mainCharacter->Render(&viewport, simulationTime);
        ++stats.actorsRendered;
    }

    return stats;
}

class GameControllerManager
{
public:
    GameControllerManager() : mController(nullptr), mInitialized(false) {}

    ~GameControllerManager()
    {
        if (mController != nullptr) {
            SDL_GameControllerClose(mController);
        }
        if (mInitialized) {
            SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        }
    }

    void Initialize()
    {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
            mInitialized = true;
            SDL_GameControllerEventState(SDL_ENABLE);
            Refresh();
        }
    }

    void Refresh()
    {
        if (!mInitialized) return;
        if (mController != nullptr && !SDL_GameControllerGetAttached(mController)) {
            SDL_GameControllerClose(mController);
            mController = nullptr;
        }
        if (mController != nullptr) return;
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index)) {
                mController = SDL_GameControllerOpen(index);
                if (mController != nullptr) return;
            }
        }
    }

    bool Button(SDL_GameControllerButton button) const
    {
        return mController != nullptr && SDL_GameControllerGetButton(mController, button) != 0;
    }

    Sint16 Axis(SDL_GameControllerAxis axis) const
    {
        return mController != nullptr ? SDL_GameControllerGetAxis(mController, axis) : 0;
    }

private:
    SDL_GameController* mController;
    bool mInitialized;
};

int FatalClientError(const std::string& message)
{
    std::fprintf(stderr, "%s\n", message.c_str());
#ifdef _WIN32
    const char* videoDriver = std::getenv("SDL_VIDEODRIVER");
    if (videoDriver == nullptr || std::strcmp(videoDriver, "dummy") != 0) {
        MessageBoxA(nullptr, message.c_str(), "HoverNet could not continue",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
#endif
    return 1;
}

bool IsPlayerMode(int argc, char** argv)
{
#ifdef HOVERNET_GAME2_PLAYER
    return true;
#else
    return HasArgument(argc, argv, "--play");
#endif
}
}

#ifdef HOVERNET_GAME2_PLAYER
// Downloads one community track now, if it is listed, not installed, and
// downloadable (used by --track NAME so a missing track just works). Returns
// false only if a download was needed and failed.
bool EnsureTrackInstalledBlocking(const std::string& trackName)
{
    const TrackEntry* entry = Tracks().Get(Tracks().Find(trackName));
    if (entry == nullptr || entry->mInstalled || !entry->mHasDownload) return true;
    std::printf("Downloading community track '%s' (%s)...\n", entry->mName.c_str(),
                FormatMegabytes(entry->mDownloadBytes).c_str());
    std::fflush(stdout);
    TrackDownloadJob job(CommunityDownloadSettings(), {*entry});
    job.RunBlocking();
    Tracks().Refresh();
    if (!job.Succeeded()) {
        std::fprintf(stderr, "Download failed: %s\n", job.Error().c_str());
        return false;
    }
    return true;
}

// "hovernet --download-community-tracks --all" or "... NAME [NAME...]": fetches
// community tracks without opening the game, for scripts and for players who
// prefer a terminal. Prints progress; exits 0 only if everything requested is
// installed afterwards.
int RunDownloadCommand(int argc, char** argv)
{
    std::vector<TrackEntry> wanted;
    bool all = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--download-community-tracks") continue;
        if (argument == "--all") { all = true; continue; }
        if (argument.compare(0, 2, "--") == 0) continue;
        const int found = Tracks().Find(argument);
        const TrackEntry* entry = Tracks().Get(found);
        if (entry == nullptr) {
            std::fprintf(stderr, "Unknown track '%s'\n", argument.c_str());
            return 1;
        }
        if (!entry->mInstalled) wanted.push_back(*entry);
    }
    if (all) wanted = Tracks().MissingDownloads();
    if (wanted.empty()) {
        std::printf("Nothing to download: the requested tracks are already installed.\n");
        return 0;
    }

    long long totalBytes = 0;
    for (const TrackEntry& entry : wanted) totalBytes += entry.mDownloadBytes;
    std::printf("Downloading %zu community track(s), %s...\n", wanted.size(), FormatMegabytes(totalBytes).c_str());
    std::fflush(stdout);

    TrackDownloadJob job(CommunityDownloadSettings(), wanted);
    job.Start();
    int reported = 0;
    while (!job.Finished()) {
        SDL_Delay(100);
        while (reported < job.CompletedTracks()) {
            ++reported;
            if (wanted.size() <= 20 || reported % 50 == 0 || reported == static_cast<int>(wanted.size())) {
                std::printf("  %d/%zu installed\n", reported, wanted.size());
                std::fflush(stdout);
            }
        }
    }
    Tracks().Refresh();
    if (!job.Succeeded()) {
        std::fprintf(stderr, "Download failed: %s\n", job.Error().c_str());
        return 1;
    }
    std::printf("Done: %d track(s) installed in %s\n", job.CompletedTracks(),
                CommunityDownloadSettings().mDestination.c_str());
    return 0;
}
#endif

#ifdef HOVERNET_GAME2_PLAYER
// Replay playback (docs/specs/replay-format.md): the race is re-simulated from the recorded inputs, tick by tick, with the
// ordinary renderer, so a replay looks exactly as the race did. The watched player's craft is the session's main craft.
struct ReplayPlayback
{
    ReplayFormat::Replay data;
    bool active = false;
    bool hashMode = false;       // test mode: fixed number of ticks per frame, print the final state hash and leave
    bool paused = false;
    bool finished = false;
    int watch = 0;
    std::uint32_t tick = 0;      // the next tick to simulate
    double carryMs = 0;
    int speedStep = 2;           // index into kReplaySpeeds
    Uint32 lastTicks = 0;
    std::vector<MR_MainCharacter*> craft;   // by player slot
    std::map<std::uint32_t, std::vector<std::uint8_t>> snapshots;   // state every 250 ticks, taken as the replay plays (for seeking)
    bool seekTest = false;       // test mode: play to tick 1000, jump back to tick 400, play on to the end
    bool seekTestDone = false;
    bool markerTest = false;     // test mode: play to tick 8000, jump to the previous lap marker, play on, print the markers found
    bool markerTestDone = false;
    std::string path;            // the file being shown (to watch it again)
    bool ended = false;          // the replay has just reached its end: the end screen is offered at the next frame
    // Timeline markers: every lap crossing and finish, noted the moment the replay plays through it (the same lap counts the race
    // itself keeps, so nothing is computed twice). They stay after a seek back, and a seek forward finds the rest on the way.
    struct Marker { std::uint32_t tick; int player; int lap; bool finished; };
    std::vector<Marker> markers;
    std::vector<int> lapsSeen;   // by player slot
    bool mouseWasDown = false;
};
const double kReplaySpeeds[] = {0.25, 0.5, 1.0, 2.0, 4.0};

// The palette index of the basic colour nearest to an RGB value (the replay timeline is drawn straight into the 8-bit frame).
std::uint8_t NearestPaletteIndex(int r, int g, int b)
{
    static PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
    int best = 0, bestDistance = 1 << 30;
    for (int i = 0; i < MR_BASIC_COLORS; ++i) {
        const int dr = colors[i].peRed - r, dg = colors[i].peGreen - g, db = colors[i].peBlue - b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return static_cast<std::uint8_t>(MR_RESERVED_COLORS_BEGINNING + best);
}

void FillFrameRect(MR_UInt8* frame, int width, int height, int x, int y, int w, int h, std::uint8_t color)
{
    for (int row = std::max(0, y); row < std::min(height, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(width, x + w); ++col) frame[row * width + col] = color;
}

std::string ParseStringArg(int argc, char** argv, const char* name)
{
    for (int argument = 1; argument + 1 < argc; ++argument) {
        if (std::strcmp(argv[argument], name) == 0) return argv[argument + 1];
    }
    return std::string();
}

bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

#if defined(HOVERNET_AUTH)
// The game's own rendered session as a prediction target: the confirmed snapshot is restored into it and it is stepped
// forward, so what is drawn is the prediction (docs/authoritative-race.md).
class RenderedWorld : public Authority::PredictedWorld
{
public:
    RenderedWorld(MR_ClientSession& session, std::vector<MR_MainCharacter*>& craft) : mSession(session), mCraft(craft) {}
    bool RestoreSnapshot(const std::vector<std::uint8_t>& bytes, std::string& error) override
    {
        std::vector<MR_FreeElement*> external(mCraft.begin(), mCraft.end());
        std::int64_t time = 0;
        if (!MR_RestoreLevelState(mSession.Level(), bytes.data(), bytes.size(), external, ReplayFormat::kSimulationRules, time, error)) return false;
        mSession.SetSimulationTime(static_cast<MR_SimulationTime>(time));
        // Every frame the prediction is rebuilt and replays ticks the player has already heard. Sounds queued while replaying are dropped; only a
        // tick newer than any played before is allowed to make a sound.
        for (MR_MainCharacter* c : mCraft) c->DiscardQueuedSounds();
        return true;
    }
    void StepTick(const std::uint16_t* controls) override
    {
        const MR_SimulationTime now = mSession.GetSimulationTime();
        for (std::size_t i = 0; i < mCraft.size(); ++i) mCraft[i]->SetControlState(controls[i], now);
        mSession.StepTicks(1, nullptr, nullptr);
        const std::int64_t tick = static_cast<std::int64_t>(mSession.GetSimulationTime()) / 16;
        if (tick <= mHeardTick) {
            for (MR_MainCharacter* c : mCraft) c->DiscardQueuedSounds();
        }
        else {
            mHeardTick = tick;
        }
    }
private:
    std::int64_t mHeardTick = -1;   // the newest tick whose sounds have been let through
    MR_ClientSession& mSession;
    std::vector<MR_MainCharacter*>& mCraft;
};

// The scripted input the test tools use for a player (so a test client plays a known game).
inline std::uint16_t SimHarnessScripted(int player, std::uint32_t tick)
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

struct AuthOnline
{
    bool active = false;
    bool verify = false;         // test mode: check that restoring into the rendered session reproduces the confirmed state
    bool scripted = false;       // test mode: the scripted input of the other tools instead of the keyboard
    bool spectator = false;      // watch a running race (delayed, from a keyframe) instead of driving in it
    int watch = 0;               // which craft the camera follows when spectating
    bool lost = false;
    bool resultReceived = false;
    bool needSnapshot = false;
    unsigned dropAfter = 0;      // test mode: close the connection after this many verified ticks, to exercise the reconnect
    bool dropped = false, reconnected = false;
    AuthConnection conn;
    std::string host;
    int port = 9610;
    std::string name;
    Crypto::Bytes serverKey;      // the pinned key of the server (32 bytes) for a secure session; empty = plain connection
    Authority::PredictingClient predictor;
    Protocol3::HelloAck ack;
    int rttMs = -1;               // the last measured round trip to the server (-1: not measured yet), shown on the HUD
    ReplayRecorder recorder;      // the race as a replay, built from the Ticks the client follows (saved when the race ends)
    bool replaySaved = false;
    std::vector<MR_MainCharacter*> craft;
    std::unique_ptr<RenderedWorld> world;
    bool interpolationOff = std::getenv("HOVERNET_TEST_NO_INTERPOLATION") != nullptr;   // test: the picture without the between-tick interpolation
    std::uint32_t lead = 12;     // ticks ahead of the confirmed tick that the client aims its inputs at
    Uint32 lastSend = 0, lastPing = 0, lastSnapshotRequest = 0, lastReconnect = 0;
    // The picture's own clock: it runs smoothly with real time and is only eased toward the server's (confirmed tick + lead), so a burst of Ticks
    // that arrives late does not make the craft lurch. After the final Result the client drives on by itself (free driving).
    double displayTick = 0;
    Uint32 displayMs = 0;
    bool displayInit = false;
    bool freeDriving = false;
    Uint32 freeMs = 0;
    double freeAcc = 0;
    std::uint32_t freeTicks = 0;
    std::uint32_t shownTick = 0;     // the tick the picture is at (for the test instrumentation)
    int reconnectAttempts = 0;
    std::vector<Protocol3::SnapshotChunk> chunks;
    std::vector<AuthFrame> backlog;
    unsigned verified = 0, tickFailures = 0, restoreChecks = 0, restoreFailures = 0;
};
#endif

void ReplayTickHook(void* pData, unsigned pTick)
{
    ReplayPlayback* replay = static_cast<ReplayPlayback*>(pData);
    if (pTick >= replay->data.ticks) return;
    for (int p = 0; p < replay->data.players; ++p) {
        replay->craft[static_cast<std::size_t>(p)]->SetControlState(
            replay->data.inputs[static_cast<std::size_t>(pTick) * replay->data.players + p], pTick * 16);
    }
}
#endif

int RunClient(int argc, char** argv)
{
#ifdef HOVERNET_GAME2_PLAYER
    if (HasArgument(argc, argv, "--download-community-tracks")) {
        return RunDownloadCommand(argc, argv);
    }
    if (HasArgument(argc, argv, "--print-config-paths")) {
        std::fprintf(stderr, "CONFIG_DIR=%s\n", ConfigDirPath().c_str());
        std::fprintf(stderr, "HOST_PREFS=%s\n", HostPrefsPath().c_str());
        std::fprintf(stderr, "LOCAL_RACE_PREFS=%s\n", LocalRacePrefsPath().c_str());
        std::fprintf(stderr, "USERNAME=%s\n", UsernamePath().c_str());
        std::fprintf(stderr, "SERVER_URL=%s\n", ServerUrlPath().c_str());
        std::fprintf(stderr, "WINDOW_SIZE=%s\n", WindowSizePath().c_str());
        std::fprintf(stderr, "MUTE=%s\n", MutePath().c_str());
        std::fprintf(stderr, "UI_SCALE=%s\n", UiScalePath().c_str());
        std::fprintf(stderr, "KEYBOARD_BINDINGS=%s\n", KeyboardBindingsPath().c_str());
        std::fprintf(stderr, "CONTROLLER_BINDINGS=%s\n", ControllerBindingsPath().c_str());
        std::fprintf(stderr, "ONBOARDING_COMPLETE=%s\n", OnboardingCompletePath().c_str());
        return 0;
    }
#endif
#ifdef HOVERNET_GAME2_PLAYER
    if (HasArgument(argc, argv, "--track") && !EnsureTrackInstalledBlocking(ParseTrackArg(argc, argv))) {
        return 1;
    }
#endif
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
#ifdef HOVERNET_WEB
    HoverNetInitializeWebFactory();
#endif
    struct FactoryCleanup
    {
        ~FactoryCleanup()
        {
            MR_DllObjectFactory::Clean(FALSE);
#ifdef HOVERNET_WEB
            HoverNetCleanWebFactory();
#endif
        }
    } factoryCleanup;
#ifdef HOVERNET_GAME2_PLAYER
    MR_SoundServer::Init(nullptr);
    MR_SoundServer::SetMasterVolume(LoadMute() ? 0.0 : LoadVolume());
    struct SoundServerCleanup
    {
        ~SoundServerCleanup() { MR_SoundServer::Close(); }
    } soundServerCleanup;
#endif

    const std::string initialTrackName = ParseTrackArg(argc, argv);
    std::string currentTrackName;
    const bool playerMode = IsPlayerMode(argc, argv);
#ifdef HOVERNET_GAME2_PLAYER
    gKeyboardBindings = LoadKeyboardBindings();
    gControllerBindings = LoadControllerBindings();
#endif
    ConfigureGameResolution();
    if (std::getenv("HOVERNET_TEST_PRINT_RES") != nullptr) { std::printf("RACEVIEW %dx%d\n", kWidth, kHeight); std::fflush(stdout); }   // test hook (scripts/test-race-view.sh)
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(kWidth, kHeight) || !buffer.Lock()) {
        return FatalClientError("Could not create the 3D framebuffer.");
    }
    MR_ClientSession session;
    // Whole 16 ms simulation ticks whatever the frame rate; off by default (see MR_ClientSession::SetFixedStep).
    session.SetFixedStep((HasArgument(argc, argv, "--fixed-step") || std::getenv("HOVERNET_FIXED_STEP") != nullptr) ? TRUE : FALSE);
#ifdef HOVERNET_GAME2_PLAYER
    ReplayPlayback replay;
    const std::string replayPath = ParseStringArg(argc, argv, "--replay");
    replay.hashMode = HasArgument(argc, argv, "--replay-hash") || HasArgument(argc, argv, "--replay-seek-test") || HasArgument(argc, argv, "--replay-marker-test");
    replay.markerTest = HasArgument(argc, argv, "--replay-marker-test");
    replay.seekTest = HasArgument(argc, argv, "--replay-seek-test");
    replay.watch = std::atoi(ParseStringArg(argc, argv, "--replay-player").c_str());
    const std::string authServer = ParseStringArg(argc, argv, "--auth-connect");     // host:port of an authoritative server
#if defined(HOVERNET_AUTH)
    AuthOnline auth;
    auth.verify = HasArgument(argc, argv, "--auth-verify") || std::getenv("HOVERNET_TEST_AUTH_VERIFY") != nullptr;   // the environment is how a browser test sets them
    auth.scripted = HasArgument(argc, argv, "--auth-script") || std::getenv("HOVERNET_TEST_AUTH_SCRIPT") != nullptr;
    auth.spectator = HasArgument(argc, argv, "--spectate");
    {
        const std::string keyText = ParseStringArg(argc, argv, "--server-key");
        if (!keyText.empty() && (!Crypto::FromHex(keyText, auth.serverKey) || auth.serverKey.size() != 32)) {
            std::fprintf(stderr, "--server-key must be 64 hex characters\n");
            return 2;
        }
    }
    auth.watch = std::atoi(ParseStringArg(argc, argv, "--spectate-player").c_str());
    auth.dropAfter = static_cast<unsigned>(std::atoi(ParseStringArg(argc, argv, "--auth-drop").c_str()));
#endif
#endif
    const BOOL allowWeapons = playerMode ? TRUE : FALSE;
    const MR_Level* level = nullptr;
    MR_MainCharacter* mainCharacter = nullptr;
    const bool autoPlay = playerMode && HasArgument(argc, argv, "--autoplay");
    MR_3DCoordinate startingPlayerPosition;
    const int player = 0;
    int room = 0;

    const std::string resourcePath = SourcePath("NetTarget/ObjFac1.dat");
    std::ifstream resourceCheck(resourcePath.c_str(), std::ios::binary);
    if (!resourceCheck.good()) {
        return FatalClientError(std::string("Could not load game resources from ") + resourcePath +
                                ". Reinstall HoverNet or restore ObjFac1.dat.");
    }
    resourceCheck.close();
    MR_ResourceLib resources(resourcePath.c_str());

    MR_3DViewPort viewport;
    // Aperture 800 is the horizontal view; with square pixels (see GetXPixelMeter in
    // HeadlessVideoBuffer.cpp) a wider framebuffer shows the same horizontal view and less vertical.
    viewport.Setup(&buffer, 0, 0, kWidth, kHeight, 800);
#ifdef HOVERNET_GAME2_PLAYER
    MR_Observer* observer = playerMode ? MR_Observer::New() : nullptr;
    if (observer != nullptr) {
        // Wide frames show less above and below the horizon than 4:3, which cut the hovercraft off
        // at the bottom. Nudge the view (each step is 1/8 of the frame height) -- by environment
        // for experiments, otherwise by how much wider than 4:3 the frame is.
        const double frameAspect = static_cast<double>(kWidth) / kHeight;
        int scroll = frameAspect >= 2.0 ? -2 : frameAspect > 1.5 ? -1 : 0;
        if (const char* testScroll = std::getenv("HOVERNET_TEST_SCROLL")) scroll = std::atoi(testScroll);
        observer->Scroll(scroll);
    }
    bool debugView = playerMode && HasArgument(argc, argv, "--debug");
    if (playerMode && observer == nullptr) {
        return FatalClientError("Could not create the race observer.");
    }
    if (observer != nullptr) observer->SetLargeHudText(LoadLargeHudText() ? TRUE : FALSE);
#endif
    MR_3DCoordinate camera;
    MR_Angle orientation = 0;
    RenderStats renderStats{0, 0};
    int nonZeroPixels = 0;

    // The standalone viewer opens its requested track immediately. The game
    // client deliberately waits until Local Play or the lobby selects a track.
    if (!playerMode) {
        MR_RecordFile* track = new MR_RecordFile;
#ifdef HOVERNET_GAME2_PLAYER
        const std::string trackPath = Tracks().PathFor(initialTrackName);
#else
        // The standalone viewer is a developer tool for the bundled tracks only.
        const std::string trackPath = SourcePath(("NetTarget/Tracks/" + initialTrackName + ".trk").c_str());
#endif
        if (trackPath.empty() || !track->OpenForRead(trackPath.c_str()) ||
            !session.LoadNew(initialTrackName.c_str(), track, 1, allowWeapons, &buffer) ||
            session.GetCurrentLevel() == nullptr) {
            std::fprintf(stderr, "Could not load %s.trk\n", initialTrackName.c_str());
            return 1;
        }
        level = session.GetCurrentLevel();
        room = level->GetStartingRoom(player);
        if (room < 0 || room >= level->GetRoomCount()) {
            std::fprintf(stderr, "%s.trk has no valid starting room\n", initialTrackName.c_str());
            return 1;
        }
        camera = level->GetStartingPos(player);
        camera.mZ += 700;
        orientation = level->GetStartingOrientation(player);
    }
    if (!playerMode && HasArgument(argc, argv, "--powerup")) {
        bool powerUpFound = false;
        for (int candidateRoom = 0; candidateRoom < level->GetRoomCount() && !powerUpFound; ++candidateRoom) {
            MR_FreeElementHandle handle = level->GetFirstFreeElement(candidateRoom);
            while (handle != nullptr) {
                MR_FreeElement* element = level->GetFreeElement(handle);
                if (element != nullptr && element->GetTypeId().mDllId == 1 &&
                    element->GetTypeId().mClassId == 152) {
                    room = candidateRoom;
                    camera = element->mPosition;
                    camera.mX -= 4000;
                    camera.mZ += 1000;
                    orientation = 0;
                    powerUpFound = true;
                    break;
                }
                handle = level->GetNextFreeElement(handle);
            }
        }
        if (!powerUpFound) {
            std::fprintf(stderr, "ClassicH does not contain a power-up\n");
            return 1;
        }
    }
    if (!playerMode) {
        ClampCameraHeight(*level, room, camera);
        renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                  SDL_GetTicks(), mainCharacter);
    }
    GameControllerManager gameController;
    if (playerMode) {
        gameController.Initialize();
    }
#ifdef HOVERNET_GAME2_PLAYER
    if (playerMode && level != nullptr) {
        if (debugView) {
            observer->RenderDebugDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                         session.GetBackImage());
        }
        else {
            observer->RenderNormalDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                          session.GetBackImage());
        }
    }
#endif
    nonZeroPixels = static_cast<int>(std::count_if(buffer.GetBuffer(),
        buffer.GetBuffer() + kWidth * kHeight, [](MR_UInt8 pixel) { return pixel != 0; }));
    if (!playerMode && (renderStats.surfacesRendered == 0 || nonZeroPixels == 0)) {
        std::fprintf(stderr, "ClassicH starting room did not render visible surfaces\n");
        return 1;
    }

    SDL2GraphicsBackend graphics;
    if (!graphics.Initialize(nullptr, kWidth, kHeight)) {
        return FatalClientError(std::string("Could not initialize graphics: ") + SDL_GetError());
    }
#ifdef HOVERNET_GAME2_PLAYER
    // The renderer's logical size keeps gameplay at its original resolution;
    // the SDL window can therefore restore the player's chosen size without
    // changing any simulation or framebuffer assumptions.
    const WindowSize savedWindowSize = LoadWindowSize();
    const bool startFullscreen = LoadFullscreen();
    if (!startFullscreen) {
        SDL_SetWindowSize(graphics.GetWindow(), savedWindowSize.width, savedWindowSize.height);
        SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    else {
        SDL_SetWindowFullscreen(graphics.GetWindow(), SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    // Screenshot tests: HOVERNET_TEST_WINDOW=WIDTHxHEIGHT opens any screen at that size (e.g. a phone).
    {
        int testW = 0, testH = 0;
        const char* testSize = std::getenv("HOVERNET_TEST_WINDOW");
        if (testSize != nullptr && std::sscanf(testSize, "%dx%d", &testW, &testH) == 2 && testW > 0 && testH > 0) {
            SDL_SetWindowSize(graphics.GetWindow(), testW, testH);
        }
    }
#endif
    auto applyPalette = [&]() {
        std::array<MR_UInt8, MR_NB_COLORS * 3> palette{};
        PALETTEENTRY* colors = MR_GetColors(0.75, 0.75, 0.05);
        for (int index = 0; index < MR_BASIC_COLORS; ++index) {
            const int paletteIndex = MR_RESERVED_COLORS_BEGINNING + index;
            palette[paletteIndex * 3] = colors[index].peRed;
            palette[paletteIndex * 3 + 1] = colors[index].peGreen;
            palette[paletteIndex * 3 + 2] = colors[index].peBlue;
        }
        delete [] colors;
        const MR_UInt8* backgroundPalette = buffer.GetBackPalette();
        if (backgroundPalette != nullptr) {
            for (int index = 0; index < MR_BACK_COLORS; ++index) {
                const PALETTEENTRY& color = MR_ConvertColor(backgroundPalette[index * 3],
                                                             backgroundPalette[index * 3 + 1],
                                                             backgroundPalette[index * 3 + 2],
                                                             0.75, 0.75, 0.05);
                const int paletteIndex = MR_RESERVED_COLORS_BEGINNING + MR_BASIC_COLORS + index;
                palette[paletteIndex * 3] = color.peRed;
                palette[paletteIndex * 3 + 1] = color.peGreen;
                palette[paletteIndex * 3 + 2] = color.peBlue;
            }
        }
        graphics.SetPalette(palette.data(), static_cast<int>(palette.size()));
    };
    applyPalette();

    const int frameLimit = ParseFrameCount(argc, argv);

    const std::string memoryReportPath = ParseMemoryReportPath(argc, argv);
    std::ofstream memoryReport;
    const int kMemoryReportSampleEvery = 50;
    if (!memoryReportPath.empty()) {
        memoryReport.open(memoryReportPath, std::ios::out | std::ios::trunc);
        if (memoryReport.is_open()) {
            memoryReport << "frame,elapsed_ms,ms_per_frame,rss_kb\n";
        } else {
            std::fprintf(stderr, "--memory-report: could not open '%s' for writing\n", memoryReportPath.c_str());
        }
    }
    const Uint32 memoryReportStartTicks = SDL_GetTicks();
    Uint32 memoryReportLastSampleTicks = memoryReportStartTicks;
    // Collected regardless of whether the CSV write above succeeded, so the
    // growth check after the loop (this run's actual pass/fail signal for the
    // ctest baseline test) doesn't depend on a writable filesystem path.
    std::vector<long> memoryReportRssSamples;

#ifdef HOVERNET_GAME2_PLAYER
    // When set, onlineClient stays connected for the rest of the run: the main
    // loop below sends this player's position each frame and applies updates from
    // remotePlayers (one MR_MainCharacter per other racer, keyed by their server-
    // assigned client id -- see RaceServerClient::SendPlayerState/ParsePlayerState
    // for why that id has to be threaded through explicitly rather than assumed).
    RaceServerClient onlineClient;
    OnlineElementBroadcastContext elementBroadcastContext{&onlineClient};
    int localClientId = -1;
    bool raceWasOnline = false;
    std::map<int, RemotePlayer> remotePlayers;
    // Resolves a race-scoped chat message's stamped sender id (see
    // RaceServerClient::ParseChatMessage) to a display name -- populated from the
    // pre-race roster and from eRSMsgConnNameSet for anyone who joins mid-race.
    std::map<int, std::string> raceNames;
#endif

#ifdef HOVERNET_GAME2_PLAYER
    MR_SpriteHandle* menuFontHandle = playerMode ? LoadUiFont() : nullptr;
#if defined(HOVERNET_AUTH)
    EnsureDeviceId();
#endif
    std::string lobbyHost = kDefaultLobbyHost;
    unsigned lobbyPort = kDefaultLobbyPort;
    LoadServerUrl(lobbyHost, lobbyPort);  // a Settings change from a previous run, if any
    ParseLobbyArg(argc, argv, lobbyHost, lobbyPort);  // --lobby always wins, for dev/testing

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    bool keepAuthForLoad = false;   // set while startAuthRace loads the race, so loading it does not drop the connection it is about to use
    // Leaving an authoritative race (to a menu or another race): drop the connection and the prediction.
    auto endAuthRace = [&]() {
        if (!auth.active && !auth.conn.IsOpen()) return;
        auth.active = false;
        auth.conn.Close();
        auth.world.reset();
        auth.craft.clear();
        auth.backlog.clear();
        auth.chunks.clear();
        auth.spectator = false;
    };
#endif
    auto resetRaceSession = [&]() -> bool {
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        endAuthRace();
#endif
        session.SetElementCreationBroadcastHook(nullptr, nullptr);
        onlineClient.Disconnect();
        remotePlayers.clear();
        raceNames.clear();
        localClientId = -1;
        raceWasOnline = false;

        return true;
    };

    // The settings of the local race being driven (kept so "restart race" can start the same race again). Not set for the races of a server or a replay,
    // which load through the same function.
    struct LocalRaceSettings { std::string track; int laps = 0; bool weapons = true; bool valid = false; } lastLocal;
    bool rememberLocal = true;
    // Create the first gameplay session only after Local Play has selected it.
    auto loadLocalRace = [&](const std::string& newTrackName, int newLaps, bool newWeapons) -> bool {
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        if (!keepAuthForLoad) endAuthRace();
#endif
        session.SetElementCreationBroadcastHook(nullptr, nullptr);
        onlineClient.Disconnect();
        remotePlayers.clear();
        raceNames.clear();
        localClientId = -1;
        raceWasOnline = false;

        MR_RecordFile* newTrackFile = new MR_RecordFile;
        const std::string newTrackPath = Tracks().PathFor(newTrackName);
        if (newTrackPath.empty() || !newTrackFile->OpenForRead(newTrackPath.c_str()) ||
            !session.LoadNew(newTrackName.c_str(), newTrackFile, newLaps, newWeapons ? TRUE : FALSE, &buffer) ||
            session.GetCurrentLevel() == nullptr || !session.CreateMainCharacter()) {
            std::fprintf(stderr, "Could not load '%s' for local play\n", newTrackName.c_str());
            return false;
        }
        level = session.GetCurrentLevel();
        mainCharacter = session.GetMainCharacter();
        currentTrackName = newTrackName;
        if (rememberLocal) { lastLocal.track = newTrackName; lastLocal.laps = newLaps; lastLocal.weapons = newWeapons; lastLocal.valid = true; }
        else lastLocal.valid = false;
        session.SetSimulationTime(-6000);
        room = mainCharacter->mRoom;
        camera = mainCharacter->mPosition;
        camera.mZ += 700;
        orientation = mainCharacter->GetCabinOrientation();
        renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                  SDL_GetTicks(), mainCharacter);
        applyPalette();
        if (observer != nullptr) observer->SetLargeHudText(LoadLargeHudText() ? TRUE : FALSE);
        return true;
    };

    auto joinOnlineRace = [&](bool autoHostStart = false, bool autoJoin = false,
                              bool waitForPeer = false) -> bool {
        std::string joinedRace;
        std::string joinedTrack;
        int joinedLaps = 1;
        bool joinedWeapons = true;
        std::vector<RaceServerPeer> knownPeers;
        localClientId = -1;
        if (menuFontHandle == nullptr ||
            !RunLobbyScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(), onlineClient,
                            lobbyHost, lobbyPort, joinedRace, joinedTrack, joinedLaps, joinedWeapons, localClientId,
                            knownPeers, frameLimit, autoHostStart, autoJoin, waitForPeer, FindHostableTrack(initialTrackName))) {
            onlineClient.Disconnect();
            return false;
        }

        std::printf("Joined race %c%s%c via the lobby (%zu other player(s) already in)\n",
                    39, joinedRace.c_str(), 39, knownPeers.size());
        raceWasOnline = true;

        // Load only the track selected by the joined race. LoadNew() invalidates the previous
        // MR_MainCharacter (see its own comment: "a newly loaded track owns a
        // completely new element graph"), so the local player has to be recreated
        // too, and every stale remote craft from any earlier race dropped.
        if (!joinedTrack.empty()) {
            MR_RecordFile* joinedTrackFile = new MR_RecordFile;
            const std::string joinedTrackPath = Tracks().PathFor(joinedTrack);
            if (joinedTrackPath.empty() || !joinedTrackFile->OpenForRead(joinedTrackPath.c_str()) ||
                !session.LoadNew(joinedTrack.c_str(), joinedTrackFile, joinedLaps, joinedWeapons ? TRUE : FALSE, &buffer) ||
                session.GetCurrentLevel() == nullptr || !session.CreateMainCharacter()) {
                std::fprintf(stderr, "Could not load '%s' for the joined race\n", joinedTrack.c_str());
                onlineClient.Disconnect();
                return false;
            }
            level = session.GetCurrentLevel();
            mainCharacter = session.GetMainCharacter();
            currentTrackName = joinedTrack;
            remotePlayers.clear();
        }
        else {
            std::fprintf(stderr, "Joined race did not specify a track\n");
            onlineClient.Disconnect();
            return false;
        }

        session.SetSimulationTime(-6000);

        for (const RaceServerPeer& peer : knownPeers) {
            raceNames[peer.mClientId] = peer.mName;
        }

        std::vector<int> raceClientIds;
        raceClientIds.push_back(localClientId);
        for (const RaceServerPeer& peer : knownPeers) {
            raceClientIds.push_back(peer.mClientId);
        }
        std::sort(raceClientIds.begin(), raceClientIds.end());
        raceClientIds.erase(std::unique(raceClientIds.begin(), raceClientIds.end()), raceClientIds.end());

        const int playerStartCount = level->GetPlayerCount();
        const auto localIt = std::lower_bound(raceClientIds.begin(), raceClientIds.end(), localClientId);
        const int localRaceSlot = static_cast<int>(std::distance(raceClientIds.begin(), localIt));
        const int localStartSlot = localRaceSlot % playerStartCount;
        if (!session.PlaceCharacterAtStart(mainCharacter, localStartSlot)) {
            std::fprintf(stderr, "Could not place local multiplayer craft in start slot %d\n", localStartSlot);
        }
        mainCharacter->SetHoverId(localRaceSlot);
        mainCharacter->SetHoverModel(0);

        for (const RaceServerPeer& peer : knownPeers) {
            if (remotePlayers.count(peer.mClientId) > 0) {
                continue;
            }
            MR_MainCharacter* remote = MR_MainCharacter::New(5, TRUE);
            if (remote == nullptr) {
                continue;
            }
            const auto remoteIt = std::lower_bound(raceClientIds.begin(), raceClientIds.end(), peer.mClientId);
            const int remoteRaceSlot = static_cast<int>(std::distance(raceClientIds.begin(), remoteIt));
            const int startSlot = remoteRaceSlot % playerStartCount;
            const int startRoom = level->GetStartingRoom(startSlot);
            remote->mPosition = level->GetStartingPos(startSlot);
            remote->SetOrientation(level->GetStartingOrientation(startSlot));
            remote->mRoom = (startRoom >= 0 && startRoom < level->GetRoomCount()) ? startRoom : room;
            remote->SetHoverId(remoteRaceSlot);
            remote->SetHoverModel(0);
            MR_FreeElementHandle remoteHandle = session.InsertRemoteCharacter(remote, remote->mRoom);
            if (remoteHandle != nullptr) {
                remotePlayers[peer.mClientId] = {remote, remoteHandle};
            }
            else {
                delete remote;
            }
        }
        session.SetElementCreationBroadcastHook(BroadcastCreatedElement, &elementBroadcastContext);
        room = mainCharacter->mRoom;
        camera = mainCharacter->mPosition;
        camera.mZ += 700;
        orientation = mainCharacter->GetCabinOrientation();
        renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                  SDL_GetTicks(), mainCharacter);
        applyPalette();
        if (observer != nullptr) observer->SetLargeHudText(LoadLargeHudText() ? TRUE : FALSE);
        return true;
    };

    // Puts every craft of a replayed or authoritative race on the grid as the recorder and the server have them: a craft per
    // slot with hover id = slot and the basic model, the watched slot being the session's main craft, and the craft in
    // slot order in each room's list (list order matters to the simulation). Returns an error message or an empty string.
    auto placeRaceCraft = [&](int players, int laps, bool weapons, int watch, std::vector<MR_MainCharacter*>& craftOut) -> std::string {
        MR_Level* raceLevel = session.Level();
        craftOut.assign(static_cast<std::size_t>(players), nullptr);
        for (int slot = 0; slot < players; ++slot) {
            const int startRoom = raceLevel->GetStartingRoom(slot);
            if (startRoom < 0 || startRoom >= raceLevel->GetRoomCount()) return "The race has more players than the track has start positions.";
            MR_MainCharacter* craft = (slot == watch) ? mainCharacter : MR_MainCharacter::New(laps, weapons ? TRUE : FALSE);
            if (craft == nullptr) return "Could not create a craft for the race.";
            craft->mPosition = raceLevel->GetStartingPos(slot);
            craft->SetOrientation(raceLevel->GetStartingOrientation(slot));
            craft->mRoom = startRoom;
            craft->SetHoverId(slot);
            craft->SetHoverModel(0);
            craftOut[static_cast<std::size_t>(slot)] = craft;
            if (slot != watch && session.InsertSimulatedCharacter(craft, startRoom) == nullptr) return "Could not place a craft for the race.";
        }
        for (int slot = 0; slot < players; ++slot) {
            for (int roomIndex = 0; roomIndex < raceLevel->GetRoomCount(); ++roomIndex) {
                for (MR_FreeElementHandle handle = raceLevel->GetFirstFreeElement(roomIndex); handle != nullptr;
                     handle = MR_Level::GetNextFreeElement(handle)) {
                    if (MR_Level::GetFreeElement(handle) == craftOut[static_cast<std::size_t>(slot)]) {
                        raceLevel->MoveElement(handle, roomIndex);
                        break;
                    }
                }
            }
        }
        return std::string();
    };

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    // Everything after the server has said HelloAck and Start (auth.ack, auth.backlog, auth.spectator and the open auth.conn are set):
    // check the track and the rules, load the race, put the craft on the grid, and start predicting. Returns an error or "".
    auto startAuthRace = [&]() -> std::string {
        std::string error;
        const std::string authTrackPath = Tracks().PathFor(auth.ack.track);
        if (authTrackPath.empty() || Sha256::HashFile(authTrackPath) != auth.ack.trackSha256) {
            return "The track '" + auth.ack.track + "' on this computer is not the one the server is using.";
        }
        if (auth.ack.rules != ReplayFormat::kSimulationRules) return "The server runs different game rules than this version of the game.";
        keepAuthForLoad = true;
        rememberLocal = false;
        const bool loaded = loadLocalRace(auth.ack.track, auth.ack.laps, auth.ack.weapons);
        rememberLocal = true;
        keepAuthForLoad = false;
        if (!loaded) return "Could not load the server's track.";
        if (auth.spectator && (auth.watch < 0 || auth.watch >= auth.ack.players)) auth.watch = 0;
        const std::string placeError = placeRaceCraft(auth.ack.players, auth.ack.laps, auth.ack.weapons, auth.spectator ? auth.watch : auth.ack.player, auth.craft);
        if (!placeError.empty()) return placeError;
        session.SetSimulationTime(0);
        if (!auth.predictor.Init(authTrackPath, auth.ack, error)) return error;
        auth.world.reset(new RenderedWorld(session, auth.craft));
        auth.active = true;
        auth.lost = auth.resultReceived = false;
        auth.displayInit = false;
        auth.freeDriving = false;
        auth.reconnectAttempts = 0;
        auth.chunks.clear();
        auth.recorder.Begin(auth.ack);
        auth.replaySaved = false;
        auth.needSnapshot = auth.spectator;   // the server sends the keyframe first; nothing is drawn or applied before it
        auth.lastSend = auth.lastPing = SDL_GetTicks();
        session.AddMessage(auth.spectator ? "Watching the race: Left and Right (or [ and ]) switch craft"
                                          : ("Connected: you are player " + std::to_string(auth.ack.player + 1) + " of " + std::to_string(auth.ack.players)).c_str());
        return std::string();
    };
#endif

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    // The HoverNet server screen and, once a race is ready, the race itself. eRace: a race is now running. eBack: the player went back (or
    // the race could not be set up: the reason is printed). eFallback: the official server could not be reached.
    auto tryAuthServer = [&]() -> AuthScreenResult {
        endAuthRace();
        OnlineLobby lobby;
        const AuthScreenResult screen = RunAuthServerScreen(graphics, lobby, frameLimit);
        if (screen != AuthScreenResult::eRace) return screen;
        auth.ack = lobby.Ack();
        auth.backlog = lobby.TakeBacklog();
        auth.spectator = lobby.Spectating();
        auth.conn.Swap(lobby.Connection());
        lobby.AddressOf(auth.host, auth.port, auth.serverKey);
        auth.name = LoadUsername();
        // The browser tests set these switches after the page has started (see hovernet_test_setenv).
        if (std::getenv("HOVERNET_TEST_AUTH_VERIFY") != nullptr) auth.verify = true;
        if (std::getenv("HOVERNET_TEST_AUTH_SCRIPT") != nullptr) auth.scripted = true;
        const std::string startError = startAuthRace();
        if (!startError.empty()) {
            endAuthRace();
            std::fprintf(stderr, "%s\n", startError.c_str());
            return AuthScreenResult::eBack;
        }
        return AuthScreenResult::eRace;
    };
#endif
    // The one way into online play (main menu, pause menu, results screen): the HoverNet server first, and the classic lobby when the
    // HoverNet server is not available. True when a race is set up, false when the player went back.
    auto enterOnlineMultiplayer = [&]() -> bool {
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        const AuthScreenResult tried = tryAuthServer();
        if (tried == AuthScreenResult::eRace) return true;
        if (tried == AuthScreenResult::eBack) return false;
        std::printf("The HoverNet server is not available: using the classic lobby\n");
#endif
        return joinOnlineRace();
    };

#ifdef HOVERNET_GAME2_PLAYER
    // Plays a replay file in the normal game window (from the command line or from the Watch a Replay screen): checks the file, the rules and the track
    // first, then loads the track and puts the craft on the grid. An error before the track is loaded changes nothing (the caller shows it and
    // carries on); after that, fatal is set and the race in the session is no longer usable.
    auto startReplayFile = [&](const std::string& path, int watch, bool& fatal, const std::string& note = std::string()) -> std::string {
        fatal = false;
        ReplayPlayback next;
        next.hashMode = replay.hashMode; next.seekTest = replay.seekTest; next.markerTest = replay.markerTest;   // test modes set on the command line
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!ReadWholeFile(path, bytes)) return "Could not read the replay file " + path;
        if (!ReplayFormat::Decode(bytes, next.data, error)) return "This is not a valid replay: " + error;
        if (next.data.rules != ReplayFormat::kSimulationRules) {
            return "This replay was recorded with a different version of the game rules and cannot be shown.";
        }
        const std::string trackPath = Tracks().PathFor(next.data.track);
        if (trackPath.empty() || Sha256::HashFile(trackPath) != next.data.trackSha256) {
            return "The track '" + next.data.track + "' on this computer is not the one the replay was recorded on.";
        }
        next.watch = (watch < 0 || watch >= next.data.players) ? 0 : watch;
        next.path = path;
        replay = next;
        fatal = true;
        rememberLocal = false;
        const bool replayLoaded = loadLocalRace(replay.data.track, replay.data.laps, replay.data.weapons);
        rememberLocal = true;
        if (!replayLoaded) {
            return "Could not load the replay's track.";
        }
        const std::string placeError = placeRaceCraft(replay.data.players, replay.data.laps, replay.data.weapons, replay.watch, replay.craft);
        if (!placeError.empty()) return placeError;
        session.SetSimulationTime(0);
        replay.active = true;
        replay.lapsSeen.assign(static_cast<std::size_t>(replay.data.players), 0);
        if (const char* speed = std::getenv("HOVERNET_TEST_REPLAY_SPEED")) replay.speedStep = std::max(0, std::min(4, std::atoi(speed)));   // test hook: start at this speed step
        replay.lastTicks = SDL_GetTicks();
        if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYTEST following player %d of %d (%s)\n", replay.watch + 1, replay.data.players, note.c_str()); std::fflush(stdout); }
        session.AddMessage(("Following player " + std::to_string(replay.watch + 1) + " of " + std::to_string(replay.data.players) + (note.empty() ? std::string() : " (" + note + ")") + ".  P pause, 1-5 speed, [ ] craft, C camera, Esc menu").c_str());
        fatal = false;
        return std::string();
    };
#endif

#ifdef HOVERNET_GAME2_PLAYER
    // The main menu and what it leads to (a local race, the online lobby, Watch a Replay, settings, controls, how to play). Returns true when a race
    // or replay is set up, false when the player quit (or loading failed: mainMenuFailed). Used at start-up and whenever a menu says "main menu".
    bool mainMenuFailed = false;
    auto showMainMenu = [&]() -> bool {
        replay.active = false;
    bool pickingMode = true;
        while (pickingMode) {
            pickingMode = false;
            const MenuChoice choice = RunMainMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                                                  frameLimit);
            if (g_QuitConfirmed) {
                // Confirmed quit from the main menu itself -- skip straight to
                // the race loop below, which starts with running = !g_QuitConfirmed
                // and so exits immediately without ever rendering a frame.
            }
            else if (choice == MenuChoice::eOnlineLobby) {
                if (!enterOnlineMultiplayer()) {
                    std::printf("Lobby skipped or unavailable; returning to the main menu\n");
                    pickingMode = !g_QuitConfirmed;
                }
            }
            else if (choice == MenuChoice::eWatchReplay) {
                const bool started = RunReplayBrowser(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
                pickingMode = !started && !g_QuitConfirmed;   // a replay is playing, or back to the main menu
            }
            else if (choice == MenuChoice::eLeaderboards) {
#if defined(HOVERNET_AUTH)
                const bool started = RunLeaderboards(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
                pickingMode = !started && !g_QuitConfirmed;   // a replay is playing, or back to the main menu
#else
                pickingMode = !g_QuitConfirmed;
#endif
            }
            else if (choice == MenuChoice::eSettings) {
                // Always back to the main menu afterward (Save or Cancel alike)
                // instead of falling through to Local Play -- there's nothing
                // else for this choice to lead to, and the player shouldn't have
                // to relaunch or stumble into a race just to get back out of it.
                const SettingsResult settings = RunSettingsScreen(
                    graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                    LoadUsername(), lobbyHost, lobbyPort, LoadVolume(), LoadMute(), LoadFullscreen(), frameLimit);
                if (settings.confirmed) {
                    SaveUsername(settings.username);
                    lobbyHost = settings.serverHost;
                    lobbyPort = settings.serverPort;
                    SaveServerUrl(lobbyHost, lobbyPort);
                    SaveVolume(settings.volume);
                    SaveMute(settings.muted);
                    SaveFullscreen(settings.fullscreen);
                    WindowSize savedSize;
                    savedSize.width = settings.windowWidth;
                    savedSize.height = settings.windowHeight;
                    SaveWindowSize(savedSize);
                    SaveUiScale(settings.uiScale);
                    SaveLargeHudText(settings.largeHudText);
                    SaveReducedMotion(settings.reducedMotion);
                    SaveHighContrast(settings.highContrast);
                }
                pickingMode = !g_QuitConfirmed;
            }
#if defined(HOVERNET_AUTH)
#endif
            else if (choice == MenuChoice::eControls) {
                RunControlsScreen(graphics, frameLimit);
                pickingMode = !g_QuitConfirmed;
            }
            else if (choice == MenuChoice::eHowToPlay) {
                if (RunOnboardingScreen(graphics, frameLimit)) SaveOnboardingComplete();
                pickingMode = !g_QuitConfirmed;
            }
            else if (autoPlay) {
                if (!loadLocalRace(initialTrackName, 1, true)) { mainMenuFailed = true; return false; }
                session.SetSimulationTime(0);
                startingPlayerPosition = mainCharacter->mPosition;
            }
            else {
                const LocalRaceSetup setup = RunLocalRaceSetup(graphics, buffer, viewport,
                                                                *menuFontHandle->GetSprite(), frameLimit);
                if (setup.confirmed) {
                    if (!loadLocalRace(setup.trackName, setup.laps, setup.weapons)) {
                        std::printf("Could not load '%s'; returning to the main menu\n",
                                    setup.trackName.c_str());
                        pickingMode = !g_QuitConfirmed;
                    }
                }
                else {
                    // Cancelled -- back to the main menu instead of falling through
                    // to whatever was already loaded (interactive mode only; a
                    // frame-limited run always confirms, see RunLocalRaceSetup),
                    // unless that "cancel" was actually a confirmed quit.
                    pickingMode = !g_QuitConfirmed;
                }
            }
        }
        return !g_QuitConfirmed;
    };
#endif

    if (playerMode && menuFontHandle == nullptr) {
        return FatalClientError("Could not load the menu font from ObjFac1.dat.");
    }
    if (playerMode) {
        if (!replayPath.empty()) {
            bool fatalSetup = false;
            const std::string replayError = startReplayFile(replayPath, replay.watch, fatalSetup);
            if (!replayError.empty()) return FatalClientError(replayError);
        }
        else if (!authServer.empty()) {
#if defined(HOVERNET_AUTH)
            const std::size_t colon = authServer.rfind(':');
            auth.host = colon == std::string::npos ? authServer : authServer.substr(0, colon);
            auth.port = colon == std::string::npos ? 9610 : std::atoi(authServer.c_str() + colon + 1);
            auth.name = LoadUsername();
            if (auth.name.empty()) auth.name = "Player";
            std::string error;
            if (!auth.conn.Connect(auth.host, auth.port, error, auth.serverKey.empty() ? nullptr : auth.serverKey.data())) return FatalClientError(error);
            Protocol3::Hello hello;
            hello.name = auth.name.substr(0, 32);
            auth.conn.Send(auth.spectator ? Protocol3::kSpectate : Protocol3::kHello, Protocol3::Encode(hello));
            bool gotAck = false, gotStart = false;
            const Uint32 waitStart = SDL_GetTicks();
            while (!(gotAck && (gotStart || auth.spectator))) {
                SDL_PumpEvents();
                std::vector<AuthFrame> frames;
                const bool open = auth.conn.Pump(frames);
                for (const AuthFrame& f : frames) {
                    if (f.type == Protocol3::kHelloAck) gotAck = Protocol3::Decode(f.payload, auth.ack);
                    else if (f.type == Protocol3::kStart) gotStart = true;
                    else if (f.type == Protocol3::kReject) {
                        Protocol3::Reject r;
                        Protocol3::Decode(f.payload, r);
                        return FatalClientError("The server refused the connection: " + r.message);
                    }
                    else if (gotAck) auth.backlog.push_back(f);   // ticks that arrive while the race is being set up
                }
                if (!open && !(gotAck && (gotStart || auth.spectator))) return FatalClientError("The connection to the server was lost before the race started.");
                if (SDL_GetTicks() - waitStart > 120000) return FatalClientError("Timed out waiting for the race to fill and start.");
                SDL_Delay(5);
            }
            {
                const std::string startError = startAuthRace();
                if (!startError.empty()) return FatalClientError(startError);
            }
#else
            return FatalClientError("Authoritative servers are not available in the browser yet.");
#endif
        }
        else if (HasArgument(argc, argv, "--online-race-host-smoke")) {
            if (!joinOnlineRace(true, false, true)) {
                return FatalClientError("Online host acceptance could not start a joined race.");
            }
        }
        else if (HasArgument(argc, argv, "--online-race-join-smoke")) {
            if (!joinOnlineRace(false, true, false)) {
                return FatalClientError("Online join acceptance could not discover, join, and load a race.");
            }
        }
        else if (HasArgument(argc, argv, "--online-race-smoke")) {
            // End-to-end installed-package acceptance: use the normal lobby protocol
            // to host and start a race, then continue through normal track loading
            // and the bounded gameplay loop below.
            if (!joinOnlineRace(true)) {
                return FatalClientError("Online race acceptance could not host, start, and load a race.");
            }
        }
        else if (HasArgument(argc, argv, "--lobby-screen")) {
            // Deterministic CI entry point: exercise protocol negotiation and
            // render the real online lobby without relying on menu navigation
            // or joining/creating a race. A bounded lobby returns false when
            // its frame budget expires, which is the expected smoke-test exit.
            joinOnlineRace();
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--onboarding-config")) {
            RunOnboardingScreen(graphics, frameLimit, 3);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--settings-screen")) {
            RunSettingsScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                              "Player", lobbyHost, lobbyPort, LoadVolume(), LoadMute(), LoadFullscreen(), frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--controls-reference")) {
            RunControlsScreen(graphics, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--replays-screen")) {   // the screen on its own (screenshots); "Watch" reports instead of playing
            RunReplayBrowser(graphics, frameLimit, [](const std::string&, int, const std::string&, bool& fatal) { fatal = false; return std::string("(screenshot mode: nothing is played)"); });
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--leaderboards-screen")) {   // the screen on its own (screenshots); "Watch" reports instead of playing
#if defined(HOVERNET_AUTH)
            RunLeaderboards(graphics, frameLimit, [](const std::string&, int, const std::string&, bool& fatal) { fatal = false; return std::string("(screenshot mode: nothing is played)"); });
#endif
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--pause-menu")) {
            RunPauseMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(), false, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--quit-confirmation")) {
            ConfirmQuit(graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--results-screen")) {
            PersonalBests::Outcome sample;   // HOVERNET_TEST_RESULTS_BESTS=new|old shows the personal-best lines (screenshot test)
            const char* bestsMode = std::getenv("HOVERNET_TEST_RESULTS_BESTS");
            if (bestsMode != nullptr) {
                const bool fresh = std::strcmp(bestsMode, "new") == 0;
                sample.newRaceBest = fresh; sample.hadRaceBest = true; sample.previousRace = 88123; sample.raceBest = fresh ? 83456 : 80100;
                sample.newLapBest = fresh; sample.hadLapBest = true; sample.previousLap = 29870; sample.lapBest = fresh ? 27341 : 26500;
            }
            RunPostRaceScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                              83456, 27341, 1, 4, frameLimit, bestsMode != nullptr ? &sample : nullptr);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--onboarding")) {
            // HOVERNET_ONBOARDING_PAGE lets screenshot tests open a later briefing page.
            const char* startPage = std::getenv("HOVERNET_ONBOARDING_PAGE");
            int testW = 0, testH = 0;
            const char* testSize = std::getenv("HOVERNET_TEST_WINDOW");
            if (testSize != nullptr && std::sscanf(testSize, "%dx%d", &testW, &testH) == 2) {
                SDL_SetWindowSize(graphics.GetWindow(), testW, testH);
            }
            RunOnboardingScreen(graphics, frameLimit, startPage != nullptr ? std::atoi(startPage) : 0);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--local-setup-screen")) {
            RunLocalRaceSetup(graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
            g_QuitConfirmed = true;
        }
        else if (frameLimit < 0 && !HasCompletedOnboarding()) {
            if (RunOnboardingScreen(graphics, frameLimit)) SaveOnboardingComplete();
        }
        bool pickingMode = !g_QuitConfirmed && !replay.active;
#if defined(HOVERNET_AUTH)
        if (auth.active) pickingMode = false;
#endif
        if (pickingMode) {
            showMainMenu();
            if (mainMenuFailed) return 1;
        }
    }
#endif

    if (playerMode && g_QuitConfirmed) {
        // Menu-only bounded runs and a confirmed quit have no race to report or
        // enter. Returning here also prevents reads of race-only state after the
        // menu flow, which previously produced corrupted counters in real logs.
#ifdef HOVERNET_GAME2_PLAYER
        delete menuFontHandle;
        observer->Delete();
#endif
        return 0;
    }
    if (playerMode && (level == nullptr || mainCharacter == nullptr)) {
        std::fprintf(stderr, "No race was selected\n");
        return 1;
    }
    nonZeroPixels = static_cast<int>(std::count_if(buffer.GetBuffer(),
        buffer.GetBuffer() + kWidth * kHeight, [](MR_UInt8 pixel) { return pixel != 0; }));
    std::printf("%s 3D view: room=%d surfaces=%d actors=%d pixels=%d\n",
                currentTrackName.empty() ? initialTrackName.c_str() : currentTrackName.c_str(),
                room, renderStats.surfacesRendered,
                renderStats.actorsRendered, nonZeroPixels);
    int framesRendered = 0;
    // Already confirmed quitting from an earlier menu screen (see g_QuitConfirmed) --
    // skip the race entirely instead of rendering a frame of it first.
    bool running = !g_QuitConfirmed;
    bool cockpitView = false;
    bool missileSeen = false;
    bool finishAnnounced = false;
    bool restartLocal = false;   // restart the local race with the same settings (pause menu, results screen)
    bool toMainMenu = false;     // set by a menu that says "main menu" (and by an online lobby that was left); acted on at the top of the next frame
    // Finishing does not take the race away: the player is told they can press Enter for the results and race options, and may drive
    // on as long as they like (some like to stay on the track). The times are kept from the moment of the finish.
    bool resultsPending = false;
    Uint32 lastResultsHint = 0;
    bool resultsSeen = false;     // the results screen has been opened once: no more reminders
    MR_SimulationTime finishTotalTime = 0, finishBestLap = 0;
    PersonalBests::Outcome finishBests;
#ifdef HOVERNET_GAME2_PLAYER
    // The ghost of the player's best run on this track and lap count (GhostRun.h): the run being driven is recorded as it goes, the saved
    // best is drawn beside the craft. A craft that is only drawn: it is not in the level, so it cannot change the race or its state hash.
    GhostRun ghostRecording, ghostBest;
    std::string ghostKey;                 // "track/laps" the ghostBest belongs to
    MR_MainCharacter* ghostCraft = nullptr;
    MR_SimulationTime ghostLastTime = 0;  // a time that goes back means the race was restarted
    bool ghostShownReported = false;      // test hook: say once that the ghost was on screen
#endif
#ifdef HOVERNET_GAME2_PLAYER
    // A mid-race RaceServer drop (crash, network outage) currently just makes
    // every onlineClient.IsConnected() check below quietly stop sending/polling --
    // the local player keeps racing solo with no indication multiplayer died.
    // Track the transition so it can be announced exactly once.
    bool wasOnlineConnected = onlineClient.IsConnected();
    Uint32 lastRacePing = SDL_GetTicks();
#endif
    // In-race chat, scoped to whatever race this connection is in (see the
    // eRSMsgChatMessage handling below) -- text input only actually does
    // anything once online, but it's harmless to leave enabled for local play.
    std::string chatBuffer;
#ifdef HOVERNET_GAME2_PLAYER
    // Watching a replay or a race as a spectator: Left and Right move the camera (and the HUD) to the previous or next craft.
    static const int kChasePercents[] = {50, 75, 100, 150, 200, 300};
    const int kChaseSteps = 6;
    int chaseStep = std::getenv("HOVERNET_TEST_CHASE_STEP") ? std::max(0, std::min(kChaseSteps - 1, std::atoi(std::getenv("HOVERNET_TEST_CHASE_STEP")))) : 2;   // the env var is a screenshot-test hook
    auto cycleWatch = [&](int direction) {
        std::vector<MR_MainCharacter*>* crafts = nullptr;
        int* watch = nullptr;
        if (replay.active) { crafts = &replay.craft; watch = &replay.watch; }
#if defined(HOVERNET_AUTH)
        else if (auth.active && auth.spectator) { crafts = &auth.craft; watch = &auth.watch; }
#endif
        if (crafts == nullptr || crafts->size() < 2) return;
        const int count = static_cast<int>(crafts->size());
        *watch = (*watch + direction + count) % count;
        mainCharacter = (*crafts)[static_cast<std::size_t>(*watch)];
        session.AddMessage(("Following player " + std::to_string(*watch + 1) + " of " + std::to_string(count)).c_str());
        if (std::getenv("HOVERNET_TEST_WATCH") != nullptr) { std::printf("WATCHTEST watching player %d of %d\n", *watch + 1, count); std::fflush(stdout); }
    };
    // Chat works in a race on the old lobby server and in one on a HoverNet 3.0 server, during the race and after the player has finished.
    auto canChat = [&]() -> bool {
        if (onlineClient.IsConnected()) return true;
#if defined(HOVERNET_AUTH)
        return auth.active && auth.conn.IsOpen();
#else
        return false;
#endif
    };
#endif
    bool pauseHintShown = false;
    MR_SimulationTime lastRaceClock = -100000;
    std::vector<uint8_t> vrEyes; // VR: left+right eye RGBA, reused between frames
    SDL_StartTextInput();
#ifdef HOVERNET_WEB
    struct InRaceFlag {
        InRaceFlag() { gHoverNetInRace = 1; }
        ~InRaceFlag() { gHoverNetInRace = 0; }
    } inRaceFlag;
#endif
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
    // One frame of an authoritative race: take in the server's messages, send our input, draw the prediction.
    auto stepAuth = [&](int controlState) -> int {   // returns 0 to carry on, or an exit code + 1 to leave the program
        std::vector<AuthFrame> frames;
        frames.swap(auth.backlog);
        auth.conn.Pump(frames);
        for (const AuthFrame& f : frames) {
            using namespace Protocol3;
            if (f.type == kTick) {
                Tick t;
                if (auth.needSnapshot) continue;   // waiting for a snapshot: ticks before it are of no use
                if (!Decode(f.payload, t) || !auth.predictor.OnTick(t)) { ++auth.tickFailures; auth.needSnapshot = true; auth.recorder.Spoil(); }
                else { ++auth.verified; auth.recorder.OnTick(t); }
            }
            else if (f.type == kSnapshotChunk) {
                SnapshotChunk c;
                if (!Decode(f.payload, c)) continue;
                if (c.index == 0) auth.chunks.clear();
                auth.chunks.push_back(c);
                if (c.index + 1 == c.count && auth.chunks.size() == c.count) {
                    std::vector<std::uint8_t> bytes;
                    for (const SnapshotChunk& part : auth.chunks) bytes.insert(bytes.end(), part.data.begin(), part.data.end());
                    std::string error;
                    if (auth.predictor.OnSnapshot(bytes, c.tick, error)) auth.needSnapshot = false;
                    auth.chunks.clear();
                }
            }
            else if (f.type == kPong) {
                Pong p;
                if (Decode(f.payload, p)) {
                    const Uint32 rtt = SDL_GetTicks() - p.token;
                    auth.rttMs = static_cast<int>(std::min<Uint32>(rtt, 9999));
                    auth.lead = std::min<std::uint32_t>(40, std::max<std::uint32_t>(3, rtt / 16 + 3));
                }
            }
            else if (f.type == kResult && !auth.resultReceived) {
                auth.resultReceived = true;
                session.AddMessage("The race is over. The server has the final result.");
                if (!auth.replaySaved) {
                    // A race followed from its first tick is a replay the player can watch again (Watch a Replay in the menu).
                    auth.replaySaved = true;
                    const std::vector<std::uint8_t> bytes = auth.recorder.Bytes();
                    if (!bytes.empty()) {
                        const std::string saved = ReplayLibrary(ConfigDirPath() + "/replays").Save(bytes, auth.ack.track, std::time(nullptr), auth.spectator ? -1 : static_cast<int>(auth.ack.player));
                        if (!saved.empty()) {
                            session.AddMessage("This race was saved: Watch a Replay in the menu");
                            if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYTEST saved %s %u ticks\n", saved.c_str(), auth.recorder.Ticks()); std::fflush(stdout); }
                        }
                    }
                    else if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYTEST not saved (the race was not followed from its first tick)\n"); std::fflush(stdout); }
                }
            }
            else if (f.type == kChatMessage) {
                ChatMessage m;
                if (Decode(f.payload, m)) {
                    const std::string line = (m.fromServer ? std::string("Server: ") : m.name + ": ") + m.text;
                    session.AddMessage(line.c_str());
                    if (std::getenv("HOVERNET_TEST_CHAT") != nullptr || std::getenv("HOVERNET_TEST_CHATLOG") != nullptr) { std::printf("CHATTEST received: %s\n", line.c_str()); std::fflush(stdout); }
                }
            }
            else if (f.type == kReject) {
                Reject r;
                Decode(f.payload, r);
                session.AddMessage(r.message.empty() ? "The server closed the connection" : r.message.c_str());
            }
        }

        const Uint32 now = SDL_GetTicks();
        if (auth.dropAfter != 0 && !auth.dropped && auth.verified >= auth.dropAfter) {
            auth.dropped = true;
            auth.conn.Close();   // test mode: a dropped connection
        }
        if (!auth.conn.IsOpen() && !auth.resultReceived) {
            // Lost the connection: try to take the slot back with the resume token, once a second, for 15 seconds.
            if (!auth.lost) { auth.lost = true; session.AddMessage("Connection lost - reconnecting..."); auth.lastReconnect = 0; }
            if (!auth.spectator && auth.reconnectAttempts < 15 && now - auth.lastReconnect >= 1000) {
                auth.lastReconnect = now;
                ++auth.reconnectAttempts;
                std::string error;
                if (auth.conn.Connect(auth.host, auth.port, error, auth.serverKey.empty() ? nullptr : auth.serverKey.data())) {
                    Protocol3::Resume resume;
                    resume.token = auth.ack.resumeToken;
                    auth.conn.Send(Protocol3::kResume, Protocol3::Encode(resume));
                    auth.needSnapshot = true;
                    auth.lastSnapshotRequest = 0;
                    auth.lost = false;
                    auth.reconnectAttempts = 0;
                    auth.reconnected = true;
                    session.AddMessage("Reconnected");
                }
            }
        }
        if (auth.conn.IsOpen()) {
            if (auth.needSnapshot && now - auth.lastSnapshotRequest > 2000) {
                auth.lastSnapshotRequest = now;
                Protocol3::SnapshotRequest request;
                request.id = now;
                auth.conn.Send(Protocol3::kSnapshotRequest, Protocol3::Encode(request));
            }
            if (now - auth.lastPing >= 500) {
                auth.lastPing = now;
                Protocol3::Ping ping;
                ping.clientTick = auth.predictor.ConfirmedTick();
                ping.token = now;
                auth.conn.Send(Protocol3::kPing, Protocol3::Encode(ping));
            }
            if (!auth.spectator && now - auth.lastSend >= 16) {
                auth.lastSend = now;
                const std::uint32_t target = auth.predictor.ConfirmedTick() + auth.lead;
                const std::uint16_t input = auth.scripted ? SimHarnessScripted(auth.ack.player, target)
                                                          : static_cast<std::uint16_t>(controlState & 0xFFFF);
                auth.predictor.SetLocalInput(target, input);
                Protocol3::Input in;
                in.firstTick = target >= 15 ? target - 15 : 0;
                for (std::uint32_t k = 0; k < 16; ++k) in.controls.push_back(auth.predictor.LocalInput(in.firstTick + k));
                auth.conn.Send(Protocol3::kInput, Protocol3::Encode(in));
            }
        }

        // Draw the prediction: the newest confirmed state, run forward to the tick the player is looking at.
        if (!auth.needSnapshot) {
            std::string error;
            if (auth.verify) {
                // Test mode: restoring into the rendered session must reproduce the confirmed state exactly.
                if (auth.world->RestoreSnapshot(auth.predictor.ConfirmedSnapshot(), error)) {
                    ++auth.restoreChecks;
                    if (MR_HashLevelState(session.Level(), session.GetSimulationTime()) != auth.predictor.Confirmed().Hash()) ++auth.restoreFailures;
                }
                else {
                    ++auth.restoreFailures;
                }
            }
            const std::uint32_t confirmed = auth.predictor.ConfirmedTick();
            if (auth.resultReceived && !auth.spectator && !auth.verify) {
                // The race is over (everyone has finished, or time ran out) and the server has stopped. The craft is not left where the server's last
                // state put it: it is restored to that state once, and then driven on from the player's own keys, as in single player.
                if (!auth.freeDriving) {
                    auth.predictor.PredictOn(*auth.world, confirmed, error);
                    auth.freeDriving = true;
                    auth.freeMs = now;
                    auth.freeAcc = 0;
                    auth.freeTicks = 0;
                    session.AddMessage("The race is over. Keep driving as long as you like");
                    if (std::getenv("HOVERNET_TEST_FREEDRIVE") != nullptr) { const MR_MainCharacter* me = auth.craft[static_cast<std::size_t>(auth.ack.player)]; std::printf("FREEDRIVETEST position at the result (%d,%d)\n", me->mPosition.mX, me->mPosition.mY); std::fflush(stdout); }
                }
                else {
                    auth.freeAcc += std::min<double>(5.0, (now - auth.freeMs) / 16.0);
                    auth.freeMs = now;
                    std::vector<std::uint16_t> controls(static_cast<std::size_t>(auth.ack.players), 0);
                    while (auth.freeAcc >= 1.0) {
                        controls[static_cast<std::size_t>(auth.ack.player)] = auth.scripted ? SimHarnessScripted(auth.ack.player, auth.freeTicks)
                                                                                           : static_cast<std::uint16_t>(controlState & 0xFFFF);
                        auth.world->StepTick(controls.data());
                        auth.freeAcc -= 1.0;
                        ++auth.freeTicks;
                    }
                    if (std::getenv("HOVERNET_TEST_FREEDRIVE") != nullptr && auth.freeTicks >= 200) {
                        // Test hook (scripts/test-free-drive.sh): after the race the craft must still be driven by its player.
                        const MR_MainCharacter* me = auth.craft[static_cast<std::size_t>(auth.ack.player)];
                        std::printf("FREEDRIVETEST position after the result (%d,%d); free-driven ticks %u\n", me->mPosition.mX, me->mPosition.mY, auth.freeTicks);
                        std::fflush(stdout);
                        return 1;
                    }
                }
            }
            else if (auth.verify) {
                // Test mode: the picture is exactly the confirmed state.
                auth.predictor.PredictOn(*auth.world, confirmed, error);
                auth.shownTick = confirmed;
            }
            else {
                // The picture's tick follows the clock; it is eased toward confirmed + lead (a spectator: toward the confirmed tick) instead of
                // being tied to it, so Ticks that arrive in a burst after a stall move the craft smoothly, not in a jump.
                const double ahead = auth.spectator ? 0.0 : static_cast<double>(auth.lead);
                const double ideal = confirmed + ahead;
                if (!auth.displayInit || auth.needSnapshot) { auth.displayTick = ideal; auth.displayInit = true; auth.displayMs = now; }
                const double elapsed = std::min<double>(8.0, (now - auth.displayMs) / 16.0);
                auth.displayMs = now;
                if (confirmed == 0) auth.displayTick = ideal;   // the race has not produced its first tick: nothing to run ahead of
                auth.displayTick += confirmed == 0 ? 0.0 : elapsed;
                const double error2 = ideal - auth.displayTick;
                if (std::fabs(error2) > 40.0) auth.displayTick = ideal;                       // lost sync (a long stall): jump
                else auth.displayTick += std::max(-0.12, std::min(0.12, error2 * 0.02)) * elapsed;   // otherwise ease: at most 12% faster or slower than the clock
                if (auth.spectator && auth.displayTick > confirmed) auth.displayTick = confirmed;   // a spectator never runs past what is known
                if (auth.displayTick < confirmed) auth.displayTick = confirmed;
                auth.shownTick = static_cast<std::uint32_t>(auth.displayTick);
                const Uint64 predictStart = SDL_GetPerformanceCounter();
                auth.predictor.PredictOn(*auth.world, auth.shownTick, error);
                // The simulation moves in whole 16 ms ticks but frames do not fall on tick boundaries (a 60 Hz screen shows 1 tick in most frames,
                // sometimes 0 or 2; a 30 Hz one alternates 2 and 3), so showing the whole tick makes the speed judder. Single player does not: its
                // clock is continuous. So the craft are drawn between this tick and the next, by how far the clock is into it.
                const double frac = auth.displayTick - auth.shownTick;
                if (frac > 0.01 && !auth.interpolationOff && !auth.craft.empty()) {
                    struct Pose { MR_3DCoordinate position; MR_Angle heading; int room; };
                    std::vector<Pose> before;
                    for (const MR_MainCharacter* c : auth.craft) before.push_back({c->mPosition, c->GetCabinOrientation(), c->mRoom});
                    auth.predictor.PredictOn(*auth.world, auth.shownTick + 1, error);
                    for (std::size_t i = 0; i < auth.craft.size(); ++i) {
                        MR_MainCharacter* c = auth.craft[i];
                        const Pose& a = before[i];
                        const double dx = c->mPosition.mX - a.position.mX, dy = c->mPosition.mY - a.position.mY, dz = c->mPosition.mZ - a.position.mZ;
                        if (dx * dx + dy * dy + dz * dz > 4000.0 * 4000.0) continue;   // a respawn or a teleport: no sweeping across the track
                        c->mPosition.mX = a.position.mX + static_cast<int>(std::lround(dx * frac));
                        c->mPosition.mY = a.position.mY + static_cast<int>(std::lround(dy * frac));
                        c->mPosition.mZ = a.position.mZ + static_cast<int>(std::lround(dz * frac));
                        int turn = (c->GetCabinOrientation() - a.heading) % MR_2PI;
                        if (turn > MR_2PI / 2) turn -= MR_2PI; else if (turn < -MR_2PI / 2) turn += MR_2PI;
                        c->SetDisplayOrientation(MR_NORMALIZE_ANGLE(a.heading + static_cast<int>(std::lround(turn * frac))));
                        if (frac < 0.5) c->mRoom = a.room;
                    }
                }
                if (std::getenv("HOVERNET_TEST_PREDICT_COST") != nullptr) {
                    static double sTotal = 0, sMax = 0; static int sN = 0;
                    const double ms = (SDL_GetPerformanceCounter() - predictStart) * 1000.0 / SDL_GetPerformanceFrequency();
                    sTotal += ms; sMax = std::max(sMax, ms);
                    if (++sN % 300 == 0) { std::printf("PREDICTCOST avg %.2f ms max %.2f ms over %d frames (ticks replayed this frame %u, lead %u)\n", sTotal / sN, sMax, sN, auth.shownTick - confirmed, auth.lead); std::fflush(stdout); }
                }
            }
        }
        // Test instrumentation (HOVERNET_TEST_JITTER): how smoothly does the craft move on screen? Per frame: how many ticks the picture advanced
        // against how many the clock says it should have (1.0 = exactly in step), and how far the craft jumped.
        static std::vector<double> sRatios, sJumps, sDts, sSteps, sVel, sOtherVel, sOtherJump;
        static double sOtherX = 0, sOtherY = 0;
        static Uint32 sLastFrame = 0;
        static std::uint32_t sLastDisplay = 0;
        static double sLastX = 0, sLastY = 0;
        static bool sHave = false;
        if (std::getenv("HOVERNET_TEST_JITTER") != nullptr && !auth.needSnapshot && !auth.resultReceived && !auth.spectator && auth.shownTick != 0) {
            const std::uint32_t displayTick = auth.shownTick;
            const MR_MainCharacter* me = auth.craft[static_cast<std::size_t>(auth.ack.player)];
            const double x = me->mPosition.mX, y = me->mPosition.mY;
            static int sWarmup = 0;
            if (sHave && now > sLastFrame && ++sWarmup > 30) {
                const double expected = (now - sLastFrame) / 16.0;
                sRatios.push_back((static_cast<double>(displayTick) - sLastDisplay) / expected);
                sJumps.push_back(std::sqrt((x - sLastX) * (x - sLastX) + (y - sLastY) * (y - sLastY)));
                sDts.push_back(static_cast<double>(now - sLastFrame));
                sVel.push_back(sJumps.back() / static_cast<double>(now - sLastFrame));
                if (auth.ack.players > 1) {
                    const MR_MainCharacter* other = auth.craft[static_cast<std::size_t>((auth.ack.player + 1) % auth.ack.players)];
                    const double jx = other->mPosition.mX - sOtherX, jy = other->mPosition.mY - sOtherY;
                    sOtherJump.push_back(std::sqrt(jx * jx + jy * jy));
                    sOtherVel.push_back(sOtherJump.back() / static_cast<double>(now - sLastFrame));
                }
                sSteps.push_back(static_cast<double>(displayTick) - sLastDisplay);
            }
            if (auth.ack.players > 1) { const MR_MainCharacter* other = auth.craft[static_cast<std::size_t>((auth.ack.player + 1) % auth.ack.players)]; sOtherX = other->mPosition.mX; sOtherY = other->mPosition.mY; }
            sHave = true; sLastFrame = now; sLastDisplay = displayTick; sLastX = x; sLastY = y;
        }
        if (auth.resultReceived && std::getenv("HOVERNET_TEST_JITTER") != nullptr && !sRatios.empty()) {
            std::vector<double> r = sRatios, j = sJumps;
            std::sort(r.begin(), r.end()); std::sort(j.begin(), j.end());
            auto pct = [](const std::vector<double>& v, double q) { return v[static_cast<std::size_t>(q * (v.size() - 1))]; };
            std::printf("JITTERTEST frames=%zu pace(ticks shown per tick elapsed) p5=%.2f p50=%.2f p95=%.2f max=%.2f   craft jump per frame (mm) p50=%.0f p95=%.0f max=%.0f\n",
                        r.size(), pct(r, 0.05), pct(r, 0.5), pct(r, 0.95), r.back(), pct(j, 0.5), pct(j, 0.95), j.back());
            std::fflush(stdout);
            {
                std::sort(sDts.begin(), sDts.end());
                int zero = 0, multi = 0;
                for (double d : sSteps) { if (d == 0) ++zero; else if (d >= 2) ++multi; }
                std::printf("JITTERTEST frame time (ms) p5=%.0f p50=%.0f p95=%.0f max=%.0f; frames showing 0 new ticks %d, 2 or more %d of %zu\n", pct(sDts, 0.05), pct(sDts, 0.5), pct(sDts, 0.95), sDts.back(), zero, multi, sSteps.size());
            }
            {
                // How much the craft's on-screen speed (distance per ms of real time) changes from one frame to the next, against its average:
                // 0 is perfectly steady motion.
                double change = 0, mean = 0;
                for (std::size_t i = 0; i < sVel.size(); ++i) { mean += sVel[i]; if (i > 0) change += std::fabs(sVel[i] - sVel[i - 1]); }
                mean /= static_cast<double>(sVel.size());
                std::printf("JITTERTEST on-screen speed changes by %.1f%% of its average from frame to frame\n", 100.0 * change / static_cast<double>(sVel.size() - 1) / mean);
            }
            if (!sOtherVel.empty()) {
                double change = 0, mean = 0;
                for (std::size_t i = 0; i < sOtherVel.size(); ++i) { mean += sOtherVel[i]; if (i > 0) change += std::fabs(sOtherVel[i] - sOtherVel[i - 1]); }
                mean /= static_cast<double>(sOtherVel.size());
                std::sort(sOtherJump.begin(), sOtherJump.end());
                std::printf("JITTERTEST another player's craft: on-screen speed changes by %.1f%% of its average from frame to frame; jump per frame (mm) p50=%.0f p95=%.0f max=%.0f\n",
                            100.0 * change / static_cast<double>(sOtherVel.size() - 1) / mean, pct(sOtherJump, 0.5), pct(sOtherJump, 0.95), sOtherJump.back());
            }
            sRatios.clear(); sJumps.clear(); sDts.clear(); sSteps.clear(); sVel.clear(); sOtherVel.clear(); sOtherJump.clear();
            return 1;   // the measurement is done: leave (exit status 0)
        }
        if (auth.resultReceived && auth.verify) {
            std::printf("auth client: %u ticks verified, %u tick failures, %u restore checks, %u restore failures%s\n", auth.verified, auth.tickFailures,
                        auth.restoreChecks, auth.restoreFailures, auth.reconnected ? ", reconnected" : (auth.dropAfter != 0 ? ", DID NOT RECONNECT" : ""));
            std::fflush(stdout);
            const bool reconnectOk = auth.dropAfter == 0 || auth.reconnected;
            return (auth.tickFailures == 0 && auth.restoreFailures == 0 && auth.verified > 0 && reconnectOk) ? 1 : 2;
        }
        return 0;
    };
#endif

    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
#ifdef HOVERNET_WEB
        // The page shows the on-screen race controls only while this is set. It is cleared before
        // any menu screen opened from here (pause menu and what it leads to, results) and set
        // again at the top of every race frame.
        gHoverNetInRace = 1;
#endif
        gameController.Refresh();
        bool sceneChanged = renderStats.actorsRendered > 0;
        bool horizontalMovement = false;
        const MR_3DCoordinate previousCamera = camera;
        bool leaveForLobby = false;
        bool startNewLocalRace = false;
        bool watchReplayMenu = false;
#ifdef HOVERNET_GAME2_PLAYER
        if (canChat() && !SDL_IsTextInputActive()) SDL_StartTextInput();   // menus opened from the race stop it
#endif
        bool openResults = false;   // Enter (or the controller's Back button) after a finish: show the results and race options
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
#ifdef HOVERNET_GAME2_PLAYER
                if (playerMode && menuFontHandle != nullptr) {
                    if (ConfirmQuit(graphics, buffer, viewport, *menuFontHandle->GetSprite())) {
                        running = false;
                    }
                }
                else
#endif
                {
                    running = false;
                }
            }
#ifdef HOVERNET_GAME2_PLAYER
            else if (event.type == SDL_TEXTINPUT && canChat()) {
                if (chatBuffer.size() < 60) {
                    chatBuffer += event.text.text;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE &&
                     canChat() && !chatBuffer.empty()) {
                chatBuffer.pop_back();
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN &&
                     canChat() && !chatBuffer.empty()) {
#if defined(HOVERNET_AUTH)
                if (!onlineClient.IsConnected()) {
                    // A 3.0 server sends every message back to everyone in the race, the sender included: it is shown when it returns.
                    Protocol3::Chat chat;
                    chat.text = chatBuffer;
                    auth.conn.Send(Protocol3::kChat, Protocol3::Encode(chat));
                    chatBuffer.clear();
                }
                else
#endif
                {
                    const std::string wireChat = RaceServerClient::EncodeInRaceChat(chatBuffer);
                    onlineClient.SendMessage(eRSMsgChatMessage, wireChat.data(), wireChat.size());
                    session.AddMessage(("You: " + chatBuffer).c_str());
                    chatBuffer.clear();
                }
            }
#endif
#ifdef HOVERNET_GAME2_PLAYER
            else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                     (event.key.keysym.sym == SDLK_LEFTBRACKET || event.key.keysym.sym == SDLK_RIGHTBRACKET ||
                      // Left and Right also work for a spectator, who has no seeking; in a replay they seek five seconds.
                      ((event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_RIGHT)
#if defined(HOVERNET_AUTH)
                       && auth.active && auth.spectator && !replay.active
#else
                       && false
#endif
                      )) &&
                     (replay.active
#if defined(HOVERNET_AUTH)
                      || (auth.active && auth.spectator)
#endif
                     )) {
                cycleWatch((event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_RIGHTBRACKET) ? 1 : -1);
            }
            else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
                     event.key.keysym.sym == SDLK_c &&
                     (replay.active
#if defined(HOVERNET_AUTH)
                      || (auth.active && auth.spectator)
#endif
                     )) {
                // Camera distance for a replay or a spectator: C steps through 50% to 300% of the classic distance and round again (picture only).
                chaseStep = (chaseStep + 1) % kChaseSteps;
                session.AddMessage(("Camera distance " + std::to_string(kChasePercents[chaseStep]) + "%").c_str());
                if (std::getenv("HOVERNET_TEST_WATCH") != nullptr) { std::printf("WATCHTEST camera distance %d%%\n", kChasePercents[chaseStep]); std::fflush(stdout); }
            }
#endif
            else if (resultsPending && ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN && event.key.repeat == 0) ||
                                        (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK))) {
                openResults = true;
            }
            else if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
                     (event.type == SDL_CONTROLLERBUTTONDOWN &&
                      event.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
#ifdef HOVERNET_WEB
                gHoverNetInRace = 0;
#endif
#ifdef HOVERNET_GAME2_PLAYER
                // Used to only show this menu online -- offline play just quit
                // immediately on Escape, with no way back to it short of
                // relaunching, and no way to start a different local race
                // without doing so. Same menu either way now; only the
                // "Leave Race"/"New Local Race" label and what it does differ.
                if (playerMode && menuFontHandle != nullptr) {
                    const bool isOnline = onlineClient.IsConnected();
                    const bool canRestart = lastLocal.valid && !isOnline && !raceWasOnline && !replay.active
#if defined(HOVERNET_AUTH)
                                            && !auth.active
#endif
                        ;
                    const PauseChoice pauseChoice = RunPauseMenu(
                        graphics, buffer, viewport, *menuFontHandle->GetSprite(), isOnline, frameLimit, true, canRestart);
                    if (pauseChoice == PauseChoice::eQuit) {
                        running = false;
                    }
                    else if (pauseChoice == PauseChoice::eLeaveRace) {
                        if (isOnline) {
                            leaveForLobby = true;
                        }
                        else {
                            startNewLocalRace = true;
                        }
                    }
                    else if (pauseChoice == PauseChoice::eWatchReplay) {
                        watchReplayMenu = true;
                    }
                    else if (pauseChoice == PauseChoice::eMainMenu) {
                        toMainMenu = true;
                    }
                    else if (pauseChoice == PauseChoice::eRestartRace) {
                        restartLocal = true;
                    }
                    else if (pauseChoice == PauseChoice::eOnlineLobby) {
                        // Same as "Leave Race" when already online -- reset and
                        // rejoin the lobby -- but always offered, so offline
                        // play has a way there too without quitting first.
                        leaveForLobby = true;
                    }
                    else if (pauseChoice == PauseChoice::eSettings) {
                        const SettingsResult settings = RunSettingsScreen(
                            graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                            LoadUsername(), lobbyHost, lobbyPort, LoadVolume(), LoadMute(), LoadFullscreen(), frameLimit);
                        if (settings.confirmed) {
                            SaveUsername(settings.username);
                            lobbyHost = settings.serverHost;
                            lobbyPort = settings.serverPort;
                            SaveServerUrl(lobbyHost, lobbyPort);
                            SaveVolume(settings.volume);
                            SaveMute(settings.muted);
                            SaveFullscreen(settings.fullscreen);
                            WindowSize savedSize;
                            savedSize.width = settings.windowWidth;
                            savedSize.height = settings.windowHeight;
                            SaveWindowSize(savedSize);
                            SaveUiScale(settings.uiScale);
                            SaveLargeHudText(settings.largeHudText);
                            SaveReducedMotion(settings.reducedMotion);
                            SaveHighContrast(settings.highContrast);
                        }
                    }
                    else if (pauseChoice == PauseChoice::eControls) {
                        RunControlsScreen(graphics, frameLimit);
                        if (g_QuitConfirmed) {
                            running = false;
                        }
                    }
                    else {
                        session.SetSimulationTime(session.GetSimulationTime());
                    }
                }
                else
#endif
                {
                    running = false;
                }
            }
        }

        if (!running) {
            break;
        }
#ifdef HOVERNET_GAME2_PLAYER
        if (watchReplayMenu) {
            const bool started = RunReplayBrowser(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
            if (g_QuitConfirmed) { running = false; break; }
            if (started) {
                missileSeen = false;
                finishAnnounced = false; resultsPending = false;
                wasOnlineConnected = onlineClient.IsConnected();
            }
            continue;
        }
        if (replay.active && replay.ended && !g_QuitConfirmed) {
            // A replay has played to its end: say so, show who finished, and offer what to do next instead of leaving the player at a frozen picture.
            replay.ended = false;
            std::vector<std::string> finishers;
            std::vector<ReplayPlayback::Marker> done;
            for (const auto& m : replay.markers) if (m.finished) done.push_back(m);
            std::sort(done.begin(), done.end(), [](const ReplayPlayback::Marker& a, const ReplayPlayback::Marker& b) { return a.tick < b.tick; });
            for (std::size_t i = 0; i < done.size(); ++i) {
                char line[64];
                const long long ms = static_cast<long long>(done[i].tick) * 16;
                std::snprintf(line, sizeof line, "%zu. Player %d   %lld:%02lld.%02lld", i + 1, done[i].player + 1, ms / 60000, (ms % 60000) / 1000, (ms % 1000) / 10);
                finishers.push_back(line);
            }
            if (finishers.empty()) finishers.push_back("Nobody finished the race.");
            const std::string againPath = replay.path;
            const int againWatch = replay.watch;
            const ReplayEndChoice endChoice = RunReplayEndScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(), replay.data.track, replay.data.players,
                                                                 replay.data.laps, static_cast<long long>(replay.data.ticks) * 16, finishers, frameLimit);
            if (g_QuitConfirmed || endChoice == ReplayEndChoice::eQuit) { running = false; break; }
            if (endChoice == ReplayEndChoice::eMainMenu) { toMainMenu = true; continue; }
            if (endChoice == ReplayEndChoice::eWatchAgain) {
                bool fatal = false;
                const std::string error = startReplayFile(againPath, againWatch, fatal);
                if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); if (fatal) { running = false; break; } toMainMenu = true; }
                else { missileSeen = false; finishAnnounced = false; resultsPending = false; }
                continue;
            }
            if (endChoice == ReplayEndChoice::eAnotherReplay) {
                const bool started = RunReplayBrowser(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
                if (g_QuitConfirmed) { running = false; break; }
                if (started) { missileSeen = false; finishAnnounced = false; resultsPending = false; }
                else replay.ended = true;   // came back without choosing: the end screen again
                continue;
            }
            if (endChoice == ReplayEndChoice::eLeaderboards) {
#if defined(HOVERNET_AUTH)
                const bool started = RunLeaderboards(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
                if (g_QuitConfirmed) { running = false; break; }
                if (started) { missileSeen = false; finishAnnounced = false; resultsPending = false; }
                else replay.ended = true;   // came back without choosing: the end screen again
#else
                replay.ended = true;
#endif
                continue;
            }
            // eStay: look around at the end (Left or Home rewinds)
            session.AddMessage("Esc for the menu; Left or Home rewinds");
        }
        if (restartLocal) {
            // The same local race again: the same track, laps and weapons, from the grid.
            restartLocal = false;
            replay.active = false;
            missileSeen = false;
            finishAnnounced = false; resultsPending = false; resultsSeen = false;
            if (!lastLocal.valid || !loadLocalRace(lastLocal.track, lastLocal.laps, lastLocal.weapons)) { toMainMenu = true; continue; }
            if (std::getenv("HOVERNET_TEST_RESTART") != nullptr) { std::printf("RACETEST restarted %s laps %d weapons %d\n", lastLocal.track.c_str(), lastLocal.laps, lastLocal.weapons ? 1 : 0); std::fflush(stdout); }
            wasOnlineConnected = onlineClient.IsConnected();
            continue;
        }
        if (toMainMenu) {
            // Back to the main menu from anywhere: whatever was running (a race, a replay, a server connection) is left, then the menu runs as at start-up.
            toMainMenu = false;
            replay.active = false;
            missileSeen = false;
            finishAnnounced = false; resultsPending = false;
            resetRaceSession();
            const bool raceReady = showMainMenu();
            if (mainMenuFailed) return 1;
            if (!raceReady || g_QuitConfirmed) { running = false; break; }
            wasOnlineConnected = onlineClient.IsConnected();
            continue;
        }
        if (leaveForLobby) {
            replay.active = false;   // leaving a replay for a race
            missileSeen = false;
            finishAnnounced = false; resultsPending = false;
            if (!resetRaceSession()) { running = false; break; }
            if (!enterOnlineMultiplayer()) {
                // The lobby was left (or the server could not be reached): back to the main menu, not out of the game.
                if (g_QuitConfirmed) { running = false; break; }
                toMainMenu = true;
                continue;
            }
            wasOnlineConnected = onlineClient.IsConnected();
            continue;
        }
        if (startNewLocalRace) {
            replay.active = false;
            const LocalRaceSetup setup = RunLocalRaceSetup(graphics, buffer, viewport,
                                                            *menuFontHandle->GetSprite(), frameLimit);
            if (setup.confirmed) {
                missileSeen = false;
                finishAnnounced = false; resultsPending = false;
                if (!loadLocalRace(setup.trackName, setup.laps, setup.weapons)) {
                    running = false;
                    break;
                }
                wasOnlineConnected = onlineClient.IsConnected();
            }
            continue;
        }
#endif

        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        int controllerSteer = static_cast<int>(gameController.Axis(gControllerBindings.steerAxis));
        if (gControllerBindings.invertSteering) controllerSteer = -controllerSteer;
        const bool controllerLeft = controllerSteer < -gControllerBindings.deadzone;
        const bool controllerRight = controllerSteer > gControllerBindings.deadzone;
        int controllerAccelerateValue =
            static_cast<int>(gameController.Axis(gControllerBindings.accelerateAxis));
        int controllerBrakeValue =
            static_cast<int>(gameController.Axis(gControllerBindings.brakeAxis));
        if (gControllerBindings.invertAccelerate) controllerAccelerateValue = -controllerAccelerateValue;
        if (gControllerBindings.invertBrake) controllerBrakeValue = -controllerBrakeValue;
        const bool controllerAccelerate = controllerAccelerateValue > gControllerBindings.deadzone;
        const bool controllerBrake = controllerBrakeValue > gControllerBindings.deadzone;
#ifdef HOVERNET_WEB
        const int touch = gHoverNetTouchMask;
#else
        const int touch = 0;
#endif
        const bool touchLeft = (touch & 1) != 0, touchRight = (touch & 2) != 0;
        const bool turnLeft = keyboard[gKeyboardBindings.steerLeft] || controllerLeft || touchLeft ||
                              (!playerMode && keyboard[SDL_SCANCODE_A]);
        const bool turnRight = keyboard[gKeyboardBindings.steerRight] || controllerRight || touchRight ||
                               (!playerMode && keyboard[SDL_SCANCODE_D]);
        const bool moveForward = playerMode ?
            keyboard[gKeyboardBindings.accelerate] ||
            (gKeyboardBindings.accelerate == SDL_SCANCODE_LSHIFT && keyboard[SDL_SCANCODE_RSHIFT]) ||
            controllerAccelerate || (touch & 4) != 0 :
            keyboard[SDL_SCANCODE_UP] || keyboard[SDL_SCANCODE_W];
        const bool moveBackward = playerMode ?
            keyboard[gKeyboardBindings.brake] || controllerBrake || (touch & 8) != 0 :
            keyboard[SDL_SCANCODE_DOWN] || keyboard[SDL_SCANCODE_S];
        const bool moveUp = keyboard[SDL_SCANCODE_Q];
        const bool moveDown = keyboard[SDL_SCANCODE_E];
        const bool fire = keyboard[gKeyboardBindings.fire] ||
                  (gKeyboardBindings.fire == SDL_SCANCODE_LCTRL && keyboard[SDL_SCANCODE_RCTRL]) ||
                  gameController.Button(gControllerBindings.fire) || (touch & 16) != 0 ||
                  HasArgument(argc, argv, "--fire");
        const bool jump = keyboard[gKeyboardBindings.jump] ||
                          gameController.Button(gControllerBindings.jump) || (touch & 32) != 0 ||
                          HasArgument(argc, argv, "--jump");
        const bool selectWeapon = keyboard[gKeyboardBindings.selectWeapon] ||
                                  gameController.Button(gControllerBindings.selectWeapon) || (touch & 64) != 0 ||
                                  HasArgument(argc, argv, "--select-weapon");

#ifdef HOVERNET_GAME2_PLAYER
        if (playerMode && mainCharacter != nullptr && level != nullptr) {
#ifdef HOVERNET_WEB
            // VR controller button: switch between the chase view and the cockpit view.
            {
                static bool viewHeld = false;
                const bool viewDown = (touch & eTouchToggleView) != 0;
                if (viewDown && !viewHeld) {
                    cockpitView = !cockpitView;
                    observer->SetCockpitView(cockpitView ? TRUE : FALSE);
                }
                viewHeld = viewDown;
            }
#endif
            if (keyboard[SDL_SCANCODE_F3]) {
                debugView = false;
                cockpitView = false;
                observer->SetCockpitView(FALSE);
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_F4]) {
                debugView = false;
                cockpitView = true;
                observer->SetCockpitView(TRUE);
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_F5]) {
                observer->PlayersListPageDn();
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_F6]) {
                observer->MoreMessages();
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_EQUALS] || keyboard[SDL_SCANCODE_KP_PLUS]) {
                observer->ReduceMargin();
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_MINUS] || keyboard[SDL_SCANCODE_KP_MINUS]) {
                observer->EnlargeMargin();
                SDL_Delay(150);
            }
            if (keyboard[SDL_SCANCODE_PAGEUP]) observer->Scroll(1);
            if (keyboard[SDL_SCANCODE_PAGEDOWN]) observer->Scroll(-1);
            if (keyboard[SDL_SCANCODE_INSERT]) observer->ZoomIn();
            if (keyboard[SDL_SCANCODE_DELETE]) observer->ZoomOut();
            if (keyboard[SDL_SCANCODE_HOME]) observer->Home();
        }
#endif

        if (playerMode && mainCharacter != nullptr && level != nullptr) {
#ifdef HOVERNET_GAME2_PLAYER
            {
                // Tell the player how to reach the pause menu as each race begins (the simulation clock
                // starts a race well below zero and counts up, so a big backwards jump means a new race).
                const MR_SimulationTime raceClock = session.GetSimulationTime();
                if (raceClock + 1000 < lastRaceClock) pauseHintShown = false;
                lastRaceClock = raceClock;
                if (!pauseHintShown && raceClock < 0) {
                    pauseHintShown = true;
                    // Shown newest first, so the craft hint is added first and sits below the pause hint.
                    if (HoverNetIsTouchDevice()) {
                        session.AddMessage("Drag stick: pick your craft");
                        session.AddMessage("Tap MENU for the pause menu");
                    }
                    else {
                        const std::string craftHint = std::string(SDL_GetScancodeName(gKeyboardBindings.steerLeft)) + "/" +
                            SDL_GetScancodeName(gKeyboardBindings.steerRight) + ": pick your craft";
                        session.AddMessage(craftHint.c_str());
                        session.AddMessage("Press Esc for the pause menu");
                    }
                }
            }
#endif
            int controlState = 0;
            if (autoPlay || moveForward) controlState |= MR_MainCharacter::eMotorOn;
            if (turnLeft) controlState |= MR_MainCharacter::eLeft;
            if (turnRight) controlState |= MR_MainCharacter::eRight;
            if (moveBackward) controlState |= MR_MainCharacter::eBreakDirection;
            if (fire) controlState |= MR_MainCharacter::eFire;
            if (jump) controlState |= MR_MainCharacter::eJump;
            if (selectWeapon) controlState |= MR_MainCharacter::eSelectWeapon;
#ifdef HOVERNET_GAME2_PLAYER
            if (!replay.active
#if defined(HOVERNET_AUTH)
                && !auth.active
#endif
                )
#endif
            session.SetControlState(controlState, 0);
#ifdef HOVERNET_WEB
            // The simulation steps by wall-clock time, so after a browser stall (hidden tab, a
            // long GC or decode pause, a slow phone frame) it used to replay the whole gap in one
            // go: hovercraft jumped into walls and platforms and missiles skipped past targets.
            // Drop gaps over a quarter of a second instead of replaying them.
            {
                static Uint32 lastSimTicks = 0;
                const Uint32 nowTicks = SDL_GetTicks();
                if (lastSimTicks != 0 && nowTicks - lastSimTicks > 250) {
                    session.SetSimulationTime(session.GetSimulationTime());
                }
                lastSimTicks = nowTicks;
            }
#endif
#ifdef HOVERNET_GAME2_PLAYER
            if (replay.active) {
                // Replay clock: real time scaled by the chosen speed, in whole 16 ms ticks (or a fixed number per frame in test mode).
                const Uint32 nowTicks = SDL_GetTicks();
                const Uint32 elapsed = nowTicks - replay.lastTicks;
                replay.lastTicks = nowTicks;
                int ticks = 0;
                if (replay.hashMode) {
                    ticks = 8;
                }
                else if (!replay.paused) {
                    replay.carryMs += std::min<double>(elapsed, 250.0) * kReplaySpeeds[replay.speedStep];
                    ticks = static_cast<int>(replay.carryMs / 16.0);
                    replay.carryMs -= ticks * 16.0;
                }
                // Seeking: Left and Right jump 5 seconds back and forward, Home restarts. The state is restored from the nearest
                // snapshot taken while the replay played (every 250 ticks) and stepped forward to the target.
                int seekTarget = -1;
                if (keyboard[SDL_SCANCODE_LEFT]) seekTarget = std::max<int>(0, static_cast<int>(replay.tick) - 312);
                if (keyboard[SDL_SCANCODE_RIGHT]) seekTarget = std::min<int>(static_cast<int>(replay.data.ticks), static_cast<int>(replay.tick) + 312);
                if (keyboard[SDL_SCANCODE_HOME]) seekTarget = 0;
                if (replay.seekTest && !replay.seekTestDone && replay.tick >= 1000) { replay.seekTestDone = true; seekTarget = 400; }
                // Lap markers: "," goes to a moment before the previous lap crossing, "." to before the next one (as far as the replay
                // has played; the markers are found as it goes). Clicking the timeline bar seeks to that point.
                {
                    const int kLeadTicks = 125;   // two seconds before the crossing, so the viewer sees it arrive
                    const bool previous = keyboard[SDL_SCANCODE_COMMA] != 0, next = keyboard[SDL_SCANCODE_PERIOD] != 0;
                    const bool testJump = replay.markerTest && !replay.markerTestDone && replay.tick >= 8000;
                    if (previous || next || testJump) {
                        int target = -1;
                        if (next) {
                            for (const auto& m : replay.markers) {
                                const int t = static_cast<int>(m.tick) - kLeadTicks;
                                if (t > static_cast<int>(replay.tick) && (target < 0 || t < target)) target = t;
                            }
                        }
                        else {
                            for (const auto& m : replay.markers) {
                                const int t = std::max(0, static_cast<int>(m.tick) - kLeadTicks);
                                if (t < static_cast<int>(replay.tick) - 30 && t > target) target = t;
                            }
                        }
                        if (testJump) { replay.markerTestDone = true; std::printf("marker jump from %u to %d with %zu markers\n", replay.tick, target, replay.markers.size()); }
                        if (target >= 0) { seekTarget = target; if (!testJump) SDL_Delay(150); }
                    }
                    int mx = 0, my = 0;
                    const bool down = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
                    if (down && !replay.mouseWasDown && !replay.hashMode && graphics.GetRenderer() != nullptr) {
                        float lx = 0, ly = 0;
                        SDL_RenderWindowToLogical(graphics.GetRenderer(), mx, my, &lx, &ly);
                        const int barX = 20, barW = kWidth - 40, barY = kHeight - 14;
                        if (replay.data.ticks > 0 && lx >= barX && lx <= barX + barW && ly >= barY - 12 && ly <= barY + 14) {
                            seekTarget = static_cast<int>(static_cast<std::int64_t>(replay.data.ticks) * (static_cast<int>(lx) - barX) / barW);
                            seekTarget = std::max(0, std::min<int>(seekTarget, static_cast<int>(replay.data.ticks)));
                        }
                    }
                    replay.mouseWasDown = down;
                }
                if (seekTarget >= 0) {
                    auto best = replay.snapshots.upper_bound(static_cast<std::uint32_t>(seekTarget));
                    if (best != replay.snapshots.begin()) {
                        --best;
                        std::vector<MR_FreeElement*> external(replay.craft.begin(), replay.craft.end());
                        std::int64_t restoredTime = 0;
                        std::string seekError;
                        if (MR_RestoreLevelState(session.Level(), best->second.data(), best->second.size(), external, ReplayFormat::kSimulationRules, restoredTime, seekError)) {
                            session.SetSimulationTime(static_cast<MR_SimulationTime>(restoredTime));
                            replay.tick = best->first;
                            replay.finished = false;
                            ticks = std::max<int>(0, seekTarget - static_cast<int>(replay.tick));   // run up to the target now
                            if (!replay.seekTest) SDL_Delay(150);   // debounce the key
                        }
                    }
                }
                ticks = std::min<int>(ticks, static_cast<int>(replay.data.ticks - replay.tick));
                while (ticks > 0) {
                    // Take a snapshot at every 250th tick on the way (so a later seek can start from it).
                    if (replay.snapshots.find(replay.tick) == replay.snapshots.end() && replay.tick % 250 == 0) {
                        std::vector<MR_FreeElement*> external(replay.craft.begin(), replay.craft.end());
                        replay.snapshots[replay.tick] = MR_SaveLevelState(session.Level(), session.GetSimulationTime(), external, ReplayFormat::kSimulationRules);
                    }
                    // One tick at a time (the result is the same as in larger steps, see HOVERNET_REPLAY_TRACE) so a lap crossing
                    // is noted at its exact tick.
                    const int n = 1;
                    session.StepTicks(n, ReplayTickHook, &replay);
                    replay.tick += static_cast<std::uint32_t>(n);
                    ticks -= n;
                    for (int p = 0; p < replay.data.players; ++p) {
                        const MR_MainCharacter* c = replay.craft[static_cast<std::size_t>(p)];
                        int& seen = replay.lapsSeen[static_cast<std::size_t>(p)];
                        if (c->GetLap() > seen) {
                            seen = c->GetLap();
                            const bool known = std::any_of(replay.markers.begin(), replay.markers.end(),
                                [&](const ReplayPlayback::Marker& m) { return m.player == p && m.lap == seen; });
                            if (!known) replay.markers.push_back({replay.tick, p, seen, c->HasFinish() != 0});
                        }
                        else if (c->GetLap() < seen) seen = c->GetLap();   // after a seek back
                    }
                    if (std::getenv("HOVERNET_REPLAY_TRACE") != nullptr) std::printf("%u %016llx\n", replay.tick, static_cast<unsigned long long>(MR_HashLevelState(session.Level(), session.GetSimulationTime())));
                }
                if (replay.tick >= replay.data.ticks && !replay.finished) {
                    replay.finished = true;
                    session.AddMessage("End of the replay");
                    if (!replay.hashMode) replay.ended = true;
                    if (replay.hashMode) {
                        if (replay.markerTest) {
                            for (const auto& m : replay.markers) std::printf("marker tick %u player %d lap %d%s\n", m.tick, m.player, m.lap, m.finished ? " finish" : "");
                        }
                        std::printf("replay hash %016llx ticks %u\n",
                                    static_cast<unsigned long long>(MR_HashLevelState(session.Level(), session.GetSimulationTime())), replay.tick);
                        std::fflush(stdout);
                        return 0;
                    }
                }
                if (keyboard[SDL_SCANCODE_P]) { replay.paused = !replay.paused; SDL_Delay(200); }
                for (int step = 0; step < 5; ++step) if (keyboard[SDL_SCANCODE_1 + step]) replay.speedStep = step;
            }
#if defined(HOVERNET_AUTH)
            else if (auth.active) {
                const int authExit = stepAuth(controlState);
                if (authExit != 0) return authExit - 1;
            }
#endif
            else {
                session.Process();
#ifndef HOVERNET_WEB
                // Test instrumentation (HOVERNET_TEST_JITTER_SP, with --autoplay): the same measurement as HOVERNET_TEST_JITTER gives online
                // (ticks shown per tick of real time, craft jump per frame), for single player, so the two can be compared.
                if (std::getenv("HOVERNET_TEST_JITTER_SP") != nullptr && mainCharacter != nullptr && session.GetSimulationTime() > 0) {
                    static std::vector<double> sRatios, sJumps, sDts, sVel;
                    static Uint32 sLastFrame = 0;
                    static double sLastTick = 0, sLastX = 0, sLastY = 0;
                    static int sWarmup = 0;
                    const Uint32 nowMs = SDL_GetTicks();
                    const double tick = session.GetSimulationTime() / 16.0;
                    const double x = mainCharacter->mPosition.mX, y = mainCharacter->mPosition.mY;
                    if (sLastFrame != 0 && nowMs > sLastFrame && ++sWarmup > 30) {
                        sRatios.push_back((tick - sLastTick) / ((nowMs - sLastFrame) / 16.0));
                        sJumps.push_back(std::sqrt((x - sLastX) * (x - sLastX) + (y - sLastY) * (y - sLastY)));
                        sDts.push_back(static_cast<double>(nowMs - sLastFrame));
                        sVel.push_back(sJumps.back() / static_cast<double>(nowMs - sLastFrame));
                    }
                    sLastFrame = nowMs; sLastTick = tick; sLastX = x; sLastY = y;
                    if (sRatios.size() >= 600) {
                        std::sort(sRatios.begin(), sRatios.end()); std::sort(sJumps.begin(), sJumps.end());
                        auto pct = [](const std::vector<double>& v, double q) { return v[static_cast<std::size_t>(q * (v.size() - 1))]; };
                        std::printf("JITTERTEST single player frames=%zu pace p5=%.2f p50=%.2f p95=%.2f max=%.2f   craft jump per frame (mm) p50=%.0f p95=%.0f max=%.0f\n",
                                    sRatios.size(), pct(sRatios, 0.05), pct(sRatios, 0.5), pct(sRatios, 0.95), sRatios.back(), pct(sJumps, 0.5), pct(sJumps, 0.95), sJumps.back());
                        double change = 0, mean = 0;
                        for (std::size_t i = 0; i < sVel.size(); ++i) { mean += sVel[i]; if (i > 0) change += std::fabs(sVel[i] - sVel[i - 1]); }
                        mean /= static_cast<double>(sVel.size());
                        std::printf("JITTERTEST on-screen speed changes by %.1f%% of its average from frame to frame (single player)\n", 100.0 * change / static_cast<double>(sVel.size() - 1) / mean);
                        std::sort(sDts.begin(), sDts.end());
                        std::printf("JITTERTEST single player frame time (ms) p5=%.0f p50=%.0f p95=%.0f max=%.0f\n", pct(sDts, 0.05), pct(sDts, 0.5), pct(sDts, 0.95), sDts.back());
                        std::fflush(stdout);
                        return 0;
                    }
                }
#endif
            }
#else
            session.Process();
#endif

#ifdef HOVERNET_GAME2_PLAYER
            if (wasOnlineConnected && !onlineClient.IsConnected()) {
                wasOnlineConnected = false;
                session.AddMessage("Connection to server lost -- continuing offline");
            }
#ifdef HOVERNET_WEB
            const bool testResults = gHoverNetTestResults != 0;
            gHoverNetTestResults = 0;
#else
            const bool testResults = false;
#endif
            // Test hook (scripts/test-finish-flow.sh): act as if the player crossed the line on frame 30, then press Enter on frame 80.
            bool testFinish = false;
            static int sTestFrames = 0;
            if (std::getenv("HOVERNET_TEST_CHAT") != nullptr) {
                // Test hook (scripts/test-chat-after-finish.sh): type a message on frame 120 and send it on frame 125.
                if (sTestFrames == 120) {
                    SDL_Event text;
                    SDL_memset(&text, 0, sizeof(text));
                    text.type = SDL_TEXTINPUT;
                    SDL_strlcpy(text.text.text, "hello from the finish line", sizeof(text.text.text));
                    SDL_PushEvent(&text);
                }
                if (sTestFrames == 125) {
                    SDL_Event key;
                    SDL_memset(&key, 0, sizeof(key));
                    key.type = SDL_KEYDOWN; key.key.state = SDL_PRESSED; key.key.keysym.sym = SDLK_RETURN; key.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    SDL_PushEvent(&key);
                }
            }
            if (const char* pauseAt = std::getenv("HOVERNET_TEST_PAUSE_AT_FRAME")) {   // test hook (scripts/test-main-menu.sh): press Escape at this frame
                static int sPauseFrames = 0;
                if (++sPauseFrames == std::atoi(pauseAt)) {
                    SDL_Event key;
                    SDL_memset(&key, 0, sizeof(key));
                    key.type = SDL_KEYDOWN; key.key.state = SDL_PRESSED; key.key.keysym.sym = SDLK_ESCAPE; key.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                    SDL_PushEvent(&key);
                }
            }
            if (std::getenv("HOVERNET_TEST_WATCH") != nullptr) {
                // Test hook (scripts/test-spectator-camera.sh): Right on frame 40, then Left twice on frame 60 and 80.
                ++sTestFrames;
                if (sTestFrames == 100) {   // C steps the chase camera distance (a replay or a spectator)
                    SDL_Event key;
                    SDL_memset(&key, 0, sizeof(key));
                    key.type = SDL_KEYDOWN; key.key.state = SDL_PRESSED; key.key.keysym.sym = SDLK_c; key.key.keysym.scancode = SDL_SCANCODE_C;
                    SDL_PushEvent(&key);
                }
                if (sTestFrames == 40 || sTestFrames == 60 || sTestFrames == 80) {
                    SDL_Event key;
                    SDL_memset(&key, 0, sizeof(key));
                    key.type = SDL_KEYDOWN; key.key.state = SDL_PRESSED;
                    // In a replay Left and Right seek, so the camera keys there are [ and ]; a spectator can use either pair.
                    if (replay.active) {
                        key.key.keysym.sym = sTestFrames == 40 ? SDLK_RIGHTBRACKET : SDLK_LEFTBRACKET;
                        key.key.keysym.scancode = sTestFrames == 40 ? SDL_SCANCODE_RIGHTBRACKET : SDL_SCANCODE_LEFTBRACKET;
                    }
                    else {
                        key.key.keysym.sym = sTestFrames == 40 ? SDLK_RIGHT : SDLK_LEFT;
                        key.key.keysym.scancode = sTestFrames == 40 ? SDL_SCANCODE_RIGHT : SDL_SCANCODE_LEFT;
                    }
                    SDL_PushEvent(&key);
                }
            }
            if (std::getenv("HOVERNET_TEST_FINISH") != nullptr || std::getenv("HOVERNET_TEST_CHAT") != nullptr) {
                ++sTestFrames;
                testFinish = sTestFrames == (std::getenv("HOVERNET_TEST_FINISH_FRAME") ? std::atoi(std::getenv("HOVERNET_TEST_FINISH_FRAME")) : 30);
                if (sTestFrames == 70 && !resultsSeen) std::printf("FINISHTEST still driving, results screen not open\n");
                if (sTestFrames == 80) {
                    SDL_Event key;
                    SDL_memset(&key, 0, sizeof(key));
                    key.type = SDL_KEYDOWN; key.key.state = SDL_PRESSED; key.key.keysym.sym = SDLK_RETURN; key.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    SDL_PushEvent(&key);
                }
            }
            if (!finishAnnounced && (mainCharacter->HasFinish() || testResults || testFinish)) {
                finishAnnounced = true;
                resultsPending = true;
                resultsSeen = false;
                finishTotalTime = mainCharacter->GetTotalTime();
                finishBestLap = mainCharacter->GetBestLapDuration();
                if (testFinish && std::getenv("HOVERNET_TEST_FINISH_TIME") != nullptr) {   // test hook (scripts/test-personal-bests.sh): a chosen time
                    finishTotalTime = std::atoi(std::getenv("HOVERNET_TEST_FINISH_TIME"));
                    finishBestLap = finishTotalTime / 3;
                }
                {
                    // Once per finish: offer the times to the player's records (a time of zero means nothing was timed).
                    const std::string bestsPath = ConfigDirPath() + "/personal_bests";
                    PersonalBests bests;
                    bests.Load(bestsPath);
                    finishBests = bests.Record(currentTrackName.empty() ? initialTrackName : currentTrackName, session.GetNbLap(), finishTotalTime, finishBestLap);
                    if (finishBests.newRaceBest || finishBests.newLapBest) bests.Save(bestsPath);
                    bool ghostWatching = replay.active;
#if defined(HOVERNET_AUTH)
                    ghostWatching = ghostWatching || (auth.active && auth.spectator);
#endif
                    if (finishBests.newRaceBest && !ghostWatching && !ghostRecording.Empty() && finishTotalTime > 0) {
                        // A new best finish: this run becomes the ghost for next time.
                        ghostRecording.track = currentTrackName.empty() ? initialTrackName : currentTrackName;
                        ghostRecording.laps = session.GetNbLap();
                        ghostRecording.finishMs = static_cast<std::int32_t>(finishTotalTime);
                        const bool saved = ghostRecording.Save(ConfigDirPath() + "/" + GhostRun::FileName(ghostRecording.track, ghostRecording.laps));
                        if (std::getenv("HOVERNET_TEST_GHOST") != nullptr) { std::printf("GHOSTTEST %s %zu samples\n", saved ? "saved" : "not saved", ghostRecording.samples.size()); std::fflush(stdout); }
                    }
                }
                lastResultsHint = SDL_GetTicks();
                if (testResults) {
                    openResults = true;   // the browser smoke test opens the screen directly
                }
                else {
                    session.AddMessage(HoverNetIsTouchDevice() ? "Finished! Keep driving, or press MENU for race options"
                                                               : "Finished! Keep driving, or press Enter for your results");
                    if (testFinish) { std::printf("FINISHTEST finished, race still running\n"); std::fflush(stdout); }
                }
            }
            else if (resultsPending && !resultsSeen && !openResults && SDL_GetTicks() - lastResultsHint > 12000) {
                lastResultsHint = SDL_GetTicks();
                session.AddMessage(HoverNetIsTouchDevice() ? "Press MENU for race options" : "Press Enter for your results and race options");
            }
            if (openResults && resultsPending) {
#ifdef HOVERNET_WEB
                gHoverNetInRace = 0;
#endif
                const PostRaceChoice resultChoice = RunPostRaceScreen(
                    graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                    finishTotalTime, finishBestLap,
                    session.GetRank(mainCharacter), session.GetNbPlayers(),
                    frameLimit < 0 ? -1 : 2, &finishBests,
                    lastLocal.valid && !raceWasOnline && !replay.active
#if defined(HOVERNET_AUTH)
                        && !auth.active
#endif
                    );
                if (resultChoice == PostRaceChoice::eQuit) {
                    running = false;
                    continue;
                }
                if (resultChoice == PostRaceChoice::eMainMenu) {
                    toMainMenu = true;
                    continue;
                }
                if (resultChoice == PostRaceChoice::eRestartRace) {
                    restartLocal = true;
                    continue;
                }
                if (resultChoice == PostRaceChoice::eOnlineLobby) {
                    missileSeen = false;
                    finishAnnounced = false; resultsPending = false;
                    if (!resetRaceSession()) running = false;
                    else if (!enterOnlineMultiplayer()) {
                        if (g_QuitConfirmed) running = false; else toMainMenu = true;   // the lobby was left: main menu, not out of the game
                    }
                    wasOnlineConnected = onlineClient.IsConnected();
                    continue;
                }
                if (resultChoice == PostRaceChoice::eNewLocalRace) {
                    const LocalRaceSetup setup = RunLocalRaceSetup(
                        graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
                    if (setup.confirmed) {
                        missileSeen = false;
                        finishAnnounced = false; resultsPending = false;
                        if (!loadLocalRace(setup.trackName, setup.laps, setup.weapons)) {
                            running = false;
                        }
                        wasOnlineConnected = onlineClient.IsConnected();
                    }
                    continue;
                }
                SDL_StartTextInput();   // the results screen stops text input; chat needs it back
                session.AddMessage("Press Enter for your results again, or ESC for race options");
                resultsSeen = true;
            }
            if (onlineClient.IsConnected() && localClientId >= 0) {
                while (mainCharacter->HitQueueCount() > 0) {
                    mainCharacter->GetHitQueue();
                    onlineClient.SendHit(localClientId);
                }
                for (auto& remoteEntry : remotePlayers) {
                    while (remoteEntry.second.mCharacter->HitQueueCount() > 0) {
                        remoteEntry.second.mCharacter->GetHitQueue();
                        onlineClient.SendHit(remoteEntry.first);
                    }
                }
            }
#endif

            room = mainCharacter->mRoom;
            camera = mainCharacter->mPosition;
            camera.mZ += 700;
            orientation = mainCharacter->GetCabinOrientation();
            sceneChanged = true;

            if (fire && !missileSeen && ContainsFreeElementType(*level, 1, 150)) {
                missileSeen = true;
            }

#ifdef HOVERNET_GAME2_PLAYER
            if (onlineClient.IsConnected() && localClientId >= 0) {
                const Uint32 now = SDL_GetTicks();
                if (now - lastRacePing >= 2000) {
                    onlineClient.Ping();
                    lastRacePing = now;
                }
                const MR_ElementNetState localState = mainCharacter->GetNetState();
                onlineClient.SendPlayerState(localClientId, localState.mData, localState.mDataLen);

                RaceServerMessage netMessage;
                while (onlineClient.PollMessage(netMessage, 0)) {
                    if (onlineClient.HandlePingReply(netMessage)) {
                        continue;
                    }
                    if (netMessage.mType == eRSMsgConnNameSet) {
                        RaceServerPeer peer;
                        if (RaceServerClient::ParsePeer(netMessage, peer)) {
                            raceNames[peer.mClientId] = peer.mName;
                            if (remotePlayers.count(peer.mClientId) == 0) {
                                // A player who joined after the race started -- spawn them
                                // the same way the pre-race roster was spawned, just with
                                // no free starting slot to reserve at this point.
                                MR_MainCharacter* remote = MR_MainCharacter::New(5, TRUE);
                                if (remote != nullptr) {
                                    remote->mPosition = mainCharacter->mPosition;
                                    remote->mRoom = mainCharacter->mRoom;
                                    remote->SetHoverId(static_cast<int>(remotePlayers.size()) + 1);
                                    MR_FreeElementHandle remoteHandle =
                                        session.InsertRemoteCharacter(remote, remote->mRoom);
                                    if (remoteHandle != nullptr) {
                                        remotePlayers[peer.mClientId] = {remote, remoteHandle};
                                    }
                                }
                            }
                        }
                    }
                    else if (netMessage.mType == eRSMsgSetMainElemState) {
                        int senderClientId = -1;
                        const MR_UInt8* stateData = nullptr;
                        std::size_t stateLen = 0;
                        if (RaceServerClient::ParsePlayerState(netMessage, senderClientId, stateData, stateLen) &&
                            stateLen == static_cast<std::size_t>(localState.mDataLen)) {
                            auto remoteIt = remotePlayers.find(senderClientId);
                            if (remoteIt != remotePlayers.end()) {
                                MR_MainCharacter* remote = remoteIt->second.mCharacter;
                                const int oldRoom = remote->mRoom;
                                remote->SetNetState(static_cast<int>(stateLen), stateData);
                                if (remote->mRoom < 0 || remote->mRoom >= level->GetRoomCount()) {
                                    remote->mRoom = oldRoom;
                                }
                                else if (remote->mRoom != oldRoom) {
                                    session.MoveRemoteCharacter(remoteIt->second.mHandle, remote->mRoom);
                                }
                            }
                        }
                    }
                    else if (netMessage.mType == eRSMsgHitMessage) {
                        int targetClientId = -1;
                        if (RaceServerClient::ParseHit(netMessage, targetClientId)) {
                            // TriggerOutOfControl() alone only replays the visible spin-out --
                            // it skips MR_MainCharacter::ApplyEffect entirely, which is where
                            // the hit sound actually gets queued (along with mLastHits
                            // tracking). ApplyNetworkMissileHit() routes through ApplyEffect
                            // with a synthetic MR_LostOfControl, matching how a locally-
                            // simulated missile hits a craft, so a hit reported by the server
                            // sounds the same as one detected locally. The wire message
                            // doesn't carry the shooter's id, so it's passed as unknown (-1).
                            if (targetClientId == localClientId) {
                                mainCharacter->ApplyNetworkMissileHit(-1, level);
                            }
                            else {
                                auto targetIt = remotePlayers.find(targetClientId);
                                if (targetIt != remotePlayers.end()) {
                                    targetIt->second.mCharacter->ApplyNetworkMissileHit(-1, level);
                                }
                            }
                        }
                    }
                    else if (netMessage.mType == eRSMsgCreateAutoElem) {
                        int dllId = 0;
                        int classId = 0;
                        int elementRoom = -1;
                        const std::uint8_t* stateData = nullptr;
                        std::size_t stateLen = 0;
                        if (RaceServerClient::ParseAutoElement(netMessage, dllId, classId, elementRoom,
                                                               stateData, stateLen) &&
                            dllId == 1 && classId == 150 &&
                            elementRoom >= 0 && elementRoom < level->GetRoomCount() &&
                            stateLen == 16) {
                            MR_ObjectFromFactoryId typeId = {
                                static_cast<MR_UInt16>(dllId), static_cast<MR_UInt16>(classId)
                            };
                            MR_FreeElement* remoteElement =
                                (MR_FreeElement*)MR_DllObjectFactory::CreateObject(typeId);
                            if (remoteElement != nullptr) {
                                remoteElement->SetNetState(static_cast<int>(stateLen), stateData);
                                if (session.InsertRemoteElement(remoteElement, elementRoom) == nullptr) {
                                    delete remoteElement;
                                }
                            }
                        }
                    }
                    else if (netMessage.mType == eRSMsgChatMessage) {
                        // Same relay the lobby uses (see ServerSocket.cpp's MRNM_CHAT_MESSAGE
                        // case), scoped to this race by the server's own mRaceId check -- a
                        // race in progress is still just a connection with mRaceId set, so
                        // this needed no server-side change, only somewhere on each client to
                        // send and show it once actual racing starts.
                        int senderClientId = -1;
                        std::string chatText;
                        if (RaceServerClient::ParseChatMessage(netMessage, senderClientId, chatText)) {
                            chatText = RaceServerClient::DecodeInRaceChat(chatText);
                            const auto nameIt = raceNames.find(senderClientId);
                            const std::string senderName = (nameIt != raceNames.end())
                                ? nameIt->second
                                : ("Player " + std::to_string(senderClientId));
                            session.AddMessage((senderName + ": " + chatText).c_str());
                        }
                    }
                }
            }
#endif
        }
        else {
            if (turnLeft != turnRight) {
                orientation = MR_NORMALIZE_ANGLE(orientation + (turnRight ? 64 : -64));
                sceneChanged = true;
            }
            if (moveForward != moveBackward) {
                const int direction = moveForward ? 1 : -1;
                camera.mX += direction * 250 * MR_Cos[orientation] / MR_TRIGO_FRACT;
                camera.mY += direction * 250 * MR_Sin[orientation] / MR_TRIGO_FRACT;
                horizontalMovement = true;
                sceneChanged = true;
            }
            if (moveUp != moveDown) {
                camera.mZ += moveUp ? 100 : -100;
                sceneChanged = true;
            }
        }
        if (sceneChanged) {
            if (horizontalMovement) {
                const MR_2DCoordinate cameraPosition(camera.mX, camera.mY);
                const int cameraRoom = level->FindRoomForPoint(cameraPosition, room);
                if (cameraRoom == -1) {
                    camera = previousCamera;
                }
                else {
                    if (cameraRoom != room) {
                        std::printf("Entered room %d\n", cameraRoom);
                    }
                    room = cameraRoom;
                }
            }
            ClampCameraHeight(*level, room, camera);
#ifdef HOVERNET_GAME2_PLAYER
            if (playerMode) {
                if (debugView) {
                    observer->RenderDebugDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                                 session.GetBackImage());
                }
                else {
                    if (raceWasOnline) {
                        observer->SetNetworkLatency(onlineClient.IsConnected()
                            ? onlineClient.GetLastPingMs() : -3);
                    }
#if defined(HOVERNET_AUTH)
                    else if (auth.active) {
                        // A race on a HoverNet server (the lobby's races, including one you host alone) is an online race: the HUD says so, with the ping.
                        observer->SetNetworkLatency(auth.resultReceived ? (auth.rttMs >= 0 ? auth.rttMs : -1) : (auth.conn.IsOpen() ? (auth.rttMs >= 0 ? auth.rttMs : -1) : -3));
                    }
#endif
                    else if (replay.active) {
                        observer->SetNetworkLatency(-4);   // "Replay"
                    }
                    else {
                        observer->SetNetworkLatency(-2);
                    }
                    {   // The ghost: record this run, and show the saved best run beside it.
                        bool watching = replay.active;
#if defined(HOVERNET_AUTH)
                        watching = watching || (auth.active && auth.spectator);
#endif
                        const std::string ghostTrack = currentTrackName.empty() ? initialTrackName : currentTrackName;
                        const int ghostLaps = session.GetNbLap();
                        const std::string key = ghostTrack + "/" + std::to_string(ghostLaps);
                        const MR_SimulationTime ghostTime = session.GetSimulationTime();
                        if (key != ghostKey || ghostTime < ghostLastTime - 1000) {   // another track or lap count, or a restarted race: the best on file is the ghost
                            ghostKey = key;
                            ghostRecording.Clear();
                            ghostBest.Clear();
                            if (!ghostTrack.empty()) ghostBest.Load(ConfigDirPath() + "/" + GhostRun::FileName(ghostTrack, ghostLaps));
                        }
                        ghostLastTime = ghostTime;
                        if (!watching && !finishAnnounced) {
                            ghostRecording.Record(ghostTime, mainCharacter->mPosition.mX, mainCharacter->mPosition.mY, mainCharacter->mPosition.mZ,
                                                  mainCharacter->GetCabinOrientation(), mainCharacter->mRoom);
                        }
                        GhostRun::Sample at;
                        if (!watching && !ghostBest.Empty() && LoadShowGhost() && ghostBest.At(ghostTime, at)) {
                            if (ghostCraft == nullptr) {
                                ghostCraft = MR_MainCharacter::New(ghostLaps, FALSE);
                                if (ghostCraft != nullptr) { ghostCraft->AddRenderer(); ghostCraft->SetHoverId(7); }
                            }
                            if (ghostCraft != nullptr) {
                                ghostCraft->SetHoverModel(mainCharacter->GetHoverModel());
                                ghostCraft->mPosition = MR_3DCoordinate(at.x, at.y, at.z);
                                ghostCraft->SetOrientation(at.orientation);
                                ghostCraft->mRoom = at.room;
                                observer->SetGhost(ghostCraft);
                                if (!ghostShownReported && std::getenv("HOVERNET_TEST_GHOST") != nullptr) {
                                    ghostShownReported = true;
                                    std::printf("GHOSTTEST ghost shown at %d ms in room %d\n", static_cast<int>(ghostTime), at.room);
                                    std::fflush(stdout);
                                }
                            }
                        }
                        else {
                            observer->SetGhost(nullptr);
                        }
                    }
                    if (HoverNetVrActive()) {
                        // VR camera: two views of the same moment, each eye shifted sideways.
                        // The spacing is exaggerated relative to a real 64 mm because the world
                        // is hovercraft-sized, which makes the depth easy to see; the cockpit
                        // camera is much nearer the scenery, so it gets half of it.
                        const int kEyeSeparationMm = cockpitView ? 70 : 140;
                        const std::size_t eyeBytes = static_cast<std::size_t>(kWidth) * kHeight * 4;
                        vrEyes.resize(eyeBytes * 2);
                        for (int eye = 0; eye < 2; ++eye) {
                            observer->SetEyeOffset(eye == 0 ? -kEyeSeparationMm / 2 : kEyeSeparationMm / 2,
                                                   eye == 1 ? TRUE : FALSE);
                            observer->RenderNormalDisplay(&buffer, &session, mainCharacter,
                                                          session.GetSimulationTime(), session.GetBackImage());
                            graphics.ConvertToRGBA(buffer.GetBuffer(), kWidth, kHeight,
                                                   vrEyes.data() + eyeBytes * eye);
                        }
                        observer->SetEyeOffset(0, FALSE);
                        HoverNetVrSubmitStereo(vrEyes.data(), vrEyes.data() + eyeBytes, kWidth, kHeight);
                    }
                    else {
                        observer->SetChaseScale(kChasePercents[chaseStep]);
                        observer->RenderNormalDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                                      session.GetBackImage());
                    }
                }
                if (!chatBuffer.empty() && menuFontHandle != nullptr) {
                    const std::string prompt = "Chat: " + chatBuffer + "_";
                    DrawUiText(*menuFontHandle->GetSprite(), 20, kHeight - 40, prompt.c_str(), &viewport,
                               MR_Sprite::eLeft, MR_Sprite::eTop, LoadLargeHudText() ? 1 : 2);
                }
                if (replay.active && !replay.hashMode && replay.data.ticks > 0) {
                    // The timeline: progress, and a mark at every lap crossing found so far (the watched craft's are brighter, a finish is taller).
                    static const std::uint8_t back = NearestPaletteIndex(40, 40, 40), fill = NearestPaletteIndex(70, 130, 220),
                                              mine = NearestPaletteIndex(255, 220, 60), other = NearestPaletteIndex(170, 170, 170);
                    MR_UInt8* frame = buffer.GetBuffer();
                    const int barX = 20, barW = kWidth - 40, barY = kHeight - 14, barH = 8;
                    FillFrameRect(frame, kWidth, kHeight, barX - 1, barY - 1, barW + 2, barH + 2, back);
                    FillFrameRect(frame, kWidth, kHeight, barX, barY, static_cast<int>(static_cast<std::int64_t>(barW) * replay.tick / replay.data.ticks), barH, fill);
                    for (const auto& m : replay.markers) {
                        const int x = barX + static_cast<int>(static_cast<std::int64_t>(barW) * m.tick / replay.data.ticks);
                        const int extra = m.finished ? 5 : 2;
                        FillFrameRect(frame, kWidth, kHeight, x - 1, barY - extra, 2, barH + 2 * extra, m.player == replay.watch ? mine : other);
                    }
                    if (menuFontHandle != nullptr) {
                        const int sec = static_cast<int>(replay.tick * 16 / 1000), total = static_cast<int>(static_cast<std::int64_t>(replay.data.ticks) * 16 / 1000);
                        char label[96];
                        std::snprintf(label, sizeof label, "Player %d   %d:%02d / %d:%02d   x%g%s", replay.watch + 1, sec / 60, sec % 60, total / 60, total % 60,
                                      kReplaySpeeds[replay.speedStep], replay.paused ? "   paused" : "");
                        DrawUiText(*menuFontHandle->GetSprite(), barX + barW, barY - 44, label, &viewport, MR_Sprite::eRight, MR_Sprite::eBottom, 1);
                    }
                }
                observer->PlaySoundsSafe(session.GetCurrentLevel(), mainCharacter);
                MR_SoundServer::ApplyContinuousPlay();
            }
            else
#endif
            {
                renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                          SDL_GetTicks(), mainCharacter);
            }
        }
        graphics.Present(buffer.GetBuffer(), kWidth, kHeight);
        ++framesRendered;
#ifdef HOVERNET_GAME2_PLAYER
        CaptureFrameIfRequested(graphics.GetRenderer(), framesRendered, frameLimit);
#endif

        if (!memoryReportPath.empty() && framesRendered % kMemoryReportSampleEvery == 0) {
            const Uint32 lNowTicks = SDL_GetTicks();
            const double lMsPerFrame = static_cast<double>(lNowTicks - memoryReportLastSampleTicks) /
                                       kMemoryReportSampleEvery;
            memoryReportLastSampleTicks = lNowTicks;
            const long lRssKb = ReadResidentMemoryKB();
            memoryReportRssSamples.push_back(lRssKb);
            if (memoryReport.is_open()) {
                memoryReport << framesRendered << ',' << (lNowTicks - memoryReportStartTicks) << ','
                             << lMsPerFrame << ',' << lRssKb << '\n';
                memoryReport.flush();
            }
        }

        HoverNetFrameDelay();
    }

    if (memoryReport.is_open()) {
        memoryReport.close();
    }

    // The actual pass/fail signal for the memory-baseline soak test: compare
    // steady-state RSS (the back half of samples) against the initial ramp-up
    // (the front half, which legitimately grows as tracks/actors/sounds get
    // loaded the first time each is used) -- see docs/roadmap-2.0.md's Phase 1
    // "repeatable frame-time and memory baseline" item. Needs enough samples for
    // "front half vs back half" to mean anything; a handful of samples from a
    // short run is noise, not a trend.
    if (!memoryReportPath.empty() && memoryReportRssSamples.size() >= 10) {
        const std::size_t lHalf = memoryReportRssSamples.size() / 2;
        long lFrontSum = 0, lBackSum = 0;
        for (std::size_t lIndex = 0; lIndex < lHalf; ++lIndex) {
            lFrontSum += memoryReportRssSamples[lIndex];
        }
        for (std::size_t lIndex = lHalf; lIndex < memoryReportRssSamples.size(); ++lIndex) {
            lBackSum += memoryReportRssSamples[lIndex];
        }
        const double lFrontAvgKb = static_cast<double>(lFrontSum) / lHalf;
        const double lBackAvgKb = static_cast<double>(lBackSum) / (memoryReportRssSamples.size() - lHalf);
        const double lGrowthKb = lBackAvgKb - lFrontAvgKb;
        // Absolute floor (not just a percentage) so a tiny process with normal
        // allocator noise doesn't fail on, say, "grew from 40KB to 80KB" -- and a
        // percentage on top of that so a huge process doesn't get a free pass on
        // genuinely leaking a large absolute amount.
        const double kGrowthFloorKb = 5000.0;
        const double kGrowthFraction = 0.20;
        std::fprintf(stderr, "Memory baseline: front-half avg %.0f KB, back-half avg %.0f KB (growth %.0f KB)\n",
                     lFrontAvgKb, lBackAvgKb, lGrowthKb);
        if (lGrowthKb > kGrowthFloorKb && lGrowthKb > lFrontAvgKb * kGrowthFraction) {
            std::fprintf(stderr, "Memory baseline FAILED: RSS grew %.0f KB (%.1f%%) from front half to back half "
                                  "of a %zu-frame run -- looks like a leak, not steady-state noise.\n",
                         lGrowthKb, lFrontAvgKb > 0 ? (lGrowthKb / lFrontAvgKb * 100.0) : 0.0,
                         memoryReportRssSamples.size() * static_cast<std::size_t>(kMemoryReportSampleEvery));
            return 1;
        }
    }

    if (autoPlay && mainCharacter != nullptr && mainCharacter->mPosition == startingPlayerPosition) {
        std::fprintf(stderr, "Autoplay did not move the main character\n");
        return 1;
    }
    if (autoPlay && mainCharacter != nullptr) {
        std::printf("Autoplay player position=(%d,%d,%d)\n", mainCharacter->mPosition.mX,
                    mainCharacter->mPosition.mY, mainCharacter->mPosition.mZ);
    }
    if (playerMode && mainCharacter != nullptr && HasArgument(argc, argv, "--fire") &&
        mainCharacter->GetCurrentWeapon() == MR_MainCharacter::eMissile && !missileSeen) {
        std::fprintf(stderr, "Firing did not create a missile\n");
        return 1;
    }

#ifdef HOVERNET_GAME2_PLAYER
    delete menuFontHandle;
    observer->Delete();
#endif

    return 0;
}

int main(int argc, char** argv)
{
#ifdef HOVERNET_WEB
    // Emscripten's virtual filesystem is backed by IDBFS (mounted in the HTML
    // preRun hook) so the shared client's existing XDG settings stay persistent.
    setenv("HOME", "/home/web", 1);
    setenv("XDG_CONFIG_HOME", "/home/web/.config", 1);
#endif
    try {
        return RunClient(argc, argv);
    }
    catch (const std::exception& error) {
        return FatalClientError(std::string("HoverNet stopped because of an unexpected error: ") + error.what());
    }
    catch (...) {
        return FatalClientError("HoverNet stopped because of an unexpected error. Reinstall HoverNet if this continues.");
    }
}
