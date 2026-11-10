#ifdef _WIN32
#include <afxwin.h>
#include <afxtempl.h>
#endif

#include "CraftStore.h"
#include "Tutorial.h"
#include "Profile.h"
#include "CraftPatterns.h"
#include "GarageUnlocks.h"
#include "CraftPreview.h"
#include "GuestName.h"
#include "../ObjFacTools/CraftLook.h"
#include "TrackCatalog.h"
#include "TrackDownloader.h"
#include <atomic>
#include <thread>
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
#ifdef HOVERNET_HAVE_RMLUI
#include "RmlMenu.h"
#endif
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "MenuInput.h"
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
#include "../VideoServices/ModernGraphics.h"
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

// Who drove each slot of the replay about to be shown (set where the names are known, taken by startReplayFile): the "Following ..." message and the craft cycling name the racer.
static std::vector<std::string> g_nextReplayNames;

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
EM_JS(void, hovernet_js_account_craft_save, (), {
    if (window.hoverNetSaveCraftAccount) window.hoverNetSaveCraftAccount();
});
EM_JS(void, hovernet_js_account_craft_load, (), {
    if (window.hoverNetLoadCraftAccount) window.hoverNetLoadCraftAccount();
});
EM_JS(void, hovernet_js_account_craft_status, (char* out, int cap), {
    stringToUTF8(window.hoverNetCraftStatus || '', out, cap);
});
EM_JS(int, hovernet_js_account_craft_revision, (), {
    return window.hoverNetCraftRevision | 0;
});
// The Outiva sign-in, from the page's own account service (packaging/web/hovernet_account.py): /account/me answers 200 with the profile when the browser holds the
// sign-in cookie, and 204 for a guest, with X-HoverNet-Accounts saying whether there is anything to sign in to. Asked in the background; the game reads the answer on
// later frames. -1 = not answered yet, 0 = a guest who can sign in, 1 = signed in, 2 = no accounts here (not configured, or the service is not reachable).
EM_JS(void, hovernet_js_account_check, (), {
    if (!window.hoverNetAccount) window.hoverNetAccount = { state: -1, callsign: '' };
    fetch('/account/me', { credentials: 'same-origin' }).then(function (r) {
        if (r.status === 200) return r.json().then(function (j) { window.hoverNetAccount = { state: 1, callsign: String(j.callsign || j.accountCallsign || '') }; });
        window.hoverNetAccount = { state: (r.status === 204 && r.headers.get('X-HoverNet-Accounts') === 'on') ? 0 : 2, callsign: '' };
    }).catch(function () { window.hoverNetAccount = { state: 2, callsign: '' }; });
});
EM_JS(int, hovernet_js_account_state, (), {
    return window.hoverNetAccount ? window.hoverNetAccount.state : -1;
});
EM_JS(void, hovernet_js_account_callsign, (char* out, int cap), {
    stringToUTF8(window.hoverNetAccount ? window.hoverNetAccount.callsign : '', out, cap);
});
EM_JS(void, hovernet_js_open_signin, (), {
    window.location.href = '/account/login';   // the sign-in happens in this tab and comes back to the game page when it is done (no new tab)
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
    static const bool noDelay = std::getenv("HOVERNET_NO_FRAME_DELAY") != nullptr;   // diagnostic: measure what the machine can do without the pause
    if (!noDelay) SDL_Delay(16);
#endif
    HoverNetGfx::NoteFrameStart();   // the next frame's work is counted from here (see ModernGraphics.h)
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
bool HoverNetQuietButton(const char* label, const ImVec2& size);

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
        // The map is drawn as a clean plan: the road in one slate-blue fill that is a little lighter towards the top, with a thin bright edge, rather than in the
        // raw colours of the game's old palette (a flat lavender blob on black).
        const int w = preview.mWidth, h = preview.mHeight;
        auto solid = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && source[y * w + x] != 0; };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int pixel = y * w + x;
                if (!solid(x, y)) { pixels[pixel * 4 + 3] = 0; continue; }
                const bool edge = !solid(x - 1, y) || !solid(x + 1, y) || !solid(x, y - 1) || !solid(x, y + 1);
                const float t = static_cast<float>(y) / static_cast<float>(h);
                const MR_UInt8 r = edge ? 232 : static_cast<MR_UInt8>(70 - 18 * t), g = edge ? 238 : static_cast<MR_UInt8>(80 - 18 * t), b = edge ? 252 : static_cast<MR_UInt8>(122 - 28 * t);
                pixels[pixel * 4] = r; pixels[pixel * 4 + 1] = g; pixels[pixel * 4 + 2] = b; pixels[pixel * 4 + 3] = 255;
            }
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
    draw->AddRectFilledMultiColor(frameMin, frameMax, IM_COL32(16, 18, 30, 255), IM_COL32(16, 18, 30, 255), IM_COL32(9, 10, 18, 255), IM_COL32(9, 10, 18, 255));
    draw->AddRect(frameMin, frameMax, IM_COL32(58, 64, 92, 255), 10.0f, 0, 1.5f);

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
TrackDownloadSettings CommunityDownloadSettings();

// --playtest FILE.trk (the track editor's Playtest): a compiled track to race on at once, listed as "Playtest" and started without the menus.
std::string gPlaytestPath;

// The Track Market catalogue as last fetched (docs/specs/track-market.md). HOVERNET_MARKET_CATALOGUE names another file (tests).
std::string MarketCataloguePath()
{
    const char* fileOverride = std::getenv("HOVERNET_MARKET_CATALOGUE");
    if (fileOverride != nullptr && fileOverride[0] != '\0') return fileOverride;
    return ConfigDirPath() + "/market-catalogue.tsv";
}

// Set by the refresh thread when a newer catalogue has been saved; applied when the main menu opens, so no track selector is holding an index.
std::atomic<bool> gMarketCatalogueUpdated{false};

// Once per run: fetch the Track Market catalogue in the background, so players see newly listed tracks without waiting and without a hiccup.
// Automated runs (a frame limit) do not touch the network unless a test points the catalogue URL at its own server; HOVERNET_NO_MARKET=1 turns it off.
void StartMarketCatalogueRefresh(bool automated)
{
    static bool started = false;
    if (started) return;
    started = true;
    const char* off = std::getenv("HOVERNET_NO_MARKET");
    if (off != nullptr && off[0] != '\0' && off[0] != '0') return;
    if (automated && std::getenv("HOVERNET_MARKET_CATALOGUE_URL") == nullptr) return;
    const std::string destination = MarketCataloguePath();
    std::thread([destination]() {
        MakeDirectories(destination.substr(0, destination.find_last_of("/\\") == std::string::npos ? 0 : destination.find_last_of("/\\") + 1));
        std::string error;
        if (FetchMarketCatalogue(destination, error)) {
            RemoveRevokedMarketTracks(destination, CommunityDownloadSettings().mDestination);   // a revoked track leaves this machine too
            gMarketCatalogueUpdated = true;
        }
    }).detach();
}

TrackCatalog& Tracks();

// Called when the main menu opens: if a newer catalogue arrived, rebuild the list. Market tracks are the last group, so the indexes of official and
// community tracks do not move.
void ApplyMarketCatalogueUpdate()
{
    if (gMarketCatalogueUpdated.exchange(false)) {
        Tracks().Rebuild();
    }
}

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
        options.mMarketCataloguePath = MarketCataloguePath();
        options.mPlaytestPath = gPlaytestPath;
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


// Screens fade in from black: the first frames of a screen that is not the one drawn just before it are covered by a dark layer that clears in about a quarter of a
// second (it never blocks input and never touches the race picture). Each screen loop calls this once per frame with its own key (HN_SCREEN_FADE). Off for the
// screenshot hooks and with HOVERNET_NO_FADE, so the regression captures stay what they were.
bool LoadReducedMotion();   // defined with the other settings below
float LoadUiScale();         // likewise

#ifdef HOVERNET_HAVE_RMLUI
// Which interface draws the main menu and the other converted screens: the RmlUi ones by default (the desktop since 2.3.46, the browser since 2.3.47), the older ImGui ones with HOVERNET_RML_MENU=0 (on the page: ?menu=imgui) and in a bounded
// run on the desktop. A screen that cannot load falls back to ImGui.
void HoverNetFrameDelayForRml() { HoverNetFrameDelay(); }
bool UseRmlUi(int pFrameLimit)
{
    static bool hooked = false;
    if (!hooked) { hooked = true; HoverNetRml::SetFrameHook(&HoverNetFrameDelayForRml); }   // the RmlUi screens count and pace their frames like the others (the web page waits for the count)
    HoverNetRml::SetUiScale(LoadUiScale());   // the player's UI Scale setting (75% to 150%) sizes the new screens too
    const char* force = std::getenv("HOVERNET_RML_MENU");
    if (force != nullptr) return std::string(force) != "0";   // HOVERNET_RML_MENU=0 brings back the older ImGui screens; =1 forces the new ones (also in bounded runs and the browser)
#ifdef HOVERNET_WEB
    (void)pFrameLimit;
    return true;    // the browser too (since 2.3.47); the page's ?menu=imgui brings back the older screens
#else
    return pFrameLimit < 0;   // desktop: the new screens by default; a bounded run (--frames N, the tests) keeps the older ones, which the tests drive
#endif
}
#endif


void ScreenFadeFrame(const void* pKey)
{
    static const void* lastKey = nullptr;
    static Uint32 enteredAt = 0;
    static int enabled = -1;
    if (enabled < 0) {
        enabled = std::getenv("HOVERNET_NO_FADE") == nullptr && std::getenv("HOVERNET_CAPTURE_FRAME") == nullptr && std::getenv("HOVERNET_CAPTURE_LOADING") == nullptr &&
                  std::getenv("HOVERNET_CAPTURE_HUB") == nullptr && std::getenv("HOVERNET_CAPTURE_MENU") == nullptr && std::getenv("HOVERNET_CAPTURE_SERVERS") == nullptr ? 1 : 0;
    }
    const Uint32 now = SDL_GetTicks();
    static bool slide = false;
    if (pKey != lastKey) { lastKey = pKey; enteredAt = now; slide = enabled != 0 && !LoadReducedMotion(); }
    if (enabled == 0) return;
    const float age = static_cast<float>(now - enteredAt);
    if (age >= 260.0f) return;
    const float t = 1.0f - age / 260.0f;
    if (slide) {
        // The screen also eases up into place: everything the windows have drawn this frame moves down by a few pixels that shrink to nothing (ease out). Only what is
        // drawn moves, so the hit areas are a few pixels off for a quarter of a second at most; the fade layer below is added after and does not move. Not with Reduced motion.
        const float shift = 26.0f * t * t * t;
        ImGuiContext& g = *ImGui::GetCurrentContext();
        std::vector<ImDrawList*> done;
        for (ImGuiWindow* window : g.Windows) {
            ImDrawList* list = window->DrawList;
            if (list == nullptr || !window->WasActive || std::find(done.begin(), done.end(), list) != done.end()) continue;
            done.push_back(list);
            for (int i = 0; i < list->VtxBuffer.Size; ++i) list->VtxBuffer.Data[i].pos.y += shift;
            for (int i = 0; i < list->CmdBuffer.Size; ++i) { list->CmdBuffer.Data[i].ClipRect.y += shift; list->CmdBuffer.Data[i].ClipRect.w += shift; }
        }
    }
    const int alpha = static_cast<int>(255.0f * 0.92f * t * t);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(10, 11, 16, alpha));
}
// The main menu's entrance and glow: whether motion is wanted (not with Reduced motion, and not for the screenshot hooks, so the regression captures stay what they were) and the
// ease-out curve used for the parts sliding in.
bool MenuMotionAllowed()
{
    static int envAllowed = -1;
    if (envAllowed < 0) envAllowed = std::getenv("HOVERNET_NO_FADE") == nullptr && std::getenv("HOVERNET_CAPTURE_FRAME") == nullptr && std::getenv("HOVERNET_CAPTURE_MENU") == nullptr && std::getenv("HOVERNET_CAPTURE_HUB") == nullptr ? 1 : 0;
    return envAllowed == 1;
}
float EaseOutCubic(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
#define HN_SCREEN_FADE() do { static const char hnFadeKey = 0; ScreenFadeFrame(&hnFadeKey); } while (0)

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
    TrackDownloadJob* Job() { return mJob.get(); }   // for screens that show the progress themselves (the RmlUi ones)
    void Reset() { mJob.reset(); }

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
                if (HoverNetQuietButton("Cancel", ImVec2(-FLT_MIN, 0))) {
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
        if (!ContainsIgnoreCase(entry.mName, search) && !(entry.mMarket && ContainsIgnoreCase(entry.mAuthor, search))) continue;
        (entry.mOfficial ? official : community).push_back(i);   // tracks players published are community tracks too
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
        if (entry.mMarket) tag = "by " + entry.mAuthor + " (" + entry.mLicence + ")";   // the credit CC-BY asks for, wherever the track is chosen
        if (entry.mFreePlay) tag += std::string(tag.empty() ? "" : "  ") + "free play";
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

// Whether the page offered an Outiva sign-in (browser build), and whether the player is signed in. A player who was offered it and did not sign in races as a guest:
// other players see " (nonreg)" after the name (GuestName.h). Without an account service (the desktop build, a server with accounts off) nobody is tagged.
bool gAccountsOffered = false;
bool gSignedIn = false;
std::string OnlineUsername()
{
    return HoverNetGuest::Tagged(LoadUsername(), gAccountsOffered && !gSignedIn);
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

// ---- Music -------------------------------------------------------------------------------------------------------------------------------------------
// Two tracks from OpenGameArt (NetTarget/Music, credits and licences in NetTarget/Music/CREDITS.md): "Hard Left" in the menus and results, "Asphalt" in races.
// Ogg Vorbis files streamed by the sound server; the music volume is its own setting (<config>/music_volume, 0 to 1).
enum class MusicTrack { eNone, eMenu, eRace, eFinalLap };

double LoadMusicVolume()
{
    std::ifstream in(ConfigDirPath() + "/music_volume");
    double volume = 0.6;
    if (in >> volume && volume >= 0.0 && volume <= 1.0) return volume;
    return 0.6;
}

void SaveMusicVolume(double volume)
{
    std::ofstream out(ConfigDirPath() + "/music_volume");
    out << volume << '\n';
}

struct MusicState
{
    MusicTrack current = MusicTrack::eNone;
    double gain = 1.0;            // the playing track's own level (see MusicFile::gain): every volume change below includes it
    bool ducked = false;
    std::string toast;            // "Asphalt - Sygil", shown for a few seconds when a track starts
    Uint32 toastAt = 0;
};
MusicState gMusicState;

// What the last race earned, for the results screen: set when a race finishes, read by RunPostRaceScreen.
struct RewardView { bool valid = false; HoverNetProfile::Reward reward; HoverNetProfile::Profile profile; };
RewardView gRewardView;

std::string ProfilePath() { return ConfigDirPath() + "/profile"; }

// The tracks that can play in a race, taken in turn: one is never played twice in a row (the last one is remembered between runs), and a package that lacks some of
// the files simply rotates through the rest. The final lap has its own, faster track.
// gain: each track is brought to the level of Asphalt (about -12.5 LUFS integrated, measured with ffmpeg's ebur128 on 2026-11-09: Hard Left -14.0, Asphalt -12.5, Fever Stadium
// -10.8, its Climax -12.0, Hyper Ultra-Racing -8.8), so a new track never jumps out louder than the one before it. The files themselves are untouched.
struct MusicFile { const char* file; const char* credit; double gain; };
const MusicFile kRaceMusic[] = {
    {"Asphalt.ogg", "Asphalt - Sygil", 1.0},
    {"FeverStadium.ogg", "Fever Stadium - MintoDog", 0.82},          // -1.7 dB
    {"HyperUltraRacing.ogg", "Hyper Ultra-Racing - cynicmusic", 0.65},   // -3.7 dB
};
const MusicFile kFinalLapMusic = {"FeverStadiumClimax.ogg", "Fever Stadium (final lap) - MintoDog", 1.0};   // -12.0: a touch above the others on purpose
const MusicFile kMenuMusic = {"HardLeft.ogg", "Hard Left - Dan Knoflicek", 1.0};

bool MusicFileExists(const char* file)
{
    return static_cast<bool>(std::ifstream(SourcePath((std::string("NetTarget/Music/") + file).c_str()), std::ios::binary));
}

// Picks the next race track: not the one played last (a race, or the last run), and chosen at random among the others.
const MusicFile& ChooseRaceMusic()
{
    const int count = static_cast<int>(sizeof(kRaceMusic) / sizeof(kRaceMusic[0]));
    const std::string lastPath = ConfigDirPath() + "/last_race_music";
    static int last = -2;   // -2: not read yet
    if (last == -2) { last = -1; std::ifstream in(lastPath); int saved = -1; if (in >> saved && saved >= 0 && saved < count) last = saved; }
    std::vector<int> candidates;
    for (int i = 0; i < count; ++i) if (i != last && MusicFileExists(kRaceMusic[i].file)) candidates.push_back(i);
    if (candidates.empty()) for (int i = 0; i < count; ++i) if (MusicFileExists(kRaceMusic[i].file)) candidates.push_back(i);
    if (candidates.empty()) return kRaceMusic[0];
    last = candidates[static_cast<std::size_t>((SDL_GetTicks() / 7 + static_cast<Uint32>(std::rand())) % candidates.size())];
    std::ofstream out(lastPath);
    out << last << '\n';
    return kRaceMusic[last];
}

void PlayMusic(MusicTrack track)
{
    if (track == gMusicState.current) return;
    if (std::getenv("HOVERNET_NO_MUSIC") != nullptr) { gMusicState.current = track; return; }
    gMusicState.current = track;
    if (track == MusicTrack::eNone) { MR_SoundServer::MusicStop(1.0); return; }
    const MusicFile& chosen = track == MusicTrack::eMenu ? kMenuMusic : track == MusicTrack::eFinalLap ? kFinalLapMusic : ChooseRaceMusic();
    gMusicState.gain = chosen.gain;
    std::ifstream in(SourcePath((std::string("NetTarget/Music/") + chosen.file).c_str()), std::ios::binary);
    if (!in) return;   // a package without the music is simply quiet
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MR_SoundServer::MusicPlay(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<unsigned long>(bytes.size()), LoadMusicVolume() * chosen.gain * (gMusicState.ducked ? 0.35 : 1.0), track == MusicTrack::eFinalLap ? 0.8 : 1.6);
    gMusicState.toast = chosen.credit;
    gMusicState.toastAt = SDL_GetTicks();
    if (std::getenv("HOVERNET_TEST_MUSIC") != nullptr) { std::printf("MUSICTEST %s\n", chosen.file); std::fflush(stdout); }
}

// The music steps back while a menu is over the race (pause), and comes back after.
// A heavy hit makes the music step back for a moment so the impact is felt, then it returns (the volume glides, see MixMusic).
void ImpactDuckMusic(bool hitNow)
{
    static Uint32 until = 0;
    static bool active = false;
    const Uint32 now = SDL_GetTicks();
    if (hitNow) until = now + 900;
    const bool want = now < until && !gMusicState.ducked;
    if (want != active) {
        active = want;
        MR_SoundServer::MusicSetVolume(LoadMusicVolume() * gMusicState.gain * (want ? 0.45 : 1.0));
    }
}

void DuckMusic(bool duck)
{
    if (gMusicState.ducked == duck) return;
    gMusicState.ducked = duck;
    MR_SoundServer::MusicSetVolume(LoadMusicVolume() * gMusicState.gain * (duck ? 0.35 : 1.0));
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

// Graphics: "Modern" adds soft craft shadows, de-banded colour and smoothed edges, glow, a tone curve and
// vignette, and light effects (engine glow, trail, sparks, light from the craft). Presentation only (see
// VideoServices/ModernGraphics.h): it never touches the race. "Reduced motion" turns off the sparks, exhaust, dust, smoke and the impact flash.
// The look when the player has not chosen: Modern on a desktop, Modern light on a phone or tablet and in the browser (where the picture passes run
// without SIMD and cost several times as much; docs/render-golden.md). A saved "classic" (from before the Classic look was removed) means this too.
HoverNetGfx::Preset DefaultGraphicsPreset()
{
    return HoverNetGfx::Preset::eAuto;   // every machine starts on Automatic: the best look it can run, followed as it plays (the starting point differs, see GraphicsOptionsFor)
}

struct GraphicsSettings
{
    HoverNetGfx::Preset preset = DefaultGraphicsPreset();
    // Everything on by default (2026-11-10, the owner's choice; it had been off since 2026-11-09): with Automatic the machine decides how much of it runs, so these are what the player allows.
    bool shadows = true, deband = true, smoothing = true, bloom = true, grade = true, effects = true, lighting = true, textures = true;
    bool atmosphere = true, occlusion = true, edges = true, craft = true, halos = true, sharpen = true, exposure = true, adaptive = true;
    bool gpu = true;   // the finishing passes on the graphics card, when there is one (see VideoServices/GpuFinisher.h)
    float brightness = 1.0f;   // 0.7 .. 1.4
};

std::string GraphicsSettingsPath()
{
    return ConfigDirPath() + "/graphics";
}

GraphicsSettings LoadGraphicsSettings()
{
    GraphicsSettings settings;
    std::ifstream in(GraphicsSettingsPath());
    std::string key;
    std::string value;
    int defaultsVersion = 0;
    while (in >> key >> value) {
        if (key == "defaults") defaultsVersion = std::atoi(value.c_str());
        else if (key == "preset") {
            settings.preset = value == "modern" ? HoverNetGfx::Preset::eModern : value == "high" ? HoverNetGfx::Preset::eModernHigh : value == "lite" ? HoverNetGfx::Preset::eModernLite : value == "auto" ? HoverNetGfx::Preset::eAuto : DefaultGraphicsPreset();
        }
        else if (key == "shadows") settings.shadows = value != "0";
        else if (key == "deband") settings.deband = value != "0";
        else if (key == "smooth") settings.smoothing = value != "0";
        else if (key == "bloom") settings.bloom = value != "0";
        else if (key == "grade") settings.grade = value != "0";
        else if (key == "effects") settings.effects = value != "0";
        else if (key == "lighting") settings.lighting = value != "0";
        else if (key == "textures") settings.textures = value != "0";
        else if (key == "atmosphere") settings.atmosphere = value != "0";
        else if (key == "occlusion") settings.occlusion = value != "0";
        else if (key == "edges") settings.edges = value != "0";
        else if (key == "craft") settings.craft = value != "0";
        else if (key == "halos") settings.halos = value != "0";
        else if (key == "sharpen") settings.sharpen = value != "0";
        else if (key == "exposure") settings.exposure = value != "0";
        else if (key == "adaptive") settings.adaptive = value != "0";
        else if (key == "gpu") settings.gpu = value != "0";
        else if (key == "brightness") { const float b = static_cast<float>(std::atof(value.c_str())); if (b >= 0.7f && b <= 1.4f) settings.brightness = b; }
    }
    if (defaultsVersion < 3) {   // a file from before the look went back on for everyone (with Automatic to fit it to the machine): the four extras start on and Automatic is chosen, and stay as chosen after that
        settings.shadows = true; settings.lighting = true; settings.effects = true; settings.grade = true;
        settings.preset = HoverNetGfx::Preset::eAuto;
    }
    return settings;
}

void SaveGraphicsSettings(const GraphicsSettings& settings)
{
    std::ofstream out(GraphicsSettingsPath());
    out << "defaults 3\n" << "preset " << (settings.preset == HoverNetGfx::Preset::eAuto ? "auto" : settings.preset == HoverNetGfx::Preset::eModern ? "modern" : settings.preset == HoverNetGfx::Preset::eModernHigh ? "high" : settings.preset == HoverNetGfx::Preset::eModernLite ? "lite" : "modern") << '\n'
        << "shadows " << settings.shadows << '\n' << "deband " << settings.deband << '\n' << "smooth " << settings.smoothing << '\n' << "bloom " << settings.bloom << '\n'
        << "grade " << settings.grade << '\n' << "effects " << settings.effects << '\n' << "lighting " << settings.lighting << '\n' << "textures " << settings.textures << '\n' << "atmosphere " << settings.atmosphere << '\n' << "occlusion " << settings.occlusion << '\n' << "edges " << settings.edges << '\n'
        << "craft " << settings.craft << '\n' << "halos " << settings.halos << '\n' << "sharpen " << settings.sharpen << '\n' << "exposure " << settings.exposure << '\n' << "adaptive " << settings.adaptive << '\n' << "gpu " << settings.gpu << '\n' << "brightness " << settings.brightness << '\n';
}

HoverNetGfx::Options GraphicsOptionsFor(const GraphicsSettings& settings, bool reducedMotion)
{
    HoverNetGfx::Options options = HoverNetGfx::OptionsForPreset(settings.preset);
    if (options.enabled) {
        options.shadows = settings.shadows;
        options.deband = settings.deband;
        options.smoothing = settings.smoothing;
        options.bloom = settings.bloom;
        options.grade = settings.grade;
        options.effects = settings.effects;
        options.lighting = settings.lighting;
        options.trueColour = options.trueColour && settings.textures;
        options.atmosphere = settings.atmosphere; options.occlusion = settings.occlusion; options.edgeAA = settings.edges;
        options.craftShading = settings.craft; options.reflections = settings.craft; options.halos = settings.halos;
        options.sharpen = settings.sharpen; options.exposure = settings.exposure; options.adaptive = settings.adaptive;
        options.brightness = settings.brightness;
        options.reducedMotion = reducedMotion;
        if (const char* pinned = std::getenv("HOVERNET_AUTO_LEVEL")) {   // diagnostic: run one fixed level of the Automatic ladder (no governor)
            options = HoverNetGfx::Presenter::OptionsForLevel(options, std::atoi(pinned));
            options.automatic = false; options.adaptive = false;
        }
        else if (options.automatic) {
            options.adaptive = true;   // Automatic is the adaptive governor: it cannot be switched off with it
#ifdef HOVERNET_WEB
            options.startLevel = 1;    // a browser draws the picture passes on one thread without SIMD: it starts low and rises if there is room
#else
            options.startLevel = HoverNetIsTouchDevice() ? 1 : 3;
#endif
        }
    }
    return options;
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
// ---- Interface and race sounds, made in code -------------------------------------------------------------------------------------------------------
// Short synthesised tones (no sound files, nothing to license): menu hover, click and back, the race countdown and start, a lap chime, a finish fanfare and a
// new-best sting. They are built once, as the sound server's own resource format, the first time one is wanted.
enum class Sfx { eHover, eClick, eBack, eCountdown, eGo, eLap, eFinish, eBest, eCount };

struct SfxBank
{
    MR_ShortSound* sounds[static_cast<int>(Sfx::eCount)] = {};
    bool built = false;
    ImGuiID hovered = 0, focused = 0;           // the button under the pointer and the button holding focus, in the frame being drawn
    ImGuiID lastHovered = 0, lastFocused = 0;   // the same, a frame earlier
    int collectFrame = -100;
    Uint32 hoverSoundAt = 0;     // when the hover tick last played
    int hoverTicks = 0;          // how many ticks have played (reported by the test hook)
    bool quiet = false;   // the button being drawn is a secondary one (Back, Cancel): its click is the "back" sound
};
SfxBank gSfx;

std::vector<char> MakeToneSound(const std::vector<std::pair<double, double>>& notes /* start seconds, frequency */, double length, double decay, double amplitude, bool harmonic)
{
    const int rate = 44100;
    const int frames = static_cast<int>(length * rate);
    std::vector<short> pcm(static_cast<std::size_t>(frames), 0);
    for (const auto& note : notes) {
        const int first = static_cast<int>(note.first * rate);
        for (int i = first; i < frames; ++i) {
            const double t = static_cast<double>(i - first) / rate;
            const double envelope = std::exp(-t * decay) * std::min(1.0, t * 400.0);   // a quick attack, then a decay
            double v = std::sin(6.283185307 * note.second * t);
            if (harmonic) v = 0.75 * v + 0.2 * std::sin(6.283185307 * note.second * 2.0 * t) + 0.08 * std::sin(6.283185307 * note.second * 3.0 * t);
            pcm[static_cast<std::size_t>(i)] = static_cast<short>(std::max(-32767.0, std::min(32767.0, pcm[static_cast<std::size_t>(i)] + v * envelope * amplitude * 32767.0)));
        }
    }
    // the sound server's resource layout: u32 length, a WAVEFORMATEX (tag, channels, rate, bytes per second, block, bits, extra), then the samples
    std::vector<char> blob(22 + pcm.size() * 2);
    auto put32 = [&](std::size_t at, std::uint32_t v) { for (int b = 0; b < 4; ++b) blob[at + static_cast<std::size_t>(b)] = static_cast<char>((v >> (8 * b)) & 255); };
    auto put16 = [&](std::size_t at, std::uint16_t v) { blob[at] = static_cast<char>(v & 255); blob[at + 1] = static_cast<char>(v >> 8); };
    put32(0, static_cast<std::uint32_t>(pcm.size() * 2));
    put16(4, 1); put16(6, 1); put32(8, rate); put32(12, rate * 2); put16(16, 2); put16(18, 16); put16(20, 0);
    std::memcpy(blob.data() + 22, pcm.data(), pcm.size() * 2);
    return blob;
}

void PlaySfx(Sfx which, int decibels = 0, double pitch = 1.0)
{
    if (!gSfx.built) {
        gSfx.built = true;
        struct Def { Sfx id; std::vector<std::pair<double, double>> notes; double length, decay, amplitude; bool harmonic; };
        const Def defs[] = {
            {Sfx::eHover, {{0.0, 1900.0}}, 0.05, 90.0, 0.10, false},
            {Sfx::eClick, {{0.0, 880.0}, {0.035, 1320.0}}, 0.14, 30.0, 0.28, true},
            {Sfx::eBack, {{0.0, 990.0}, {0.035, 660.0}}, 0.14, 30.0, 0.26, true},
            {Sfx::eCountdown, {{0.0, 660.0}}, 0.22, 14.0, 0.40, true},
            {Sfx::eGo, {{0.0, 1320.0}, {0.0, 1980.0}}, 0.65, 5.5, 0.38, true},
            {Sfx::eLap, {{0.0, 880.0}, {0.09, 1108.0}, {0.18, 1318.0}}, 0.75, 7.0, 0.30, true},
            {Sfx::eFinish, {{0.0, 523.0}, {0.14, 659.0}, {0.28, 784.0}, {0.42, 1046.0}, {0.62, 784.0}, {0.62, 1046.0}, {0.62, 1318.0}}, 1.9, 2.6, 0.30, true},
            {Sfx::eBest, {{0.0, 784.0}, {0.1, 988.0}, {0.2, 1175.0}, {0.3, 1568.0}}, 1.2, 4.0, 0.30, true},
        };
        for (const Def& d : defs) {
            const std::vector<char> blob = MakeToneSound(d.notes, d.length, d.decay, d.amplitude, d.harmonic);
            gSfx.sounds[static_cast<int>(d.id)] = MR_SoundServer::CreateShortSound(blob.data(), 4);
        }
    }
    MR_ShortSound* sound = gSfx.sounds[static_cast<int>(which)];
    if (sound != nullptr) MR_SoundServer::Play(sound, decibels, pitch, 0);
}

// The sound of speed, made in code: a loop of wind (filtered noise, brighter as it gets faster) and a low rumble of the air under the craft. They follow the craft's
// speed through the volume and the pitch of two continuous sounds; nothing here touches the simulation or the engine sound.
std::vector<char> MakeNoiseLoop(double seconds, double lowpass /* 0..1, higher = brighter */, double amplitude, unsigned seed)
{
    const int rate = 44100, frames = static_cast<int>(seconds * rate);
    std::vector<double> raw(static_cast<std::size_t>(frames));
    std::uint32_t state = seed * 2654435761u + 12345u;
    double low = 0.0, low2 = 0.0;
    for (int i = 0; i < frames; ++i) {
        state = state * 1664525u + 1013904223u;
        const double white = (static_cast<double>(state >> 8) / 8388608.0) - 1.0;
        low += (white - low) * lowpass; low2 += (low - low2) * lowpass;   // two poles
        raw[static_cast<std::size_t>(i)] = low2;
    }
    // seamless: the last quarter second fades into what came before the start
    const int fade = rate / 4;
    for (int i = 0; i < fade; ++i) {
        const double t = static_cast<double>(i) / fade;
        raw[static_cast<std::size_t>(frames - fade + i)] = raw[static_cast<std::size_t>(frames - fade + i)] * (1.0 - t) + raw[static_cast<std::size_t>(i)] * t;
    }
    const int length = frames - fade;
    double peak = 1e-9;
    for (int i = 0; i < length; ++i) peak = std::max(peak, std::fabs(raw[static_cast<std::size_t>(i)]));
    std::vector<char> blob(22 + static_cast<std::size_t>(length) * 2);
    auto put32 = [&](std::size_t at, std::uint32_t v) { for (int b = 0; b < 4; ++b) blob[at + static_cast<std::size_t>(b)] = static_cast<char>((v >> (8 * b)) & 255); };
    auto put16 = [&](std::size_t at, std::uint16_t v) { blob[at] = static_cast<char>(v & 255); blob[at + 1] = static_cast<char>(v >> 8); };
    put32(0, static_cast<std::uint32_t>(length * 2));
    put16(4, 1); put16(6, 1); put32(8, rate); put32(12, rate * 2); put16(16, 2); put16(18, 16); put16(20, 0);
    for (int i = 0; i < length; ++i) put16(22 + static_cast<std::size_t>(i) * 2, static_cast<std::uint16_t>(static_cast<short>(raw[static_cast<std::size_t>(i)] / peak * amplitude * 32767.0)));
    return blob;
}

// Called once per race frame with the craft's speed as a fraction of the top speed (0 to about 1.2).
void UpdateSpeedSounds(double speed)
{
    static MR_ContinuousSound* wind = nullptr;
    static MR_ContinuousSound* rumble = nullptr;
    static bool built = false;
    static double smooth = 0.0;
    if (!built) {
        built = true;
        // (the original wind, the craft's friction sound, plays through the craft itself now that the object library shares the game's sound server, so only the rumble is added here)
        const std::vector<char> rumbleBlob = MakeNoiseLoop(2.5, 0.035, 0.80, 11);
        rumble = MR_SoundServer::CreateContinuousSound(rumbleBlob.data(), 1);
    }
    if (std::getenv("HOVERNET_NO_SPEED_SOUNDS") != nullptr) return;
    smooth += (std::max(0.0, std::min(1.3, speed)) - smooth) * 0.12;   // the sound follows a little behind, so a bump does not click
    if (smooth < 0.04) return;
    if (wind != nullptr) {
        const double t = std::min(1.0, smooth / 1.0);
        MR_SoundServer::Play(wind, 0, static_cast<int>(-3200 + 2300 * t * t), 0.75 + 0.55 * t, 0);   // louder and higher as it gets faster (the dB are in hundredths)
    }
    if (rumble != nullptr) {
        const double t = std::min(1.0, smooth / 1.0);
        MR_SoundServer::Play(rumble, 0, static_cast<int>(-3200 + 1500 * t), 0.8 + 0.4 * t, 0);
    }
}

// A soft tick as a slider moves (at most every 45 ms, so a drag is a stream of ticks and not a buzz), and a click when another tab is chosen.
void SliderTick()
{
    static Uint32 last = 0;
    const Uint32 now = SDL_GetTicks();
    if (now - last < 45) return;
    last = now;
    PlaySfx(Sfx::eHover, -500, 1.5);
}

void NoteTabSound(const char* name)
{
    static const char* lastTab = nullptr;
    static int lastFrame = -100;
    const int frame = ImGui::GetFrameCount();
    if (frame == lastFrame + 1 && lastTab != nullptr && lastTab != name) PlaySfx(Sfx::eClick, -400, 1.2);   // only a change while the screen is open; opening one is silent
    if (frame != lastFrame) lastFrame = frame;
    lastTab = name;
}

bool HoverNetButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
    // The fill eases between its resting and hovered colours (about a tenth of a second, in both directions) instead of snapping. Each button keeps how hovered it is in ImGui's own
    // per-widget storage; the colour it draws this frame is the blend of the resting and hovered colours (a press still shows the active colour). Skipped with Reduced motion.
    static int reducedCached = 0;
    static int reducedAt = -1000;
    {
        const int frameNow = ImGui::GetFrameCount();
        if (frameNow - reducedAt > 120 || frameNow < reducedAt) { reducedAt = frameNow; reducedCached = LoadReducedMotion() ? 1 : 0; }
    }
    const ImGuiID easeId = ImGui::GetID(label);
    ImGuiStorage* easeStore = ImGui::GetStateStorage();
    const float eased = easeStore->GetFloat(easeId, 0.0f);
    const ImVec4 resting = ImGui::GetStyleColorVec4(ImGuiCol_Button), hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    if (!reducedCached) {
        const ImVec4 blend(resting.x + (hovered.x - resting.x) * eased, resting.y + (hovered.y - resting.y) * eased, resting.z + (hovered.z - resting.z) * eased, resting.w + (hovered.w - resting.w) * eased);
        ImGui::PushStyleColor(ImGuiCol_Button, blend);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, blend);
    }
    const bool pressed = ImGui::Button(label, size);
    if (!reducedCached) ImGui::PopStyleColor(2);
    if (!reducedCached && ImGui::IsItemFocused() && !ImGui::IsItemHovered() && ImGui::GetIO().NavVisible) {   // keyboard or controller focus: a soft pulsing ring
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(SDL_GetTicks()) / 1000.0f * 4.0f);
        ImGui::GetForegroundDrawList()->AddRect(ImVec2(lo.x - 2.0f, lo.y - 2.0f), ImVec2(hi.x + 2.0f, hi.y + 2.0f), IM_COL32(255, 120, 135, static_cast<int>(150.0f * pulse)), 7.0f, 0, 2.0f);
    }
    {
        const float target = ImGui::IsItemHovered() ? 1.0f : 0.0f;   // the pointer only: a button that merely has keyboard focus keeps its usual look
        const float step = std::min(1.0f, ImGui::GetIO().DeltaTime / 0.10f);
        easeStore->SetFloat(easeId, eased + (target - eased) * step);
    }
    ImGui::PopStyleColor();
    // sounds: a tick as the pointer or the keyboard focus arrives on a button, and a click (or the lower "back" tone for a secondary button) when it is pressed
    // The tick plays once when the pointer arrives on a button, and once when keyboard or controller focus moves to one: never while either stays put. The hovered
    // button and the focused button are two separate things (the mouse can be over one while the focus rests on another), so each is followed on its own, once a
    // frame, and the tick plays only when that one changes. The first frame of a screen only learns where they are, so opening a menu is silent.
    {
        const int frame = ImGui::GetFrameCount();
        if (frame != gSfx.collectFrame) {
            if (frame == gSfx.collectFrame + 1) {
                const Uint32 now = SDL_GetTicks();
                const bool hoverMoved = gSfx.hovered != 0 && gSfx.hovered != gSfx.lastHovered;
                const bool focusMoved = gSfx.focused != 0 && gSfx.focused != gSfx.lastFocused && gSfx.focused != gSfx.hovered;
                if ((hoverMoved || focusMoved) && now - gSfx.hoverSoundAt >= 70) { gSfx.hoverSoundAt = now; PlaySfx(Sfx::eHover); ++gSfx.hoverTicks; }
            }
            gSfx.lastHovered = gSfx.hovered; gSfx.lastFocused = gSfx.focused;
            gSfx.hovered = 0; gSfx.focused = 0;
            gSfx.collectFrame = frame;
        }
        if (ImGui::IsItemHovered()) gSfx.hovered = ImGui::GetItemID();
        if (ImGui::IsItemFocused()) gSfx.focused = ImGui::GetItemID();
    }
    if (pressed) PlaySfx(gSfx.quiet ? Sfx::eBack : Sfx::eClick);
    // a little depth: a soft highlight across the top half and a shade along the bottom, so a button reads as a button and not a flat block
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float rounding = ImGui::GetStyle().FrameRounding;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(lo, ImVec2(hi.x, lo.y + (hi.y - lo.y) * 0.5f), IM_COL32(255, 255, 255, ImGui::IsItemHovered() ? 38 : 20), rounding, ImDrawFlags_RoundCornersTop);
    draw->AddRectFilled(ImVec2(lo.x, hi.y - 3.0f), hi, IM_COL32(0, 0, 0, 46), rounding, ImDrawFlags_RoundCornersBottom);
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
    gSfx.quiet = true;
    const bool pressed = HoverNetButton(label, size);
    gSfx.quiet = false;
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
    // A soft vignette: the edges darken, which pulls the eye to the middle and gives the flat backdrop some depth.
    {
        const float w = end.x - start.x, h = end.y - start.y;
        const float sideBand = w * 0.16f, topBand = h * 0.20f;
        const ImU32 dark = IM_COL32(0, 0, 0, 120), clear = IM_COL32(0, 0, 0, 0);
        draw->AddRectFilledMultiColor(start, ImVec2(start.x + sideBand, end.y), dark, clear, clear, dark);
        draw->AddRectFilledMultiColor(ImVec2(end.x - sideBand, start.y), end, clear, dark, dark, clear);
        draw->AddRectFilledMultiColor(start, ImVec2(end.x, start.y + topBand), dark, dark, clear, clear);
        draw->AddRectFilledMultiColor(ImVec2(start.x, end.y - topBand), end, clear, clear, dark, dark);
    }
}

// A screen's title: the display face straight on the dark backdrop, with a thin accent rule that fades out under it. It replaces the solid red slab the older
// screens drew (a big flat block with the title stuck in its top corner). `compact` is the phone layout.
void HoverNetScreenHeader(const char* title, bool compact)
{
    ImGui::Dummy(ImVec2(0.0f, compact ? 2.0f : 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
    if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
    ImGui::SetWindowFontScale(compact ? 1.0f : 1.3f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
    ImGui::PopStyleColor();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + width, at.y + 4.0f),
                                  IM_COL32(224, 42, 67, 255), IM_COL32(224, 42, 67, 0), IM_COL32(224, 42, 67, 0), IM_COL32(224, 42, 67, 255));
    ImGui::Dummy(ImVec2(0.0f, compact ? 8.0f : 14.0f));
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
                                 const PersonalBests::Outcome* pBests = nullptr, bool pWasOnline = false);

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
            MenuProcessEvent(event);
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
            MenuBeginChild("ConnectionLost", ImVec2(panelWidth, 275), true);
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
            if (HoverNetQuietButton("Open Settings", ImVec2(-1, 42))) {
                requestOpenRecoverySettings = true;
            }
            if (HoverNetQuietButton("Back to main menu", ImVec2(-1, 42))) {
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
            MenuBeginChild("ChooseRacerName", ImVec2(cardWidth, cardHeight), true);
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
                MenuBeginChild("GameListPanel", ImVec2(stacked ? 0.0f : leftColWidth, listHeight), true);
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

            MenuBeginChild("GameDetailsPanel", ImVec2(stacked ? 0.0f : detailsColWidth, detailsHeight), true,
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
                MenuBeginChild("RaceInfoColumn",
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
                    MenuBeginChild("SelectedRaceMap", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                    DrawTrackPreview(preview, 80.0f, false);
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
                MenuBeginChild("PlayersColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::Text("Players (%d / %d)", 1 + static_cast<int>(raceMembers.size()), kMaxPlayersPerRace);
                MenuBeginChild("PlayersListBox", ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y), true);
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
                MenuBeginChild("SelectedRaceMap", ImVec2(mapWidth, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
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
                MenuBeginChild("SelectedRaceInfo", ImVec2(0, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
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
            MenuBeginChild("ActionsPanel", ImVec2(0, actionsHeight), true);
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
            MenuBeginChild("UsersListPanel", ImVec2(tabbed ? 0.0f : leftColWidth, 0), true);
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
            MenuBeginChild("UsersListBox", ImVec2(-FLT_MIN, -FLT_MIN), true);
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
            MenuBeginChild("ChatPanel", ImVec2(0, 0), true);
            HoverNetSectionHeading("Chat");
            const float chatInputRow = ImGui::GetFrameHeightWithSpacing();
            MenuBeginChild("ChatLog", ImVec2(0, ImGui::GetContentRegionAvail().y - chatInputRow), true);
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

            MenuBeginChild("HostRaceOptions",
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
            MenuSliderInt("##HostLaps", &hostPrefs.mLaps, 1, 20);
            ImGui::EndDisabled();
            ImGui::Checkbox("Weapons enabled", &hostPrefs.mWeapons);
            ImGui::EndChild();

            TrackPreview& preview = lobbyTrackPreviews[hostPrefs.mTrackIndex];
            if (!preview.mLoaded) {
                preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(hostPrefs.mTrackIndex).c_str());
            }
            if (sideBySide) {
                ImGui::SameLine(0.0f, gap);
                MenuBeginChild("HostTrackPreview", ImVec2(0, selectorHeight), false);
                DrawTrackPreview(preview);
                ImGui::EndChild();
            }
            else {
                ImGui::Spacing();
                MenuBeginChild("HostTrackPreview", ImVec2(0, 190.0f), false);
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
            if (HoverNetQuietButton("Cancel", ImVec2(120, 36)) && !download.Active()) {
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
        HN_SCREEN_FADE();
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
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this dialog (docs/rmlui.md); if it cannot load, the ImGui one below is shown
        HoverNetRml::ChoiceInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        info.title = "QUIT HOVERNET?";
        info.eyebrow = "ARE YOU SURE?";
        info.subtitle = "Your settings and progress are kept.";
        info.panelTitle = "LEAVING SO SOON";
        info.reducedMotion = LoadReducedMotion();
        info.sound = [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); };
        { HoverNetRml::ChoiceOption keep; keep.label = "KEEP PLAYING"; keep.primary = true; info.options.push_back(keep); }
        { HoverNetRml::ChoiceOption quit; quit.label = "QUIT"; info.options.push_back(quit); }
        const int picked = HoverNetRml::RunChoiceMenu(graphics.GetWindow(), graphics.GetRenderer(), info, std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit, kWidth, kHeight);
        if (picked == 1) return true;
        if (picked == 0 || picked == HoverNetRml::kChoiceBack) return false;
        if (picked == HoverNetRml::kChoiceNone && pFrameLimit >= 0) return false;
        // otherwise (could not load): the ImGui dialog below
    }
#endif
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
            MenuProcessEvent(event);
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
        MenuBeginChild("QuitConfirmationPanel", ImVec2(panelWidth, panelHeight), true);
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

        HN_SCREEN_FADE();
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
    DuckMusic(true);   // the music steps back while the pause menu is up
    struct Unduck { ~Unduck() { DuckMusic(false); } } unduck;
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
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md); if it cannot load, the ImGui screen below is shown
        HoverNetRml::ChoiceInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        info.title = "GAME PAUSED";
        info.eyebrow = pIsOnline ? "ONLINE RACE" : "LOCAL RACE";
        info.subtitle = "Take a breath. The track will wait.";
        info.panelTitle = "RACE OPTIONS";
        info.reducedMotion = LoadReducedMotion();
        info.sound = [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); };
        for (int index = 0; index < optionCount; ++index) {
            HoverNetRml::ChoiceOption option;
            option.label = options[static_cast<std::size_t>(index)].label;
            option.primary = index == 0;   // Resume is the one red button
            if (index == primaryCount) option.headingBefore = "GAME & ACCESSIBILITY";
            info.options.push_back(option);
        }
        const int picked = HoverNetRml::RunChoiceMenu(graphics.GetWindow(), graphics.GetRenderer(), info, std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit, kWidth, kHeight);   // the scripted-input test lets the screen run until its script has acted
        auto shutdownImGui = [&]() { if (ownsImGuiContext) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); } };
        if (picked >= 0 && picked < optionCount) { shutdownImGui(); return options[static_cast<std::size_t>(picked)].choice; }
        if (picked == HoverNetRml::kChoiceBack) { shutdownImGui(); return PauseChoice::eResume; }
        if (picked == HoverNetRml::kChoiceClose && ConfirmQuit(graphics, buffer, viewport, font)) { shutdownImGui(); return PauseChoice::eQuit; }
        if (picked == HoverNetRml::kChoiceNone && pFrameLimit >= 0) { shutdownImGui(); return PauseChoice::eResume; }
        // otherwise (could not load, or the player changed their mind about closing): the ImGui pause menu below
    }
#endif
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
            MenuProcessEvent(event);
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
        MenuBeginChild("PauseMenuPanel", ImVec2(panelWidth, panelHeight), true);
        HoverNetSectionHeading("RACE OPTIONS");
        ImGui::Spacing();

        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        for (int index = 0; index < optionCount; ++index) {
            const float height = index < primaryCount ? 54.0f : 42.0f;
            // the first (Resume) is the one red button; the other choices are quiet, so a player sees at once what carries on the race
            if ((index == 0 ? HoverNetButton(options[index].label, ImVec2(-FLT_MIN, height)) : HoverNetQuietButton(options[index].label, ImVec2(-FLT_MIN, height)))) {
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
        HN_SCREEN_FADE();
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
            MenuProcessEvent(event);
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
        MenuBeginChild("ReplayEndPanel", ImVec2(panelWidth, panelHeight), true);
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
            if ((i == 0 ? HoverNetButton(actions[i].label, ImVec2(-FLT_MIN, 54.0f)) : HoverNetQuietButton(actions[i].label, ImVec2(-FLT_MIN, 44.0f)))) { choice = actions[i].choice; running = false; }
            ImGui::Spacing();
        }
        HoverNetHint("Esc / B: stay and look around (Left or Home rewinds)");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::End();
        HN_SCREEN_FADE();
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
                                 const PersonalBests::Outcome* pBests, bool pWasOnline)
{
    if (std::getenv("HOVERNET_TEST_FINISH") != nullptr) { std::printf("FINISHTEST results screen opened\n"); std::fflush(stdout); }
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md)
        HoverNetRml::ResultsInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        const bool podium = pRank >= 2 && pRank <= 3 && pPlayerCount >= 3;
        info.victory = pRank == 1;
        info.heading = pRank == 1 ? "VICTORY!" : podium ? "PODIUM FINISH" : "RACE COMPLETE";
        info.subtitle = pRank == 1 ? "You owned the track." : podium ? "On the podium. Now go and win it." : "Finish line crossed.";
        char finishText[32], bestLapText[32], previousText[32], lineText[160];
        FormatRaceTime(pFinishTime, finishText, sizeof(finishText));
        FormatRaceTime(pBestLap, bestLapText, sizeof(bestLapText));
        info.finishTime = finishText;
        info.bestLap = bestLapText;
        info.position = std::to_string(pRank) + " / " + std::to_string(std::max(1, pPlayerCount));
        if (pBests != nullptr && pBests->Any()) {
            if (pBests->newRaceBest) {
                if (pBests->hadRaceBest) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->previousRace), previousText, sizeof(previousText)); std::snprintf(lineText, sizeof(lineText), "NEW PERSONAL BEST!  (was %s)", previousText); }
                else std::snprintf(lineText, sizeof(lineText), "NEW PERSONAL BEST!  (your first finish on this track)");
                info.bests.push_back({lineText, true});
            }
            else if (pBests->raceBest > 0) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->raceBest), previousText, sizeof(previousText)); std::snprintf(lineText, sizeof(lineText), "Your best finish here: %s", previousText); info.bests.push_back({lineText, false}); }
            if (pBests->newLapBest) {
                if (pBests->hadLapBest) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->previousLap), previousText, sizeof(previousText)); std::snprintf(lineText, sizeof(lineText), "New best lap!  (was %s)", previousText); }
                else std::snprintf(lineText, sizeof(lineText), "New best lap!");
                info.bests.push_back({lineText, true});
            }
            else if (pBests->lapBest > 0) { FormatRaceTime(static_cast<MR_SimulationTime>(pBests->lapBest), previousText, sizeof(previousText)); std::snprintf(lineText, sizeof(lineText), "Your best lap here: %s", previousText); info.bests.push_back({lineText, false}); }
        }
        if (gRewardView.valid && gRewardView.reward.xp > 0) {
            const HoverNetProfile::Reward& reward = gRewardView.reward;
            info.hasReward = true;
            info.xp = reward.xp;
            info.breakdown = reward.breakdown;
            info.levelUp = reward.levelAfter > reward.levelBefore;
            info.levelLine = std::string(info.levelUp ? "LEVEL UP!  Level " : "Level ") + std::to_string(reward.levelAfter) + "  -  " + HoverNetCraft::LevelTitle(reward.levelAfter);
            if (info.levelUp) {
                std::string opened;
                for (int lv = reward.levelBefore + 1; lv <= reward.levelAfter; ++lv) { const std::string here = HoverNetCraft::UnlockedAtLevel(lv); if (!here.empty()) opened += (opened.empty() ? "" : ", ") + here; }
                if (!opened.empty()) info.opened = "New in the Garage: " + opened;
            }
            info.levelProgress = gRewardView.profile.LevelProgress();
            std::string names;
            for (int achievement : reward.unlocked) names += (names.empty() ? "" : ", ") + std::string(HoverNetProfile::AchievementName(achievement));
            if (!names.empty()) info.unlocked = "Unlocked: " + names;
        }
        info.online = pWasOnline;
        info.reducedMotion = LoadReducedMotion();
        info.sound = [](HoverNetRml::Sound sound) {
            if (std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST sound %s\n", sound == HoverNetRml::Sound::eHover ? "hover" : sound == HoverNetRml::Sound::eClick ? "click" : "back"); std::fflush(stdout); }
            PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack);
        };
        const char* testPick = std::getenv("HOVERNET_TEST_RESULTS_CHOICE");
        if (testPick != nullptr && std::string(testPick) == "main") return PostRaceChoice::eMainMenu;
        const HoverNetRml::ResultsChoice picked = HoverNetRml::RunResults(graphics.GetWindow(), graphics.GetRenderer(), info, std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit, kWidth, kHeight);   // the scripted-input test lets the screen run until its script has acted
        switch (picked) {
        case HoverNetRml::ResultsChoice::eLobby: return PostRaceChoice::eOnlineLobby;
        case HoverNetRml::ResultsChoice::eMainMenu: return PostRaceChoice::eMainMenu;
        case HoverNetRml::ResultsChoice::eQuitRequested: if (ConfirmQuit(graphics, buffer, viewport, font)) return PostRaceChoice::eQuit; break;   // changed their mind: the ImGui screen below
        case HoverNetRml::ResultsChoice::eNone: if (pFrameLimit >= 0) return PostRaceChoice::eMainMenu; break;
        }
    }
#endif
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
    // The card eases up into place over about a third of a second (not with reduced motion, and not for the screenshot hooks, so the regression captures stay what they were).
    const Uint32 openedAt = SDL_GetTicks();
    const bool slideIn = !LoadReducedMotion() && std::getenv("HOVERNET_NO_FADE") == nullptr && std::getenv("HOVERNET_CAPTURE_FRAME") == nullptr && std::getenv("HOVERNET_CAPTURE_MENU") == nullptr;
    bool focusFirstButton = true;
    bool running = true;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        if (framesShown == 2 && std::getenv("HOVERNET_TEST_RESULTS_CHOICE") != nullptr) {   // test hook (scripts/test-main-menu.sh, test-restart-race.sh)
            const std::string pick = std::getenv("HOVERNET_TEST_RESULTS_CHOICE");
            if (pick == "main") { choice = PostRaceChoice::eMainMenu; running = false; break; }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
            if (event.type == SDL_QUIT) {
                if (ConfirmQuit(graphics, buffer, viewport, font)) {
                    choice = PostRaceChoice::eQuit;
                    running = false;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                choice = PostRaceChoice::eMainMenu;
                running = false;
            }
            else if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                     event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                choice = PostRaceChoice::eMainMenu;
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
        const float cardHeight = std::min((pBests != nullptr && pBests->Any() ? 440.0f : 360.0f) + (pWasOnline ? 54.0f : 0.0f) +
                                          (gRewardView.valid ? 150.0f + (gRewardView.reward.unlocked.empty() ? 0.0f : 26.0f * (1.0f + static_cast<float>(gRewardView.reward.unlocked.size() / 3))) : 0.0f),
                                          workSize.y - 24.0f);
        float slideOffset = 0.0f;
        if (slideIn) {
            const float t = std::min(1.0f, static_cast<float>(SDL_GetTicks() - openedAt) / 360.0f);
            slideOffset = 46.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);   // ease out: fast at first, settling softly
        }
        ImGui::SetCursorPos(ImVec2((workSize.x - cardWidth) * 0.5f,
                                   (workSize.y - cardHeight) * 0.5f + slideOffset));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.065f, 0.085f, 0.97f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
        MenuBeginChild("ResultsCard", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(2.0f);
        const bool podium = pRank >= 2 && pRank <= 3 && pPlayerCount >= 3;   // 2nd or 3rd in a field of three or more
        const char* heading = pRank == 1 ? "VICTORY!" : podium ? "PODIUM FINISH" : "RACE COMPLETE";
        const float headingWidth = ImGui::CalcTextSize(heading).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - headingWidth) * 0.5f);
        ImGui::TextUnformatted(heading);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        const char* subtitle = pRank == 1 ? "You owned the track." : podium ? "On the podium. Now go and win it." : "Finish line crossed.";
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

        if (gRewardView.valid && gRewardView.reward.xp > 0) {
            // Progress: the XP this race earned, and the level bar (which fills as the screen opens).
            const HoverNetProfile::Reward& reward = gRewardView.reward;
            const HoverNetProfile::Profile& profile = gRewardView.profile;
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            char line[160];
            {   // the XP counts up over the first second, easing out, so the reward lands instead of just appearing
                const float t = std::min(1.0f, static_cast<float>(framesShown) / 50.0f);
                const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                std::snprintf(line, sizeof line, "+%d XP", static_cast<int>(reward.xp * eased + 0.5f));
            }
            if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.65f, 1.0f), "%s", line);
            if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", reward.breakdown.c_str());
            const bool levelUp = reward.levelAfter > reward.levelBefore;
            std::snprintf(line, sizeof line, levelUp ? "LEVEL UP!  Level %d  -  %s" : "Level %d  -  %s", reward.levelAfter, HoverNetCraft::LevelTitle(reward.levelAfter));
            ImGui::TextColored(levelUp ? ImVec4(1.0f, 0.82f, 0.35f, 1.0f) : ImVec4(0.92f, 0.93f, 0.97f, 1.0f), "%s", line);
            if (levelUp) {
                std::string opened;
                for (int lv = reward.levelBefore + 1; lv <= reward.levelAfter; ++lv) { const std::string here = HoverNetCraft::UnlockedAtLevel(lv); if (!here.empty()) opened += (opened.empty() ? "" : ", ") + here; }
                if (!opened.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "New in the Garage: %s", opened.c_str()); ImGui::PopTextWrapPos(); }
            }
            {
                const float width = ImGui::GetContentRegionAvail().x;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float fill = std::min(1.0f, profile.LevelProgress() * std::min(1.0f, static_cast<float>(framesShown) / 40.0f));
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(at, ImVec2(at.x + width, at.y + 8.0f), IM_COL32(48, 51, 66, 255), 4.0f);
                draw->AddRectFilled(at, ImVec2(at.x + width * fill, at.y + 8.0f), levelUp ? IM_COL32(255, 210, 90, 255) : IM_COL32(250, 115, 122, 255), 4.0f);
                ImGui::Dummy(ImVec2(width, 12.0f));
            }
            if (!reward.unlocked.empty()) {
                std::string names;
                for (int achievement : reward.unlocked) names += (names.empty() ? "" : ", ") + std::string(HoverNetProfile::AchievementName(achievement));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Unlocked: %s", names.c_str());
                ImGui::PopTextWrapPos();
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
        std::vector<PostRaceAction> actions;
        if (pWasOnline) actions.push_back({"RETURN TO LOBBY", PostRaceChoice::eOnlineLobby});
        actions.push_back({"MAIN MENU", PostRaceChoice::eMainMenu});
        static bool buttonsReported = false;
        if (!buttonsReported && framesShown >= 2 && std::getenv("HOVERNET_TEST_FINISH") != nullptr) {   // test hook (scripts/test-restart-race.sh): which buttons the results screen offers
            buttonsReported = true;
            std::string labels;
            for (const PostRaceAction& action : actions) labels += (labels.empty() ? "" : ", ") + std::string(action.label);
            std::printf("FINISHTEST results buttons: %s\n", labels.c_str()); std::fflush(stdout);
        }
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const float rowRoom = (ImGui::GetContentRegionAvail().y - 34.0f) / static_cast<float>(actions.size()) - ImGui::GetStyle().ItemSpacing.y;
            if ((index == 0 ? HoverNetButton(actions[index].label, ImVec2(-FLT_MIN, std::max(34.0f, std::min(54.0f, rowRoom + 8.0f)))) : HoverNetQuietButton(actions[index].label, ImVec2(-FLT_MIN, std::max(30.0f, std::min(44.0f, rowRoom)))))) {
                choice = actions[index].choice;
                running = false;
            }
            ImGui::Spacing();
        }
        HoverNetHint("Esc / B: main menu");
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::End();

        HN_SCREEN_FADE();
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

#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md): the same choices, briefing, records, map and download; if it cannot load, the ImGui screen below runs
        using HoverNetRml::FormField;
        std::unique_ptr<HoverNetRml::FormScreen> form(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "LOCAL RACE SETUP", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight));
        if (form->Ok()) {
            form->SetLayout(true, false);
            std::vector<std::string> items;
            for (int i = 0; i < Tracks().Count(); ++i) {
                const TrackEntry* e = Tracks().Get(i);
                items.push_back(e == nullptr ? std::string() : e->mName + (e->mOfficial ? "" : "  -  community") + (e->mInstalled ? "" : "  (download)"));
            }
            auto note = [](const std::string& text) { FormField f; f.type = FormField::Type::eNote; f.label = text; return f; };
            auto bestsText = [&]() {
                const std::string trackName = TrackNameAt(prefs.mTrackIndex);
                const long long raceBest = personalBests.RaceBest(trackName, prefs.mLaps), lapBest = personalBests.LapBest(trackName);
                char best[32];
                std::string text;
                if (raceBest > 0) { FormatRaceTime(static_cast<MR_SimulationTime>(raceBest), best, sizeof(best)); text += "Your best over " + std::to_string(prefs.mLaps) + " lap" + (prefs.mLaps == 1 ? "" : "s") + ": " + best; }
                if (lapBest > 0) { FormatRaceTime(static_cast<MR_SimulationTime>(lapBest), best, sizeof(best)); text += std::string(text.empty() ? "" : "   -   ") + "Your best lap on this track: " + best; }
                return text;
            };
            auto selectedEntry = [&]() { return Tracks().Get(prefs.mTrackIndex); };
            auto needsDownloadNow = [&]() { const TrackEntry* e = selectedEntry(); return e != nullptr && !e->mInstalled; };
            auto show = [&]() {
                const TrackBriefing guide = BriefingFor(prefs.mTrackIndex);
                const TrackEntry* entry = selectedEntry();
                std::vector<FormField> f;
                { FormField c; c.type = FormField::Type::eChooser; c.id = "track"; c.label = "Track"; c.items = items; c.selected = prefs.mTrackIndex; f.push_back(c); }
                { FormField h; h.type = FormField::Type::eHeading; h.label = "Course briefing"; f.push_back(h); }
                f.push_back(note("Style: " + guide.mCharacter + "     Difficulty: " + guide.mDifficulty));
                f.push_back(note(guide.mDescription));
                if (entry != nullptr && !entry->mInstalled) f.push_back(note("Not on this computer yet - it downloads (" + FormatMegabytes(entry->mDownloadBytes) + ") when you start."));
                if (guide.mFreePlay) f.push_back(note("Laps: not used on a free-play track."));
                else { FormField l; l.type = FormField::Type::eSlider; l.id = "laps"; l.label = "Laps"; l.value = static_cast<float>(prefs.mLaps); l.min = 1.0f; l.max = 20.0f; l.step = 1.0f; l.hint = bestsText(); f.push_back(l); f.push_back(note("Suggested: " + std::to_string(guide.mSuggestedLaps))); }
                { FormField w; w.type = FormField::Type::eToggle; w.id = "weapons"; w.label = "Weapons enabled"; w.on = prefs.mWeapons; f.push_back(w); }
                const std::vector<TrackEntry> missing = Tracks().MissingDownloads();
                if (!missing.empty()) { FormField b; b.type = FormField::Type::eButton; b.id = "dlall"; b.quiet = true; b.label = "Download all " + std::to_string(missing.size()) + " community tracks (" + FormatMegabytes(Tracks().MissingDownloadBytes()) + ")"; f.push_back(b); }
                form->SetFields(f);
                form->SetFooter(needsDownloadNow() ? "Download & Start Race" : "Start Race", "Cancel");
                form->SetSaveEnabled(!download.Active());
            };
            form->SetSideImage([&](SDL_Rect& crop) -> SDL_Texture* {
                TrackPreview& preview = trackPreviews[static_cast<std::size_t>(prefs.mTrackIndex)];
                if (!preview.mLoaded) preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(prefs.mTrackIndex).c_str());
                if (preview.mTexture == nullptr) return nullptr;
                crop = SDL_Rect{0, 0, preview.mWidth, preview.mHeight};
                return preview.mTexture;
            });
            show();
            int frame = 0;
            const int limit = std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit;
            bool closeRequested = false;
            while (running && (limit < 0 || frame < limit)) {
                ++frame;
                std::vector<HoverNetRml::FormEvent> events;
                HoverNetRml::FormOutcome outcome = form->Frame(events, frame, limit);
                if (autoStartForTest && frame == 1) outcome = HoverNetRml::FormOutcome::eSave;   // the test hook: Start Race pressed on the first frame
                bool refresh = false;
                for (const HoverNetRml::FormEvent& e : events) {
                    if (e.id == "track") { prefs.mTrackIndex = std::max(0, std::min(Tracks().Count() - 1, e.index)); refresh = true; }
                    else if (e.id == "laps") { prefs.mLaps = static_cast<int>(e.value + 0.5f); form->SetHint("laps", bestsText()); }
                    else if (e.id == "weapons") prefs.mWeapons = e.on;
                    else if (e.id == "dlall") { const std::vector<TrackEntry> missing = Tracks().MissingDownloads(); if (!download.Active()) { download.Begin(missing, 0); startAfterDownload = false; } }
                    else if (e.id == "busy") {
                        if (TrackDownloadJob* job = download.Job()) { if (!job->Finished()) job->Cancel(); else { download.Reset(); startAfterDownload = false; form->SetBusy("", "", -1.0f, ""); refresh = true; } }
                    }
                }
                if (outcome == HoverNetRml::FormOutcome::eSave && !download.Active()) {
                    const TrackEntry* entry = selectedEntry();
                    if (entry != nullptr && !entry->mInstalled) { download.Begin({*entry}, 0); startAfterDownload = true; }
                    else running = false;
                }
                else if (outcome == HoverNetRml::FormOutcome::eCancel && !download.Active()) { cancelled = true; running = false; }
                else if (outcome == HoverNetRml::FormOutcome::eClose) { cancelled = true; running = false; closeRequested = true; }
                if (TrackDownloadJob* job = download.Job()) {   // a download is under way: shown over the screen until it ends
                    if (!job->Finished()) {
                        const int total = job->TotalTracks(), done = std::min(job->CompletedTracks() + 1, total);
                        const long long totalBytes = std::max<long long>(1, job->TotalBytes());
                        form->SetBusy(total > 1 ? "Downloading track " + std::to_string(done) + " of " + std::to_string(total) : std::string("Downloading track"),
                                      job->CurrentName() + "   " + FormatMegabytes(job->BytesDone()) + " of " + FormatMegabytes(totalBytes),
                                      std::min(1.0f, static_cast<float>(job->BytesDone()) / static_cast<float>(totalBytes)), "Cancel");
                    }
                    else {
                        Tracks().Refresh();
                        if (job->Succeeded()) { download.Reset(); form->SetBusy("", "", -1.0f, ""); if (startAfterDownload) { startAfterDownload = false; running = false; } else refresh = true; }
                        else if (job->Cancelled() && job->Error().empty()) { download.Reset(); startAfterDownload = false; form->SetBusy("", "", -1.0f, ""); refresh = true; }
                        else form->SetBusy("The download did not finish", job->Error(), -1.0f, "OK");   // "busy" (OK) then closes it
                    }
                }
                if (refresh) {
                    for (TrackPreview& preview : trackPreviews) if (preview.mTexture != nullptr && !preview.mLoaded) { SDL_DestroyTexture(preview.mTexture); preview = TrackPreview(); }
                    show();
                }
            }
            form.reset();
            if (closeRequested && ConfirmQuit(graphics, buffer, viewport, font)) g_QuitConfirmed = true;
        }
        else form.reset();
    }
#endif

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
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
        HoverNetScreenHeader("LOCAL RACE SETUP", phoneLayout);
        ImGui::Spacing();
        if (!phoneLayout) ImGui::Spacing();

        const float panelWidth = std::min(900.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
        MenuBeginChild("LocalRaceSetupPanel", ImVec2(panelWidth, 0), true);

        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const bool sideBySide = contentWidth >= 610.0f;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float controlsWidth = sideBySide ? contentWidth * 0.52f : contentWidth;
        // Stacked (narrow) layout: size the options to their content so Cancel is never clipped.
        MenuBeginChild("RaceOptions", ImVec2(controlsWidth, sideBySide ? 0.0f : 0.0f),
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
        MenuSliderInt("##Laps", &prefs.mLaps, 1, 20);
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
        if (HoverNetQuietButton("Cancel", ImVec2(-FLT_MIN, tightHeight ? 40.0f : 36.0f))) {
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
            if (HoverNetQuietButton(label, ImVec2(-FLT_MIN, 0)) && !download.Active()) {
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
            MenuBeginChild("TrackPreview", ImVec2(0, 0), false);
            DrawTrackPreview(preview);
            ImGui::EndChild();
        }
        else {
            ImGui::Spacing();
            MenuBeginChild("TrackPreview", ImVec2(0, phoneLayout ? 200.0f : 240.0f), false);
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

        HN_SCREEN_FADE();
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
    GraphicsSettings graphicsSettings = LoadGraphicsSettings();
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

#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md): the same settings, side effects and Save/Cancel; if it cannot load, the ImGui screen below runs
        using HoverNetRml::FormField;
        std::unique_ptr<HoverNetRml::FormScreen> form(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "SETTINGS", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight));
        if (form->Ok()) {
            const std::vector<std::string> tabNames = {"Player", "Sound", "Access", "Graphics", "Display"};
            int tab = 0;
            bool gfxMore = false;
            float musicPercent = static_cast<float>(LoadMusicVolume() * 100.0);
            static const HoverNetGfx::Preset presets[] = {HoverNetGfx::Preset::eAuto, HoverNetGfx::Preset::eModern, HoverNetGfx::Preset::eModernHigh, HoverNetGfx::Preset::eModernLite};
            static const char* const raceViewLabels[] = {"Classic (4:3)", "Widescreen (16:9)", "Match my window"};
            auto heading = [](const char* label) { FormField f; f.type = FormField::Type::eHeading; f.label = label; return f; };
            auto note = [](const char* label) { FormField f; f.type = FormField::Type::eNote; f.label = label; return f; };
            auto text = [](const char* id, const char* label, const char* value, const char* hint = "") { FormField f; f.type = FormField::Type::eText; f.id = id; f.label = label; f.text = value; f.hint = hint; return f; };
            auto toggle = [](const char* id, const char* label, bool on, const char* hint = "") { FormField f; f.type = FormField::Type::eToggle; f.id = id; f.label = label; f.on = on; f.hint = hint; return f; };
            auto slider = [](const char* id, const char* label, float value, float lo, float hi, const char* suffix, const char* hint = "") {
                FormField f; f.type = FormField::Type::eSlider; f.id = id; f.label = label; f.value = value; f.min = lo; f.max = hi; f.step = 1.0f; f.suffix = suffix; f.hint = hint; return f; };
            auto select = [](const char* id, const char* label, std::vector<std::string> options, int selected, const char* hint = "") {
                FormField f; f.type = FormField::Type::eSelect; f.id = id; f.label = label; f.options = std::move(options); f.selected = selected; f.hint = hint; return f; };
            auto button = [](const char* id, const char* label) { FormField f; f.type = FormField::Type::eButton; f.id = id; f.label = label; f.quiet = true; return f; };
            auto resolutionLabel = [&](int index) { return std::to_string(resolutions[index].width) + " x " + std::to_string(resolutions[index].height); };

            // what Save needs to be true (the same rules as the ImGui screen)
            auto validate = [&](std::string& hostHint, std::string& authHint) {
                const bool usernameBlank = IsBlank(usernameBuf), hostBlank = IsBlank(hostBuf), portValid = port > 0 && port <= 65535;
                bool authAddressValid = true;
                std::string problem;
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
                if (authAddressBuf[0] != '\0') {
                    const std::string typedAddress = authAddressBuf;
#ifdef HOVERNET_WEB
                    authAddressValid = typedAddress.compare(0, 6, "wss://") == 0 || typedAddress.compare(0, 5, "ws://") == 0;
                    problem = "A web address starts with wss://";
#else
                    std::string checkHost; int checkPort = 0;
                    authAddressValid = SplitServerAddress(typedAddress, checkHost, checkPort);
                    problem = "Type it as name:port, for example race.example.org:9610";
                    Crypto::Bytes checkKey;
                    if (authAddressValid && authKeyBuf[0] != '\0' && (!Crypto::FromHex(authKeyBuf, checkKey) || checkKey.size() != 32)) { authAddressValid = false; problem = "The server key is 64 characters (0 to 9, a to f)"; }
#endif
                }
#endif
                hostHint = hostBlank ? "Enter a server address" : !portValid ? "Port must be between 1 and 65535" : "";
                authHint = authAddressValid ? "" : problem;
                form->SetHint("name", usernameBlank ? "Enter a display name" : "");
                form->SetHint("host", hostHint);
                form->SetHint("authaddr", authHint);
                return !usernameBlank && !hostBlank && portValid && authAddressValid;
            };
            auto fieldsFor = [&](int which) {
                std::vector<FormField> f;
                if (which == 0) {
                    f.push_back(heading("Display Name"));
                    f.push_back(text("name", "", usernameBuf));
                    f.push_back(heading("RaceServer Address"));
                    f.push_back(text("host", "Address", hostBuf));
                    f.push_back(text("port", "Port", std::to_string(port).c_str()));
                    f.push_back(button("resetserver", "Reset to default server"));
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
                    f.push_back(heading("Online server"));
                    f.push_back(note("Leave empty to use the official HoverNet server. For a friend's or a club's server, type its address."));
#ifdef HOVERNET_WEB
                    f.push_back(text("authaddr", "Address (wss://race.example.org/auth/)", authAddressBuf));
#else
                    f.push_back(text("authaddr", "Address (race.example.org:9610)", authAddressBuf));
                    f.push_back(text("authkey", "Server key, 64 characters (optional)", authKeyBuf));
#endif
                    f.push_back(button("useofficial", "Use the official HoverNet server"));
#endif
                }
                else if (which == 1) {
                    f.push_back(heading("Audio"));
                    f.push_back(toggle("mute", "Mute all sound", muted));
                    f.push_back(slider("volume", "Volume", volumePercent, 0.0f, 100.0f, "%"));
                    f.push_back(slider("music", "Music volume", musicPercent, 0.0f, 100.0f, "%"));
                    f.push_back(note("Music: Hard Left by Dan Knoflicek, Asphalt by Sygil (open licences, see NetTarget/Music/CREDITS.md)."));
                }
                else if (which == 2) {
                    f.push_back(slider("uiscale", "UI Scale", uiScalePercent, 75.0f, 150.0f, "%", "Applies to the other menus, and to this one the next time it opens."));
                    f.push_back(toggle("largehud", "Large HUD text", largeHudText, "Keeps race information and chat at the font's native size."));
                    f.push_back(toggle("reduced", "Reduced motion", reducedMotion, "Turns off menu animations and the moving effects in a race."));
                    f.push_back(toggle("ghost", "Show my best run as a ghost", showGhost, "Draws your fastest run on this track beside your craft, so you race your own best time. It never touches the race."));
                    f.push_back(toggle("contrast", "High contrast menus", highContrast, "Strengthens text, borders, fields, and focus states (the older menus; these menus already have strong contrast)."));
                }
                else if (which == 3) {
                    std::vector<std::string> presetLabels;
                    int presetIndex = 0;
                    for (int i = 0; i < 4; ++i) { presetLabels.push_back(HoverNetGfx::PresetLabel(presets[i])); if (presets[i] == graphicsSettings.preset) presetIndex = i; }
                    f.push_back(heading("Graphics"));
                    f.push_back(select("preset", "Quality", presetLabels, presetIndex));
                    if (graphicsSettings.preset == HoverNetGfx::Preset::eAuto) f.push_back(note("Automatic keeps the picture as good as this computer can run smoothly: it turns effects up when there is room and down when frames slow. The options below are what it may use; one that is off is never used."));
                    f.push_back(toggle("gpu", "Use my graphics card for the picture passes", graphicsSettings.gpu));
                    f.push_back(toggle("shadows", "Soft craft shadows", graphicsSettings.shadows));
                    if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite) f.push_back(toggle("deband", "Smooth colour bands", graphicsSettings.deband));
                    if (graphicsSettings.preset == HoverNetGfx::Preset::eModernHigh || graphicsSettings.preset == HoverNetGfx::Preset::eAuto) {
                        f.push_back(toggle("smoothing", "Smooth jagged edges", graphicsSettings.smoothing));
                        f.push_back(toggle("bloom", "Glow around bright lights", graphicsSettings.bloom));
                    }
                    if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite) f.push_back(toggle("textures", "Smooth textures (true colour)", graphicsSettings.textures));
                    f.push_back(toggle("lighting", "Light from the craft, and distance haze", graphicsSettings.lighting));
                    if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite) {
                        f.push_back(button("gfxmore", gfxMore ? "Fewer graphics options" : "More graphics options"));
                        if (gfxMore) {
                            f.push_back(toggle("occlusion", "Walls block light", graphicsSettings.occlusion));
                            f.push_back(toggle("atmosphere", "Sky-tinted ambient light, mist and horizon glow", graphicsSettings.atmosphere));
                            f.push_back(toggle("edges", "Anti-aliased edges", graphicsSettings.edges));
                            f.push_back(toggle("craft", "Craft shine, rim light and reflection (high quality)", graphicsSettings.craft));
                            f.push_back(toggle("halos", "Glows on missiles, mines and power-ups", graphicsSettings.halos));
                            f.push_back(toggle("sharpen", "Sharper textures (high quality)", graphicsSettings.sharpen));
                            f.push_back(toggle("exposure", "Automatic exposure", graphicsSettings.exposure));
                            f.push_back(toggle("adaptive", "Lower the quality automatically when it is too slow", graphicsSettings.adaptive));
                        }
                    }
                    f.push_back(toggle("effects", "Engine glow, trail, sparks, exhaust and dust", graphicsSettings.effects));
                    f.push_back(toggle("grade", "Richer colour and vignette", graphicsSettings.grade));
                    f.push_back(slider("brightness", "Brightness", graphicsSettings.brightness * 100.0f, 70.0f, 140.0f, "%"));
                    f.push_back(note("Only how the race looks: it never changes how it plays. High needs a fast processor; Light suits phones and older machines."));
                }
                else {
                    if (!HoverNetIsTouchDevice()) f.push_back(select("raceview", "Race view", {raceViewLabels[0], raceViewLabels[1], raceViewLabels[2]}, static_cast<int>(raceView), "The shape of the picture while racing. Takes effect the next time you start HoverNet."));
#ifdef HOVERNET_WEB
                    if (!HoverNetIsTouchDevice()) {
                        const int browserWindow = hovernet_js_window_size();
                        const int browserW = browserWindow >> 16, browserH = browserWindow & 0xffff;
                        const int currentSize = hovernet_js_get_game_size();
                        std::vector<std::string> labels; int at = 0;
                        static const int sizes[][2] = {{0, 0}, {800, 600}, {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}};
                        for (const auto& size : sizes) {
                            if (size[0] > browserW || size[1] > browserH) continue;
                            if ((size[0] << 16 | size[1]) == currentSize) at = static_cast<int>(labels.size());
                            labels.push_back(size[0] == 0 ? std::string("Fit browser window") : std::to_string(size[0]) + " x " + std::to_string(size[1]));
                        }
                        f.push_back(select("gamesize", "Game Size", labels, at, "Sizes only the game area; the browser window stays as it is."));
                    }
#else
                    std::vector<std::string> labels;
                    for (int i = 0; i < static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0])); ++i) labels.push_back(resolutionLabel(i));
                    f.push_back(select("resolution", "Window Resolution", labels, std::max(0, selectedResolution)));
#endif
                    f.push_back(toggle("fullscreen", "Fullscreen", fullscreen));
                    f.push_back(button("defaults", "Restore all defaults"));
                    f.push_back(note("Preview only until Save is selected."));
                }
                return f;
            };
            auto show = [&]() { form->SetTabs(tabNames, tab); form->SetFields(fieldsFor(tab)); std::string a, b; form->SetSaveEnabled(validate(a, b)); };
            show();
            int frame = 0;
            const int limit = std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit;
            bool closeRequested = false;
            while (running && (limit < 0 || frame < limit)) {
                ++frame;
                std::vector<HoverNetRml::FormEvent> events;
                const HoverNetRml::FormOutcome outcome = form->Frame(events, frame, limit);
                bool refresh = false;
                for (const HoverNetRml::FormEvent& e : events) {
                    const std::string& id = e.id;
                    if (id == "tab") { tab = e.index; show(); continue; }
                    if (id == "name") std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", e.text.c_str());
                    else if (id == "host") std::snprintf(hostBuf, sizeof(hostBuf), "%s", e.text.c_str());
                    else if (id == "port") port = std::atoi(e.text.c_str());
                    else if (id == "resetserver") { std::snprintf(hostBuf, sizeof(hostBuf), "%s", kDefaultLobbyHost); port = static_cast<int>(kDefaultLobbyPort); refresh = true; }
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
                    else if (id == "authaddr") std::snprintf(authAddressBuf, sizeof(authAddressBuf), "%s", e.text.c_str());
                    else if (id == "authkey") std::snprintf(authKeyBuf, sizeof(authKeyBuf), "%s", e.text.c_str());
                    else if (id == "useofficial") { authAddressBuf[0] = '\0'; authKeyBuf[0] = '\0'; refresh = true; }
#endif
                    else if (id == "mute") { muted = e.on; MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0); }
                    else if (id == "volume") { volumePercent = e.value; MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0); }
                    else if (id == "music") { musicPercent = e.value; MR_SoundServer::MusicSetVolume(musicPercent / 100.0 * gMusicState.gain); SaveMusicVolume(musicPercent / 100.0); }
                    else if (id == "uiscale") uiScalePercent = e.value;
                    else if (id == "largehud") largeHudText = e.on;
                    else if (id == "reduced") reducedMotion = e.on;
                    else if (id == "ghost") showGhost = e.on;
                    else if (id == "contrast") highContrast = e.on;
                    else if (id == "preset") { graphicsSettings.preset = presets[std::max(0, std::min(2, e.index))]; refresh = true; }
                    else if (id == "gfxmore") { gfxMore = !gfxMore; refresh = true; }
                    else if (id == "gpu") graphicsSettings.gpu = e.on;
                    else if (id == "shadows") graphicsSettings.shadows = e.on;
                    else if (id == "deband") graphicsSettings.deband = e.on;
                    else if (id == "smoothing") graphicsSettings.smoothing = e.on;
                    else if (id == "bloom") graphicsSettings.bloom = e.on;
                    else if (id == "textures") graphicsSettings.textures = e.on;
                    else if (id == "lighting") graphicsSettings.lighting = e.on;
                    else if (id == "occlusion") graphicsSettings.occlusion = e.on;
                    else if (id == "atmosphere") graphicsSettings.atmosphere = e.on;
                    else if (id == "edges") graphicsSettings.edges = e.on;
                    else if (id == "craft") graphicsSettings.craft = e.on;
                    else if (id == "halos") graphicsSettings.halos = e.on;
                    else if (id == "sharpen") graphicsSettings.sharpen = e.on;
                    else if (id == "exposure") graphicsSettings.exposure = e.on;
                    else if (id == "adaptive") graphicsSettings.adaptive = e.on;
                    else if (id == "effects") graphicsSettings.effects = e.on;
                    else if (id == "grade") graphicsSettings.grade = e.on;
                    else if (id == "brightness") graphicsSettings.brightness = e.value / 100.0f;
                    else if (id == "raceview") raceView = static_cast<RaceView>(std::max(0, std::min(2, e.index)));
#ifdef HOVERNET_WEB
                    else if (id == "gamesize") {
                        static const int sizes[][2] = {{0, 0}, {800, 600}, {1024, 768}, {1280, 720}, {1600, 900}, {1920, 1080}};
                        const int browserWindow = hovernet_js_window_size();
                        int shown = 0;
                        for (const auto& size : sizes) { if (size[0] > (browserWindow >> 16) || size[1] > (browserWindow & 0xffff)) continue; if (shown++ == e.index) hovernet_js_set_game_size(size[0], size[1]); }
                    }
#else
                    else if (id == "resolution") {
                        selectedResolution = std::max(0, std::min(static_cast<int>(sizeof(resolutions) / sizeof(resolutions[0])) - 1, e.index));
                        windowWidth = resolutions[selectedResolution].width;
                        windowHeight = resolutions[selectedResolution].height;
                        if (!fullscreen) {
                            SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
                            SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                        }
                    }
#endif
                    else if (id == "fullscreen") {
                        fullscreen = e.on;
                        SDL_SetWindowFullscreen(graphics.GetWindow(), fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        if (!fullscreen) {
                            SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
                            SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                        }
                    }
                    else if (id == "defaults") {
                        std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", "Player");
                        std::snprintf(hostBuf, sizeof(hostBuf), "%s", kDefaultLobbyHost);
                        port = static_cast<int>(kDefaultLobbyPort);
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
                        authAddressBuf[0] = '\0'; authKeyBuf[0] = '\0';
#endif
                        volumePercent = 100.0f; muted = false; uiScalePercent = 100.0f; largeHudText = false; raceView = RaceView::eClassic; showGhost = true;
                        reducedMotion = false; highContrast = false; selectedResolution = 0; windowWidth = resolutions[0].width; windowHeight = resolutions[0].height; fullscreen = false;
#ifdef HOVERNET_WEB
                        hovernet_js_set_game_size(0, 0);
#endif
                        MR_SoundServer::SetMasterVolume(1.0);   // a live preview, like changing the controls one by one; Cancel restores the entry state
                        SDL_SetWindowFullscreen(graphics.GetWindow(), 0);
                        SDL_SetWindowSize(graphics.GetWindow(), windowWidth, windowHeight);
                        SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                        refresh = true;
                    }
                }
                if (refresh) show();
                else if (!events.empty()) { std::string a, b; form->SetSaveEnabled(validate(a, b)); }
                if (outcome == HoverNetRml::FormOutcome::eSave) running = false;
                else if (outcome == HoverNetRml::FormOutcome::eCancel) { cancelled = true; running = false; }
                else if (outcome == HoverNetRml::FormOutcome::eClose) { cancelled = true; running = false; closeRequested = true; }
            }
            form.reset();   // the screen is closed before anything else is shown
            if (closeRequested && ConfirmQuit(graphics, buffer, viewport, font)) g_QuitConfirmed = true;
        }
        else form.reset();
    }
#endif

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
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
        HoverNetScreenHeader("SETTINGS", phoneHeader);
        ImGui::Spacing();
        ImGui::Spacing();

        const float centerWidth = std::min(460.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        // Fill the available height so added accessibility/audio controls do not
        // force scrolling while the screen still has unused space below.
        // Phones: the Save/Cancel row is pinned under the scrolling panel so it never needs a scroll.
        MenuBeginChild("SettingsPanel", ImVec2(centerWidth, -56.0f), true, ImGuiWindowFlags_NoScrollbar);   // Save and Cancel sit under the panel, so the panel never scrolls
        if (const char* testScroll = std::getenv("HOVERNET_TEST_SETTINGS_SCROLL")) { if (framesShown >= 2) ImGui::SetScrollY(static_cast<float>(std::atoi(testScroll))); }   // screenshot tests: look further down

        // What the Save button needs to be true. Computed before the tabs (and again after them), because the fields live on different tabs.
        bool usernameBlank = false, hostBlank = false, portValid = true, authAddressValid = true;
        const char* authAddressProblem = "";
        auto validateSettings = [&]() {
            usernameBlank = IsBlank(usernameBuf);
            hostBlank = IsBlank(hostBuf);
            portValid = port > 0 && port <= 65535;
            authAddressValid = true;
            authAddressProblem = "";
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
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
#endif
        };
        validateSettings();
        // Tabs instead of one long list, so nothing needs scrolling: Player, Sound, Access, Graphics, Display.
        const char* testTab = std::getenv("HOVERNET_TEST_SETTINGS_TAB");   // screenshots: open a given tab (0..3)
        auto settingsTabFlag = [&](int index) { return testTab != nullptr && std::atoi(testTab) == index && framesShown <= 2 ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        const bool tabsOpen = ImGui::BeginTabBar("##settingsTabs");
        bool tabOpen = tabsOpen && ImGui::BeginTabItem("Player", nullptr, settingsTabFlag(0));
        if (tabOpen) NoteTabSound("Player");
        if (tabOpen) {
        HoverNetSectionHeading("Display Name");
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputText("##Username", usernameBuf, sizeof(usernameBuf));
        ImGui::PopItemWidth();
        if (usernameBlank) {
            HoverNetHint("Enter a display name");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        HoverNetSectionHeading("RaceServer Address");
        ImGui::SetNextItemWidth(-96.0f);
        ImGui::InputText("##ServerHost", hostBuf, sizeof(hostBuf));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("##ServerPort", &port, 0);
        if (hostBlank) {
            HoverNetHint("Enter a server address");
        }
        else if (!portValid) {
            HoverNetHint("Port must be between 1 and 65535");
        }
        ImGui::Spacing();
        if (HoverNetQuietButton("Reset to default server", ImVec2(-FLT_MIN, 0))) {
            std::snprintf(hostBuf, sizeof(hostBuf), "%s", kDefaultLobbyHost);
            port = static_cast<int>(kDefaultLobbyPort);
        }

#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        HoverNetSectionHeading("Online server");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Leave empty to use the official HoverNet server. For a friend's or a club's server, type its address.");
        ImGui::PopTextWrapPos();
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
        if (HoverNetQuietButton("Use the official HoverNet server", ImVec2(-FLT_MIN, 0))) {
            authAddressBuf[0] = '\0';
            authKeyBuf[0] = '\0';
        }
#endif

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        }
        if (tabOpen) ImGui::EndTabItem();
        tabOpen = tabsOpen && ImGui::BeginTabItem("Sound", nullptr, settingsTabFlag(1));
        if (tabOpen) NoteTabSound("Sound");
        if (tabOpen) {
        HoverNetSectionHeading("Audio");
        if (ImGui::Checkbox("Mute all sound", &muted)) {
            MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0);
        }
        ImGui::PushItemWidth(-1.0f);
        // Keep the chosen level independently of mute so unmuting restores it
        // instead of forgetting the player's setting. Both controls preview live.
        if (MenuSliderFloat("##Volume", &volumePercent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            MR_SoundServer::SetMasterVolume(muted ? 0.0 : volumePercent / 100.0);
        }
        ImGui::PopItemWidth();

        {
            // The music has its own level, and it changes (and is kept) at once.
            ImGui::Spacing();
            ImGui::TextUnformatted("Music volume");
            static float musicPercent = -1.0f;
            if (musicPercent < 0.0f) musicPercent = static_cast<float>(LoadMusicVolume() * 100.0);
            ImGui::PushItemWidth(-1.0f);
            if (MenuSliderFloat("##MusicVolume", &musicPercent, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
                MR_SoundServer::MusicSetVolume(musicPercent / 100.0 * gMusicState.gain);
                SaveMusicVolume(musicPercent / 100.0);
            }
            ImGui::PopItemWidth();
            ImGui::TextDisabled("Music: Hard Left by Dan Knoflicek, Asphalt by Sygil (open licences, see NetTarget/Music/CREDITS.md).");
        }
        }
        if (tabOpen) ImGui::EndTabItem();
        tabOpen = tabsOpen && ImGui::BeginTabItem("Access", nullptr, settingsTabFlag(4));
        if (tabOpen) NoteTabSound("Access");
        if (tabOpen) {
        ImGui::TextUnformatted("UI Scale");
        ImGui::PushItemWidth(-1.0f);
        if (MenuSliderFloat("##UiScale", &uiScalePercent, 75.0f, 150.0f, "%.0f%%",
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

        }
        if (tabOpen) ImGui::EndTabItem();
        tabOpen = tabsOpen && ImGui::BeginTabItem("Graphics", nullptr, settingsTabFlag(3));
        if (tabOpen) NoteTabSound("Graphics");
        if (tabOpen) {
        ImGui::TextUnformatted("Graphics");
        {
            static const HoverNetGfx::Preset presets[] = {HoverNetGfx::Preset::eAuto, HoverNetGfx::Preset::eModern, HoverNetGfx::Preset::eModernHigh, HoverNetGfx::Preset::eModernLite};
            ImGui::PushItemWidth(-1.0f);
            if (ImGui::BeginCombo("##GraphicsPreset", HoverNetGfx::PresetLabel(graphicsSettings.preset))) {
                for (const HoverNetGfx::Preset preset : presets) {
                    if (ImGui::Selectable(HoverNetGfx::PresetLabel(preset), graphicsSettings.preset == preset)) graphicsSettings.preset = preset;
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            if (graphicsSettings.preset == HoverNetGfx::Preset::eAuto) HoverNetHint("Keeps the picture as good as this computer can run smoothly: effects go up when there is room and down when frames slow. The options below are what it may use; one that is off is never used.");
            {
                ImGui::Checkbox("Use my graphics card for the picture passes", &graphicsSettings.gpu);
                ImGui::Checkbox("Soft craft shadows", &graphicsSettings.shadows);
                if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite) {
                    ImGui::Checkbox("Smooth colour bands", &graphicsSettings.deband);
                }
                if (graphicsSettings.preset == HoverNetGfx::Preset::eModernHigh || graphicsSettings.preset == HoverNetGfx::Preset::eAuto) {
                    ImGui::Checkbox("Smooth jagged edges", &graphicsSettings.smoothing);
                    ImGui::Checkbox("Glow around bright lights", &graphicsSettings.bloom);
                }
                if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite) {
                    ImGui::Checkbox("Smooth textures (true colour)", &graphicsSettings.textures);
                }
                ImGui::Checkbox("Light from the craft, and distance haze", &graphicsSettings.lighting);
                if (graphicsSettings.preset != HoverNetGfx::Preset::eModernLite && ImGui::TreeNode("More graphics options")) {
                    ImGui::Checkbox("Walls block light", &graphicsSettings.occlusion);
                    ImGui::Checkbox("Sky-tinted ambient light, mist and horizon glow", &graphicsSettings.atmosphere);
                    ImGui::Checkbox("Anti-aliased edges", &graphicsSettings.edges);
                    ImGui::Checkbox("Craft shine, rim light and reflection (high quality)", &graphicsSettings.craft);
                    ImGui::Checkbox("Glows on missiles, mines and power-ups", &graphicsSettings.halos);
                    ImGui::Checkbox("Sharper textures (high quality)", &graphicsSettings.sharpen);
                    ImGui::Checkbox("Automatic exposure", &graphicsSettings.exposure);
                    ImGui::Checkbox("Lower the quality automatically when it is too slow", &graphicsSettings.adaptive);
                    ImGui::TreePop();
                }
                ImGui::Checkbox("Engine glow, trail, sparks, exhaust and dust", &graphicsSettings.effects);
                ImGui::Checkbox("Richer colour and vignette", &graphicsSettings.grade);
            }
            {
                ImGui::TextUnformatted("Brightness");
                float brightnessPercent = graphicsSettings.brightness * 100.0f;
                ImGui::PushItemWidth(-1.0f);
                if (MenuSliderFloat("##Brightness", &brightnessPercent, 70.0f, 140.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) graphicsSettings.brightness = brightnessPercent / 100.0f;
                ImGui::PopItemWidth();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Only how the race looks: it never changes how it plays. High needs a fast processor; Light suits phones and older machines.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }

        }
        if (tabOpen) ImGui::EndTabItem();
        tabOpen = tabsOpen && ImGui::BeginTabItem("Display", nullptr, settingsTabFlag(4));
        if (tabOpen) NoteTabSound("Display");
        if (tabOpen) {
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
        if (HoverNetQuietButton("Restore all defaults", ImVec2(-FLT_MIN, 34))) {
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
        }   // the Display tab ends with the Restore button
        if (tabOpen) ImGui::EndTabItem();
        if (tabsOpen) ImGui::EndTabBar();

        validateSettings();
        const bool canSave = !usernameBlank && !hostBlank && portValid && authAddressValid;
        if (false) {
            ImGui::BeginDisabled(!canSave);
            if (HoverNetButton("Save", ImVec2(-FLT_MIN, 40))) {
                running = false;
            }
            ImGui::EndDisabled();
            ImGui::Spacing();
            if (HoverNetQuietButton("Cancel", ImVec2(-FLT_MIN, 36))) {
                cancelled = true;
                running = false;
            }
        }

        ImGui::EndChild();
        if (true) {
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
            const float half = (centerWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::BeginDisabled(!canSave);
            if (HoverNetButton("Save", ImVec2(half, 40))) {
                running = false;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (HoverNetQuietButton("Cancel", ImVec2(half, 40))) {
                cancelled = true;
                running = false;
            }
        }
        ImGui::End();

        HN_SCREEN_FADE();
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

    if (!cancelled) { SaveRaceView(raceView); SaveShowGhost(showGhost); SaveGraphicsSettings(graphicsSettings); }
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

#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md): the same bindings, swapping on conflict, axes and defaults; if it cannot load, the ImGui screen below runs
        using HoverNetRml::FormField;
        std::unique_ptr<HoverNetRml::FormScreen> form(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "CONTROLS", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight));
        if (form->Ok()) {
            const std::vector<std::string> tabNames = {"Keyboard", "Controller"};
            const char* keyLabels[] = {"Accelerate", "Brake / Reverse", "Steer left", "Steer right", "Jump", "Fire weapon", "Select weapon"};
            const char* padLabels[] = {"Jump", "Fire weapon", "Select weapon"};
            const char* axisNames[] = {"Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "Left trigger", "Right trigger"};
            const int axisCount = static_cast<int>(SDL_CONTROLLER_AXIS_MAX);
            int tab = 0;
            std::string status;
            auto heading = [](const char* label) { FormField f; f.type = FormField::Type::eHeading; f.label = label; return f; };
            auto note = [](const std::string& label) { FormField f; f.type = FormField::Type::eNote; f.label = label; return f; };
            auto button = [](const std::string& id, const std::string& label, bool quiet = false) { FormField f; f.type = FormField::Type::eButton; f.id = id; f.label = label; f.quiet = quiet; return f; };
            auto toggle = [](const char* id, const char* label, bool on) { FormField f; f.type = FormField::Type::eToggle; f.id = id; f.label = label; f.on = on; return f; };
            auto axisSelect = [&](const char* id, const char* label, int current) {
                FormField f; f.type = FormField::Type::eSelect; f.id = id; f.label = label; for (int i = 0; i < axisCount; ++i) f.options.push_back(axisNames[i]); f.selected = std::max(0, std::min(axisCount - 1, current)); return f; };
            auto show = [&]() {
                std::vector<FormField> f;
                if (tab == 0) {
                    f.push_back(heading("Keyboard"));
                    f.push_back(note("Choose a control, then press the key you want. Escape cancels. Using a key that another control has swaps the two."));
                    for (int i = 0; i < 7; ++i) {
                        const std::string key = listeningForBinding == i ? "Press a key..." : SDL_GetScancodeName(*remappableBindings[i]);
                        f.push_back(button("key" + std::to_string(i), std::string(keyLabels[i]) + ":   " + key, true));
                    }
                    f.push_back(heading("Fixed keys"));
                    f.push_back(note("External / Cockpit view:  F3 / F4"));
                    f.push_back(note("Player list / More messages:  F5 / F6"));
                    f.push_back(note("HUD margin:  + / -"));
                    f.push_back(note("Scroll HUD:  Page Up / Page Down"));
                    f.push_back(note("HUD zoom:  Insert / Delete"));
                    f.push_back(note("Reset HUD:  Home"));
                    f.push_back(note("Camera distance (replays, spectating):  C"));
                    f.push_back(note("Pause menu:  Escape"));
                }
                else {
                    int recognised = 0;
                    for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) ++recognised;
                    f.push_back(heading("Controller buttons"));
                    f.push_back(note(recognised > 0 ? std::to_string(recognised) + " controller" + (recognised == 1 ? "" : "s") + " connected." : "No SDL-compatible controller detected; keyboard controls remain available."));
                    f.push_back(note("Choose a control, then press the button you want. Start is reserved for pause."));
                    for (int i = 0; i < 3; ++i) {
                        const char* name = listeningForControllerBinding == i ? "Press a button..." : SDL_GameControllerGetStringForButton(*remappableControllerBindings[i]);
                        if (name == nullptr || name[0] == '\0') name = "Unknown";
                        f.push_back(button("pad" + std::to_string(i), std::string(padLabels[i]) + ":   " + name, true));
                    }
                    f.push_back(heading("Controller axes"));
                    f.push_back(axisSelect("steeraxis", "Steering axis", static_cast<int>(gControllerBindings.steerAxis)));
                    f.push_back(toggle("invertsteer", "Invert steering", gControllerBindings.invertSteering));
                    f.push_back(axisSelect("accelaxis", "Accelerate axis", static_cast<int>(gControllerBindings.accelerateAxis)));
                    f.push_back(toggle("invertaccel", "Invert accelerate direction", gControllerBindings.invertAccelerate));
                    f.push_back(axisSelect("brakeaxis", "Brake / reverse axis", static_cast<int>(gControllerBindings.brakeAxis)));
                    f.push_back(toggle("invertbrake", "Invert brake direction", gControllerBindings.invertBrake));
                    { FormField d; d.type = FormField::Type::eSlider; d.id = "deadzone"; d.label = "Axis deadzone"; d.value = static_cast<float>(gControllerBindings.deadzone); d.min = 0.0f; d.max = 24000.0f; d.step = 500.0f; f.push_back(d); }
                }
                f.push_back(button("defaults", "Reset all control defaults", true));
                if (!status.empty()) f.push_back(note(status));
                f.push_back(note("Remaps, axis direction and deadzone changes apply immediately."));
                form->SetFields(f);
                form->SetCapture(listeningForBinding >= 0, listeningForControllerBinding >= 0);
            };
            form->SetLayout(false, true);
            form->SetFooter("Back", "");
            form->SetTabs(tabNames, tab);
            show();
            int frame = 0;
            const int limit = std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit;
            bool running = true;
            while (running && (limit < 0 || frame < limit)) {
                ++frame;
                std::vector<HoverNetRml::FormEvent> events;
                const HoverNetRml::FormOutcome outcome = form->Frame(events, frame, limit);
                bool refresh = false;
                for (const HoverNetRml::FormEvent& e : events) {
                    const std::string& id = e.id;
                    if (id == "tab") { tab = e.index; listeningForBinding = listeningForControllerBinding = -1; form->SetTabs(tabNames, tab); refresh = true; }
                    else if (id.compare(0, 3, "key") == 0 && id.size() == 4 && id != "keys") { listeningForBinding = id[3] - '0'; listeningForControllerBinding = -1; refresh = true; }
                    else if (id.compare(0, 3, "pad") == 0 && id.size() == 4) { listeningForControllerBinding = id[3] - '0'; listeningForBinding = -1; refresh = true; }
                    else if (id == "capturecancel") { listeningForBinding = listeningForControllerBinding = -1; refresh = true; }
                    else if (id == "capturekey" && listeningForBinding >= 0) {
                        const SDL_Scancode scancode = static_cast<SDL_Scancode>(e.index);
                        if (IsValidBinding(scancode)) {
                            const int target = listeningForBinding;
                            const SDL_Scancode previous = *remappableBindings[target];
                            for (int index = 0; index < 7; ++index) if (index != target && *remappableBindings[index] == scancode) *remappableBindings[index] = previous;   // a taken key swaps
                            *remappableBindings[target] = scancode;
                            SaveKeyboardBindings(gKeyboardBindings);
                            status = std::string(keyLabels[target]) + " is now " + SDL_GetScancodeName(scancode) + ".";
                        }
                        else status = "That key cannot be used.";
                        listeningForBinding = -1; refresh = true;
                    }
                    else if (id == "capturebutton" && listeningForControllerBinding >= 0) {
                        const SDL_GameControllerButton requested = static_cast<SDL_GameControllerButton>(e.index);
                        if (IsValidControllerBinding(requested)) {
                            const int target = listeningForControllerBinding;
                            const SDL_GameControllerButton previous = *remappableControllerBindings[target];
                            for (int index = 0; index < 3; ++index) if (index != target && *remappableControllerBindings[index] == requested) *remappableControllerBindings[index] = previous;
                            *remappableControllerBindings[target] = requested;
                            SaveControllerBindings(gControllerBindings);
                            status = std::string(padLabels[target]) + " is now " + SDL_GameControllerGetStringForButton(requested) + ".";
                        }
                        else status = "That button cannot be used.";
                        listeningForControllerBinding = -1; refresh = true;
                    }
                    else if (id == "steeraxis") { gControllerBindings.steerAxis = static_cast<SDL_GameControllerAxis>(e.index); SaveControllerBindings(gControllerBindings); }
                    else if (id == "accelaxis") { gControllerBindings.accelerateAxis = static_cast<SDL_GameControllerAxis>(e.index); SaveControllerBindings(gControllerBindings); }
                    else if (id == "brakeaxis") { gControllerBindings.brakeAxis = static_cast<SDL_GameControllerAxis>(e.index); SaveControllerBindings(gControllerBindings); }
                    else if (id == "invertsteer") { gControllerBindings.invertSteering = e.on; SaveControllerBindings(gControllerBindings); }
                    else if (id == "invertaccel") { gControllerBindings.invertAccelerate = e.on; SaveControllerBindings(gControllerBindings); }
                    else if (id == "invertbrake") { gControllerBindings.invertBrake = e.on; SaveControllerBindings(gControllerBindings); }
                    else if (id == "deadzone") { gControllerBindings.deadzone = static_cast<int>(e.value); SaveControllerBindings(gControllerBindings); }
                    else if (id == "defaults") {
                        gKeyboardBindings = KeyboardBindings{};
                        gControllerBindings = ControllerBindings{};
                        SaveKeyboardBindings(gKeyboardBindings);
                        SaveControllerBindings(gControllerBindings);
                        listeningForBinding = listeningForControllerBinding = -1;
                        status = "Default controls restored.";
                        refresh = true;
                    }
                }
                if (refresh) show();
                if (outcome == HoverNetRml::FormOutcome::eSave || outcome == HoverNetRml::FormOutcome::eCancel) running = false;
                else if (outcome == HoverNetRml::FormOutcome::eClose) { running = false; }
            }
            form->SetCapture(false, false);
            form.reset();
            if (ownsImGuiContext) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); }
            return;
        }
        else form.reset();
    }
#endif
    struct CaptureReset { ~CaptureReset() { MenuGamepadCapture() = false; } } captureReset;
    bool running = true;
    int framesShown = 0;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        MenuGamepadCapture() = listeningForControllerBinding >= 0;   // B can be bound while waiting for a button
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
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
        HoverNetScreenHeader("CONTROLS", phoneHeader);
        ImGui::Spacing();

        const float centerWidth = std::min(700.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        MenuBeginChild("ControlsPanel", ImVec2(centerWidth, -52.0f), true);
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
        controllerAxesChanged |= MenuSliderInt("Axis deadzone", &gControllerBindings.deadzone,
                                                   0, 24000, "%d");
        if (controllerAxesChanged) {
            gControllerBindings.steerAxis = static_cast<SDL_GameControllerAxis>(steerAxis);
            gControllerBindings.accelerateAxis = static_cast<SDL_GameControllerAxis>(accelerateAxis);
            gControllerBindings.brakeAxis = static_cast<SDL_GameControllerAxis>(brakeAxis);
            SaveControllerBindings(gControllerBindings);
        }
        ImGui::Spacing();
        if (HoverNetQuietButton("Reset all control defaults", ImVec2(-FLT_MIN, 34))) {
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
        if (HoverNetQuietButton("Back", ImVec2(-FLT_MIN, 40))) {
            running = false;
        }
        ImGui::End();

        HN_SCREEN_FADE();
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


// A player's craft designs reach the renderer by slot (docs/craft-designer.md); these keep the table in step with the race being driven.
// The craft is drawn by the object library (a separate shared library on desktop, with its own copy of the craft look code), so every change goes to both copies.
void SetRaceCraftDesign(int slot, const Authority::CraftDesign* design)
{
    HoverNetCraft::SetSlotDesign(slot, design);
    if (design != nullptr) {
        const std::vector<std::uint8_t> bytes = Authority::EncodeCraftDesign(Authority::SanitiseCraftDesign(*design));
        MR_DllObjectFactory::SetCraftDesign(slot, bytes.data(), static_cast<int>(bytes.size()));
    }
    else MR_DllObjectFactory::SetCraftDesign(slot, nullptr, 0);
}

void ClearRaceCraftDesigns()
{
    HoverNetCraft::ClearSlotDesigns();
    MR_DllObjectFactory::SetCraftDesign(-1, nullptr, 0);
}

void ApplyRelayedCraftDesign(int slot, const std::vector<std::uint8_t>& bytes)
{
    Authority::CraftDesign design;
    if (slot >= 0 && slot < 16 && Authority::DecodeCraftDesign(bytes.data(), bytes.size(), design)) SetRaceCraftDesign(slot, &design);
}


// What the tutorial calls the controls: the player's own keys, with the controller's buttons beside them.
HoverNetTutorial::Labels TutorialLabels()
{
    HoverNetTutorial::Labels labels;
    auto key = [](SDL_Scancode code) { const char* name = SDL_GetScancodeName(code); return std::string(name != nullptr && name[0] != 0 ? name : "?"); };
    auto button = [](SDL_GameControllerButton b) { const char* name = SDL_GameControllerGetStringForButton(b); return std::string(name != nullptr ? name : "a button"); };
    if (HoverNetIsTouchDevice()) {
        labels.accelerate = "the gas button"; labels.brake = "the brake button"; labels.left = "the left side of the stick"; labels.right = "the right side of the stick";
        labels.jump = "JUMP"; labels.fire = "FIRE"; labels.skip = "SKIP"; labels.pause = "MENU"; labels.results = "MENU";
        return labels;
    }
    labels.accelerate = key(gKeyboardBindings.accelerate) + " (or right trigger)";
    labels.brake = key(gKeyboardBindings.brake) + " (or left trigger)";
    labels.left = key(gKeyboardBindings.steerLeft); labels.right = key(gKeyboardBindings.steerRight);
    labels.jump = key(gKeyboardBindings.jump) + " (or " + button(gControllerBindings.jump) + ")";
    labels.fire = key(gKeyboardBindings.fire) + " (or " + button(gControllerBindings.fire) + ")";
    return labels;
}


// ---- The tutorial's lesson card: a real card in the menu typefaces, drawn over the race (not in the bitmap HUD font) -----------------------------------------
struct TutorialCard
{
    bool visible = false, flash = false;
    int number = 0, total = 0;
    std::string heading, text, hint;
    float progress = -1.0f;
};
TutorialCard gTutorialCard;
bool gTutorialOverlayOwnsContext = false;
// Set while a finished race waits for the player to open the results screen: the overlay then says so, in big letters, until they do.
std::string gReplayLabel;            // who is being watched and how far through (replays and spectating), drawn by the overlay
std::string gChatPrompt;            // the chat line being typed, drawn by the overlay (empty when not typing)
bool gHudTextInOverlay = false;      // the HUD's words (and the chat line) are drawn by the overlay, not in the bitmap font
int gCountdownNumber = 0;             // 3, 2, 1 while the start is counted down (0 = none)
Uint32 gCountdownTickAt = 0;          // when that number appeared
Uint32 gGoShownAt = 0;                // when GO appeared (0 = not showing)
bool gFinishPromptVisible = false;
bool gFinishPromptTouch = false;

void ShutdownTutorialOverlay()
{
    gTutorialCard.visible = false;
    if (gTutorialOverlayOwnsContext && ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    gTutorialOverlayOwnsContext = false;
}

// ---- The race HUD's words, in the menu typefaces ----------------------------------------------------------------------------------------------------------
// The bars, meters and map are still drawn in the picture by the game; the words (race clock, laps, status, callouts, chat) come from MR_Observer::GetHudText
// and are drawn here as clean text on soft panels. HOVERNET_CLASSIC_HUD_TEXT=1 brings back the bitmap font.
namespace
{
// "Race 0:05.39  Lap 1/1" -> segments split at runs of two spaces; each segment "Label value" -> label before the first token that starts with a digit.
struct HudSegment { std::string label, value; };

std::vector<HudSegment> SplitHudLine(const char* line)
{
    std::vector<HudSegment> segments;
    const std::string text(line);
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && text[at] == ' ') ++at;
        std::size_t end = text.find("  ", at);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(at, end - at);
        at = end;
        if (part.empty()) continue;
        HudSegment segment;
        std::size_t digit = std::string::npos;
        for (std::size_t i = 0; i < part.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(part[i])) && (i == 0 || part[i - 1] == ' ')) { digit = i; break; }
        }
        if (digit == std::string::npos || digit == 0) { segment.value = part; }
        else {
            segment.label = part.substr(0, digit);
            while (!segment.label.empty() && segment.label.back() == ' ') segment.label.pop_back();
            segment.value = part.substr(digit);
        }
        for (char& c : segment.label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        segments.push_back(segment);
    }
    return segments;
}

float DrawHudSegments(ImDrawList* draw, const std::vector<HudSegment>& segments, ImVec2 origin, float scale, bool measureOnly, ImFont* valueFont, ImFont* labelFont)
{
    const float gap = 22.0f * scale;
    float x = origin.x;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const HudSegment& seg = segments[i];
        if (!seg.label.empty()) {
            const float labelSize = 15.0f * scale;
            const ImVec2 size = labelFont->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, seg.label.c_str());
            if (!measureOnly) draw->AddText(labelFont, labelSize, ImVec2(x, origin.y + 9.0f * scale), IM_COL32(250, 125, 132, 255), seg.label.c_str());
            x += size.x + 7.0f * scale;
        }
        const float valueSize = 28.0f * scale;
        const ImVec2 size = valueFont->CalcTextSizeA(valueSize, FLT_MAX, 0.0f, seg.value.c_str());
        if (!measureOnly) {
            draw->AddText(valueFont, valueSize, ImVec2(x + 1.0f, origin.y + 1.0f), IM_COL32(0, 0, 0, 150), seg.value.c_str());
            draw->AddText(valueFont, valueSize, ImVec2(x, origin.y), IM_COL32(255, 255, 255, 255), seg.value.c_str());
        }
        x += size.x;
        if (i + 1 < segments.size()) x += gap;
    }
    return x - origin.x;
}

// A rounded panel with the segments centred in it; returns its bottom edge.
float DrawHudPanel(ImDrawList* draw, const std::vector<HudSegment>& segments, float centreX, float top, float scale, ImFont* valueFont, ImFont* labelFont)
{
    if (segments.empty()) return top;
    const float width = DrawHudSegments(draw, segments, ImVec2(0, 0), scale, true, valueFont, labelFont);
    const float padX = 20.0f * scale, height = 44.0f * scale;
    const ImVec2 lo(centreX - width * 0.5f - padX, top), hi(centreX + width * 0.5f + padX, top + height);
    draw->AddRectFilled(lo, hi, IM_COL32(8, 10, 16, 150), 12.0f * scale);
    draw->AddRect(lo, hi, IM_COL32(255, 255, 255, 28), 12.0f * scale);
    DrawHudSegments(draw, segments, ImVec2(centreX - width * 0.5f, top + 4.0f * scale), scale, false, valueFont, labelFont);
    return hi.y;
}

void DrawRaceHud(const HudText& hud, const ImVec2& origin, const ImVec2& display)
{
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    ImFont* display_font = gHoverNetDisplayFont != nullptr ? gHoverNetDisplayFont : body;
    const float scale = std::max(0.7f, std::min(1.5f, display.y / 768.0f));
    const float centreX = origin.x + display.x * 0.5f;

    // top: the race clock and lap, then a row of status chips under it
    float y = origin.y + display.y * 0.095f;
    y = DrawHudPanel(draw, SplitHudLine(hud.mainLine), centreX, y, scale, display_font, body);
    {
        struct Chip { std::string text; ImU32 colour; float pop; };
        std::vector<Chip> chips;
        const ImU32 normal = IM_COL32(235, 238, 245, 255), good = IM_COL32(130, 225, 150, 255), warn = IM_COL32(255, 196, 90, 255), bad = IM_COL32(255, 110, 110, 255);
        // Live numbers: the speed eases towards its value instead of jumping, and a change of place makes the position chip pop and flash (gold for a place gained, red for one lost).
        static float shownSpeed = 0.0f;
        static Uint32 lastHudTick = 0;
        static int lastPlace = 0;
        static Uint32 placeChangedAt = 0;
        static bool placeGained = false;
        const Uint32 nowTick = SDL_GetTicks();
        const float dt = lastHudTick == 0 ? 1.0f : std::min(0.25f, (nowTick - lastHudTick) / 1000.0f);
        lastHudTick = nowTick;
        const float target = static_cast<float>(hud.speedPercent);
        shownSpeed += (target - shownSpeed) * std::min(1.0f, dt * 9.0f);
        if (std::fabs(target - shownSpeed) < 0.5f) shownSpeed = target;
        int place = 0;
        if (hud.position[0] != 0) std::sscanf(hud.position, "Pos %d", &place);
        if (place > 0 && lastPlace > 0 && place != lastPlace) { placeChangedAt = nowTick; placeGained = place < lastPlace; }
        if (place > 0) lastPlace = place;
        const float placeAge = static_cast<float>(nowTick - placeChangedAt);
        const bool placeFlash = placeChangedAt != 0 && placeAge < 1400.0f;
        if (hud.position[0] != 0) chips.push_back({hud.position, placeFlash ? (placeGained ? IM_COL32(255, 205, 70, 255) : bad) : normal, placeFlash ? std::max(0.0f, 1.0f - placeAge / 500.0f) : 0.0f});
        char speed[24]; std::snprintf(speed, sizeof speed, "Speed %d%%", static_cast<int>(shownSpeed + 0.5f));
        chips.push_back({speed, normal, 0.0f});
        if (hud.fuel[0] != 0) chips.push_back({hud.fuel, hud.fuelPercent < 15 ? bad : hud.fuelPercent < 30 ? warn : normal, 0.0f});
        if (hud.weapon[0] != 0) chips.push_back({hud.weapon, std::strstr(hud.weapon, "ready") != nullptr ? good : std::strstr(hud.weapon, "charging") != nullptr ? warn : normal, 0.0f});
        std::string net = hud.network;
        if (hud.ghost[0] != 0) net += std::string("  Ghost ") + hud.ghost;
        if (!net.empty() && net != "Local") chips.push_back({net, std::strstr(hud.network, "lost") != nullptr ? bad : normal, 0.0f});
        const float size = 17.0f * scale, gap = 18.0f * scale;
        float total = 0;
        for (const Chip& c : chips) total += body->CalcTextSizeA(size, FLT_MAX, 0.0f, c.text.c_str()).x + gap;
        total -= gap;
        float x = centreX - total * 0.5f;
        const float rowTop = y + 7.0f * scale;
        draw->AddRectFilled(ImVec2(x - 14.0f * scale, rowTop - 3.0f * scale), ImVec2(x + total + 14.0f * scale, rowTop + size + 5.0f * scale), IM_COL32(8, 10, 16, 120), 9.0f * scale);
        for (const Chip& c : chips) {
            const float chipSize = size * (1.0f + 0.3f * c.pop);   // a chip that has just changed grows, then settles
            const float lift = (chipSize - size) * 0.5f;
            draw->AddText(body, chipSize, ImVec2(x + 1.0f, rowTop + 1.0f - lift), IM_COL32(0, 0, 0, 140), c.text.c_str());
            draw->AddText(body, chipSize, ImVec2(x, rowTop - lift), c.colour, c.text.c_str());
            x += body->CalcTextSizeA(size, FLT_MAX, 0.0f, c.text.c_str()).x + gap;
        }
    }
    // a callout (impact, pickup) in the middle of the upper picture
    if (hud.callout[0] != 0) {
        const float size = 30.0f * scale;
        const ImVec2 box = display_font->CalcTextSizeA(size, FLT_MAX, 0.0f, hud.callout);
        const ImVec2 at(centreX - box.x * 0.5f, origin.y + display.y * 0.30f);
        draw->AddRectFilled(ImVec2(at.x - 18.0f * scale, at.y - 6.0f * scale), ImVec2(at.x + box.x + 18.0f * scale, at.y + box.y + 6.0f * scale), IM_COL32(8, 10, 16, 170), 10.0f * scale);
        draw->AddText(display_font, size, at, IM_COL32(255, 205, 110, 255), hud.callout);
    }
    // bottom: the lap line
    if (hud.lapLine[0] != 0) {
        const std::vector<HudSegment> lap = SplitHudLine(hud.lapLine);
        const float panelHeight = 44.0f * scale;
        DrawHudPanel(draw, lap, centreX, origin.y + display.y - panelHeight - display.y * 0.025f, scale, display_font, body);
    }
    // the track that has just started, credited for a few seconds (bottom right)
    if (!gMusicState.toast.empty() && SDL_GetTicks() - gMusicState.toastAt < 6000) {
        const float age = static_cast<float>(SDL_GetTicks() - gMusicState.toastAt);
        const float alpha = age < 400.0f ? age / 400.0f : age > 5200.0f ? std::max(0.0f, (6000.0f - age) / 800.0f) : 1.0f;
        const std::string text = "Now playing: " + gMusicState.toast;
        const float size = 17.0f * scale;
        const ImVec2 box = body->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        const ImVec2 hi(origin.x + display.x - 16.0f * scale, origin.y + display.y - display.y * 0.17f);
        const ImVec2 lo(hi.x - box.x - 26.0f * scale, hi.y - box.y - 14.0f * scale);
        draw->AddRectFilled(lo, hi, IM_COL32(8, 10, 16, static_cast<int>(150 * alpha)), 9.0f * scale);
        draw->AddText(body, size, ImVec2(lo.x + 13.0f * scale, lo.y + 7.0f * scale), IM_COL32(235, 238, 245, static_cast<int>(255 * alpha)), text.c_str());
    }
    // chat and messages, bottom left, newest at the bottom
    {
        const float size = 18.0f * scale;
        const float left = origin.x + display.x * 0.02f;
        float y2 = origin.y + display.y - display.y * 0.20f;
        auto line = [&](const std::string& text, ImU32 colour) {
            // y2 is the bottom of the next line: each line (which may wrap to several) is drawn above the one before it, so none overlap.
            const ImVec2 box = body->CalcTextSizeA(size, FLT_MAX, display.x * 0.5f, text.c_str());
            const float top = y2 - box.y - 6.0f;
            draw->AddRectFilled(ImVec2(left - 8.0f, top), ImVec2(left + box.x + 8.0f, y2), IM_COL32(8, 10, 16, 150), 7.0f);
            draw->AddText(body, size, ImVec2(left, top + 3.0f), colour, text.c_str(), nullptr, display.x * 0.5f);
            y2 = top - 6.0f;
        };
        if (!hud.typing.empty()) line(hud.typing + "_", IM_COL32(255, 255, 255, 255));
        for (const std::string& message : hud.chat) line(message, IM_COL32(225, 230, 240, 255));
    }
}
}

// The start: 3, 2, 1, GO! as big numbers that pop in and fade (the matching beeps are played by the race loop).
void DrawStartSequence(const ImVec2& origin, const ImVec2& display)
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* big = gHoverNetDisplayFont != nullptr ? gHoverNetDisplayFont : ImGui::GetIO().Fonts->Fonts[0];
    const float scale = std::max(0.7f, std::min(1.6f, display.y / 768.0f));
    const Uint32 now = SDL_GetTicks();
    std::string text;
    float age;
    ImU32 colour;
    if (gGoShownAt != 0) {
        age = static_cast<float>(now - gGoShownAt);
        if (age > 1000.0f) { gGoShownAt = 0; return; }
        text = "GO!"; colour = IM_COL32(130, 235, 150, 255);
    }
    else {
        age = static_cast<float>(now - gCountdownTickAt);
        text = std::to_string(gCountdownNumber); colour = IM_COL32(255, 255, 255, 255);
    }
    const float pop = std::min(1.0f, age / 260.0f);                       // grows in over a quarter of a second
    const float size = (150.0f + 90.0f * (1.0f - pop)) * scale * (gGoShownAt != 0 ? 1.15f : 1.0f);
    const float fade = std::max(0.0f, std::min(1.0f, (1000.0f - age) / 300.0f));
    const ImVec2 box = big->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const ImVec2 at(origin.x + (display.x - box.x) * 0.5f, origin.y + display.y * 0.30f - box.y * 0.5f);
    draw->AddText(big, size, ImVec2(at.x + 4.0f * scale, at.y + 5.0f * scale), IM_COL32(0, 0, 0, static_cast<int>(130 * fade)), text.c_str());
    const ImU32 faded = (colour & 0x00FFFFFF) | (static_cast<ImU32>(255 * fade) << 24);
    draw->AddText(big, size, at, faded, text.c_str());
}

// A banner under the start numbers' place when a lap is completed: the lap just finished, its time, and "BEST LAP" when it beat the lap record of this race.
std::string gLapBannerText;
std::string gLapBannerSub;
bool gLapBannerBest = false;
Uint32 gLapBannerAt = 0;

void DrawLapBanner(const ImVec2& origin, const ImVec2& display)
{
    if (gLapBannerAt == 0) return;
    const float age = static_cast<float>(SDL_GetTicks() - gLapBannerAt);
    if (age > 2600.0f) { gLapBannerAt = 0; return; }
    const float slide = std::min(1.0f, age / 250.0f), fade = std::max(0.0f, std::min(1.0f, (2600.0f - age) / 500.0f));
    const float scale = std::max(0.7f, std::min(1.6f, display.y / 768.0f));
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* big = gHoverNetDisplayFont != nullptr ? gHoverNetDisplayFont : ImGui::GetIO().Fonts->Fonts[0];
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    const float bigSize = 54.0f * scale, subSize = 24.0f * scale;
    const ImVec2 titleBox = big->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, gLapBannerText.c_str());
    const ImVec2 subBox = body->CalcTextSizeA(subSize, FLT_MAX, 0.0f, gLapBannerSub.c_str());
    const float width = std::max(titleBox.x, subBox.x) + 60.0f * scale, height = titleBox.y + subBox.y + 30.0f * scale;
    const float eased = 1.0f - (1.0f - slide) * (1.0f - slide);
    const ImVec2 lo(origin.x + (display.x - width) * 0.5f, origin.y + display.y * 0.22f - 30.0f * scale * (1.0f - eased)), hi(lo.x + width, lo.y + height);
    const int a = static_cast<int>(255 * fade * eased);
    const ImU32 accent = gLapBannerBest ? IM_COL32(255, 205, 70, a) : IM_COL32(90, 170, 255, a);
    draw->AddRectFilled(lo, hi, IM_COL32(8, 10, 16, static_cast<int>(190 * fade * eased)), 12.0f * scale);
    draw->AddRectFilled(lo, ImVec2(lo.x + 6.0f * scale, hi.y), accent, 12.0f * scale, ImDrawFlags_RoundCornersLeft);
    draw->AddText(big, bigSize, ImVec2(lo.x + (width - titleBox.x) * 0.5f, lo.y + 8.0f * scale), IM_COL32(245, 247, 252, a), gLapBannerText.c_str());
    draw->AddText(body, subSize, ImVec2(lo.x + (width - subBox.x) * 0.5f, lo.y + 14.0f * scale + titleBox.y), accent, gLapBannerSub.c_str());
}

// A flash round the edges of the picture when something happens to the craft: red for an impact, green for fuel, gold for a pickup. The picture itself does not move.
void DrawImpactFlash(const HudText& hud, const ImVec2& origin, const ImVec2& display)
{
    static std::string lastCallout;
    static Uint32 flashAt = 0;
    static ImU32 flashColour = 0;
    if (hud.callout[0] != 0 && lastCallout != hud.callout) {
        flashAt = SDL_GetTicks();
        const std::string text = hud.callout;
        flashColour = text.find("IMPACT") != std::string::npos ? IM_COL32(235, 40, 50, 0) : text.find("FUEL") != std::string::npos ? IM_COL32(60, 220, 110, 0) : IM_COL32(255, 200, 70, 0);
    }
    lastCallout = hud.callout;
    if (flashAt == 0) return;
    const float age = static_cast<float>(SDL_GetTicks() - flashAt);
    if (age > 600.0f) { flashAt = 0; return; }
    const float strength = (1.0f - age / 600.0f) * 0.55f;
    const ImU32 solid = (flashColour & 0x00FFFFFF) | (static_cast<ImU32>(255 * strength) << 24);
    const ImU32 clear = flashColour & 0x00FFFFFF;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float band = std::min(display.x, display.y) * 0.18f;
    const ImVec2 a = origin, b(origin.x + display.x, origin.y + display.y);
    draw->AddRectFilledMultiColor(a, ImVec2(a.x + band, b.y), solid, clear, clear, solid);                       // left
    draw->AddRectFilledMultiColor(ImVec2(b.x - band, a.y), b, clear, solid, solid, clear);                       // right
    draw->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + band), solid, solid, clear, clear);                       // top
    draw->AddRectFilledMultiColor(ImVec2(a.x, b.y - band), b, clear, clear, solid, solid);                       // bottom
}

void DrawReplayLabel(const ImVec2& origin, const ImVec2& display, const std::string& text)
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    const float scale = std::max(0.7f, std::min(1.5f, display.y / 768.0f));
    const float size = 20.0f * scale;
    const ImVec2 box = body->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const float right = origin.x + display.x - 20.0f * display.x / 640.0f;
    const float bottom = origin.y + display.y - 44.0f * display.y / 480.0f - 14.0f * display.y / 480.0f;
    const ImVec2 lo(right - box.x - 24.0f * scale, bottom - box.y - 14.0f * scale), hi(right, bottom);
    draw->AddRectFilled(lo, hi, IM_COL32(8, 10, 16, 190), 10.0f * scale);
    draw->AddText(body, size, ImVec2(lo.x + 12.0f * scale, lo.y + 7.0f * scale), IM_COL32(240, 242, 248, 255), text.c_str());
}

void DrawChatPrompt(const ImVec2& origin, const ImVec2& display, const std::string& typed)
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    const float scale = std::max(0.7f, std::min(1.5f, display.y / 768.0f));
    const float size = 22.0f * scale;
    const std::string line = typed + "_";
    const float labelW = body->CalcTextSizeA(size * 0.75f, FLT_MAX, 0.0f, "CHAT").x;
    const float width = std::min(display.x * 0.8f, std::max(display.x * 0.28f, body->CalcTextSizeA(size, FLT_MAX, 0.0f, line.c_str()).x + labelW + 60.0f * scale));
    const ImVec2 lo(origin.x + display.x * 0.02f, origin.y + display.y - display.y * 0.075f - 40.0f * scale), hi(lo.x + width, lo.y + 40.0f * scale);
    draw->AddRectFilled(lo, hi, IM_COL32(8, 10, 16, 225), 12.0f * scale);
    draw->AddRect(lo, hi, IM_COL32(250, 115, 122, 200), 12.0f * scale, 0, 1.5f);
    draw->AddText(body, size * 0.75f, ImVec2(lo.x + 14.0f * scale, lo.y + 11.0f * scale), IM_COL32(250, 125, 132, 255), "CHAT");
    draw->AddText(body, size, ImVec2(lo.x + 28.0f * scale + labelW, lo.y + 7.0f * scale), IM_COL32(255, 255, 255, 255), line.c_str(), nullptr, width - labelW - 40.0f * scale);
}

void DrawFinishPrompt(const ImVec2& origin, const ImVec2& display, bool touch)
{
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    ImFont* big = gHoverNetDisplayFont != nullptr ? gHoverNetDisplayFont : body;
    const float scale = std::max(0.7f, std::min(1.5f, display.y / 768.0f));
    const char* title = "RACE COMPLETE";
    const char* before = touch ? "Tap " : "Press ";
    const char* key = touch ? "MENU" : "ENTER";
    const char* after = touch ? " for your results" : " for your results";
    const char* note = touch ? "" : "Esc opens the pause menu, not your results.";
    const float titleSize = 38.0f * scale, lineSize = 24.0f * scale, noteSize = 16.0f * scale;
    const ImVec2 titleBox = big->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
    const float beforeW = body->CalcTextSizeA(lineSize, FLT_MAX, 0.0f, before).x, keyW = big->CalcTextSizeA(lineSize, FLT_MAX, 0.0f, key).x, afterW = body->CalcTextSizeA(lineSize, FLT_MAX, 0.0f, after).x;
    const float keyPad = 12.0f * scale;
    const float lineW = beforeW + keyW + keyPad * 2.0f + afterW;
    const float noteW = body->CalcTextSizeA(noteSize, FLT_MAX, 0.0f, note).x;
    const float width = std::max(std::max(titleBox.x, lineW), noteW) + 56.0f * scale;
    const float height = titleBox.y + lineSize + 30.0f * scale + (note[0] != 0 ? noteSize + 14.0f * scale : 0.0f) + 26.0f * scale;
    const float cx = origin.x + display.x * 0.5f;
    const float top = origin.y + display.y * 0.30f;
    draw->AddRectFilled(ImVec2(cx - width * 0.5f, top), ImVec2(cx + width * 0.5f, top + height), IM_COL32(8, 10, 16, 215), 16.0f * scale);
    draw->AddRect(ImVec2(cx - width * 0.5f, top), ImVec2(cx + width * 0.5f, top + height), IM_COL32(120, 215, 140, 170), 16.0f * scale, 0, 2.0f);
    float y = top + 14.0f * scale;
    draw->AddText(big, titleSize, ImVec2(cx - titleBox.x * 0.5f, y), IM_COL32(130, 230, 155, 255), title);
    y += titleBox.y + 10.0f * scale;
    float x = cx - lineW * 0.5f;
    const float lineH = lineSize;
    draw->AddText(body, lineSize, ImVec2(x, y + 2.0f * scale), IM_COL32(240, 242, 248, 255), before);
    x += beforeW;
    // the key, drawn as a key cap
    draw->AddRectFilled(ImVec2(x, y - 3.0f * scale), ImVec2(x + keyW + keyPad * 2.0f, y + lineH + 5.0f * scale), IM_COL32(250, 115, 122, 255), 8.0f * scale);
    draw->AddText(big, lineSize, ImVec2(x + keyPad, y + 1.0f * scale), IM_COL32(20, 12, 16, 255), key);
    x += keyW + keyPad * 2.0f;
    draw->AddText(body, lineSize, ImVec2(x, y + 2.0f * scale), IM_COL32(240, 242, 248, 255), after);
    y += lineH + 18.0f * scale;
    if (note[0] != 0) draw->AddText(body, noteSize, ImVec2(cx - noteW * 0.5f, y), IM_COL32(160, 164, 178, 255), note);
}

void DrawGameOverlay(SDL2GraphicsBackend& graphics, SDL_Renderer* renderer, const HudText* hud)
{
    const bool hudVisible = hud != nullptr && hud->valid;
    if (!gTutorialCard.visible && !hudVisible && !gFinishPromptVisible && gChatPrompt.empty() && gReplayLabel.empty() && gCountdownNumber == 0 && gGoShownAt == 0) return;
    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        gTutorialOverlayOwnsContext = true;
    }
    else if (!gTutorialOverlayOwnsContext) return;   // a menu has its own context open: nothing to draw over it

    int logicalW = 0, logicalH = 0;
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    // The picture is scaled to fit the window and centred (with bars at the sides if the window is wider): the words go on the picture, not on the window.
    ImVec2 origin(0.0f, 0.0f), display = ImGui::GetIO().DisplaySize;
    if (logicalW > 0 && logicalH > 0) {
        const float fit = std::min(display.x / static_cast<float>(logicalW), display.y / static_cast<float>(logicalH));
        const ImVec2 size(logicalW * fit, logicalH * fit);
        origin = ImVec2(std::floor((display.x - size.x) * 0.5f), std::floor((display.y - size.y) * 0.5f));
        display = size;
    }
    if (hudVisible) DrawRaceHud(*hud, origin, display);
    if (gCountdownNumber > 0 || gGoShownAt != 0) DrawStartSequence(origin, display);
    if (hudVisible) DrawImpactFlash(*hud, origin, display);
    if (hudVisible) DrawLapBanner(origin, display);
    if (gFinishPromptVisible) DrawFinishPrompt(origin, display, gFinishPromptTouch);
    if (!gChatPrompt.empty()) DrawChatPrompt(origin, display, gChatPrompt);
    if (!gReplayLabel.empty()) DrawReplayLabel(origin, display, gReplayLabel);
    if (gTutorialCard.visible) {
    // Top right, just under the weapon meter.
    const float width = std::max(270.0f, std::min(380.0f, display.x * 0.30f));
    ImGui::SetNextWindowPos(ImVec2(origin.x + display.x * 0.975f, origin.y + display.y * 0.105f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.050f, 0.070f, 0.90f));
    ImGui::Begin("##tutorialCard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const TutorialCard& card = gTutorialCard;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // the lesson number, and a dot for each lesson
    ImGui::TextColored(kHoverNetCoral, "TUTORIAL");
    {
        const float dotRadius = 4.0f, dotGap = 13.0f;
        const float right = windowPos.x + width - 18.0f;
        for (int i = 0; i < card.total; ++i) {
            const ImVec2 centre(right - (card.total - 1 - i) * dotGap - dotRadius, windowPos.y + 14.0f + 10.0f);
            const ImU32 colour = i + 1 < card.number ? IM_COL32(120, 215, 140, 255) : i + 1 == card.number ? IM_COL32(250, 115, 122, 255) : IM_COL32(90, 94, 110, 255);
            draw->AddCircleFilled(centre, dotRadius, colour);
        }
    }
    if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
    ImGui::PushStyleColor(ImGuiCol_Text, card.flash ? ImVec4(0.55f, 0.95f, 0.65f, 1.0f) : kHoverNetWhite);
    ImGui::TextUnformatted(card.flash ? "Done!" : card.heading.c_str());
    ImGui::PopStyleColor();
    if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.91f, 0.95f, 1.0f));
    ImGui::TextUnformatted(card.text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    if (card.progress >= 0.0f) {
        ImGui::Spacing();
        const ImVec2 barStart = ImGui::GetCursorScreenPos();
        const float barWidth = ImGui::GetContentRegionAvail().x;
        draw->AddRectFilled(barStart, ImVec2(barStart.x + barWidth, barStart.y + 8.0f), IM_COL32(48, 51, 66, 255), 4.0f);
        if (card.progress > 0.0f) draw->AddRectFilled(barStart, ImVec2(barStart.x + barWidth * std::min(1.0f, card.progress), barStart.y + 8.0f), IM_COL32(250, 115, 122, 255), 4.0f);
        ImGui::Dummy(ImVec2(barWidth, 10.0f));
    }
    if (!card.hint.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.72f, 1.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(card.hint.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
}


// ---- The loading screen: one frame with the track's name and a tip, shown before a race is loaded (the load itself blocks) --------------------------------
void ShowLoadingScreen(SDL2GraphicsBackend& graphics, const std::string& trackName)
{
    static const char* const tips[] = {
        "Hold the brake before a bend, not in it: a hard turn scrubs off your speed.",
        "Fuel pads refill your tank. Running dry leaves you stranded.",
        "Jump over gaps and obstacles, and watch where you land.",
        "A missile takes a moment to recharge. The HUD says when it is ready.",
        "Design your own craft in the Garage: other players see it in online races.",
        "Your best run on each track is kept as a ghost you can race against.",
        "Press Enter after the finish line for your results and race options.",
        "The Tutorial in the main menu teaches every control in one short lap.",
    };
    if (std::getenv("HOVERNET_NO_LOADING_SCREEN") != nullptr || graphics.GetRenderer() == nullptr) return;
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        LoadHoverNetMenuFonts(io);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }
    else if (!gTutorialOverlayOwnsContext) return;   // a menu has the context: leave it alone
    SDL_Renderer* renderer = graphics.GetRenderer();
    int logicalW = 0, logicalH = 0;
    SDL_RenderGetLogicalSize(renderer, &logicalW, &logicalH);
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(-1)) {   // the RmlUi version of this screen (docs/rmlui.md); if it cannot load, the ImGui one below is drawn
        HoverNetRml::LoadingInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        info.trackName = trackName.empty() ? "THE TRACK" : trackName;
        TrackPreview map;
        const int trackIndex = Tracks().Find(trackName);
        if (trackIndex >= 0) {
            const TrackBriefing guide = BriefingFor(trackIndex);
            info.briefing = guide.mCharacter + "   -   " + guide.mDifficulty + (guide.mFreePlay ? std::string() : "   -   suggested " + std::to_string(guide.mSuggestedLaps) + " laps");
            map = LoadTrackPreview(renderer, info.trackName.c_str());
            info.map = map.mTexture; info.mapW = map.mWidth; info.mapH = map.mHeight;
        }
        const Uint32 ticks = SDL_GetTicks();
        info.tip = tips[(ticks / 1000 + static_cast<Uint32>(info.trackName.size())) % (sizeof(tips) / sizeof(tips[0]))];
        info.spinnerPhase = static_cast<int>((ticks % 1000) / 125);
        info.beforePresent = [&]() { CaptureFrameIfRequested(renderer, 1, 1, "HOVERNET_CAPTURE_LOADING"); };
        const bool shown = HoverNetRml::ShowLoadingFrame(graphics.GetWindow(), renderer, info, logicalW, logicalH);
        if (map.mTexture != nullptr) SDL_DestroyTexture(map.mTexture);
        if (shown) {
            if (ownsImGuiContext) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); }
            return;
        }
    }
#endif
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("##loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(ImVec2(0, 0), display, IM_COL32(10, 11, 18, 255), IM_COL32(26, 10, 18, 255), IM_COL32(8, 8, 13, 255), IM_COL32(10, 6, 10, 255));
    ImFont* big = gHoverNetDisplayFont != nullptr ? gHoverNetDisplayFont : ImGui::GetIO().Fonts->Fonts[0];
    ImFont* body = ImGui::GetIO().Fonts->Fonts[0];
    const float scale = std::max(0.8f, std::min(1.6f, display.y / 768.0f));
    const std::string name = trackName.empty() ? "THE TRACK" : trackName;
    // The track's map, faint and large behind the title (loaded for this one frame and released), and the course's style and difficulty under the name.
    TrackPreview mapPreview;
    {
        const int trackIndex = Tracks().Find(trackName);
        if (trackIndex >= 0) {
            TrackPreview map = LoadTrackPreview(renderer, name.c_str());
            if (map.mTexture != nullptr && map.mWidth > 0 && map.mHeight > 0) {
                const float aspect = static_cast<float>(map.mWidth) / static_cast<float>(map.mHeight);
                float h = display.y * 0.62f, w = h * aspect;
                if (w > display.x * 0.7f) { w = display.x * 0.7f; h = w / aspect; }
                draw->AddImage(reinterpret_cast<ImTextureID>(map.mTexture), ImVec2((display.x - w) * 0.5f, display.y * 0.5f - h * 0.5f), ImVec2((display.x + w) * 0.5f, display.y * 0.5f + h * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 30));
            }
            mapPreview = map;   // destroyed once this frame has been presented
        }
    }
    const float titleSize = 54.0f * scale;
    const ImVec2 titleBox = big->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, name.c_str());
    draw->AddText(big, 20.0f * scale, ImVec2((display.x - big->CalcTextSizeA(20.0f * scale, FLT_MAX, 0.0f, "LOADING").x) * 0.5f, display.y * 0.36f), IM_COL32(250, 125, 132, 255), "LOADING");
    draw->AddText(big, titleSize, ImVec2((display.x - titleBox.x) * 0.5f, display.y * 0.36f + 30.0f * scale), IM_COL32(255, 255, 255, 255), name.c_str());
    {
        const int trackIndex = Tracks().Find(trackName);
        if (trackIndex >= 0) {
            const TrackBriefing guide = BriefingFor(trackIndex);
            const std::string line = guide.mCharacter + "   -   " + guide.mDifficulty + (guide.mFreePlay ? std::string() : "   -   suggested " + std::to_string(guide.mSuggestedLaps) + " laps");
            const ImVec2 lineBox = body->CalcTextSizeA(18.0f * scale, FLT_MAX, 0.0f, line.c_str());
            draw->AddText(body, 18.0f * scale, ImVec2((display.x - lineBox.x) * 0.5f, display.y * 0.36f + 30.0f * scale + titleSize + 8.0f * scale), IM_COL32(250, 125, 132, 255), line.c_str());
        }
    }
    // a spinner of dots
    const Uint32 ticks = SDL_GetTicks();
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * 0.785398f - static_cast<float>(ticks % 1000) / 1000.0f * 6.2831853f;
        const float strength = 0.25f + 0.75f * static_cast<float>(i) / 7.0f;
        draw->AddCircleFilled(ImVec2(display.x * 0.5f + std::cos(angle) * 26.0f * scale, display.y * 0.36f + 150.0f * scale + std::sin(angle) * 26.0f * scale), 4.5f * scale, IM_COL32(250, 115, 122, static_cast<int>(255 * strength)));
    }
    const char* tip = tips[(ticks / 1000 + static_cast<Uint32>(name.size())) % (sizeof(tips) / sizeof(tips[0]))];
    const float tipSize = 19.0f * scale;
    const ImVec2 tipBox = body->CalcTextSizeA(tipSize, display.x * 0.7f, display.x * 0.7f, tip);
    draw->AddText(body, tipSize, ImVec2((display.x - tipBox.x) * 0.5f, display.y * 0.78f), IM_COL32(190, 194, 208, 255), tip, nullptr, display.x * 0.7f);
    draw->AddText(body, tipSize * 0.8f, ImVec2((display.x - body->CalcTextSizeA(tipSize * 0.8f, FLT_MAX, 0.0f, "TIP").x) * 0.5f, display.y * 0.78f - 26.0f * scale), IM_COL32(250, 125, 132, 255), "TIP");
    ImGui::End();
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 10, 11, 18, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    CaptureFrameIfRequested(renderer, 1, 1, "HOVERNET_CAPTURE_LOADING");   // test hook
    SDL_RenderPresent(renderer);
    if (mapPreview.mTexture != nullptr) SDL_DestroyTexture(mapPreview.mTexture);
    SDL_RenderSetLogicalSize(renderer, logicalW, logicalH);
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
}

// ---- The Settings hub: everything that is not racing, behind one button on the main menu ---------------------------------------------------------------
enum class HubChoice { eBack, eGame, eControls, eReplays, eTutorial };

HubChoice RunSettingsHub(SDL2GraphicsBackend& graphics, int pFrameLimit)
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
    const bool tutorialDone = HoverNetTutorial::HasCompleted(ConfigDirPath());
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md); if it cannot load, the ImGui screen below is shown
        HoverNetRml::ChoiceInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        info.title = "SETTINGS";
        info.eyebrow = "HOVERNET";
        info.panelTitle = "SETTINGS";
        info.backdrop = 1;
        info.reducedMotion = LoadReducedMotion();
        info.sound = [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); };
        const HubChoice choices[] = {HubChoice::eGame, HubChoice::eControls, HubChoice::eReplays, HubChoice::eTutorial, HubChoice::eBack};
        const char* labels[] = {"Game settings", "Controls", "Watch a replay", "Tutorial", "Back"};
        const char* details[] = {"Your name, server, sound, display, text size and graphics.", "Keyboard and controller buttons.", "Races saved on this device.",
                                 tutorialDone ? "A hands-on practice race: run it again any time." : "A hands-on practice race. New here? Start with this.", ""};
        for (int i = 0; i < 5; ++i) { HoverNetRml::ChoiceOption option; option.label = labels[i]; option.detail = details[i]; option.primary = i == 0; info.options.push_back(option); }
        const int picked = HoverNetRml::RunChoiceMenu(graphics.GetWindow(), graphics.GetRenderer(), info, std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit, kWidth, kHeight);
        auto shutdownImGui = [&]() { if (ownsImGuiContext) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); } };
        if (picked >= 0 && picked < 5) { shutdownImGui(); return choices[picked]; }
        if (picked == HoverNetRml::kChoiceBack) { shutdownImGui(); return HubChoice::eBack; }
        if (picked == HoverNetRml::kChoiceClose) { g_QuitConfirmed = true; shutdownImGui(); return HubChoice::eBack; }
        if (picked == HoverNetRml::kChoiceNone && pFrameLimit >= 0) { shutdownImGui(); return HubChoice::eBack; }
        // otherwise (could not load): the ImGui screen below
    }
#endif
    HubChoice result = HubChoice::eBack;
    bool running = true, focusFirst = true;
    int framesShown = 0;
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
            if (event.type == SDL_QUIT) { g_QuitConfirmed = true; running = false; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) running = false;
        }
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Settings Hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawHoverNetScreenBackdrop(mainViewport);
        HoverNetScreenHeader("SETTINGS", mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f);
        ImGui::Spacing();
        const float width = std::min(620.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
        MenuBeginChild("HubPanel", ImVec2(width, -52.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        struct Row { const char* label; const char* detail; HubChoice choice; };
        const Row rows[] = {
            {"Game settings", "Your name, server, sound, display, text size and graphics.", HubChoice::eGame},
            {"Controls", "Keyboard and controller buttons.", HubChoice::eControls},
            {"Watch a replay", "Races saved on this device.", HubChoice::eReplays},
            {"Tutorial", tutorialDone ? "A hands-on practice race: run it again any time." : "A hands-on practice race. New here? Start with this.", HubChoice::eTutorial},
        };
        const int count = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
        const float room = ImGui::GetContentRegionAvail().y;
        const float rowHeight = std::max(48.0f, std::min(86.0f, room / count - ImGui::GetStyle().ItemSpacing.y));
        if (focusFirst) { ImGui::SetKeyboardFocusHere(); focusFirst = false; }
        for (int i = 0; i < count; ++i) {
            char label[160];
            std::snprintf(label, sizeof label, "%s##hub%d", rows[i].label, i);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (HoverNetButton(label, ImVec2(-FLT_MIN, rowHeight))) { result = rows[i].choice; running = false; }
            if (rowHeight >= 60.0f) {   // the detail sits under the label, inside the button
                ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 16.0f, at.y + rowHeight - ImGui::GetTextLineHeight() - 6.0f), IM_COL32(215, 215, 224, 215), rows[i].detail);
            }
            else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rows[i].detail);
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (HoverNetQuietButton("Back", ImVec2(-FLT_MIN, 40))) running = false;
        ImGui::End();
        HN_SCREEN_FADE();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit, "HOVERNET_CAPTURE_HUB");
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }
    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
    return result;
}


// ---- The Garage: design your own craft (docs/craft-designer.md) -------------------------------------------------------------------------------------------
// Shape, colours and a painted livery for the hovercraft, kept on this device (and, when signed in, on the account). A look only: the hull still decides how
// the craft drives, and nothing here is read by the simulation. Other players see the design through the race server.
void RunGarageScreen(SDL2GraphicsBackend& graphics, MR_ResourceLib& resources, int pFrameLimit)
{
    using Authority::CraftDesign;
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

    // The preview is drawn in true colour, so the textures need the palette's true-colour copies (set again, unchanged, by the race when it starts).
    const MR_UInt8* palette768 = graphics.GetRenderPalette();
    std::array<std::uint32_t, 256> paletteWords{};
    if (palette768 != nullptr) {
        for (int i = 0; i < 256; ++i) paletteWords[i] = (static_cast<std::uint32_t>(palette768[i * 3]) << 16) | (static_cast<std::uint32_t>(palette768[i * 3 + 1]) << 8) | palette768[i * 3 + 2];
        MR_ResBitmap::SetColourPalette(palette768, 1.0f, false);
        MR_ResBitmap::SetBakePalette(palette768);
    }

    constexpr int kPreviewW = 480, kPreviewH = 360;
    SDL_Texture* previewTexture = SDL_CreateTexture(graphics.GetRenderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kPreviewW, kPreviewH);

    const HoverNetCraft::CraftStore store(ConfigDirPath());
    CraftDesign design;
    int slot = store.Active();
    if (slot < 0) slot = 0;
    if (!store.Load(slot, design)) design = CraftDesign();
    CraftDesign lastSavedDesign = design;
#ifdef HOVERNET_WEB
    int accountCraftRevision = hovernet_js_account_craft_revision();
#endif
    const char* patternDemo = std::getenv("HOVERNET_GARAGE_PATTERN");   // test hook: a pattern painted into the demo design
    if (std::getenv("HOVERNET_GARAGE_DEMO") != nullptr) {   // test hook: a sample design, to look at the screen
        design.length = 8; design.taper = 10; design.body = {{200, 25, 30}}; design.bodyStrength = 200; design.accent = {{30, 170, 70}}; design.accentStrength = 255; design.name = "Demo";
        if (const char* stampHook = std::getenv("HOVERNET_GARAGE_STAMP")) {   // test hook: a decal stamped near the middle with the same code as a click
            const int cx = 16, cy = 16;
            for (int dy = 0; dy < HoverNetCraft::kDecalSize; ++dy) for (int dx = 0; dx < HoverNetCraft::kDecalSize; ++dx)
                if (HoverNetCraft::DecalCovers(std::atoi(stampHook), dx, dy)) design.paint[static_cast<size_t>(cy + dy - 4) * Authority::kPaintSize + (cx + dx - 4)] = 3;
        }
        if (patternDemo != nullptr) {
            design.paint.fill(0);
            for (int y = 0; y < Authority::kPaintSize; ++y) for (int x = 0; x < Authority::kPaintSize; ++x) if (HoverNetCraft::PatternCovers(std::atoi(patternDemo), x, y)) design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x] = 5;
        }
        for (int y = 0; y < Authority::kPaintSize; ++y) for (int x = 12; x < 16; ++x) design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x] = 5;
    }
    std::vector<std::array<std::uint8_t, Authority::kPaintSize * Authority::kPaintSize>> undo;
    int tool = 0, colour = 3;
    int decal = 0;   // the stamp chosen (tool 4)
    auto decalBase = design.paint, decalResult = design.paint;   // the paint before the last decal BUTTON stamped, and the paint it made: a different decal button replaces that decal (only while nothing was drawn since)
    bool decalPlaced = false;
    // What this player has earned: patterns, colour sets and shape range by level, decals by achievement (HOVERNET_GARAGE_LEVEL=n, HOVERNET_GARAGE_ALL=1: test hooks).
    HoverNetProfile::Profile garageProfile;
    garageProfile.Load(ProfilePath());
    int garageLevel = garageProfile.Level();
    unsigned garageAchievements = garageProfile.achievements;
    if (const char* levelHook = std::getenv("HOVERNET_GARAGE_LEVEL")) garageLevel = std::max(1, std::atoi(levelHook));
    if (std::getenv("HOVERNET_GARAGE_ALL") != nullptr) { garageLevel = 99; garageAchievements = ~0u; }
    const int shapeLimit = garageLevel >= 9 ? Authority::kMaxShapePercent : garageLevel >= 6 ? 20 : garageLevel >= 3 ? 15 : 10;   // how far the length, width and height sliders reach
    const int taperLimit = shapeLimit * Authority::kMaxTaperPercent / Authority::kMaxShapePercent;
    bool mirror = false, spin = true;
    float yaw = 25.0f;
    std::string status;
    std::vector<std::uint32_t> rgba;
    std::uint64_t shownHash = 0;
    int shownYaw = -1000, shownCraft = -1;
    Uint32 lastBuild = 0;
    bool previewOk = false;
    Uint32 lastTick = SDL_GetTicks();

    auto floodFill = [&](int sx, int sy, std::uint8_t to) {
        auto& p = design.paint;
        const std::uint8_t from = p[static_cast<size_t>(sy) * Authority::kPaintSize + sx];
        if (from == to) return;
        std::vector<std::pair<int, int>> stack{{sx, sy}};
        while (!stack.empty()) {
            const auto point = stack.back(); stack.pop_back();
            const int x = point.first, y = point.second;
            if (x < 0 || y < 0 || x >= Authority::kPaintSize || y >= Authority::kPaintSize) continue;
            std::uint8_t& cell = p[static_cast<size_t>(y) * Authority::kPaintSize + x];
            if (cell != from) continue;
            cell = to;
            stack.push_back({x + 1, y}); stack.push_back({x - 1, y}); stack.push_back({x, y + 1}); stack.push_back({x, y - 1});
        }
    };
    auto pushUndo = [&]() { undo.push_back(design.paint); if (undo.size() > 30) undo.erase(undo.begin()); };
    auto stampDecal = [&](int centreX, int centreY) {
        pushUndo();
        for (int dy = 0; dy < HoverNetCraft::kDecalSize; ++dy)
            for (int dx = 0; dx < HoverNetCraft::kDecalSize; ++dx) {
                if (!HoverNetCraft::DecalCovers(decal, dx, dy)) continue;
                const int px = centreX + dx - HoverNetCraft::kDecalSize / 2;
                const int py = centreY + dy - HoverNetCraft::kDecalSize / 2;
                for (int pass = 0; pass < (mirror ? 2 : 1); ++pass) {
                    const int qx = pass == 0 ? px : Authority::kPaintSize - 1 - px;
                    if (qx >= 0 && py >= 0 && qx < Authority::kPaintSize && py < Authority::kPaintSize)
                        design.paint[static_cast<size_t>(py) * Authority::kPaintSize + qx] = static_cast<std::uint8_t>(colour);
                }
            }
    };

    auto saveCurrent = [&]() {
        const CraftDesign clean = Authority::SanitiseCraftDesign(design);
        if (!store.Save(slot, clean) || !store.SetActive(slot)) {
            status = "Could not save the craft on this device.";
            return false;
        }
        design = clean;
        lastSavedDesign = clean;
        HoverNetProfile::Profile profile;
        profile.Load(ProfilePath());
        if (profile.Unlock(HoverNetProfile::eGarageDesigner).xp > 0) profile.Save(ProfilePath());
        garageProfile.Load(ProfilePath());
        if (std::getenv("HOVERNET_GARAGE_ALL") == nullptr) {
            garageAchievements = garageProfile.achievements;
            garageLevel = std::max(garageLevel, garageProfile.Level());
        }
        status = "Saved in slot " + std::to_string(slot + 1) + ". Other players see it in online races.";
        return true;
    };

    // Leaving with changes that were never saved asks first, in the game's own dialog (a system message box does not exist in a browser).
    int pendingLeave = 0;   // 0: no question; 1: asked while going back; 2: asked while closing the program
    auto leaveGarage = [&](bool quitting) {
        if (Authority::SanitiseCraftDesign(design) == lastSavedDesign) {
            if (quitting) g_QuitConfirmed = true;
            return true;
        }
        pendingLeave = quitting ? 2 : 1;
        return false;
    };

    bool running = true;
    int framesShown = 0;
    bool strokeActive = false;
    // Test hook HOVERNET_TEST_GARAGE_CLICK_DECAL=a,b,...: real pointer clicks (SDL mouse events, through the interface) on those decals' buttons one after another, then the paint is counted.
    std::vector<int> testDecals;
    if (const char* list = std::getenv("HOVERNET_TEST_GARAGE_CLICK_DECAL")) for (const char* c = list; *c != 0;) { testDecals.push_back(std::atoi(c)); while (*c != 0 && *c != ',') ++c; if (*c == ',') ++c; }
    int testClickX = -1, testClickY = -1;
#ifdef HOVERNET_HAVE_RMLUI
    // The RmlUi version of this screen (docs/rmlui.md): the same design, tools, patterns, decals, slots and saving; the paint canvas is a picture drawn into its element and painted with the
    // pointer as before. Not used by the tests that click the ImGui widgets; if it cannot load, the ImGui screen below runs.
    if (UseRmlUi(pFrameLimit) && testDecals.empty() && std::getenv("HOVERNET_TEST_HOVER") == nullptr) {
        std::unique_ptr<HoverNetRml::FormScreen> rml(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "GARAGE", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight, "garage.rml"));
        if (rml->Ok()) {
            using Escape = HoverNetRml::FormScreen;
            auto E = [](const std::string& text) { return Escape::Escape(text); };
            constexpr int kPaint = Authority::kPaintSize;
            SDL_Texture* paintTexture = SDL_CreateTexture(graphics.GetRenderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kPaint, kPaint);
            if (paintTexture != nullptr) SDL_SetTextureScaleMode(paintTexture, SDL_ScaleModeNearest);
            static const std::uint8_t kSwatches[][3] = {{220, 30, 35}, {240, 120, 20}, {245, 215, 30}, {40, 175, 70}, {30, 185, 190}, {40, 90, 225}, {130, 60, 210}, {235, 70, 170}, {245, 245, 245}, {30, 30, 36}};
            const int swatchCount = static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));
            auto slider = [&](const std::string& id, const std::string& label, int low, int high, int value) {
                return "<div class=\"srow\"><span class=\"slabel\">" + E(label) + "</span><input type=\"range\" class=\"range\" id=\"in_" + id + "\" data-id=\"" + id + "\" min=\"" + std::to_string(low) + "\" max=\"" + std::to_string(high) + "\" step=\"1\" value=\"" + std::to_string(value) +
                       "\"/><span class=\"sval\" id=\"v_" + id + "\"></span></div>";
            };
            auto chip = [&](const std::string& id, int index, const std::string& label, bool selected, bool locked = false) {
                return "<button class=\"chip" + std::string(selected ? " sel" : "") + (locked ? " disabled" : "") + "\" id=\"" + id + std::to_string(index) + "\" data-id=\"" + id + "\" data-index=\"" + std::to_string(index) + "\">" + E(label) + "</button>";
            };
            auto swatch = [&](const std::string& id, int index, int r, int g, int b) {
                return "<div class=\"swatch\" id=\"" + id + std::to_string(index) + "\" data-id=\"" + id + "\" data-index=\"" + std::to_string(index) + "\" style=\"background-color: rgb(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ");\"></div>";
            };
            auto percent = [](int v) { return (v >= 0 ? "+" : "") + std::to_string(v) + "%"; };
            // ---- the left column: hull, shape, colours (built once; the values are set in place)
            {
                std::string html = "<p class=\"heading\">HULL</p><div class=\"btnrow\">";
                for (int c = 0; c < Authority::kCraftCount; ++c) html += chip("hull", c, Authority::CraftName(c), design.craft == c);
                html += "</div><p class=\"note\">Speed, weight and handling come from the craft you pick before the race, never from this design.</p><p class=\"heading\">SHAPE</p>";
                html += slider("len", "Length", 0, 2 * shapeLimit, shapeLimit) + slider("wid", "Width", 0, 2 * shapeLimit, shapeLimit) + slider("hei", "Height", 0, 2 * shapeLimit, shapeLimit) + slider("nose", "Nose", 0, 2 * taperLimit, taperLimit);   // (a slider that runs from -n to n showed its knob wrongly at 0 in RmlUi, so these run from 0 and are offset by n)
                html += "<button class=\"act quiet\" data-id=\"shapereset\">Reset shape</button>";
                if (shapeLimit < Authority::kMaxShapePercent) html += "<p class=\"note\">The sliders reach further as you level up (now +/-" + std::to_string(shapeLimit) + "%, up to +/-" + std::to_string(Authority::kMaxShapePercent) + "% at level 9).</p>";
                html += "<p class=\"heading\">COLOURS</p>";
                for (const char* which : {"b", "t"}) {
                    const bool body = which[0] == 'b';
                    html += std::string("<p class=\"label\">") + (body ? "Body" : "Trim") + "</p><div class=\"swatches\">";
                    for (int i = 0; i < swatchCount; ++i) html += swatch(std::string(which) + "sw", i, kSwatches[i][0], kSwatches[i][1], kSwatches[i][2]);
                    html += "</div>" + slider(std::string(which) + "R", "Red", 0, 255, 0) + slider(std::string(which) + "G", "Green", 0, 255, 0) + slider(std::string(which) + "B", "Blue", 0, 255, 0) + slider(std::string(which) + "A", "Amount", 0, 100, 0);
                }
                rml->SetHtml("controls", html);
            }
            // ---- the right column: tools, patterns, colour sets, decals, palette, my craft (built once; the parts that change are updated in place)
            {
                std::string html = "<p class=\"heading\">PAINT</p><div class=\"btnrow\">";
                const char* tools[] = {"Pencil", "Eraser", "Fill", "Pick"};
                for (int t = 0; t < 4; ++t) html += chip("tool", t, tools[t], tool == t);
                html += "</div><div class=\"btnrow\"><div class=\"toggle\" id=\"mirror\" data-id=\"mirror\"><span class=\"box\"></span><span class=\"tlabel\">Mirror left and right</span></div>";
                html += "<button class=\"chip\" data-id=\"undo\">Undo</button><button class=\"chip\" data-id=\"clearpaint\">Clear paint</button></div>";
                html += "<p class=\"label\">Patterns (replace the paint, in the selected colour)</p><div class=\"btnrow\">";
                for (int p = 0; p < HoverNetCraft::kPatternCount; ++p) { const bool locked = garageLevel < HoverNetCraft::PatternLevel(p); html += chip("pattern", p, locked ? "Level " + std::to_string(HoverNetCraft::PatternLevel(p)) : std::string(HoverNetCraft::PatternName(p)), false, locked); }
                html += "</div><p class=\"label\">Colour sets</p><div class=\"btnrow\">";
                for (int t = 0; t < HoverNetCraft::kThemeCount; ++t) { const bool locked = garageLevel < HoverNetCraft::ThemeLevel(t); html += chip("theme", t, locked ? "Level " + std::to_string(HoverNetCraft::ThemeLevel(t)) : std::string(HoverNetCraft::ThemeName(t)), false, locked); }
                html += "</div><p class=\"label\">Decals (click the canvas to stamp, in the selected colour)</p><div class=\"btnrow\" id=\"decals\"></div><p class=\"note\" id=\"decalhint\"></p>";
                html += "<p class=\"label\">Paint colours</p><div class=\"swatches\" id=\"palette\"></div>" + slider("pR", "Red", 0, 255, 0) + slider("pG", "Green", 0, 255, 0) + slider("pB", "Blue", 0, 255, 0);
                html += "<p class=\"note\">The paint wraps round the hull panels; use Mirror for symmetrical stripes.</p><p class=\"heading\">MY CRAFT</p>";
                html += "<input type=\"text\" class=\"text\" id=\"in_name\" data-id=\"name\" maxlength=\"" + std::to_string(Authority::kMaxDesignNameBytes) + "\" value=\"\"/><div id=\"slots\"></div>";
                html += "<button class=\"act\" data-id=\"save\">Save to this slot and use it in races</button><button class=\"act quiet\" data-id=\"stock\">Race with the standard paint (stock craft)</button><button class=\"act quiet\" data-id=\"delete\">Delete this slot</button>";
#ifdef HOVERNET_WEB
                html += "<p class=\"heading\">OUTIVA ACCOUNT</p>";
                if (gAccountsOffered && !gSignedIn) html += "<p class=\"note\">You are playing as a guest. Sign in with Outiva (reload the page) to keep your crafts on your account.</p>";
                html += "<button class=\"act\" data-id=\"acctsave\">Save local slots to account</button><button class=\"act quiet\" data-id=\"acctload\">Restore account slots (replaces local)</button><p class=\"note\" id=\"acctstatus\"></p>";
#endif
                rml->SetHtml("rightscroll", html);
            }
            std::string shownDecals, shownSlots, shownStatus, shownDialog;
            auto setSlider = [&](const std::string& id, int value, const std::string& text) { rml->SetInput("in_" + id, std::to_string(value)); rml->SetHtml("v_" + id, E(text)); };
            auto refreshColourRows = [&]() {
                for (int w = 0; w < 2; ++w) {
                    const std::string p = w == 0 ? "b" : "t";
                    const std::array<std::uint8_t, 3>& rgb = w == 0 ? design.body : design.accent;
                    const std::uint8_t strength = w == 0 ? design.bodyStrength : design.accentStrength;
                    setSlider(p + "R", rgb[0], std::to_string(rgb[0])); setSlider(p + "G", rgb[1], std::to_string(rgb[1])); setSlider(p + "B", rgb[2], std::to_string(rgb[2]));
                    const int amount = strength * 100 / 255;
                    setSlider(p + "A", amount, amount == 0 ? "off" : std::to_string(amount) + "%");
                    for (int i = 0; i < swatchCount; ++i) rml->SetClass(p + "sw" + std::to_string(i), "sel", strength > 0 && rgb[0] == kSwatches[i][0] && rgb[1] == kSwatches[i][1] && rgb[2] == kSwatches[i][2]);
                }
            };
            auto refreshShape = [&]() {
                setSlider("len", design.length + shapeLimit, percent(design.length)); setSlider("wid", design.width + shapeLimit, percent(design.width)); setSlider("hei", design.height + shapeLimit, percent(design.height)); setSlider("nose", design.taper + taperLimit, percent(design.taper));
                for (int c = 0; c < Authority::kCraftCount; ++c) rml->SetClass("hull" + std::to_string(c), "sel", design.craft == c);
            };
            auto refreshPalette = [&]() {
                std::string html;
                for (int i = 1; i < Authority::kPaintColours; ++i) html += swatch("pcol", i, design.palette[i][0], design.palette[i][1], design.palette[i][2]);
                rml->SetHtml("palette", html);
                for (int i = 1; i < Authority::kPaintColours; ++i) rml->SetClass("pcol" + std::to_string(i), "sel", colour == i);
                setSlider("pR", design.palette[colour][0], std::to_string(design.palette[colour][0])); setSlider("pG", design.palette[colour][1], std::to_string(design.palette[colour][1])); setSlider("pB", design.palette[colour][2], std::to_string(design.palette[colour][2]));
            };
            auto refreshTools = [&]() {
                for (int t = 0; t < 4; ++t) rml->SetClass("tool" + std::to_string(t), "sel", tool == t);
                rml->SetClass("mirror", "on", mirror);
                for (int d = 0; d < HoverNetCraft::kDecalCount; ++d) rml->SetClass("decal" + std::to_string(d), "sel", tool == 4 && decal == d);
            };
            auto refreshSlots = [&]() {
                std::string html;
                const int active = store.Active();
                for (int i = 0; i < HoverNetCraft::kSavedDesigns; ++i) {
                    CraftDesign saved;
                    const bool have = store.Load(i, saved);
                    html += "<button class=\"slot" + std::string(slot == i ? " sel" : "") + "\" data-id=\"slot\" data-index=\"" + std::to_string(i) + "\">" + E(std::to_string(i + 1) + "  " + (have ? (saved.name.empty() ? "(unnamed)" : saved.name) : std::string("(empty)")) + (active == i ? "   - used in races" : "")) + "</button>";
                }
                if (html != shownSlots) { shownSlots = html; rml->SetHtml("slots", html); }
            };
            auto refreshDecals = [&]() {
                std::string html, hint;
                for (int d = 0; d < HoverNetCraft::kDecalCount; ++d) html += chip("decal", d, HoverNetCraft::DecalName(d), tool == 4 && decal == d, !HoverNetCraft::DecalOpen(d, garageAchievements));
                for (int d = 0; d < HoverNetCraft::kDecalCount && hint.empty(); ++d) if (!HoverNetCraft::DecalOpen(d, garageAchievements)) hint = std::string("Next decal: ") + HoverNetCraft::DecalName(d) + " - " + HoverNetCraft::DecalEarnedBy(d);
                if (html + hint != shownDecals) { shownDecals = html + hint; rml->SetHtml("decals", html); rml->SetHtml("decalhint", E(hint)); }
            };
            auto refreshAll = [&]() { refreshShape(); refreshColourRows(); refreshPalette(); refreshTools(); refreshSlots(); refreshDecals(); rml->SetInput("in_name", design.name); };
            refreshAll();
            rml->SetImage("preview", [&](SDL_Rect& crop) -> SDL_Texture* { if (previewTexture == nullptr || !previewOk) return nullptr; crop = SDL_Rect{0, 0, kPreviewW, kPreviewH}; return previewTexture; });
            rml->SetImage("canvas", [&](SDL_Rect& crop) -> SDL_Texture* { crop = SDL_Rect{0, 0, kPaint, kPaint}; return paintTexture; });
            std::vector<std::uint32_t> paintPixels(static_cast<std::size_t>(kPaint) * kPaint);
            int frame = 0;
            const int limit = pFrameLimit;   // (a scripted run, HOVERNET_TEST_RML, keeps its own limit: steps start at frame 60)
            while (running && (limit < 0 || frame < limit)) {
                ++frame;
                if (frame % 20 == 0 && std::getenv("HOVERNET_RML_DEBUG") != nullptr) { std::printf("RMLDEBUG garage frame %d at %u ms, previewOk %d\n", frame, SDL_GetTicks(), previewOk ? 1 : 0); std::fflush(stdout); }
                if (std::getenv("HOVERNET_TEST_GARAGE_ESC") != nullptr && frame == 5) { SDL_Event esc; SDL_zero(esc); esc.type = SDL_KEYDOWN; esc.key.keysym.sym = SDLK_ESCAPE; esc.key.keysym.scancode = SDL_SCANCODE_ESCAPE; esc.key.state = SDL_PRESSED; SDL_PushEvent(&esc); }
                const Uint32 now = SDL_GetTicks();
                if (spin) yaw = std::fmod(yaw + 30.0f * static_cast<float>(now - lastTick) / 1000.0f, 360.0f);
                lastTick = now;
#ifdef HOVERNET_WEB
                const int newAccountCraftRevision = hovernet_js_account_craft_revision();
                if (newAccountCraftRevision != accountCraftRevision) {
                    accountCraftRevision = newAccountCraftRevision;
                    slot = store.Active(); if (slot < 0) slot = 0;
                    if (!store.Load(slot, design)) design = CraftDesign();
                    undo.clear(); status = "Restored account designs to this browser."; shownHash = 0; refreshAll();
                }
#endif
                // the paint canvas: its picture, and the pointer on it
                for (int y = 0; y < kPaint; ++y)
                    for (int x = 0; x < kPaint; ++x) {
                        const std::uint8_t index = design.paint[static_cast<std::size_t>(y) * kPaint + x];
                        std::uint32_t r = ((x + y) & 1) != 0 ? 52 : 66, g = r, b = ((x + y) & 1) != 0 ? 60 : 76;
                        if (index != 0) { r = design.palette[index][0]; g = design.palette[index][1]; b = design.palette[index][2]; }
                        paintPixels[static_cast<std::size_t>(y) * kPaint + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
                    }
                if (paintTexture != nullptr) SDL_UpdateTexture(paintTexture, nullptr, paintPixels.data(), kPaint * 4);
                const bool dialogOpen = pendingLeave != 0;
                {
                    int mx = 0, my = 0;
                    const bool down = (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
                    SDL_FRect box;
                    if (down && !dialogOpen && rml->ElementBox("canvas", box)) {
                        const float side = std::min(box.w, box.h), ox = box.x + (box.w - side) * 0.5f, oy = box.y + (box.h - side) * 0.5f;
                        if (static_cast<float>(mx) >= ox && static_cast<float>(my) >= oy && static_cast<float>(mx) < ox + side && static_cast<float>(my) < oy + side) {
                            const int x = std::max(0, std::min(kPaint - 1, static_cast<int>((static_cast<float>(mx) - ox) / side * kPaint)));
                            const int y = std::max(0, std::min(kPaint - 1, static_cast<int>((static_cast<float>(my) - oy) / side * kPaint)));
                            const bool strokeStart = !strokeActive;
                            if (!strokeActive) { if (tool != 4) pushUndo(); strokeActive = true; }
                            const std::uint8_t value = tool == 1 ? 0 : static_cast<std::uint8_t>(colour);
                            if (tool == 4) { if (strokeStart) stampDecal(x, y); }
                            else if (tool == 3) { const std::uint8_t picked = design.paint[static_cast<std::size_t>(y) * kPaint + x]; if (picked != 0) { colour = picked; refreshPalette(); } tool = 0; refreshTools(); }
                            else if (tool == 2) { floodFill(x, y, value); if (mirror) floodFill(kPaint - 1 - x, y, value); }
                            else { design.paint[static_cast<std::size_t>(y) * kPaint + x] = value; if (mirror) design.paint[static_cast<std::size_t>(y) * kPaint + (kPaint - 1 - x)] = value; }
                        }
                    }
                    if (!down) strokeActive = false;
                }
                std::vector<HoverNetRml::FormEvent> events;
                const HoverNetRml::FormOutcome outcome = rml->Frame(events, frame, limit);
                if (outcome == HoverNetRml::FormOutcome::eClose) { if (leaveGarage(true)) running = false; }
                else if (outcome == HoverNetRml::FormOutcome::eCancel || outcome == HoverNetRml::FormOutcome::eSave) { if (pendingLeave != 0) pendingLeave = 0; else if (leaveGarage(false)) running = false; }
                for (const HoverNetRml::FormEvent& e : events) {
                    const std::string& id = e.id;
                    const int index = e.index;
                    auto clamp = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
                    const int value = static_cast<int>(e.value + (e.value >= 0 ? 0.5f : -0.5f));
                    if (id == "back") { if (leaveGarage(false)) running = false; }
                    else if (id == "spin") { spin = e.on; rml->SetClass("yawrow", "hide", spin); }
                    else if (id == "yaw") yaw = e.value;
                    else if (id == "hull") { design.craft = static_cast<std::uint8_t>(clamp(index, 0, Authority::kCraftCount - 1)); refreshShape(); }
                    else if (id == "len") { design.length = static_cast<std::int8_t>(clamp(value - shapeLimit, -shapeLimit, shapeLimit)); rml->SetHtml("v_len", percent(design.length)); }
                    else if (id == "wid") { design.width = static_cast<std::int8_t>(clamp(value - shapeLimit, -shapeLimit, shapeLimit)); rml->SetHtml("v_wid", percent(design.width)); }
                    else if (id == "hei") { design.height = static_cast<std::int8_t>(clamp(value - shapeLimit, -shapeLimit, shapeLimit)); rml->SetHtml("v_hei", percent(design.height)); }
                    else if (id == "nose") { design.taper = static_cast<std::int8_t>(clamp(value - taperLimit, -taperLimit, taperLimit)); rml->SetHtml("v_nose", percent(design.taper)); }
                    else if (id == "shapereset") { design.length = design.width = design.height = design.taper = 0; refreshShape(); }
                    else if ((id == "bsw" || id == "tsw") && index >= 0 && index < swatchCount) {
                        std::array<std::uint8_t, 3>& rgb = id == "bsw" ? design.body : design.accent; std::uint8_t& strength = id == "bsw" ? design.bodyStrength : design.accentStrength;
                        rgb = {{kSwatches[index][0], kSwatches[index][1], kSwatches[index][2]}}; if (strength == 0) strength = 255; refreshColourRows();
                    }
                    else if (id.size() == 2 && (id[0] == 'b' || id[0] == 't') && (id[1] == 'R' || id[1] == 'G' || id[1] == 'B' || id[1] == 'A')) {
                        std::array<std::uint8_t, 3>& rgb = id[0] == 'b' ? design.body : design.accent; std::uint8_t& strength = id[0] == 'b' ? design.bodyStrength : design.accentStrength;
                        if (id[1] == 'A') strength = static_cast<std::uint8_t>(clamp(value, 0, 100) * 255 / 100);
                        else { rgb[id[1] == 'R' ? 0 : id[1] == 'G' ? 1 : 2] = static_cast<std::uint8_t>(clamp(value, 0, 255)); if (strength == 0) strength = 255; }
                        const int amount = strength * 100 / 255;
                        rml->SetHtml(std::string("v_") + id, id[1] == 'A' ? (amount == 0 ? std::string("off") : std::to_string(amount) + "%") : std::to_string(clamp(value, 0, 255)));
                        for (int i = 0; i < swatchCount; ++i) rml->SetClass(std::string(1, id[0]) + "sw" + std::to_string(i), "sel", strength > 0 && rgb[0] == kSwatches[i][0] && rgb[1] == kSwatches[i][1] && rgb[2] == kSwatches[i][2]);
                    }
                    else if (id == "tool") { tool = clamp(index, 0, 3); refreshTools(); }
                    else if (id == "mirror") mirror = e.on;
                    else if (id == "undo") { if (!undo.empty()) { design.paint = undo.back(); undo.pop_back(); } }
                    else if (id == "clearpaint") { pushUndo(); design.paint.fill(0); }
                    else if (id == "pattern" && index >= 0 && index < HoverNetCraft::kPatternCount && garageLevel >= HoverNetCraft::PatternLevel(index)) {
                        pushUndo(); design.paint.fill(0);
                        const int last = kPaint - 1;
                        for (int y = 0; y < kPaint; ++y) for (int x = 0; x < kPaint; ++x) if (HoverNetCraft::PatternCovers(index, x, y) || (mirror && HoverNetCraft::PatternCovers(index, last - x, y))) design.paint[static_cast<std::size_t>(y) * kPaint + x] = static_cast<std::uint8_t>(colour);
                    }
                    else if (id == "theme" && index >= 0 && index < HoverNetCraft::kThemeCount && garageLevel >= HoverNetCraft::ThemeLevel(index)) {
                        for (int i = 1; i < Authority::kPaintColours; ++i) { const std::uint8_t* c = HoverNetCraft::ThemeColour(index, i); design.palette[i] = {{c[0], c[1], c[2]}}; }
                        refreshPalette();
                    }
                    else if (id == "decal" && index >= 0 && index < HoverNetCraft::kDecalCount && HoverNetCraft::DecalOpen(index, garageAchievements)) {
                        tool = 4; decal = index;
                        if (decalPlaced && design.paint == decalResult) { design.paint = decalBase; if (!undo.empty()) undo.pop_back(); }
                        decalBase = design.paint;
                        stampDecal(kPaint / 2, kPaint / 2);
                        decalResult = design.paint; decalPlaced = true;
                        status = std::string(HoverNetCraft::DecalName(index)) + " decal added. Click the canvas to place another.";
                        refreshTools(); refreshDecals();
                    }
                    else if (id == "pcol" && index >= 1 && index < Authority::kPaintColours) { colour = index; if (tool == 1) tool = 0; refreshPalette(); refreshTools(); }
                    else if (id.size() == 2 && id[0] == 'p' && (id[1] == 'R' || id[1] == 'G' || id[1] == 'B')) {
                        design.palette[colour][id[1] == 'R' ? 0 : id[1] == 'G' ? 1 : 2] = static_cast<std::uint8_t>(clamp(value, 0, 255));
                        rml->SetHtml(std::string("v_") + id, std::to_string(clamp(value, 0, 255)));
                    }
                    else if (id == "name") design.name = Authority::SanitiseCraftDesign([&] { CraftDesign d = design; d.name = e.text; return d; }()).name;
                    else if (id == "slot" && index >= 0 && index < HoverNetCraft::kSavedDesigns) {
                        CraftDesign saved;
                        const bool have = store.Load(index, saved);
                        slot = index; design = have ? saved : CraftDesign(); lastSavedDesign = design; undo.clear();
                        status = have ? "Loaded the craft in slot " + std::to_string(index + 1) + "." : "Empty slot " + std::to_string(index + 1) + ".";
                        refreshAll();
                    }
                    else if (id == "save") { if (saveCurrent()) { refreshAll(); } }
                    else if (id == "stock") { store.SetActive(-1); status = "Your races use the stock craft."; refreshSlots(); }
                    else if (id == "delete") { store.Delete(slot); design = CraftDesign(); lastSavedDesign = design; undo.clear(); status = "Deleted."; refreshAll(); }
#ifdef HOVERNET_WEB
                    else if (id == "acctsave") hovernet_js_account_craft_save();
                    else if (id == "acctload") hovernet_js_account_craft_load();
#endif
                    else if (id == "leave_save") { const bool closing = pendingLeave == 2; if (saveCurrent()) { if (closing) g_QuitConfirmed = true; running = false; pendingLeave = 0; } }
                    else if (id == "leave_discard") { if (pendingLeave == 2) g_QuitConfirmed = true; running = false; pendingLeave = 0; }
                    else if (id == "leave_keep") pendingLeave = 0;
                }
                // the status line, the unsaved-changes question, and the preview
                if (status != shownStatus) { shownStatus = status; rml->SetHtml("gstatus", E(status)); refreshSlots(); }
                {
                    const std::string dialog = pendingLeave == 0 ? std::string() : "<div id=\"dialogbox\"><p class=\"cardhead\">Unsaved craft</p><p class=\"note\">You changed this craft since you last saved it. Save it before you leave the Garage?</p>"
                        "<button class=\"act\" data-id=\"leave_save\">Save and leave</button><button class=\"act quiet\" data-id=\"leave_discard\">Leave without saving</button><button class=\"act quiet back\" data-id=\"leave_keep\">Keep editing</button></div>";
                    if (dialog != shownDialog) { shownDialog = dialog; rml->SetHtml("dialog", dialog); rml->SetClass("dialog", "hide", dialog.empty()); }
                }
#ifdef HOVERNET_WEB
                { char accountStatus[160] = {}; hovernet_js_account_craft_status(accountStatus, sizeof accountStatus); rml->SetHtml("acctstatus", E(accountStatus)); }
#endif
                const std::uint64_t hash = Authority::CraftDesignHash(design);
                const int yawNow = static_cast<int>(yaw) / 2 * 2;
                if (previewTexture != nullptr && (hash != shownHash || yawNow != shownYaw || design.craft != shownCraft) && (now - lastBuild >= 80 || !previewOk)) {
                    const CraftDesign shown = Authority::SanitiseCraftDesign(design);
                    previewOk = HoverNetCraft::RenderCraftPreview(resources, shown.IsPlain() ? nullptr : &shown, shown.craft, yawNow, paletteWords.data(), kPreviewW, kPreviewH, rgba);
                    if (previewOk) SDL_UpdateTexture(previewTexture, nullptr, rgba.data(), kPreviewW * 4);
                    shownHash = hash; shownYaw = yawNow; shownCraft = design.craft; lastBuild = now;
                }
            }
            if (paintTexture != nullptr) SDL_DestroyTexture(paintTexture);
            rml.reset();
            if (previewTexture != nullptr) SDL_DestroyTexture(previewTexture);
            if (ownsImGuiContext) { ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext(); }
            return;
        }
    }
#endif
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        if (!testDecals.empty()) {
            const int phase = (framesShown - 6) / 8, step = (framesShown - 6) % 8;   // per decal: scroll its button into view (steps 0-3), find it (4), move, press, release (5-7)
            if (framesShown >= 6 && phase < static_cast<int>(testDecals.size()) && step >= 5) {
                SDL_Event click; SDL_zero(click);
                if (step == 5) { click.type = SDL_MOUSEMOTION; click.motion.x = testClickX; click.motion.y = testClickY; }
                else { click.type = step == 6 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; click.button.button = SDL_BUTTON_LEFT; click.button.x = testClickX; click.button.y = testClickY; click.button.state = step == 6 ? SDL_PRESSED : SDL_RELEASED; click.button.clicks = 1; }
                SDL_PushEvent(&click);
            }
            if (framesShown == pFrameLimit) {
                int painted = 0;
                for (std::uint8_t cell : design.paint) if (cell != 0) ++painted;
                std::printf("GARAGETEST paint cells after the clicks: %d; status: %s\n", painted, status.c_str());
                std::fflush(stdout);
            }
        }
        if (std::getenv("HOVERNET_TEST_GARAGE_ESC") != nullptr && framesShown == 5) {   // test hook: press Escape once, as a player leaving the Garage would
            SDL_Event esc; SDL_zero(esc); esc.type = SDL_KEYDOWN; esc.key.keysym.sym = SDLK_ESCAPE; esc.key.keysym.scancode = SDL_SCANCODE_ESCAPE; esc.key.state = SDL_PRESSED; SDL_PushEvent(&esc);
        }
        if (const char* hoverTest = std::getenv("HOVERNET_TEST_HOVER")) {   // test hook: keyboard focus on one button and the pointer held over another; reports how many ticks played
            SDL_Event motion; SDL_zero(motion);
            if (framesShown == 5) { motion.type = SDL_KEYDOWN; motion.key.keysym.sym = SDLK_DOWN; motion.key.keysym.scancode = SDL_SCANCODE_DOWN; motion.key.state = SDL_PRESSED; SDL_PushEvent(&motion); }
            SDL_zero(motion);
            motion.type = SDL_MOUSEMOTION; motion.motion.x = framesShown < 10 ? 20 : std::atoi(hoverTest) / 10000; motion.motion.y = framesShown < 10 ? 20 : std::atoi(hoverTest) % 10000; SDL_PushEvent(&motion);
            if (framesShown == pFrameLimit) { std::printf("hover ticks played: %d\n", gSfx.hoverTicks); std::fflush(stdout); }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
            if (event.type == SDL_QUIT) { if (leaveGarage(true)) running = false; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !ImGui::GetIO().WantTextInput) { if (pendingLeave != 0) pendingLeave = 0; else if (leaveGarage(false)) running = false; }
        }
        const Uint32 now = SDL_GetTicks();
        if (spin) yaw = std::fmod(yaw + 30.0f * static_cast<float>(now - lastTick) / 1000.0f, 360.0f);
        lastTick = now;
#ifdef HOVERNET_WEB
        const int newAccountCraftRevision = hovernet_js_account_craft_revision();
        if (newAccountCraftRevision != accountCraftRevision) {
            accountCraftRevision = newAccountCraftRevision;
            slot = store.Active();
            if (slot < 0) slot = 0;
            if (!store.Load(slot, design)) design = CraftDesign();
            undo.clear();
            status = "Restored account designs to this browser.";
            shownHash = 0;
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
        ImGui::Begin("HoverNet Garage", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);
        const bool narrow = mainViewport->WorkSize.x < 900.0f;
        HoverNetScreenHeader("GARAGE", mainViewport->WorkSize.x < 640.0f || mainViewport->WorkSize.y < 460.0f);
        ImGui::TextDisabled("Design the shape, colours and paint of your craft. It changes how it looks, never how it drives.");
        MenuBeginChild("GaragePanel", ImVec2(-FLT_MIN, -52.0f), true);

        // The preview stays in view while the controls scroll beneath it (on a wide screen the paint scrolls on its own, beside them).
        auto drawPreview = [&]() {
        // ---- the preview, hull, shape, colours
        const float maxHeight = std::min(static_cast<float>(kPreviewH), mainViewport->WorkSize.y * (narrow ? 0.28f : 0.32f));   // small enough to leave room for the controls under it
        const float previewWidth = std::min(std::min(static_cast<float>(kPreviewW), ImGui::GetContentRegionAvail().x), maxHeight * kPreviewW / kPreviewH);
        if (previewTexture != nullptr && previewOk) ImGui::Image(reinterpret_cast<ImTextureID>(previewTexture), ImVec2(previewWidth, previewWidth * kPreviewH / kPreviewW));
        ImGui::Checkbox("Turn the craft", &spin);
        if (!spin) { ImGui::SameLine(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::SliderFloat("##yaw", &yaw, 0.0f, 359.0f, "%.0f deg"); }
        };
        auto drawControls = [&]() {
        HoverNetSectionHeading("HULL");
        // A segmented choice: one hull is always picked, the picked one is marked, every choice can be reached with the keyboard or a controller.
        for (int c = 0; c < Authority::kCraftCount; ++c) {
            if (c > 0) ImGui::SameLine();
            const bool picked = design.craft == c;
            if (picked) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.16f, 1.0f));
            if (ImGui::Button(Authority::CraftName(c))) design.craft = static_cast<std::uint8_t>(c);
            if (picked) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s hull%s", Authority::CraftName(c), picked ? " (selected)" : "");
        }
        ImGui::PushTextWrapPos(0.0f);
        HoverNetHint("Speed, weight and handling come from the craft you pick before the race, never from this design.");
        ImGui::PopTextWrapPos();

        // A labelled slider: the label on the left, the slider filling the rest, the value on it. Ctrl+click or double-click types a number, right-click puts it back
        // to its starting value, the arrow keys and a controller's D-pad step it by one.
        auto labelledSlider = [&](const char* label, const char* hint, int* value, int low, int high, int standard, const char* format) {
            ImGui::PushID(label);
            const float labelWidth = std::min(150.0f, ImGui::GetContentRegionAvail().x * 0.34f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = MenuSliderInt("##value", value, low, high, format);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { *value = standard; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nCtrl+click to type a value. Right-click to reset to %d.", hint, standard);
            ImGui::PopID();
            return changed || *value == standard;
        };
        HoverNetSectionHeading("SHAPE");
        int length = design.length, width = design.width, height = design.height, taper = design.taper;
        labelledSlider("Length", "How long the craft is.", &length, -shapeLimit, shapeLimit, 0, "%+d%%");
        labelledSlider("Width", "How wide the craft is.", &width, -shapeLimit, shapeLimit, 0, "%+d%%");
        labelledSlider("Height", "How tall the craft is (it still sits on its cushion).", &height, -shapeLimit, shapeLimit, 0, "%+d%%");
        labelledSlider("Nose", "Narrower (-) or wider (+) at the front than at the back.", &taper, -taperLimit, taperLimit, 0, "%+d%%");
        design.length = static_cast<std::int8_t>(length); design.width = static_cast<std::int8_t>(width); design.height = static_cast<std::int8_t>(height); design.taper = static_cast<std::int8_t>(taper);
        if (HoverNetQuietButton("Reset shape", ImVec2(-FLT_MIN, 30))) design.length = design.width = design.height = design.taper = 0;
        if (shapeLimit < Authority::kMaxShapePercent) {
            ImGui::PushTextWrapPos(0.0f);
            HoverNetHint(("The sliders reach further as you level up (now +/-" + std::to_string(shapeLimit) + "%, up to +/-" + std::to_string(Authority::kMaxShapePercent) + "% at level 9).").c_str());
            ImGui::PopTextWrapPos();
        }

        // Colours: a row of ready-made swatches for the usual choices, then the full picker for anything else; the amount says how far the hull's own colours give way.
        HoverNetSectionHeading("COLOURS");
        static const std::uint8_t kSwatches[][3] = {{220, 30, 35}, {240, 120, 20}, {245, 215, 30}, {40, 175, 70}, {30, 185, 190}, {40, 90, 225}, {130, 60, 210}, {235, 70, 170}, {245, 245, 245}, {30, 30, 36}};
        auto colourRow = [&](const char* label, const char* hint, std::array<std::uint8_t, 3>& rgb, std::uint8_t& strength) {
            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            for (int i = 0; i < static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0])); ++i) {
                if (i > 0) ImGui::SameLine(0.0f, 4.0f);
                char id[16]; std::snprintf(id, sizeof id, "##s%d", i);
                const ImVec4 c(kSwatches[i][0] / 255.0f, kSwatches[i][1] / 255.0f, kSwatches[i][2] / 255.0f, 1.0f);
                if (ImGui::ColorButton(id, c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(22, 22))) {
                    rgb = {{kSwatches[i][0], kSwatches[i][1], kSwatches[i][2]}};
                    if (strength == 0) strength = 255;   // picking a colour means wanting to see it
                }
                if (rgb[0] == kSwatches[i][0] && rgb[1] == kSwatches[i][1] && rgb[2] == kSwatches[i][2] && strength > 0) {
                    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(lo, hi, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.5f);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use this colour for the %s", hint);
            }
            ImGui::SameLine(0.0f, 8.0f);
            float c[3] = {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f};
            if (ImGui::ColorEdit3("##custom", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                for (int i = 0; i < 3; ++i) rgb[i] = static_cast<std::uint8_t>(c[i] * 255.0f + 0.5f);
                if (strength == 0) strength = 255;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick any colour for the %s", hint);
            int amount = strength * 100 / 255;
            const float labelWidth = std::min(150.0f, ImGui::GetContentRegionAvail().x * 0.34f);
            ImGui::TextDisabled("Amount");
            ImGui::SameLine(labelWidth);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (MenuSliderInt("##amount", &amount, 0, 100, amount == 0 ? "off (the hull's own colours)" : "%d%%")) strength = static_cast<std::uint8_t>(amount * 255 / 100);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) strength = 0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much of this colour covers the hull's own. Right-click to switch it off.");
            ImGui::PopID();
        };
        colourRow("Body", "body", design.body, design.bodyStrength);
        colourRow("Trim", "cockpit and trim", design.accent, design.accentStrength);
        };
        auto drawPaint = [&]() {
        if (const char* scrollHook = std::getenv("HOVERNET_GARAGE_SCROLL")) ImGui::SetScrollY(static_cast<float>(std::atof(scrollHook)));   // test hook: scroll the panel down by this many pixels
        HoverNetSectionHeading("PAINT");
        {
            const float cell = std::max(6.0f, std::min(12.0f, (ImGui::GetContentRegionAvail().x - 8.0f) / Authority::kPaintSize));
            const float side = cell * Authority::kPaintSize;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##canvas", ImVec2(side, side));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (int y = 0; y < Authority::kPaintSize; ++y)
                for (int x = 0; x < Authority::kPaintSize; ++x) {
                    const std::uint8_t index = design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x];
                    const bool dark = ((x + y) & 1) != 0;
                    ImU32 fill = dark ? IM_COL32(52, 52, 60, 255) : IM_COL32(66, 66, 76, 255);
                    if (index != 0) fill = IM_COL32(design.palette[index][0], design.palette[index][1], design.palette[index][2], 255);
                    draw->AddRectFilled(ImVec2(origin.x + x * cell, origin.y + y * cell), ImVec2(origin.x + (x + 1) * cell, origin.y + (y + 1) * cell), fill);
                }
            draw->AddRect(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(200, 200, 210, 255));
            const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (ImGui::IsItemHovered() && down) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const int x = std::max(0, std::min(Authority::kPaintSize - 1, static_cast<int>((m.x - origin.x) / cell)));
                const int y = std::max(0, std::min(Authority::kPaintSize - 1, static_cast<int>((m.y - origin.y) / cell)));
                const bool strokeStart = !strokeActive;
                if (!strokeActive) { if (tool != 4) pushUndo(); strokeActive = true; }
                const std::uint8_t value = tool == 1 ? 0 : static_cast<std::uint8_t>(colour);
                if (tool == 4) {
                    if (strokeStart) stampDecal(x, y);   // one stamp for each press, centred on the cell
                }
                else if (tool == 3) { const std::uint8_t picked = design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x]; if (picked != 0) colour = picked; tool = 0; }
                else if (tool == 2) { floodFill(x, y, value); if (mirror) floodFill(Authority::kPaintSize - 1 - x, y, value); }
                else { design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x] = value; if (mirror) design.paint[static_cast<size_t>(y) * Authority::kPaintSize + (Authority::kPaintSize - 1 - x)] = value; }
            }
            if (!down) strokeActive = false;
        }
        {
            const char* tools[] = {"Pencil", "Eraser", "Fill", "Pick"};
            for (int t = 0; t < 4; ++t) {
                if (t > 0) ImGui::SameLine();
                const bool on = tool == t;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.16f, 1.0f));
                if (ImGui::Button(tools[t])) tool = t;
                if (on) ImGui::PopStyleColor();
            }
            ImGui::Checkbox("Mirror left and right", &mirror);
            ImGui::SameLine();
            if (ImGui::Button("Undo") && !undo.empty()) { design.paint = undo.back(); undo.pop_back(); }
            ImGui::SameLine();
            if (ImGui::Button("Clear paint")) { pushUndo(); design.paint.fill(0); }
        }
        {
            // Ready-made patterns: each clears the paint and fills its cells with the selected paint colour (Undo takes it back; Mirror keeps it symmetrical).
            ImGui::TextUnformatted("Patterns (replace the paint, in the selected colour)");
            const int perRow = ImGui::GetContentRegionAvail().x > 470.0f ? 4 : 3;
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (perRow - 1)) / perRow;
            for (int p = 0; p < HoverNetCraft::kPatternCount; ++p) {
                if (p % perRow != 0) ImGui::SameLine();
                const bool locked = garageLevel < HoverNetCraft::PatternLevel(p);
                char label[64];
                if (locked) std::snprintf(label, sizeof label, "Level %d##pattern%d", HoverNetCraft::PatternLevel(p), p);
                else std::snprintf(label, sizeof label, "%s##pattern%d", HoverNetCraft::PatternName(p), p);
                if (locked) ImGui::BeginDisabled();
                const bool patternPressed = ImGui::Button(label, ImVec2(buttonWidth, 0));
                if (locked) { ImGui::EndDisabled(); if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s: opens at level %d", HoverNetCraft::PatternName(p), HoverNetCraft::PatternLevel(p)); }
                if (patternPressed && !locked) {
                    pushUndo();
                    design.paint.fill(0);   // a pattern replaces the paint (Undo brings it back); decals are the thing that goes on top
                    const int last = Authority::kPaintSize - 1;
                    for (int y = 0; y < Authority::kPaintSize; ++y)
                        for (int x = 0; x < Authority::kPaintSize; ++x)
                            if (HoverNetCraft::PatternCovers(p, x, y) || (mirror && HoverNetCraft::PatternCovers(p, last - x, y))) design.paint[static_cast<size_t>(y) * Authority::kPaintSize + x] = static_cast<std::uint8_t>(colour);
                }
            }
        }
        {
            // Colour sets: fifteen swatches at once (they recolour what is already painted, as the swatches are shared). The first is the Garage's own.
            ImGui::TextUnformatted("Colour sets");
            for (int t = 0; t < HoverNetCraft::kThemeCount; ++t) {
                if (t > 0) ImGui::SameLine();
                const bool locked = garageLevel < HoverNetCraft::ThemeLevel(t);
                char label[64];
                if (locked) std::snprintf(label, sizeof label, "Level %d##theme%d", HoverNetCraft::ThemeLevel(t), t);
                else std::snprintf(label, sizeof label, "%s##theme%d", HoverNetCraft::ThemeName(t), t);
                if (locked) ImGui::BeginDisabled();
                const bool pressed = ImGui::Button(label);
                if (locked) { ImGui::EndDisabled(); if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s: opens at level %d", HoverNetCraft::ThemeName(t), HoverNetCraft::ThemeLevel(t)); }
                if (pressed && !locked) {
                    for (int i = 1; i < Authority::kPaintColours; ++i) { const std::uint8_t* c = HoverNetCraft::ThemeColour(t, i); design.palette[i] = {{c[0], c[1], c[2]}}; }
                }
            }
            ImGui::TextUnformatted("Decals (click the canvas to stamp, in the selected colour)");
            const int perRow = ImGui::GetContentRegionAvail().x > 560.0f ? 5 : 3;
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (perRow - 1)) / perRow;
            for (int d = 0; d < HoverNetCraft::kDecalCount; ++d) {
                if (d % perRow != 0) ImGui::SameLine();
                const bool locked = !HoverNetCraft::DecalOpen(d, garageAchievements);
                const bool on = tool == 4 && decal == d;
                char label[64];
                std::snprintf(label, sizeof label, "%s##decal%d", HoverNetCraft::DecalName(d), d);
                if (locked) ImGui::BeginDisabled();
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.16f, 1.0f));
                const bool pressed = ImGui::Button(label, ImVec2(buttonWidth, 0));
                if (on) ImGui::PopStyleColor();
                if (!testDecals.empty() && framesShown >= 6) {
                    const int phase = (framesShown - 6) / 8, step = (framesShown - 6) % 8;
                    if (phase < static_cast<int>(testDecals.size()) && d == testDecals[static_cast<size_t>(phase)]) {
                        if (step < 4) ImGui::SetScrollHereY(0.5f);   // the panel scrolls: bring the button on screen first
                        else if (step == 4) {
                            const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                            testClickX = static_cast<int>((lo.x + hi.x) * 0.5f); testClickY = static_cast<int>((lo.y + hi.y) * 0.5f);
                        }
                    }
                }
                if (locked) { ImGui::EndDisabled(); if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s decal: %s", HoverNetCraft::DecalName(d), HoverNetCraft::DecalEarnedBy(d)); }
                if (pressed && !locked) {
                    tool = 4;
                    decal = d;
                    if (decalPlaced && design.paint == decalResult) {   // nothing has been drawn since the last decal button: swap it for this one instead of piling them up
                        design.paint = decalBase;
                        if (!undo.empty()) undo.pop_back();   // that stamp's undo step goes with it, so one Undo still takes back the decal now shown
                    }
                    decalBase = design.paint;
                    stampDecal(Authority::kPaintSize / 2, Authority::kPaintSize / 2);
                    decalResult = design.paint;
                    decalPlaced = true;
                    status = std::string(HoverNetCraft::DecalName(d)) + " decal added. Click the canvas to place another.";
                }
            }
            for (int d = 0; d < HoverNetCraft::kDecalCount; ++d)
                if (!HoverNetCraft::DecalOpen(d, garageAchievements)) {
                    ImGui::PushTextWrapPos(0.0f);
                    HoverNetHint((std::string("Next decal: ") + HoverNetCraft::DecalName(d) + " - " + HoverNetCraft::DecalEarnedBy(d)).c_str());
                    ImGui::PopTextWrapPos();
                    break;
                }
        }
        for (int i = 1; i < Authority::kPaintColours; ++i) {
            if (i > 1 && (i - 1) % 8 != 0) ImGui::SameLine();
            char id[16]; std::snprintf(id, sizeof id, "##sw%d", i);
            const ImVec4 c(design.palette[i][0] / 255.0f, design.palette[i][1] / 255.0f, design.palette[i][2] / 255.0f, 1.0f);
            if (ImGui::ColorButton(id, c, ImGuiColorEditFlags_NoTooltip, ImVec2(26, 26))) { colour = i; if (tool == 1) tool = 0; }
            if (colour == i) { const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(); ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.5f); }
        }
        {
            float c[3] = {design.palette[colour][0] / 255.0f, design.palette[colour][1] / 255.0f, design.palette[colour][2] / 255.0f};
            if (ImGui::ColorEdit3("Change this paint colour", c, ImGuiColorEditFlags_NoInputs)) for (int i = 0; i < 3; ++i) design.palette[colour][i] = static_cast<std::uint8_t>(c[i] * 255.0f + 0.5f);
        }
        ImGui::PushTextWrapPos(0.0f);
        HoverNetHint("The paint wraps round the hull panels; use Mirror for symmetrical stripes.");
        ImGui::PopTextWrapPos();

        HoverNetSectionHeading("MY CRAFT");
        {
            char nameBuffer[Authority::kMaxDesignNameBytes + 1];
            std::snprintf(nameBuffer, sizeof nameBuffer, "%s", design.name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##designName", nameBuffer, sizeof nameBuffer)) design.name = Authority::SanitiseCraftDesign([&] { CraftDesign d = design; d.name = nameBuffer; return d; }()).name;
            const int active = store.Active();
            for (int i = 0; i < HoverNetCraft::kSavedDesigns; ++i) {
                CraftDesign saved;
                const bool have = store.Load(i, saved);
                char label[96];
                std::snprintf(label, sizeof label, "%d  %s%s##slot%d", i + 1, have ? (saved.name.empty() ? "(unnamed)" : saved.name.c_str()) : "(empty)", active == i ? "   - used in races" : "", i);
                const bool on = slot == i;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.16f, 1.0f));
                if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
                    slot = i;
                    design = have ? saved : CraftDesign();
                    lastSavedDesign = design;
                    undo.clear();
                    status = have ? "Loaded the craft in slot " + std::to_string(i + 1) + "." : "Empty slot " + std::to_string(i + 1) + ".";
                }
                if (on) ImGui::PopStyleColor();
            }
            if (HoverNetButton("Save to this slot and use it in races", ImVec2(-FLT_MIN, 40))) {
                saveCurrent();
            }
            if (HoverNetQuietButton("Race with the standard paint (stock craft)", ImVec2(-FLT_MIN, 32))) { store.SetActive(-1); status = "Your races use the stock craft."; }
            if (HoverNetQuietButton("Delete this slot", ImVec2(-FLT_MIN, 32))) { store.Delete(slot); design = CraftDesign(); lastSavedDesign = design; undo.clear(); status = "Deleted."; }
#ifdef HOVERNET_WEB
            HoverNetSectionHeading("OUTIVA ACCOUNT");
            if (gAccountsOffered && !gSignedIn) {
                ImGui::PushTextWrapPos(0.0f);
                HoverNetHint("You are playing as a guest. Sign in with Outiva (reload the page) to keep your crafts on your account.");
                ImGui::PopTextWrapPos();
            }
            if (HoverNetButton("Save local slots to account", ImVec2(-FLT_MIN, 36))) hovernet_js_account_craft_save();
            if (HoverNetQuietButton("Restore account slots (replaces local)", ImVec2(-FLT_MIN, 32))) hovernet_js_account_craft_load();
            char accountStatus[160] = {};
            hovernet_js_account_craft_status(accountStatus, sizeof accountStatus);
            if (accountStatus[0] != '\0') HoverNetHint(accountStatus);
#endif
        }
        if (!status.empty()) ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.65f, 1.0f), "%s", status.c_str());
        };
        if (!narrow) {
            const float half = ImGui::GetContentRegionAvail().x * 0.5f - 6.0f;
            ImGui::BeginChild("##garageLeft", ImVec2(half, 0), false);
            drawPreview();
            ImGui::BeginChild("##garageControls", ImVec2(0, 0), false);
            drawControls();
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##garageRight", ImVec2(0, 0), false);
            drawPaint();
            ImGui::EndChild();
        }
        else {
            drawPreview();
            ImGui::BeginChild("##garageScroll", ImVec2(0, 0), false);
            drawControls();
            drawPaint();
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::Spacing();
        if (HoverNetQuietButton("Back", ImVec2(-FLT_MIN, 40)) && leaveGarage(false)) running = false;
        if (pendingLeave != 0 && !ImGui::IsPopupOpen("Unsaved craft")) ImGui::OpenPopup("Unsaved craft");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.30f, 0.09f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.45f, 0.11f, 0.16f, 1.0f));
        const bool leaveDialogOpen = ImGui::BeginPopupModal("Unsaved craft", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor(2);
        if (leaveDialogOpen) {
            if (pendingLeave == 0) ImGui::CloseCurrentPopup();
            ImGui::PushTextWrapPos(380.0f);
            ImGui::TextUnformatted("You changed this craft since you last saved it. Save it before you leave the Garage?");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const bool closing = pendingLeave == 2;
            if (HoverNetButton("Save and leave", ImVec2(380, 42)) && saveCurrent()) { if (closing) g_QuitConfirmed = true; running = false; pendingLeave = 0; ImGui::CloseCurrentPopup(); }
            ImGui::SetItemDefaultFocus();
            if (HoverNetQuietButton("Leave without saving", ImVec2(380, 34))) { if (closing) g_QuitConfirmed = true; running = false; pendingLeave = 0; ImGui::CloseCurrentPopup(); }
            if (HoverNetQuietButton("Keep editing", ImVec2(380, 34))) { pendingLeave = 0; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        ImGui::End();

        // Redraw the preview when the design, the hull or the angle changed (the design at most every 80 ms: each change builds new textures).
        const std::uint64_t hash = Authority::CraftDesignHash(design);
        const int yawNow = static_cast<int>(yaw) / 2 * 2;
        if (previewTexture != nullptr && (hash != shownHash || yawNow != shownYaw || design.craft != shownCraft) && (now - lastBuild >= 80 || !previewOk)) {
            const CraftDesign shown = Authority::SanitiseCraftDesign(design);
            previewOk = HoverNetCraft::RenderCraftPreview(resources, shown.IsPlain() ? nullptr : &shown, shown.craft, yawNow, paletteWords.data(), kPreviewW, kPreviewH, rgba);
            if (previewOk) SDL_UpdateTexture(previewTexture, nullptr, rgba.data(), kPreviewW * 4);
            shownHash = hash; shownYaw = yawNow; shownCraft = design.craft; lastBuild = now;
        }

        HN_SCREEN_FADE();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (pFrameLimit > 0 && framesShown == pFrameLimit && std::getenv("HOVERNET_CAPTURE_FRAME") != nullptr) {   // test hook: the last frame of a bounded run, as a BMP
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            if (SDL_Surface* shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, shot->pixels, shot->pitch);
                SDL_SaveBMP(shot, std::getenv("HOVERNET_CAPTURE_FRAME"));
                SDL_FreeSurface(shot);
            }
        }
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }
    if (previewTexture != nullptr) SDL_DestroyTexture(previewTexture);
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
    std::string name = OnlineUsername();
    if (name.empty() || !Protocol3::IsValidText(name, 32)) name = HoverNetGuest::Tagged("Viewer", gAccountsOffered && !gSignedIn);
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

#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md): my races, server replays and the "follow which craft" question; if it cannot load, the ImGui screen below runs
        using HoverNetRml::FormField;
        std::unique_ptr<HoverNetRml::FormScreen> form(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "WATCH A REPLAY", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight));
        if (form->Ok()) {
            form->SetLayout(false, true);
            form->SetCardWidth(760);
            form->SetFooter("Back", "");
            std::vector<std::string> tabNames = {"MY RACES"};
#if defined(HOVERNET_AUTH)
            tabNames.push_back("SERVER REPLAYS");
#endif
            int tab = std::max(0, std::min(static_cast<int>(tabNames.size()) - 1, wantTab));
            form->SetTabs(tabNames, tab);
            auto note = [](const std::string& text) { FormField f; f.type = FormField::Type::eNote; f.label = text; return f; };
            auto heading = [](const std::string& text) { FormField f; f.type = FormField::Type::eHeading; f.label = text; return f; };
            auto row = [](const std::string& id, const std::string& title, const std::string& sub, const std::string& button) { FormField f; f.type = FormField::Type::eRow; f.id = id; f.label = title; f.hint = sub; f.text = button; return f; };
            auto button = [](const std::string& id, const std::string& label, bool quiet) { FormField f; f.type = FormField::Type::eButton; f.id = id; f.label = label; f.quiet = quiet; return f; };
            std::string shown;
            int frame = 0;
            const int limit = std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit;
            while (running && !started && (limit < 0 || frame < limit)) {
                ++frame;
                std::vector<HoverNetRml::FormEvent> events;
                const HoverNetRml::FormOutcome outcome = form->Frame(events, frame, limit);
                std::string startPath;
                int startSlot = 0;
                std::string startNote;
                if (outcome == HoverNetRml::FormOutcome::eSave || outcome == HoverNetRml::FormOutcome::eCancel) { if (craftChoice.active) craftChoice.active = false; else running = false; }
                else if (outcome == HoverNetRml::FormOutcome::eClose) { g_QuitConfirmed = true; running = false; }
#if defined(HOVERNET_AUTH)
                if (tab == 1 && !serverTried) { serverTried = true; if (!ConnectLobbyForReplays(server)) message = "The server address is not usable. Check it in the HoverNet servers screen."; }
                server.Pump();
                if (server.GetState() == OnlineLobby::State::eBrowsing && needReplays) { server.RefreshReplays(serverTrack == 0 ? std::string() : Tracks().Get(serverTrack - 1)->mName); needReplays = false; }
                std::vector<std::uint8_t> downloaded;
                if (downloadSlotId != 0 && server.TakeReplay(downloaded)) {
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
#endif
                bool refresh = false;
                for (const HoverNetRml::FormEvent& e : events) {
                    if (e.id == "tab") { tab = e.index; form->SetTabs(tabNames, tab); refresh = true; craftChoice.active = false; }
                    else if (e.id == "cancelcraft") { craftChoice.active = false; refresh = true; }
                    else if (e.id.compare(0, 5, "craft") == 0 && craftChoice.active) {   // "Follow which craft?": the slot chosen
                        const int chosen = std::atoi(e.id.c_str() + 5);
                        g_nextReplayNames = craftChoice.slotNames;
                        std::string noteText;
                        if (chosen == craftChoice.mySlot) noteText = "you";
                        else if (chosen == craftChoice.winnerSlot) noteText = craftChoice.winnerName.empty() ? std::string("the winner") : "the winner, " + craftChoice.winnerName;
#if defined(HOVERNET_AUTH)
                        if (craftChoice.server) { downloadSlotId = craftChoice.id; downloadSlot = chosen; downloadNote = noteText; server.FetchReplay(craftChoice.id); }
                        else
#endif
                        { startPath = craftChoice.path; startSlot = chosen; startNote = noteText; }
                        craftChoice.active = false;
                        refresh = true;
                    }
                    else if (e.id.compare(0, 2, "m:") == 0) {   // my races: m:<n> watch, m:<n>_2 delete
                        const bool del = e.id.size() > 2 && e.id.find("_2") != std::string::npos;
                        const int index = std::atoi(e.id.c_str() + 2);
                        if (index >= 0 && index < static_cast<int>(entries.size())) {
                            const ReplayLibrary::Entry& entry = entries[static_cast<std::size_t>(index)];
                            if (del) {
                                if (confirmDelete == index) { library.Remove(entry.name); entries = library.List(); confirmDelete = -1; }
                                else confirmDelete = index;
                            }
                            else if (entry.players > 1) { craftChoice = CraftChoice(); craftChoice.active = true; craftChoice.path = entry.path; craftChoice.title = entry.track; craftChoice.players = entry.players; craftChoice.mySlot = entry.mySlot; craftChoice.slotNames = entry.names; }
                            else { g_nextReplayNames = entry.names; startPath = entry.path; startSlot = 0; }
                            refresh = true;
                        }
                    }
#if defined(HOVERNET_AUTH)
                    else if (e.id == "track") { serverTrack = e.index; needReplays = true; }
                    else if (e.id == "retry") { serverTried = false; needReplays = true; server.Close(); message.clear(); }
                    else if (e.id.compare(0, 2, "s:") == 0 && downloadSlotId == 0 && server.ReplaysLoaded()) {
                        const std::size_t n = static_cast<std::size_t>(std::atoi(e.id.c_str() + 2));
                        if (n < server.Replays().size()) {
                            const Protocol3::ReplayInfo& r = server.Replays()[n];
                            message.clear();
                            if (r.players > 1) askCraftFor(r);
                            else { downloadSlotId = r.id; downloadSlot = 0; downloadNote.clear(); server.FetchReplay(r.id); }
                            refresh = true;
                        }
                    }
#endif
                }
                std::string signature = std::to_string(tab) + "|" + std::to_string(entries.size()) + "|" + std::to_string(confirmDelete) + "|" + message + "|" + std::to_string(craftChoice.active);
#if defined(HOVERNET_AUTH)
                signature += "|" + std::to_string(static_cast<int>(server.GetState())) + server.Message() + "|" + std::to_string(serverTrack) + "|" + std::to_string(server.ReplaysLoaded()) + std::to_string(server.ReplaysLoaded() ? server.Replays().size() : 0) + "|" +
                             std::to_string(downloadSlotId) + "/" + std::to_string(downloadSlotId != 0 ? server.DownloadPercent() : 0);
#endif
                if (refresh || signature != shown) {
                    shown = signature;
                    std::vector<FormField> f;
                    if (craftChoice.active) {
                        f.push_back(heading(craftChoice.title + ": which craft do you want to follow?"));
                        f.push_back(note("You can switch craft while it plays with [ and ]."));
                        for (int slot = 0; slot < craftChoice.players; ++slot) {
                            std::string suffix;
                            if (slot == craftChoice.mySlot) suffix += "  (you)";
                            if (slot == craftChoice.winnerSlot) { char t[32]; formatMs(craftChoice.winnerMs, t, sizeof t); suffix += std::string("  (winner") + (craftChoice.winnerName.empty() ? std::string() : " " + craftChoice.winnerName) + ", " + t + ")"; }
                            const std::string who = slot < static_cast<int>(craftChoice.slotNames.size()) && !craftChoice.slotNames[static_cast<std::size_t>(slot)].empty() ? " - " + craftChoice.slotNames[static_cast<std::size_t>(slot)] : std::string();
                            f.push_back(button("craft" + std::to_string(slot), "PLAYER " + std::to_string(slot + 1) + who + suffix, false));
                        }
                        f.push_back(button("cancelcraft", "CANCEL", true));
                    }
                    else if (tab == 0) {
                        if (entries.empty()) {
                            f.push_back(note("No saved races yet."));
                            f.push_back(note("A race on a HoverNet server that you take part in, or watch from its first moment, is saved here when it ends (the newest " + std::to_string(ReplayLibrary::kKeep) + " are kept). Nothing is sent anywhere: the files stay on this device. The Server replays tab has everyone's races."));
                        }
                        for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
                            const ReplayLibrary::Entry& e = entries[static_cast<std::size_t>(index)];
                            if (e.valid) {
                                const long long seconds = e.durationMs() / 1000;
                                FormField r = row("m:" + std::to_string(index), e.track, e.when + "   " + std::to_string(e.players) + " player" + (e.players == 1 ? "" : "s") + ", " + std::to_string(e.laps) + " lap" + (e.laps == 1 ? "" : "s") + ", " + std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60), "WATCH");
                                r.suffix = confirmDelete == index ? "SURE? DELETE" : "DELETE";
                                f.push_back(r);
                            }
                            else { FormField r = row("m:" + std::to_string(index), "Damaged file", e.name, ""); r.suffix = confirmDelete == index ? "SURE? DELETE" : "DELETE"; f.push_back(r); }
                        }
                    }
#if defined(HOVERNET_AUTH)
                    else {
                        if (server.GetState() == OnlineLobby::State::eFailed) { f.push_back(note(server.Message().empty() ? "Could not reach the server." : server.Message())); f.push_back(button("retry", "TRY AGAIN", false)); }
                        else if (server.GetState() != OnlineLobby::State::eBrowsing) f.push_back(note("Connecting to the server..."));
                        else {
                            { FormField c; c.type = FormField::Type::eSelect; c.id = "track"; c.label = "Track"; c.options.push_back("All tracks"); for (int t = 0; t < officialTracks; ++t) c.options.push_back(Tracks().Get(t)->mName); c.selected = serverTrack; f.push_back(c); }
                            if (server.ListsUnreadable() && !server.ReplaysLoaded()) f.push_back(note("The server answered in a form this version of the game cannot read: the game needs updating."));
                            else if (!server.ReplaysLoaded()) f.push_back(note("Loading the list..."));
                            else if (server.Replays().empty()) f.push_back(note(std::string("The server has no replays") + (serverTrack == 0 ? "" : " of this track") + " yet."));
                            for (std::size_t i = 0; i < server.Replays().size(); ++i) {
                                const Protocol3::ReplayInfo& r = server.Replays()[i];
                                const long long seconds = static_cast<long long>(r.ticks) * ReplayFormat::kFrameMs / 1000;
                                std::string sub = UkDateTime(r.savedAt) + "   " + std::to_string(r.players) + " player" + (r.players == 1 ? "" : "s") + ", " + std::to_string(r.laps) + " lap" + (r.laps == 1 ? "" : "s") + ", " + std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
                                if (r.finishers.empty()) sub += "\nNo results recorded";
                                for (std::size_t k = 0; k < r.finishers.size(); ++k) {
                                    const Protocol3::ReplayFinisher& fin = r.finishers[k];
                                    char time[32] = "did not finish";
                                    if (fin.ms > 0) formatMs(fin.ms, time, sizeof time);
                                    sub += "\n" + std::to_string(k + 1) + ". " + (fin.name.empty() ? "Player " + std::to_string(fin.slot + 1) : fin.name) + (fin.craft < Authority::kCraftCount ? std::string(" (") + Authority::CraftName(fin.craft) + ")" : std::string()) + "   " + time;
                                }
                                f.push_back(row("s:" + std::to_string(i), r.track, sub, downloadSlotId == r.id ? "DOWNLOADING " + std::to_string(server.DownloadPercent()) + "%" : std::string("WATCH")));
                            }
                        }
                    }
#endif
                    if (!message.empty()) f.push_back(note(message));
                    form->SetFields(f);
                }
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
            return started;
        }
    }
#endif

    while (running && !started && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
            if (event.type == SDL_QUIT) { g_QuitConfirmed = true; running = false; }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) running = false;
        }
        std::string startPath;   // set by a WATCH button (or the test hook) and acted on after the frame is drawn
        int startSlot = 0;
        std::string startNote;
        if (framesShown == 3 && std::getenv("HOVERNET_TEST_REPLAY_PICK") != nullptr) {
            for (const ReplayLibrary::Entry& e : entries) if (e.valid) {
                if (std::getenv("HOVERNET_TEST_REPLAY_PICK_CRAFT") != nullptr && e.players > 1) { craftChoice = CraftChoice(); craftChoice.active = true; craftChoice.path = e.path; craftChoice.title = e.track; craftChoice.players = e.players; craftChoice.mySlot = e.mySlot; craftChoice.slotNames = e.names; }   // through the question
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
        HoverNetScreenHeader("WATCH A REPLAY", phoneHeader);
        ImGui::Spacing();

        const float centerWidth = std::min(760.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        MenuBeginChild("ReplayPanel", ImVec2(centerWidth, -52.0f), true);
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
                            if (e.players > 1) { craftChoice = CraftChoice(); craftChoice.active = true; craftChoice.path = e.path; craftChoice.title = e.track; craftChoice.players = e.players; craftChoice.mySlot = e.mySlot; craftChoice.slotNames = e.names; }
                            else { g_nextReplayNames = e.names; startPath = e.path; startSlot = 0; }
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
                if (HoverNetQuietButton("CANCEL", ImVec2(360, 34))) { craftChoice.active = false; ImGui::CloseCurrentPopup(); }
                if (chosen >= 0) {
                    g_nextReplayNames = craftChoice.slotNames;
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
        if (HoverNetQuietButton("Back", ImVec2(-FLT_MIN, 40))) running = false;
        ImGui::End();

        HN_SCREEN_FADE();
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
    int track = 0;                       // the track whose records are shown: an index into the catalog (the official tracks first, then the community's)
    int laps = 0;                        // 0: fastest laps; n: fastest races of n laps
    bool need = true, needWinners = true, needLeague = true, needBreaks = true;
    std::string leagueMonth;             // the month shown in the league tab (empty: the current month)
    int wantTab = -1;                    // test hook: open on this tab
    std::uint64_t downloadId = 0;
    int downloadSlot = 0;
    std::string downloadNote, message, startPath, startNote;
    int startSlot = 0;
    bool started = false, running = true;
    int framesShown = 0;
    // Watching a time on a community track this computer does not have: the track is downloaded first (a few hundred kilobytes, checked against the
    // manifest), then the replay is fetched and played.
    DownloadUi trackDownload;
    struct PendingWatch { std::uint64_t id = 0; int slot = 0; std::string note; } pendingWatch;
    auto watchReplay = [&](std::uint64_t replayId, int slot, const std::string& note, const std::string& trackName, const std::string& racerName) {
        g_nextReplayNames.assign(8, std::string()); if (slot >= 0 && slot < 8) g_nextReplayNames[static_cast<std::size_t>(slot)] = racerName;
        downloadId = replayId; downloadSlot = slot; downloadNote = note; message.clear();
        const TrackEntry* entry = Tracks().Get(Tracks().Find(trackName));
        if (entry != nullptr && !entry->mInstalled && entry->mHasDownload && !trackDownload.Active()) {
            pendingWatch.id = replayId; pendingWatch.slot = slot; pendingWatch.note = note;
            trackDownload.Begin({*entry}, 0);
        }
        else server.FetchReplay(replayId);
    };
    if (const char* trackEnv = std::getenv("HOVERNET_TEST_RECORDS_TRACK")) { const int found = Tracks().Find(trackEnv); if (found >= 0) track = found; }   // screenshot/test hook
    if (const char* lapsEnv = std::getenv("HOVERNET_TEST_RECORDS_LAPS")) laps = std::atoi(lapsEnv);
    if (const char* tabEnv = std::getenv("HOVERNET_TEST_LEADERBOARD_TAB")) wantTab = std::atoi(tabEnv);
    auto formatMs = [](long long ms, char* out, std::size_t n) { std::snprintf(out, n, "%lld:%02lld.%02lld", ms / 60000, (ms % 60000) / 1000, (ms % 1000) / 10); };
    auto formatDate = [](std::uint32_t unixSeconds, char* out, std::size_t n) { std::snprintf(out, n, "%s", UkDateTime(unixSeconds).c_str()); };
    bool printedReport = false, printedWinners = false, printedLeague = false, printedBreaks = false;

#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit)) {   // the RmlUi version of this screen (docs/rmlui.md): the same four tabs and data; if it cannot load, the ImGui screen below runs
        using HoverNetRml::FormField;
        std::unique_ptr<HoverNetRml::FormScreen> form(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "LEADERBOARDS", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight));
        if (form->Ok()) {
            form->SetLayout(false, true);
            form->SetCardWidth(760);
            form->SetFooter("Back", "");
            const std::vector<std::string> tabNames = {"RECORDS BROKEN", "ALL RECORDS", "RECENT WINNERS", "MONTHLY LEAGUE"};
            // the ImGui screen numbers its tabs all-records first; this one has the newest records first
            int tab = wantTab == 3 ? 0 : wantTab == 0 ? 1 : wantTab == 1 ? 2 : wantTab == 2 ? 3 : 0;
            form->SetTabs(tabNames, tab);
            std::vector<std::string> trackItems;
            for (int i = 0; i < Tracks().Count(); ++i) { const TrackEntry* e = Tracks().Get(i); trackItems.push_back(e == nullptr ? std::string() : e->mName + (e->mOfficial ? "" : "  -  community")); }
            static const char* const monthNames[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
            auto monthLabel = [&](const std::string& key) -> std::string { const int m = key.size() == 7 ? std::atoi(key.substr(5, 2).c_str()) : 0; return m >= 1 && m <= 12 ? std::string(monthNames[m - 1]) + " " + key.substr(0, 4) : key; };
            auto note = [](const std::string& text) { FormField f; f.type = FormField::Type::eNote; f.label = text; return f; };
            auto heading = [](const std::string& text) { FormField f; f.type = FormField::Type::eHeading; f.label = text; return f; };
            auto row = [](const std::string& id, const std::string& title, const std::string& sub, const std::string& button) { FormField f; f.type = FormField::Type::eRow; f.id = id; f.label = title; f.hint = sub; f.text = button; return f; };
            const char* unreadable = "The server answered in a form this version of the game cannot read: the game needs updating.";
            std::vector<std::function<void()>> watchers;   // a row's button n starts watchers[n]
            std::string shown;                              // what the fields were last built from; they are rebuilt only when it changes (a rebuild puts the list back at the top)
            int frame = 0;
            const int limit = std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit;
            while (running && !started && (limit < 0 || frame < limit)) {
                ++frame;
                std::vector<HoverNetRml::FormEvent> events;
                const HoverNetRml::FormOutcome outcome = form->Frame(events, frame, limit);
                if (outcome == HoverNetRml::FormOutcome::eSave || outcome == HoverNetRml::FormOutcome::eCancel) running = false;
                else if (outcome == HoverNetRml::FormOutcome::eClose) { g_QuitConfirmed = true; running = false; }
                if (!serverTried) { serverTried = true; if (!ConnectLobbyForReplays(server)) message = "The server address is not usable. Check it in the HoverNet servers screen."; }
                server.Pump();
                if (server.GetState() == OnlineLobby::State::eBrowsing) {
                    if (need && officialTracks > 0) { server.RefreshRecords(Tracks().Get(track)->mName, laps); need = false; }
                    if (needWinners) { server.RefreshWinners(); needWinners = false; }
                    if (needLeague) { server.RefreshLeague(leagueMonth); needLeague = false; }
                    if (needBreaks) { server.RefreshRecordBreaks(); needBreaks = false; }
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
                else if (downloadId != 0 && !trackDownload.Active() && !server.Downloading()) {
                    message = server.ReplayNotice().empty() ? "The replay could not be downloaded." : server.ReplayNotice();
                    downloadId = 0;
                }
                bool refresh = false;
                for (const HoverNetRml::FormEvent& e : events) {
                    if (e.id == "tab") { tab = e.index; form->SetTabs(tabNames, tab); refresh = true; }
                    else if (e.id == "track") { track = std::max(0, std::min(Tracks().Count() - 1, e.index)); need = true; laps = 0; }
                    else if (e.id == "kind") {
                        laps = 0;
                        if (e.index > 0 && server.RecordsLoaded() && e.index - 1 < static_cast<int>(server.Records().availableLaps.size())) laps = server.Records().availableLaps[static_cast<std::size_t>(e.index - 1)];
                        need = true;
                    }
                    else if (e.id == "month") {
                        if (server.LeagueLoaded()) {
                            std::vector<std::string> months; months.push_back(server.League().currentMonth);
                            for (const Protocol3::LeagueMonthWinner& w : server.League().pastWinners) months.push_back(w.month);
                            if (e.index >= 0 && e.index < static_cast<int>(months.size())) { leagueMonth = months[static_cast<std::size_t>(e.index)] == server.League().currentMonth ? std::string() : months[static_cast<std::size_t>(e.index)]; needLeague = true; }
                        }
                    }
                    else if (e.id == "retry") { serverTried = false; need = needWinners = needLeague = needBreaks = true; server.Close(); message.clear(); }
                    else if (e.id.size() > 2 && e.id[0] == 'w' && e.id[1] == ':' && downloadId == 0 && !trackDownload.Active()) {
                        const std::size_t n = static_cast<std::size_t>(std::atoi(e.id.c_str() + 2));
                        if (n < watchers.size()) watchers[n]();
                        refresh = true;
                    }
                }
                // what the screen would show: rebuilt when this changes
                std::string signature = std::to_string(tab) + "|" + std::to_string(static_cast<int>(server.GetState())) + "|" + server.Message() + "|" + message + "|" + std::to_string(track) + "|" + std::to_string(laps) + "|" + std::to_string(downloadId) +
                    "|" + std::to_string(server.RecordsLoaded()) + std::to_string(server.WinnersLoaded()) + std::to_string(server.LeagueLoaded()) + std::to_string(server.RecordBreaksLoaded()) + std::to_string(server.ListsUnreadable());
                if (server.RecordsLoaded()) signature += "|r" + std::to_string(server.Records().boards.size()) + server.Records().track + std::to_string(server.Records().laps);
                if (server.WinnersLoaded()) signature += "|w" + std::to_string(server.Winners().size());
                if (server.RecordBreaksLoaded()) signature += "|b" + std::to_string(server.RecordBreaks().size());
                if (server.LeagueLoaded()) signature += "|l" + server.League().month + std::to_string(server.League().standings.size());
                if (refresh || signature != shown) {
                    shown = signature;
                    watchers.clear();
                    std::vector<FormField> f;
                    auto watchButton = [&](const std::string& title, std::uint64_t replayId, int slot, const std::string& noteText, const std::string& trackName, const std::string& racer, const std::string& sub, const std::string& label) {
                        watchers.push_back([&, replayId, slot, noteText, trackName, racer]() { watchReplay(replayId, slot, noteText, trackName, racer); });
                        f.push_back(row("w:" + std::to_string(watchers.size() - 1), title, sub, downloadId == replayId ? "DOWNLOADING..." : label));
                    };
                    if (server.GetState() == OnlineLobby::State::eFailed) {
                        f.push_back(note(server.Message().empty() ? "Could not reach the server." : server.Message()));
                        FormField b; b.type = FormField::Type::eButton; b.id = "retry"; b.label = "TRY AGAIN"; f.push_back(b);
                    }
                    else if (server.GetState() != OnlineLobby::State::eBrowsing) f.push_back(note("Connecting to the server..."));
                    else if (tab == 0) {
                        f.push_back(note("The latest records: a time that beat the best lap or the best race time of a craft on a track, or the first time on one (it sets the record)."));
                        if (server.ListsUnreadable() && !server.RecordBreaksLoaded()) f.push_back(note(unreadable));
                        else if (!server.RecordBreaksLoaded()) f.push_back(note("Loading the records..."));
                        else if (server.RecordBreaks().empty()) f.push_back(note("No record has been broken yet."));
                        for (const Protocol3::RecordBreakEntry& b : server.RecordBreaks()) {
                            char time[32], before[32];
                            formatMs(b.ms, time, sizeof time); formatMs(b.previousMs, before, sizeof before);
                            const std::string craft = Authority::CraftName(b.craft), who = b.name.empty() ? "player" : b.name;
                            std::string kind = b.lap ? "Fastest lap, " + craft : "Fastest race, " + std::to_string(b.raceLaps) + " lap" + (b.raceLaps == 1 ? "" : "s") + ", " + craft;
                            std::string what = b.previousMs == 0 ? who + "  " + time + "  set the first record" : who + "  " + time + "  beat  " + (b.previousName.empty() ? "player" : b.previousName) + "  " + before;
                            watchButton(b.track, b.replay, b.slot, b.lap ? "the fastest lap" : "the record holder", b.track, b.name, UkDateTime(b.savedAt) + "   " + kind + "\n" + what, "WATCH");
                        }
                    }
                    else if (tab == 1) {
                        f.push_back(note("The best times of everyone who races on this server, for each craft."));
                        if (Tracks().Count() > 0) { FormField c; c.type = FormField::Type::eChooser; c.id = "track"; c.label = "Track"; c.items = trackItems; c.selected = track; f.push_back(c); }
                        FormField k; k.type = FormField::Type::eSelect; k.id = "kind"; k.label = "Show"; k.options.push_back("Fastest laps"); k.selected = 0;
                        if (server.RecordsLoaded()) for (std::uint8_t l : server.Records().availableLaps) { k.options.push_back("Fastest races: " + std::to_string(l) + " lap" + (l == 1 ? "" : "s")); if (laps == l) k.selected = static_cast<int>(k.options.size()) - 1; }
                        f.push_back(k);
                        if (server.ListsUnreadable() && !server.RecordsLoaded()) f.push_back(note(unreadable));
                        else if (!server.RecordsLoaded()) f.push_back(note("Loading the leaderboard..."));
                        else {
                            const std::string trackName = Tracks().Get(track)->mName;
                            const long long mine = laps == 0 ? myBests.LapBest(trackName) : myBests.RaceBest(trackName, laps);
                            if (mine) { char t[32]; formatMs(mine, t, sizeof t); f.push_back(note(std::string("Your best on this device: ") + t + " (any craft)")); }
                            if (server.Records().boards.empty()) f.push_back(note("No times for this yet. Times come from races on the server; races against the clock on your own do not count."));
                            for (int craft = 0; craft < Authority::kCraftCount; ++craft) {
                                const Protocol3::RecordBoard* board = nullptr;
                                for (const Protocol3::RecordBoard& b : server.Records().boards) if (b.craft == craft) board = &b;
                                f.push_back(heading(Authority::CraftName(craft)));
                                if (board == nullptr || board->entries.empty()) { f.push_back(note("No times yet")); continue; }
                                for (std::size_t i = 0; i < board->entries.size(); ++i) {
                                    const Protocol3::RecordEntry& e = board->entries[i];
                                    char time[32]; formatMs(e.ms, time, sizeof time);
                                    const std::string who = e.name.empty() ? "player " + std::to_string(e.slot + 1) : e.name;
                                    watchButton(std::to_string(i + 1) + ".  " + who + "   " + time, e.replay, e.slot, laps == 0 ? "the fastest lap" : "the record holder", trackName, e.name, UkDateTime(e.savedAt), "WATCH");
                                }
                            }
                        }
                    }
                    else if (tab == 2) {
                        f.push_back(note("Who won the latest races. A race is listed only if two or more different players were in it."));
                        if (server.ListsUnreadable() && !server.WinnersLoaded()) f.push_back(note(unreadable));
                        else if (!server.WinnersLoaded()) f.push_back(note("Loading the winners..."));
                        else if (server.Winners().empty()) f.push_back(note("No races with two or more players have been won yet."));
                        for (const Protocol3::WinnerEntry& w : server.Winners()) {
                            char time[32]; formatMs(w.ms, time, sizeof time);
                            std::string craft = w.craft < Authority::kCraftCount ? std::string(" (") + Authority::CraftName(w.craft) + ")" : std::string();
                            watchButton(w.track, w.replay, w.slot, "the winner", w.track, w.name, UkDateTime(w.savedAt) + "   " + std::to_string(w.laps) + " lap" + (w.laps == 1 ? "" : "s") + ", " + std::to_string(w.players) + " players\nWinner: " + (w.name.empty() ? "player" : w.name) + craft + "   " + time, "WATCH");
                        }
                    }
                    else {
                        f.push_back(note("Win a race: 3 points. Only races where two or more players on different devices finished count. It starts again on the 1st of every month (UTC)."));
                        if (server.ListsUnreadable() && !server.LeagueLoaded()) f.push_back(note(unreadable));
                        else if (!server.LeagueLoaded()) f.push_back(note("Loading the league..."));
                        else {
                            const Protocol3::LeagueData& lg = server.League();
                            FormField m; m.type = FormField::Type::eSelect; m.id = "month"; m.label = "Month";
                            std::vector<std::string> months; months.push_back(lg.currentMonth);
                            for (const Protocol3::LeagueMonthWinner& w : lg.pastWinners) months.push_back(w.month);
                            for (std::size_t i = 0; i < months.size(); ++i) { m.options.push_back(monthLabel(months[i]) + (months[i] == lg.currentMonth ? "  (this month)" : "")); if (months[i] == lg.month) m.selected = static_cast<int>(i); }
                            f.push_back(m);
                            f.push_back(heading(monthLabel(lg.month) + (lg.month == lg.currentMonth ? " so far" : "")));
                            if (lg.standings.empty()) f.push_back(note("Nobody has scored yet."));
                            for (std::size_t i = 0; i < lg.standings.size(); ++i) { const Protocol3::LeagueRow& r = lg.standings[i]; f.push_back(row("", std::to_string(i + 1) + ".  " + r.name + "   " + std::to_string(r.points) + " point" + (r.points == 1 ? "" : "s"), std::to_string(r.wins) + " win" + (r.wins == 1 ? "" : "s") + ", " + std::to_string(r.races) + " race" + (r.races == 1 ? "" : "s"), "")); }
                            f.push_back(heading("Winners of earlier months"));
                            if (lg.pastWinners.empty()) f.push_back(note("No month has finished yet."));
                            for (const Protocol3::LeagueMonthWinner& w : lg.pastWinners) f.push_back(note(w.name.empty() ? monthLabel(w.month) + ":  nobody scored" : monthLabel(w.month) + ":  " + w.name + "  (" + std::to_string(w.points) + " points)"));
                            f.push_back(heading(std::to_string(lg.currentYear) + " so far"));
                            if (lg.yearStandings.empty()) f.push_back(note("Nobody has scored yet this year."));
                            for (std::size_t i = 0; i < lg.yearStandings.size() && i < 5; ++i) f.push_back(note(std::to_string(i + 1) + ".  " + lg.yearStandings[i].name + "   " + std::to_string(lg.yearStandings[i].points) + " points"));
                            f.push_back(heading("Winners of earlier years"));
                            if (lg.yearWinners.empty()) f.push_back(note("The winner of each year is announced here when the year ends."));
                            for (const Protocol3::LeagueYearWinner& w : lg.yearWinners) f.push_back(note(w.name.empty() ? std::to_string(w.year) + ":  nobody scored" : std::to_string(w.year) + ":  " + w.name + "  (" + std::to_string(w.points) + " points)"));
                        }
                    }
                    if (!message.empty()) f.push_back(note(message));
                    form->SetFields(f);
                }
                // a community track this computer does not have is downloaded before the replay is fetched
                if (TrackDownloadJob* job = trackDownload.Job()) {
                    if (!job->Finished()) {
                        const long long totalBytes = std::max<long long>(1, job->TotalBytes());
                        form->SetBusy("Downloading track", job->CurrentName() + "   " + FormatMegabytes(job->BytesDone()) + " of " + FormatMegabytes(totalBytes), std::min(1.0f, static_cast<float>(job->BytesDone()) / static_cast<float>(totalBytes)), "Cancel");
                        for (const HoverNetRml::FormEvent& e : events) if (e.id == "busy") job->Cancel();
                    }
                    else {
                        Tracks().Refresh();
                        const bool ok = job->Succeeded();
                        trackDownload.Reset();
                        form->SetBusy("", "", -1.0f, "");
                        if (ok && pendingWatch.id != 0) { const std::uint64_t id = pendingWatch.id; pendingWatch.id = 0; downloadId = id; downloadSlot = pendingWatch.slot; downloadNote = pendingWatch.note; server.FetchReplay(id); }
                        else { pendingWatch.id = 0; downloadId = 0; }
                    }
                }
                if (!startPath.empty()) {
                    bool fatal = false;
                    message = start(startPath, startSlot, startNote, fatal);
                    startPath.clear();
                    if (message.empty()) started = true;
                    else if (fatal) { g_QuitConfirmed = true; running = false; }
                }
            }
            if (!started) server.Close();
            return started;
        }
    }
#endif

    while (running && !started && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
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
            if (needBreaks) { server.RefreshRecordBreaks(); needBreaks = false; }
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
        else if (downloadId != 0 && !trackDownload.Active() && !server.Downloading()) {   // (while the track downloads, the replay has not been asked for yet)
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
                watchReplay(r.boards[0].entries[0].replay, r.boards[0].entries[0].slot, laps == 0 ? "the fastest lap" : "the record holder", r.track, r.boards[0].entries[0].name);
            }
        }

        if (server.WinnersLoaded() && !printedWinners && std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {   // test hook: what the tabs received
            printedWinners = true;
            std::printf("WINNERS %zu", server.Winners().size());
            for (const Protocol3::WinnerEntry& w : server.Winners()) std::printf(" | %s: %s %d of %d", w.track.c_str(), w.name.c_str(), w.ms, w.players);
            std::printf("\n");
            std::fflush(stdout);
        }
        if (server.RecordBreaksLoaded() && !printedBreaks && std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {
            printedBreaks = true;
            std::printf("BREAKS %zu", server.RecordBreaks().size());
            for (const Protocol3::RecordBreakEntry& b : server.RecordBreaks()) {
                std::printf(" | %s %s %s: %s %d", b.track.c_str(), Authority::CraftName(b.craft), b.lap ? "lap" : "race", b.name.c_str(), b.ms);
                if (b.previousMs == 0) std::printf(" set the first record"); else std::printf(" beat %s %d", b.previousName.c_str(), b.previousMs);
            }
            std::printf("\n");
            std::fflush(stdout);
        }
        if (server.LeagueLoaded() && !printedLeague && std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {
            printedLeague = true;
            const Protocol3::LeagueData& lg = server.League();
            std::printf("LEAGUE %s of %s", lg.month.c_str(), lg.currentMonth.c_str());
            for (const Protocol3::LeagueRow& r : lg.standings) std::printf(" | %s %d", r.name.c_str(), r.points);
            std::printf(" || months");
            for (const Protocol3::LeagueMonthWinner& w : lg.pastWinners) std::printf(" | %s %s %d", w.month.c_str(), w.name.c_str(), w.points);
            std::printf(" || years");
            for (const Protocol3::LeagueYearWinner& w : lg.yearWinners) std::printf(" | %d %s %d", w.year, w.name.c_str(), w.points);
            std::printf("\n");
            std::fflush(stdout);
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
        HoverNetScreenHeader("LEADERBOARDS", phoneHeader);
        ImGui::Spacing();
        const float centerWidth = std::min(760.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - centerWidth) * 0.5f);
        MenuBeginChild("LeaderboardPanel", ImVec2(centerWidth, -52.0f), true);
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
            // ---- the newest records that were broken (the first tab)
            if (ImGui::BeginTabItem("RECORDS BROKEN", nullptr, wantTab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("The latest records: a time that beat the best lap or the best race time of a craft on a track, or the first time on one (it sets the record).");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                if (server.ListsUnreadable() && !server.RecordBreaksLoaded()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextColored(warn, "The server answered in a form this version of the game cannot read: the game and the server are on different versions. Reload the page (or update the game)."); ImGui::PopTextWrapPos(); }
                else if (!server.RecordBreaksLoaded()) ImGui::TextDisabled("Loading the records...");
                else if (server.RecordBreaks().empty()) ImGui::TextDisabled("No record has been broken yet.");
                for (std::size_t i = 0; i < server.RecordBreaks().size(); ++i) {
                    const Protocol3::RecordBreakEntry& b = server.RecordBreaks()[i];
                    ImGui::PushID(2000 + static_cast<int>(i));
                    char time[32], before[32], when[48];
                    formatMs(b.ms, time, sizeof time);
                    formatMs(b.previousMs, before, sizeof before);
                    formatDate(b.savedAt, when, sizeof when);
                    ImGui::Text("%s", b.track.c_str());
                    if (b.lap) ImGui::TextDisabled("%s   Fastest lap, %s", when, Authority::CraftName(b.craft));
                    else ImGui::TextDisabled("%s   Fastest race, %d lap%s, %s", when, b.raceLaps, b.raceLaps == 1 ? "" : "s", Authority::CraftName(b.craft));
                    ImGui::PushTextWrapPos(0.0f);
                    if (b.previousMs == 0) ImGui::Text("%s  %s   set the first record", b.name.empty() ? "player" : b.name.c_str(), time);
                    else ImGui::Text("%s  %s   beat   %s  %s", b.name.empty() ? "player" : b.name.c_str(), time, b.previousName.empty() ? "player" : b.previousName.c_str(), before);
                    ImGui::PopTextWrapPos();
                    const bool busy = downloadId != 0;
                    if (HoverNetButton(busy && downloadId == b.replay ? "DOWNLOADING..." : "WATCH", ImVec2(-FLT_MIN, 34)) && !busy) {
                        watchReplay(b.replay, b.slot, b.lap ? "the fastest lap" : "the record holder", b.track, b.name);
                    }
                    ImGui::PopID();
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                }
                ImGui::EndTabItem();
            }
            // ---- all the records, per craft
            if (ImGui::BeginTabItem("ALL RECORDS", nullptr, wantTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::TextDisabled("The best times of everyone who races on this server, for each craft.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (Tracks().Count() > 0 && TrackSelector("##Track", 3, track)) { need = true; laps = 0; }   // any track: the official ones first, then the community's, with a search box
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
                            watchReplay(e.replay, e.slot, laps == 0 ? "the fastest lap" : "the record holder", Tracks().Get(track)->mName, e.name);
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
                        watchReplay(w.replay, w.slot, "the winner", w.track, w.name);
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
                ImGui::TextDisabled("Win a race: 3 points. Only races where two or more players on different devices finished count. It starts again on the 1st of every month (UTC).");
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
        if (HoverNetQuietButton("Back", ImVec2(-FLT_MIN, 40))) running = false;
        {   // the track of a replay being watched is downloaded first when it is a community track this computer does not have
            const DownloadUi::State trackState = trackDownload.Draw(0);
            if (trackState == DownloadUi::State::eDone && pendingWatch.id != 0) { const std::uint64_t id = pendingWatch.id; pendingWatch.id = 0; downloadId = id; downloadSlot = pendingWatch.slot; downloadNote = pendingWatch.note; server.FetchReplay(id); }
            else if (trackState == DownloadUi::State::eFailed) { pendingWatch.id = 0; downloadId = 0; }
        }
        ImGui::End();
        HN_SCREEN_FADE();
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
#ifdef HOVERNET_WEB
// Before the main menu, once per visit: a player who is not signed in with Outiva chooses to sign in or to play as a guest. Signing in matters from the start because a
// registered player's Garage crafts can be kept on their account (Garage > Outiva account), and their Outiva callsign is their name. A signed-in player never sees this; a
// page with no account service (or accounts switched off) never shows it either. A guest races as "name (nonreg)" (GuestName.h).
void RunSignInGate(SDL2GraphicsBackend& graphics)
{
    hovernet_js_account_check();
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
    const Uint32 started = SDL_GetTicks();
    bool running = true, focusFirst = true;
#ifdef HOVERNET_HAVE_RMLUI
    // The RmlUi version of this screen (docs/rmlui.md): the same states, drawn with a card; if it cannot load, the ImGui one below runs.
    std::unique_ptr<HoverNetRml::FormScreen> rml;
    if (UseRmlUi(-1)) {
        rml.reset(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "SIGN IN", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight, "signin.rml"));
        if (!rml->Ok()) rml.reset();
    }
#endif
    while (running) {
        const int state = hovernet_js_account_state();
        if (state == 1) {   // signed in: the Outiva callsign is the name
            char callsign[64] = {};
            hovernet_js_account_callsign(callsign, sizeof callsign);
            const std::string who = callsign;
            if (!who.empty() && Protocol3::IsValidText(who, 32)) { SaveUsername(who); std::printf("ACCOUNT signed in as %s\n", who.c_str()); std::fflush(stdout); }
            gAccountsOffered = true; gSignedIn = true;
            break;
        }
        if (state == 2 || (state == -1 && SDL_GetTicks() - started > 4000)) break;   // no accounts here, or no answer: carry on as before
        if (state == 0) gAccountsOffered = true;
#ifdef HOVERNET_HAVE_RMLUI
        if (rml) {
            rml->SetClass("card", "hide", state != 0);
            std::vector<HoverNetRml::FormEvent> events;
            const HoverNetRml::FormOutcome outcome = rml->Frame(events, 1, -1);
            if (outcome == HoverNetRml::FormOutcome::eClose) { g_QuitConfirmed = true; running = false; }
            for (const HoverNetRml::FormEvent& e : events) {
                if (e.id == "signin") hovernet_js_open_signin();
                else if (e.id == "guest") running = false;
            }
            continue;
        }
#endif
        SDL_Event event;
        while (SDL_PollEvent(&event)) { MenuProcessEvent(event); }
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::Begin("HoverNet Sign In", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        DrawHoverNetScreenBackdrop(mainViewport);
        if (state == 0) {
            const float cardWidth = std::min(520.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - cardWidth) * 0.5f);
            ImGui::SetCursorPosY(std::max(20.0f, (ImGui::GetWindowHeight() - 330.0f) * 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetSurface);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
            MenuBeginChild("SignInCard", ImVec2(cardWidth, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            HoverNetSectionHeading("WELCOME TO HOVERNET");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Sign in to race under your Outiva name and keep your Garage crafts on your account.");
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (focusFirst) { ImGui::SetKeyboardFocusHere(); focusFirst = false; }
            if (HoverNetButton("SIGN IN WITH OUTIVA", ImVec2(-FLT_MIN, 56.0f))) hovernet_js_open_signin();
            ImGui::Spacing();
            if (HoverNetQuietButton("PLAY AS A GUEST", ImVec2(-FLT_MIN, 46.0f))) running = false;
            HoverNetHint("Guests race as \"name (nonreg)\".");
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::End();
        HN_SCREEN_FADE();
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
#endif

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
    if (const char* lapsHook = std::getenv("HOVERNET_TEST_HOST_LAPS")) laps = std::max(1, std::min(10, std::atoi(lapsHook)));   // test hook: as if the player had moved the slider
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
    bool trackDownloadFailed = false;   // the download at the start of a race did not finish: do not try again, start and say what is missing
    int framesShown = 0;
    // Test hook: with HOVERNET_TEST_AUTH_SERVER=host:port the screen hosts a one-player race by itself once connected; never set in normal play.
    int testStep = testServer != nullptr ? 2 : 0;

    // Test hook (scripts/test-online-screen-clicks.sh): real SDL mouse events are pushed at the on-screen position of the HOST A RACE button and
    // the dialog's create button, so the test proves they answer an actual click (HOVERNET_TEST_AUTH_CLICKS=host:port); never set in normal play.
    int clickStep = 0, clickFrame = 0, clickHold = 0;
    ImVec2 connectCenter(-1, -1), hostOpenCenter(-1, -1), createCenter(-1, -1), startRaceCenter(-1, -1);
    std::vector<TrackPreview> previews(static_cast<std::size_t>(Tracks().Count()));
    bool hostPopupOpen = false, requestHostPopup = false, privatePopupOpen = false, requestPrivatePopup = false;
    // A race on a community track the player does not have yet: the track is downloaded first (a few hundred kilobytes, checked against the manifest), then
    // the thing the player asked for is done. The same download runs again at the moment a race is about to start if the track is still missing (a private
    // race, whose track is only known once it is joined).
    DownloadUi trackDownload;
    enum class AfterDownload { eNone, eHost, eJoin, eWatch } afterDownload = AfterDownload::eNone;
    std::uint32_t afterDownloadRace = 0;
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
    // Join or watch a listed race. If its track is a community track this computer does not have, download it first and then do it.
    auto trackToFetch = [&](const std::string& trackName) -> const TrackEntry* {
        const TrackEntry* entry = Tracks().Get(Tracks().Find(trackName));
        return (entry != nullptr && !entry->mInstalled && entry->mHasDownload && !trackDownload.Active()) ? entry : nullptr;
    };
    auto joinListedRace = [&](const Protocol3::RaceInfo& race) {
        if (const TrackEntry* fetch = trackToFetch(race.track)) { afterDownload = AfterDownload::eJoin; afterDownloadRace = race.id; trackDownload.Begin({*fetch}, 0); }
        else lobby.Join(race.id, "");
    };
    auto watchListedRace = [&](const Protocol3::RaceInfo& race) {
        if (const TrackEntry* fetch = trackToFetch(race.track)) { afterDownload = AfterDownload::eWatch; afterDownloadRace = race.id; trackDownload.Begin({*fetch}, 0); }
        else lobby.WatchWhenStarted(race.id, "");
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

#ifdef HOVERNET_HAVE_RMLUI
    // The RmlUi version of this screen (docs/rmlui.md): the logic of the loop below is shared; only the drawing and the input differ. Not used by the tests that click the ImGui
    // widgets with real mouse events (HOVERNET_TEST_AUTH_CLICKS, HOVERNET_TEST_AUTH_STARTCLICK); if it cannot load, the ImGui screen runs.
    std::unique_ptr<HoverNetRml::FormScreen> rml;
    if (UseRmlUi(pFrameLimit) && clickTest == nullptr && std::getenv("HOVERNET_TEST_AUTH_STARTCLICK") == nullptr) {
        rml.reset(new HoverNetRml::FormScreen(
            graphics.GetWindow(), graphics.GetRenderer(), SourcePath("NetTarget/LinuxClient/assets"), "HOVERNET LOBBY", LoadReducedMotion(),
            [](HoverNetRml::Sound sound) { PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack); }, kWidth, kHeight, "lobby.rml"));
        if (!rml->Ok()) rml.reset();
    }
    bool rHostOpen = false, rPrivOpen = false;
    std::string rHostFilter, rSigEntry, rSigTop, rSigUsers, rSigChat, rSigStatus, rSigHost;
    bool rFocusedName = false;
    char rHostLapsText[8] = {0};
    auto E = [](const std::string& text) { return HoverNetRml::FormScreen::Escape(text); };
    auto trackLabelOf = [](int index) { const TrackEntry* e = Tracks().Get(index); return e == nullptr ? std::string() : e->mName + (e->mOfficial ? "" : "  -  community") + (e->mInstalled ? "" : "  (download)"); };
    if (rml) {
        rml->SetSideImage([&](SDL_Rect& crop) -> SDL_Texture* {
            int trackIndex = -1;
            if (rHostOpen) trackIndex = prefs.mTrackIndex;
            else if (lobby.GetState() == OnlineLobby::State::eWaiting) trackIndex = FindHostableTrack(lobby.Ack().track);
            else for (const auto& r : lobby.Races()) if (r.id == selectedRaceId) trackIndex = FindHostableTrack(r.track);
            if (trackIndex < 0 || trackIndex >= static_cast<int>(previews.size())) return nullptr;
            TrackPreview& preview = previews[static_cast<std::size_t>(trackIndex)];
            if (!preview.mLoaded) preview = LoadTrackPreview(graphics.GetRenderer(), TrackNameAt(trackIndex).c_str());
            if (preview.mTexture == nullptr) return nullptr;
            crop = SDL_Rect{0, 0, preview.mWidth, preview.mHeight};
            return preview.mTexture;
        });
    }
#endif
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
#ifdef HOVERNET_HAVE_RMLUI
        while (!rml && SDL_PollEvent(&event)) {   // (the RmlUi screen reads its own events)
#else
        while (SDL_PollEvent(&event)) {
#endif
            MenuProcessEvent(event);
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
        if (lobby.GetState() == OnlineLobby::State::eReady) {
            // The race is about to start: its track must be on this computer. A community track that is not is downloaded now (the server's first ticks wait in
            // the backlog); if that fails the race still starts and says what is missing.
            const TrackEntry* readyTrack = Tracks().Get(Tracks().Find(lobby.Ack().track));
            if (readyTrack != nullptr && !readyTrack->mInstalled && readyTrack->mHasDownload && !trackDownloadFailed) {
                if (!trackDownload.Active()) trackDownload.Begin({*readyTrack}, 0);
            }
            else if (!trackDownload.Active()) { ready = true; break; }
        }
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
            else if (clickStep == 3 && hostPopupOpen && clickFrame > 8) { clickStep = 4; clickHold = 0; clickFrame = 0; std::printf("CLICKTEST host dialog opened by a click\n"); std::printf("CLICKTEST host dialog laps %d\n", laps); }
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

#ifdef HOVERNET_HAVE_RMLUI
        if (rml) {
            using HoverNetRml::FormScreen;
            const OnlineLobby::State rState = lobby.GetState();
            const bool rWaiting = rState == OnlineLobby::State::eWaiting;
            const bool rConnected = rState == OnlineLobby::State::eBrowsing || rWaiting;
            const auto& rRaces = lobby.Races();
            auto pickedRace = [&]() -> const Protocol3::RaceInfo* { for (const auto& r : rRaces) if (r.id == selectedRaceId) return &r; return nullptr; };
            if (!rWaiting) hostedCode.clear();
            auto closeDialogs = [&]() { if (rHostOpen || rPrivOpen) { rHostOpen = rPrivOpen = false; rml->SetClass("dialog", "hide", true); rml->SetHtml("dialog", ""); rml->SetSideTarget("side"); } };
            auto trimmed = [](std::string text) { while (!text.empty() && text.back() == ' ') text.pop_back(); while (!text.empty() && text.front() == ' ') text.erase(text.begin()); return text; };
            auto submitName = [&]() {
                const std::string typed = trimmed(nameBuffer);
                if (!typed.empty() && Protocol3::IsValidText(typed, 32)) { name = typed; SaveUsername(name); nameKnown = true; problem.clear(); }
                else problem = "Type a name (1 to 32 letters, no control characters).";
            };
            auto createRace = [&]() {
                const TrackEntry* hostTrack = Tracks().Get(prefs.mTrackIndex);
                if (hostTrack == nullptr) return;
                lobby.Host(hostTrack->mName, players, laps, weapons, hostCode);
                hostedCode = hostCode;
                prefs.mLaps = laps; prefs.mWeapons = weapons;
                SaveLocalRacePrefs(prefs);
                closeDialogs();
            };
            auto fillHostList = [&]() {
                std::string needle = rHostFilter;
                for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::string html;
                int shown = 0, matches = 0;
                for (int i = 0; i < Tracks().Count(); ++i) {
                    std::string label = trackLabelOf(i), lower = label;
                    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (!needle.empty() && lower.find(needle) == std::string::npos) continue;
                    ++matches;
                    if (shown >= 40) continue;
                    ++shown;
                    html += "<button class=\"item" + std::string(i == prefs.mTrackIndex ? " cur" : "") + "\" data-id=\"ht\" data-index=\"" + std::to_string(i) + "\">" + E(label) + "</button>";
                }
                if (matches > shown) html += "<p class=\"note\">" + std::to_string(matches - shown) + " more: keep typing to narrow the list</p>";
                if (matches == 0) html += "<p class=\"note\">Nothing matches that</p>";
                rml->SetHtml("hlist", html);
            };
            auto refreshHost = [&]() {   // the parts of the Host dialog that follow the chosen track and the code
                const TrackEntry* hostTrack = Tracks().Get(prefs.mTrackIndex);
                const TrackBriefing guide = BriefingFor(prefs.mTrackIndex);
                std::string brief = "<p class=\"cardhead\">Course briefing</p><p class=\"note\">Style: " + E(guide.mCharacter) + "     Difficulty: " + E(guide.mDifficulty) + "</p><p class=\"note\">" + E(guide.mDescription) + "</p>";
                const bool needsDownload = hostTrack != nullptr && !hostTrack->mInstalled && hostTrack->mHasDownload;
                if (hostTrack != nullptr && !hostTrack->mOfficial) {
                    if (needsDownload) brief += "<p class=\"accent\">Not on this computer yet - it downloads (" + E(FormatMegabytes(hostTrack->mDownloadBytes)) + ") when you create the race.</p>";
                    brief += "<p class=\"accent\">A server hosts a community track only if its owner has installed it; if this one has not, it says so.</p>";
                }
                brief += "<p class=\"note\">Up to 8 players can join. You start the race when you are ready, with whoever is there (alone, it is a time trial).</p>";
                rml->SetHtml("hbrief", brief);
                rml->SetHtml("hcur", E(trackLabelOf(prefs.mTrackIndex)));
                rml->SetHtml("hlapshint", guide.mFreePlay ? "Not used on a free-play track" : "Suggested: " + std::to_string(guide.mSuggestedLaps));
                const bool codeOk = PrivateCodeOk(hostCode);
                rml->SetHtml("hwarn", codeOk ? std::string() : "<p class=\"accent\">A private code needs 6 to 16 characters.</p>");
                rml->SetHtml("hcreate", E(needsDownload ? "Download & Create" : (hostCode[0] != '\0' ? "Create Private Race" : "Create Race")));
                rml->SetClass("hcreate", "disabled", !codeOk || hostTrack == nullptr || trackDownload.Active());
            };
            auto openHost = [&]() {
                closeDialogs();
                rHostOpen = true; problem.clear();
                std::snprintf(rHostLapsText, sizeof rHostLapsText, "%d", laps);
                std::string html = "<div id=\"dialogbox\"><p class=\"cardhead\">Host a race</p><div id=\"dleft\"><p class=\"label\">Track</p><div class=\"trackcur\" id=\"hcur\"></div>"
                    "<input type=\"text\" class=\"text\" id=\"hq\" data-id=\"hq\" value=\"\"/><div id=\"hlist\"></div><div id=\"hbrief\"></div>"
                    "<p class=\"label\">Laps<span class=\"val\" id=\"hlapsval\">" + std::string(rHostLapsText) + "</span> <span class=\"note\" id=\"hlapshint\"></span></p>"
                    "<input type=\"range\" class=\"range\" id=\"hlaps\" data-id=\"hlaps\" min=\"1\" max=\"10\" step=\"1\" value=\"" + std::to_string(laps) + "\"/>"
                    "<div class=\"toggle" + std::string(weapons ? " on" : "") + "\" id=\"hweapons\" data-id=\"hweapons\"><span class=\"box\"></span><span class=\"tlabel\">Weapons enabled</span></div>"
                    "<p class=\"label\">Private code <span class=\"note\">6 to 16 characters; empty for a public race</span></p><input type=\"text\" class=\"text\" id=\"hcode\" data-id=\"hcode\" value=\"" + E(hostCode) + "\"/>"
                    "<div id=\"hwarn\"></div></div><div id=\"dright\"><div id=\"dside\"></div></div>"
                    "<div id=\"dbuttons\"><button class=\"act big\" id=\"hcreate\" data-id=\"hcreate\">Create Race</button><button class=\"act quiet back\" data-id=\"hcancel\">Cancel</button></div></div>";
                rml->SetHtml("dialog", html);
                rml->SetClass("dialog", "hide", false);
                rml->SetSideTarget("dside");
                fillHostList();
                refreshHost();
            };
            auto openPrivate = [&]() {
                closeDialogs();
                rPrivOpen = true; problem.clear();
                rml->SetHtml("dialog", "<div id=\"dialogbox\" class=\"small\"><p class=\"cardhead\">Join a private race</p><p class=\"note\">Private races are not listed. Type the race number and the code your friend gave you.</p>"
                    "<p class=\"label\">Race number</p><input type=\"text\" class=\"text\" id=\"pn\" data-id=\"pn\" value=\"\"/><p class=\"label\">Code</p><input type=\"text\" class=\"text\" id=\"pc\" data-id=\"pc\" value=\"\"/>"
                    "<div id=\"dbuttons\"><button class=\"act disabled\" id=\"pjoin\" data-id=\"pjoin\" style=\"width:100dp\">Join</button><button class=\"act quiet disabled\" id=\"pwatch\" data-id=\"pwatch\" style=\"width:100dp\">Watch</button><button class=\"act quiet back\" data-id=\"pcancel\" style=\"width:100dp\">Cancel</button></div></div>");
                rml->SetClass("dialog", "hide", false);
                raceNumber[0] = joinCode[0] = '\0';
            };
            if (requestHostPopup) { requestHostPopup = false; openHost(); }   // (the screenshot hook asks through these two)
            if (requestPrivatePopup) { requestPrivatePopup = false; openPrivate(); }
            HoverNetRml::FormOutcome rOutcome;
            std::vector<HoverNetRml::FormEvent> rEvents;
            rOutcome = rml->Frame(rEvents, framesShown, std::getenv("HOVERNET_TEST_RML") != nullptr ? -1 : pFrameLimit);
            if (rOutcome == HoverNetRml::FormOutcome::eClose) { running = false; g_QuitConfirmed = true; }
            else if (rOutcome == HoverNetRml::FormOutcome::eCancel || rOutcome == HoverNetRml::FormOutcome::eSave) {
                if (rHostOpen || rPrivOpen) { if (!trackDownload.Active()) closeDialogs(); }
                else if (rWaiting) lobby.Cancel();
                else { if (rConnected) lobby.Close(); running = false; }
            }
            for (const HoverNetRml::FormEvent& e : rEvents) {
                if (e.id == "name") { std::snprintf(nameBuffer, 33, "%s", e.text.c_str()); }
                else if (e.id == "name#enter" || e.id == "entername") { std::snprintf(nameBuffer, 33, "%s", e.id == "name#enter" ? e.text.c_str() : nameBuffer); submitName(); }
                else if (e.id == "back") { if (rConnected) lobby.Close(); running = false; }
                else if (e.id == "retry") { showCard = false; autoPending = true; autoAttempt = false; connectingFrames = 0; problem.clear(); lobby.Close(); }
                else if (e.id == "official") { settings = AuthServerSettings(); AuthServerSettings official; SaveAuthServerSettings(official); showCard = false; autoPending = true; autoAttempt = false; connectingFrames = 0; problem.clear(); lobby.Close(); }
                else if (e.id == "race") { if (e.index >= 0 && e.index < static_cast<int>(rRaces.size())) selectedRaceId = rRaces[static_cast<std::size_t>(e.index)].id; }
                else if (e.id == "join") { if (const Protocol3::RaceInfo* r = pickedRace()) if (!r->started && r->joined < r->players) joinListedRace(*r); }
                else if (e.id == "watch") { if (const Protocol3::RaceInfo* r = pickedRace()) watchListedRace(*r); }
                else if (e.id == "cancelwatch") lobby.CancelQueuedWatch();
                else if (e.id == "host") openHost();
                else if (e.id == "private") openPrivate();
                else if (e.id == "start") lobby.StartRace();
                else if (e.id == "leave") lobby.Cancel();
                else if (e.id == "chat") std::snprintf(chatInput, sizeof chatInput, "%s", e.text.c_str());
                else if (e.id == "chat#enter" || e.id == "send") {
                    if (e.id == "chat#enter") std::snprintf(chatInput, sizeof chatInput, "%s", e.text.c_str());
                    if (chatInput[0] != '\0') { lobby.SendChat(chatInput); chatInput[0] = '\0'; rml->SetInput("chatinput", ""); }
                    rml->FocusElement("chatinput");
                }
                else if (e.id == "hq") { rHostFilter = e.text; fillHostList(); }
                else if (e.id == "ht") { if (e.index >= 0 && e.index < Tracks().Count()) { prefs.mTrackIndex = e.index; fillHostList(); refreshHost(); } }
                else if (e.id == "hlaps") { laps = std::max(1, std::min(10, static_cast<int>(e.value + 0.5f))); rml->SetHtml("hlapsval", std::to_string(laps)); }
                else if (e.id == "hweapons") weapons = e.on;
                else if (e.id == "hcode") { std::snprintf(hostCode, sizeof hostCode, "%s", e.text.c_str()); refreshHost(); }
                else if (e.id == "hcancel") { if (!trackDownload.Active()) closeDialogs(); }
                else if (e.id == "hcreate") {
                    const TrackEntry* hostTrack = Tracks().Get(prefs.mTrackIndex);
                    if (hostTrack != nullptr && PrivateCodeOk(hostCode) && !trackDownload.Active()) {
                        if (!hostTrack->mInstalled && hostTrack->mHasDownload) { afterDownload = AfterDownload::eHost; trackDownload.Begin({*hostTrack}, 1); }
                        else createRace();
                    }
                }
                else if (e.id == "pn") { std::snprintf(raceNumber, sizeof raceNumber, "%s", e.text.c_str()); }
                else if (e.id == "pc") { std::snprintf(joinCode, sizeof joinCode, "%s", e.text.c_str()); }
                else if (e.id == "pjoin" || e.id == "pwatch") {
                    const std::uint32_t number = static_cast<std::uint32_t>(std::strtoul(raceNumber, nullptr, 10));
                    if (number != 0 && joinCode[0] != '\0') { if (e.id == "pjoin") lobby.Join(number, joinCode); else lobby.Spectate(number, joinCode); closeDialogs(); }
                }
                else if (e.id == "pcancel") closeDialogs();
                if (rPrivOpen && (e.id == "pn" || e.id == "pc")) {
                    const bool valid = std::strtoul(raceNumber, nullptr, 10) != 0 && joinCode[0] != '\0';
                    rml->SetClass("pjoin", "disabled", !valid); rml->SetClass("pwatch", "disabled", !valid);
                }
            }
            // a track download (a listed race, the host dialog's track, or the race about to start) is shown over the screen
            if (TrackDownloadJob* job = trackDownload.Job()) {
                if (!job->Finished()) {
                    const long long totalBytes = std::max<long long>(1, job->TotalBytes());
                    rml->SetBusy("Downloading track", job->CurrentName() + "   " + FormatMegabytes(job->BytesDone()) + " of " + FormatMegabytes(totalBytes), std::min(1.0f, static_cast<float>(job->BytesDone()) / static_cast<float>(totalBytes)), "");
                }
                else {
                    Tracks().Refresh();
                    const bool ok = job->Succeeded();
                    const std::string error = job->Error();
                    trackDownload.Reset();
                    rml->SetBusy("", "", -1.0f, "");
                    for (TrackPreview& preview : previews) if (preview.mTexture != nullptr && !preview.mLoaded) { SDL_DestroyTexture(preview.mTexture); preview = TrackPreview(); }
                    const AfterDownload what = afterDownload;
                    afterDownload = AfterDownload::eNone;
                    if (ok) {
                        if (what == AfterDownload::eJoin) lobby.Join(afterDownloadRace, "");
                        else if (what == AfterDownload::eWatch) lobby.WatchWhenStarted(afterDownloadRace, "");
                        else if (what == AfterDownload::eHost) createRace();
                    }
                    else { if (what == AfterDownload::eNone) trackDownloadFailed = true; else problem = "The track download did not finish: " + error; }
                    if (rHostOpen) refreshHost();
                }
            }
            if (rHostOpen) rml->SetClass("hcreate", "disabled", !PrivateCodeOk(hostCode) || trackDownload.Active());
            // ---- what is on the screen
            rml->SetClass("entry", "hide", rConnected);
            rml->SetClass("grid", "hide", !rConnected);
            {
                const std::string status = rConnected ? (settings.custom ? "Connected to " + settings.address : std::string("Connected to the HoverNet server")) + " as " + name : std::string();
                if (status != rSigStatus) { rSigStatus = status; rml->SetHtml("status", E(status)); }
            }
            if (!rConnected) {
                const std::string sig = std::to_string(nameKnown) + std::to_string(showCard) + std::to_string(static_cast<int>(rState)) + lobby.Message() + problem;
                if (sig != rSigEntry) {
                    rSigEntry = sig;
                    std::string html;
                    const std::string err = problem.empty() ? std::string() : "<p class=\"accent\">" + E(problem) + "</p>";
                    if (!nameKnown) {
                        html = "<p class=\"cardhead\">WELCOME, RACER</p><p class=\"note\">Choose the name other drivers will see in the lobby.</p><input type=\"text\" class=\"text\" id=\"namein\" data-id=\"name\" value=\"" + E(nameBuffer) + "\"/>" + err +
                               "<button class=\"act big\" data-id=\"entername\">ENTER THE LOBBY</button><button class=\"act quiet back\" data-id=\"back\">Back to Menu</button>";
                    }
                    else if (showCard) {
                        html = "<p class=\"cardhead\">CAN'T REACH THE SERVER</p><p class=\"note\">This is the HoverNet server you chose in Settings. Check it there, or go back to the official HoverNet server.</p>";
                        if (rState == OnlineLobby::State::eFailed || (rState == OnlineLobby::State::eIdle && !lobby.Message().empty())) html += "<p class=\"accent\">" + E(lobby.Message()) + "</p>";
                        html += err + "<button class=\"act big\" data-id=\"retry\">TRY AGAIN</button><button class=\"act quiet\" data-id=\"official\">Use the official HoverNet server</button><button class=\"act quiet back\" data-id=\"back\">Back to Menu</button>";
                    }
                    else html = "<p class=\"cardhead\">CONNECTING</p><p class=\"note\">Looking for the HoverNet server...</p><button class=\"act quiet back\" data-id=\"back\">Back to Menu</button>";
                    rml->SetHtml("entrycard", html);
                    if (!nameKnown && !rFocusedName) { rFocusedName = true; rml->FocusElement("namein"); }
                }
            }
            else {
                const Protocol3::RaceInfo* picked = pickedRace();
                if (picked == nullptr && !rWaiting && !rRaces.empty() && std::getenv("HOVERNET_TEST_AUTH_SELECT") != nullptr) { selectedRaceId = rRaces[0].id; picked = &rRaces[0]; }
                rml->SetClass("listpanel", "hide", rWaiting);
                rml->SetClass("userspanel", "hide", rWaiting);
                // the top row: list, details, actions
                std::string sig = std::to_string(static_cast<int>(rState)) + "|" + std::to_string(selectedRaceId) + "|" + lobby.Notice() + "|" + problem + "|" + std::to_string(lobby.QueuedWatchId()) + "|" + hostedCode + "|" + std::to_string(trackDownload.Active());
                for (const auto& r : rRaces) sig += "|" + std::to_string(r.id) + ":" + std::to_string(r.joined) + (r.started ? "s" : "w");
                if (rWaiting) { sig += "|" + lobby.Ack().track + std::to_string(lobby.Ack().laps) + std::to_string(lobby.Ack().raceId); for (const auto& m : lobby.GetRoster().members) sig += "|" + std::to_string(m.first) + m.second; }
                if (sig != rSigTop) {
                    rSigTop = sig;
                    auto infoRow = [&](const std::string& label, const std::string& value) { return "<p class=\"irow\"><span class=\"ilabel\">" + E(label) + "</span>" + E(value) + "</p>"; };
                    std::string races, details, actions;
                    if (!rWaiting) {
                        if (rRaces.empty()) races = "<p class=\"note\">No open races - host one to get started</p>";
                        for (std::size_t i = 0; i < rRaces.size(); ++i) {
                            const auto& r = rRaces[i];
                            races += "<button class=\"item" + std::string(r.id == selectedRaceId ? " cur" : "") + "\" data-id=\"race\" data-index=\"" + std::to_string(i) + "\">" + E(r.track + "  (" + std::to_string(r.joined) + "/" + std::to_string(r.players) + ")" + (r.started ? "  (racing)" : "")) + "</button>";
                        }
                    }
                    rml->SetHtml("detailshead", rWaiting ? "Your race" : "Game details");
                    rml->SetClass("detailsbody", "nomap", false);
                    if (rWaiting) {
                        const Protocol3::HelloAck& ack = lobby.Ack();
                        const Protocol3::Roster& roster = lobby.GetRoster();
                        const TrackBriefing briefing = BriefingFor(FindHostableTrack(ack.track));
                        details += infoRow("Track:", ack.track.empty() ? "-" : ack.track);
                        if (!ack.track.empty()) {
                            details += infoRow("Laps:", briefing.mFreePlay ? "none (free play)" : std::to_string(ack.laps));
                            details += infoRow("Weapons:", ack.weapons ? "On" : "Off");
                            if (ack.raceId != 0) details += infoRow("Race:", "number " + std::to_string(ack.raceId));
                        }
                        if (ack.raceId != 0 && !hostedCode.empty()) details += "<p class=\"accent\">Private race: tell your friends number " + std::to_string(ack.raceId) + " and code " + E(hostedCode) + ". They type both under \"Join a private race\".</p>";
                        details += "<p class=\"cardhead\">Players (" + std::to_string(roster.members.size()) + " / " + std::to_string(roster.seats != 0 ? static_cast<int>(roster.seats) : static_cast<int>(ack.players)) + ")</p>";
                        for (const auto& member : roster.members) details += "<p class=\"bullet\">" + E(member.second + (member.first == ack.player ? " (you)" : "") + (member.first == 0 ? " - host" : "")) + "</p>";
                        if (roster.members.empty()) details += "<p class=\"bullet\">" + E(name + " (you)" + (lobby.IsHost() ? " - host" : "")) + "</p>";
                        const bool alone = roster.members.size() < 2;
                        if (lobby.IsHost()) actions += "<button class=\"act big\" data-id=\"start\">Start Race</button><p class=\"note\">" + std::string(alone ? "Others can still join until you start." : "Start the race when you are ready.") + "</p>";
                        else actions += "<p class=\"note\">Waiting for the host to start the race...</p>";
                        actions += "<button class=\"act quiet\" data-id=\"leave\">Leave Race</button>";
                    }
                    else if (picked != nullptr) {
                        const int trackIndex = FindHostableTrack(picked->track);
                        const TrackBriefing briefing = BriefingFor(trackIndex);
                        details += infoRow("Track:", picked->track) + infoRow("Laps:", briefing.mFreePlay ? "none (free play)" : std::to_string(picked->laps)) + infoRow("Weapons:", picked->weapons ? "On" : "Off") +
                                   infoRow("Players:", std::to_string(picked->joined) + " / " + std::to_string(picked->players));
                        if (trackIndex >= 0) details += infoRow("Style:", briefing.mCharacter + " - " + briefing.mDifficulty);
                        details += infoRow("Status:", picked->started ? "Race in progress" : "Waiting for players") + infoRow("Race:", "number " + std::to_string(picked->id));
                        if (trackIndex >= 0 && !briefing.mDescription.empty()) details += "<p class=\"note\">" + E(briefing.mDescription) + "</p>";
                    }
                    else { details = "<p class=\"note\">Select a race in the list to see its track, laps, weapons and players, or host your own.</p>"; rml->SetClass("detailsbody", "nomap", true); }
                    if (!rWaiting) {
                        const bool canJoin = picked != nullptr && !picked->started && picked->joined < picked->players;
                        const TrackEntry* joinTrack = picked != nullptr ? Tracks().Get(Tracks().Find(picked->track)) : nullptr;
                        const bool needsDownload = joinTrack != nullptr && !joinTrack->mInstalled && joinTrack->mHasDownload;
                        actions += std::string("<button class=\"act big") + (canJoin ? "" : " disabled") + "\" data-id=\"join\">" + (needsDownload ? "Download &amp; Join" : "Join Game") + "</button>";
                        if (lobby.QueuedWatchId() != 0) actions += "<button class=\"act quiet\" data-id=\"cancelwatch\">Waiting for the race to start (click to cancel)</button>";
                        else actions += std::string("<button class=\"act quiet") + (picked != nullptr ? "" : " disabled") + "\" data-id=\"watch\">Watch Race</button>";
                        actions += "<div class=\"gap\"></div><button class=\"act big\" data-id=\"host\">HOST A RACE</button><button class=\"act quiet\" data-id=\"private\">Join a private race</button>";
                    }
                    if (!lobby.Notice().empty() || !problem.empty()) actions += "<div class=\"gap\"></div>" + (lobby.Notice().empty() ? std::string() : "<p class=\"accent\">" + E(lobby.Notice()) + "</p>") + (problem.empty() ? std::string() : "<p class=\"accent\">" + E(problem) + "</p>");
                    actions += "<div class=\"gap\"></div><button class=\"act quiet back\" data-id=\"back\">Back to Menu</button>";
#ifdef HOVERNET_WEB
                    if (rWaiting) actions += "<p class=\"note\">Keep this tab in front: your browser pauses a tab you cannot see.</p>";
#endif
                    if (!rWaiting) rml->SetHtml("racelist", races);
                    rml->SetHtml("info", details);
                    rml->SetHtml("actions", actions);
                }
                // who is here
                if (!rWaiting) {
                    std::string usersSig = std::to_string(lobby.UsersTotal());
                    for (const std::string& u : lobby.Users()) usersSig += "|" + u;
                    if (usersSig != rSigUsers) {
                        rSigUsers = usersSig;
                        rml->SetHtml("usershead", "In the lobby (" + std::to_string(lobby.UsersTotal() > 0 ? lobby.UsersTotal() : 1) + ")");
                        std::string html;
                        bool listedYou = false;
                        for (const std::string& user : lobby.Users()) { html += "<p class=\"bullet\">" + E(user) + (!listedYou && user == name ? " (you)" : "") + "</p>"; if (user == name) listedYou = true; }
                        if (lobby.Users().empty()) html += "<p class=\"bullet\">" + E(name) + " (you)</p>";
                        if (lobby.UsersTotal() > static_cast<int>(lobby.Users().size())) html += "<p class=\"note\">+ " + std::to_string(lobby.UsersTotal() - static_cast<int>(lobby.Users().size())) + " more</p>";
                        rml->SetHtml("users", html);
                    }
                }
                // the chat
                const std::string chatSig = std::to_string(lobby.ChatLines().size()) + (rWaiting ? "w" : "l");
                if (chatSig != rSigChat) {
                    rSigChat = chatSig;
                    rml->SetHtml("chathead", rWaiting ? "Race chat" : "Lobby chat");
                    std::string html;
                    for (const std::string& line : lobby.ChatLines()) html += "<p class=\"chatline\">" + E(line) + "</p>";
                    rml->SetHtml("chatlines", html);
                    rml->ScrollToEnd("chatlines");
                }
                if (focusChat) { focusChat = false; rml->FocusElement("chatinput"); }   // (the loop above asks for it once the lobby is open)
            }
            continue;
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
            MenuBeginChild("EntryCard", ImVec2(cardWidth, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
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
                MenuBeginChild("GameListPanel", ImVec2(stacked ? 0.0f : listWidth, listHeight), true);
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
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !chosen.started && chosen.joined < chosen.players) joinListedRace(chosen);
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
            }

            MenuBeginChild("GameDetailsPanel", ImVec2(stacked ? 0.0f : detailsWidth, detailsHeight), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            HoverNetSectionHeading(waitingRoom ? "Your race" : "Game details");
            if (waitingRoom) {
                const Protocol3::HelloAck& ack = lobby.Ack();
                const Protocol3::Roster& roster = lobby.GetRoster();
                const int trackIndex = FindHostableTrack(ack.track);
                const TrackBriefing briefing = BriefingFor(trackIndex);
                // Race info and the map on the left, the player list on the right at full height, as in the classic waiting room.
                const float infoWidth = stacked ? 0.0f : std::max(250.0f, ImGui::GetContentRegionAvail().x * 0.50f);
                MenuBeginChild("RaceInfoColumn", stacked ? ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.55f) : ImVec2(infoWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
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
                    MenuBeginChild("RaceMapColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                    DrawTrackPreview(previewFor(trackIndex), 80.0f, false);
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                if (!stacked) ImGui::SameLine();
                MenuBeginChild("PlayersColumn", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
                ImGui::Text("Players (%d / %d)", static_cast<int>(roster.members.size()), roster.seats != 0 ? static_cast<int>(roster.seats) : static_cast<int>(ack.players));
                MenuBeginChild("PlayersListBox", ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y), true);
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
                MenuBeginChild("SelectedRaceMap", ImVec2(mapWidth, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
                DrawTrackPreview(trackIndex >= 0 ? previewFor(trackIndex) : TrackPreview(), 96.0f, false);
                ImGui::EndChild();
                ImGui::SameLine();
                MenuBeginChild("SelectedRaceInfo", ImVec2(0, infoHeight), false, ImGuiWindowFlags_NoScrollbar);
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
            MenuBeginChild("ActionsPanel", ImVec2(0, actionsHeight), true);
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
                const TrackEntry* joinTrack = selectedInfo != nullptr ? Tracks().Get(Tracks().Find(selectedInfo->track)) : nullptr;
                const bool joinNeedsDownload = joinTrack != nullptr && !joinTrack->mInstalled && joinTrack->mHasDownload;
                if (HoverNetButton(joinNeedsDownload ? "Download & Join" : "Join Game", ImVec2(-FLT_MIN, 40))) joinListedRace(*selectedInfo);
                ImGui::EndDisabled();
                // A race that has not started can be chosen too: the client waits in the list and starts watching when the race starts.
                const bool waitingToWatch = lobby.QueuedWatchId() != 0;
                ImGui::BeginDisabled(selectedInfo == nullptr && !waitingToWatch);
                if (waitingToWatch) {
                    if (HoverNetQuietButton("Waiting for the race to start (click to cancel)", ImVec2(-FLT_MIN, 0))) lobby.CancelQueuedWatch();
                }
                else if (HoverNetQuietButton("Watch Race", ImVec2(-FLT_MIN, 0))) watchListedRace(*selectedInfo);
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
                MenuBeginChild("UsersListPanel", ImVec2(tabbed ? 0.0f : listWidth, 0), true);
                char heading[48];
                std::snprintf(heading, sizeof(heading), "In the lobby (%d)", lobby.UsersTotal() > 0 ? lobby.UsersTotal() : 1);
                HoverNetSectionHeading(heading);
                MenuBeginChild("UsersListBox", ImVec2(-FLT_MIN, -FLT_MIN), true);
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
                MenuBeginChild("ChatPanel", ImVec2(0, 0), true);
                HoverNetSectionHeading(waitingRoom ? "Race chat" : "Lobby chat");
                const float inputRow = ImGui::GetFrameHeightWithSpacing() + 4.0f;
                MenuBeginChild("ChatLog", ImVec2(0, std::max(40.0f, ImGui::GetContentRegionAvail().y - inputRow)), true);
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
            MenuBeginChild("HostRaceOptions", ImVec2(controlsWidth, sideBySide ? selectorHeight : 330.0f), false);
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
            const bool trackOk = hostTrack != nullptr;
            const bool hostNeedsDownload = hostTrack != nullptr && !hostTrack->mInstalled && hostTrack->mHasDownload;
            if (hostTrack != nullptr && !hostTrack->mOfficial) {
                if (hostNeedsDownload) AccentNote("Not on this computer yet - it downloads (" + FormatMegabytes(hostTrack->mDownloadBytes) + ") when you create the race.");
                AccentNote("A server hosts a community track only if its owner has installed it; if this one has not, it says so.");
            }
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
            MenuSliderInt("##HostLaps", &laps, 1, 10);
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
                MenuBeginChild("HostTrackPreview", ImVec2(0, selectorHeight), false);
                DrawTrackPreview(previewFor(prefs.mTrackIndex));
                ImGui::EndChild();
            }
            else {
                ImGui::Spacing();
                MenuBeginChild("HostTrackPreview", ImVec2(0, 190.0f), false);
                DrawTrackPreview(previewFor(prefs.mTrackIndex));
                ImGui::EndChild();
            }
            auto createRace = [&]() {
                lobby.Host(hostTrack->mName, players, laps, weapons, hostCode);
                hostedCode = hostCode;
                prefs.mLaps = laps;
                prefs.mWeapons = weapons;
                SaveLocalRacePrefs(prefs);   // remembered: the next race starts with these laps, weapons and track (it was kept in memory only before, so the laps went back to 1)
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            };
            ImGui::BeginDisabled(!codeOk || !trackOk || trackDownload.Active());
            if (HoverNetButton(hostNeedsDownload ? "Download & Create" : (hostCode[0] != '\0' ? "Create Private Race" : "Create Race"), ImVec2(190, 36))) {
                if (hostNeedsDownload) { afterDownload = AfterDownload::eHost; trackDownload.Begin({*hostTrack}, 1); }
                else createRace();
            }
            createCenter = centreOfLastItem();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (HoverNetQuietButton("Cancel", ImVec2(120, 36)) && !trackDownload.Active()) { hostPopupOpen = false; ImGui::CloseCurrentPopup(); }
            {
                const DownloadUi::State downloadState = trackDownload.Draw(1);
                if (downloadState == DownloadUi::State::eDone && afterDownload == AfterDownload::eHost) { afterDownload = AfterDownload::eNone; createRace(); }
                else if (downloadState == DownloadUi::State::eFailed) afterDownload = AfterDownload::eNone;
            }
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

        {   // the track download of a listed race, or of the race that is about to start (scope 0)
            const DownloadUi::State downloadState = trackDownload.Draw(0);
            if (downloadState == DownloadUi::State::eDone) {
                const AfterDownload what = afterDownload;
                afterDownload = AfterDownload::eNone;
                if (what == AfterDownload::eJoin) lobby.Join(afterDownloadRace, "");
                else if (what == AfterDownload::eWatch) lobby.WatchWhenStarted(afterDownloadRace, "");
            }
            else if (downloadState == DownloadUi::State::eFailed) {
                if (afterDownload == AfterDownload::eNone) trackDownloadFailed = true;   // the one at the start of a race: start anyway, it says what is missing
                afterDownload = AfterDownload::eNone;
            }
        }

        ImGui::End();

        HN_SCREEN_FADE();
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
    eLocalPlay,       // Play Offline
    eOnlineLobby,     // Race Online
    eLeaderboards,
    eGarage,
    eSettings,        // the Settings hub: game settings, controls, replays, tutorial
    eWatchReplay,     // reached from the hub (and by the test hooks)
    eControls,
    eTutorial,
};

// The first player screen. A bounded/headless run still defaults to Local Play
// if no button is activated, preserving the automated gameplay contract.
// The object library, for the craft turning on the main menu (set by the caller before the menu opens; null = the menu keeps its picture).
MR_ResourceLib* gMenuCraftResources = nullptr;

MenuChoice RunMainMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                       const MR_Sprite& font, int pFrameLimit)
{
    StartMarketCatalogueRefresh(pFrameLimit >= 0);   // the Track Market's list of tracks, fetched in the background
    ApplyMarketCatalogueUpdate();
#ifdef HOVERNET_HAVE_RMLUI
    if (UseRmlUi(pFrameLimit) && std::getenv("HOVERNET_TEST_MENU") == nullptr) {   // the RmlUi version of this menu (docs/rmlui.md); anything it cannot do falls back to the menu below (the test hook that opens a screen straight from the menu uses the older menu)
        HoverNetRml::MainMenuInfo info;
        info.assetDir = SourcePath("NetTarget/LinuxClient/assets");
        info.version = HOVERNET_VERSION;
        HoverNetProfile::Profile profile;
        profile.Load(ProfilePath());
        if (profile.xp > 0 || profile.races > 0) { info.level = profile.Level(); info.levelTitle = HoverNetCraft::LevelTitle(profile.Level()); info.levelProgress = profile.LevelProgress(); }
        info.tutorialDone = HoverNetTutorial::HasCompleted(ConfigDirPath());
        info.reducedMotion = LoadReducedMotion();
        info.sound = [](HoverNetRml::Sound sound) {
            if (std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST sound %s\n", sound == HoverNetRml::Sound::eHover ? "hover" : sound == HoverNetRml::Sound::eClick ? "click" : "back"); std::fflush(stdout); }
            PlaySfx(sound == HoverNetRml::Sound::eHover ? Sfx::eHover : sound == HoverNetRml::Sound::eClick ? Sfx::eClick : Sfx::eBack);
        };
        // The player's own craft, as on the ImGui menu: the saved design from the Garage, drawn by the Garage's preview code into a texture that is redrawn as it turns.
        constexpr int kCraftW = 560, kCraftH = 420;
        SDL_Texture* rmlCraftTexture = nullptr;
        std::vector<std::uint32_t> rmlCraftPixels, rmlPalette(256, 0);
        Authority::CraftDesign rmlCraftDesign;
        bool rmlCraftReady = false;
        SDL_Rect rmlCrop = {kCraftW / 10, kCraftH * 22 / 100, kCraftW * 8 / 10, kCraftH * 64 / 100};   // the part of the craft picture to show (see craftFrame); these two live as long as the menu, which the lambda below outlives its block
        bool rmlCropKnown = false;
        int rmlCraftYaw = -1000;
        Uint32 rmlCraftBuilt = 0;
        if (gMenuCraftResources != nullptr && graphics.GetRenderPalette() != nullptr && std::getenv("HOVERNET_NO_MENU_CRAFT") == nullptr) {
            const MR_UInt8* palette768 = graphics.GetRenderPalette();
            for (int i = 0; i < 256; ++i) rmlPalette[i] = (static_cast<std::uint32_t>(palette768[i * 3]) << 16) | (static_cast<std::uint32_t>(palette768[i * 3 + 1]) << 8) | palette768[i * 3 + 2];
            MR_ResBitmap::SetColourPalette(palette768, 1.0f, false);
            MR_ResBitmap::SetBakePalette(palette768);
            const HoverNetCraft::CraftStore store(ConfigDirPath());
            int slot = store.Active();
            if (slot < 0) slot = 0;
            if (!store.Load(slot, rmlCraftDesign)) rmlCraftDesign = Authority::CraftDesign();
            rmlCraftDesign = Authority::SanitiseCraftDesign(rmlCraftDesign);
            if (const char* craftHook = std::getenv("HOVERNET_TEST_MENU_CRAFT")) rmlCraftDesign.craft = std::max(0, std::min(Authority::kCraftCount - 1, std::atoi(craftHook)));   // test hook: show this craft
            rmlCraftTexture = SDL_CreateTexture(graphics.GetRenderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kCraftW, kCraftH);
            if (rmlCraftTexture != nullptr) SDL_SetTextureBlendMode(rmlCraftTexture, SDL_BLENDMODE_BLEND);
        }
        if (rmlCraftTexture != nullptr) {
            // The part of the picture to show: the box that holds the craft at every angle it turns through (found once, by drawing it at twelve angles), with a margin. A fixed box
            // cut off the top of a craft with a tall fin or engine; this one fits whatever the player designed.
            info.craftFrame = [&](int yaw, SDL_Rect& crop) -> SDL_Texture* {
                const Uint32 now = SDL_GetTicks();
                if (!rmlCropKnown) {
                    rmlCropKnown = true;
                    int x0 = kCraftW, y0 = kCraftH, x1 = -1, y1 = -1;
                    for (int angle = 0; angle < 360; angle += 30) {
                        if (!HoverNetCraft::RenderCraftPreview(*gMenuCraftResources, rmlCraftDesign.IsPlain() ? nullptr : &rmlCraftDesign, rmlCraftDesign.craft, angle, rmlPalette.data(), kCraftW, kCraftH, rmlCraftPixels, true)) break;
                        for (int y = 0; y < kCraftH; ++y) for (int x = 0; x < kCraftW; ++x) if ((rmlCraftPixels[static_cast<std::size_t>(y) * kCraftW + x] >> 24) > 8) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
                    }
                    rmlCraftYaw = -1000;   // the picture drawn for the next frame is the one for its own angle
                    if (x1 > x0 && y1 > y0) {
                        const int margin = 10;
                        x0 = std::max(0, x0 - margin); y0 = std::max(0, y0 - margin); x1 = std::min(kCraftW - 1, x1 + margin); y1 = std::min(kCraftH - 1, y1 + margin);
                        rmlCrop = SDL_Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
                    }
                }
                if (yaw != rmlCraftYaw && (now - rmlCraftBuilt >= 40 || !rmlCraftReady)) {
                    rmlCraftReady = HoverNetCraft::RenderCraftPreview(*gMenuCraftResources, rmlCraftDesign.IsPlain() ? nullptr : &rmlCraftDesign, rmlCraftDesign.craft, yaw,
                                                                      rmlPalette.data(), kCraftW, kCraftH, rmlCraftPixels, true);
                    if (rmlCraftReady) SDL_UpdateTexture(rmlCraftTexture, nullptr, rmlCraftPixels.data(), kCraftW * 4);
                    rmlCraftYaw = yaw; rmlCraftBuilt = now;
                }
                crop = rmlCrop;
                return rmlCraftReady ? rmlCraftTexture : nullptr;
            };
        }
        const HoverNetRml::Choice picked = HoverNetRml::RunMainMenu(graphics.GetWindow(), graphics.GetRenderer(), info, pFrameLimit, kWidth, kHeight);
        if (rmlCraftTexture != nullptr) SDL_DestroyTexture(rmlCraftTexture);
        switch (picked) {
        case HoverNetRml::Choice::eOnline: return MenuChoice::eOnlineLobby;
        case HoverNetRml::Choice::eOffline: return MenuChoice::eLocalPlay;
        case HoverNetRml::Choice::eLeaderboards: return MenuChoice::eLeaderboards;
        case HoverNetRml::Choice::eGarage: return MenuChoice::eGarage;
        case HoverNetRml::Choice::eTutorial: return MenuChoice::eTutorial;
        case HoverNetRml::Choice::eSettings: return MenuChoice::eSettings;
        case HoverNetRml::Choice::eBack: return MenuChoice::eLocalPlay;   // as the ImGui menu: Esc / B leave with the default choice
        case HoverNetRml::Choice::eQuitRequested:
            if (ConfirmQuit(graphics, buffer, viewport, font)) { g_QuitConfirmed = true; return MenuChoice::eLocalPlay; }
            break;   // the player changed their mind: show the menu below instead
        case HoverNetRml::Choice::eNone: if (pFrameLimit >= 0) return MenuChoice::eLocalPlay; break;
        }
    }
#endif
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

    // The hovercraft banner picture; when it is missing the earlier hovercraft picture is used.
    SDL_Texture* menuBannerTexture = nullptr;
    float menuBannerAspect = 1.0f;
    {
        const std::string bannerPath = SourcePath("NetTarget/LinuxClient/assets/menu-banner.bmp");
        if (SDL_Surface* bannerSurface = SDL_LoadBMP(bannerPath.c_str())) {
            SDL_SetSurfaceBlendMode(bannerSurface, SDL_BLENDMODE_BLEND);
            menuBannerTexture = SDL_CreateTextureFromSurface(graphics.GetRenderer(), bannerSurface);
            if (menuBannerTexture != nullptr) {
                SDL_SetTextureBlendMode(menuBannerTexture, SDL_BLENDMODE_BLEND);
                menuBannerAspect = static_cast<float>(bannerSurface->w) / static_cast<float>(std::max(1, bannerSurface->h));
            }
            SDL_FreeSurface(bannerSurface);
        }
    }

    // The player's own craft, turning slowly on the menu with the design saved in the Garage (the same drawing as the Garage preview, on a clear background).
    constexpr int kMenuCraftW = 560, kMenuCraftH = 420;
    SDL_Texture* menuCraftTexture = nullptr;
    std::vector<std::uint32_t> menuCraftPixels, menuPaletteWords(256, 0);
    Authority::CraftDesign menuCraftDesign;
    bool menuCraftReady = false;
    int menuCraftShownYaw = -1000;
    Uint32 menuCraftLastBuild = 0;
    if (gMenuCraftResources != nullptr && graphics.GetRenderPalette() != nullptr && std::getenv("HOVERNET_NO_MENU_CRAFT") == nullptr) {
        const MR_UInt8* palette768 = graphics.GetRenderPalette();
        for (int i = 0; i < 256; ++i) menuPaletteWords[i] = (static_cast<std::uint32_t>(palette768[i * 3]) << 16) | (static_cast<std::uint32_t>(palette768[i * 3 + 1]) << 8) | palette768[i * 3 + 2];
        MR_ResBitmap::SetColourPalette(palette768, 1.0f, false);
        MR_ResBitmap::SetBakePalette(palette768);
        const HoverNetCraft::CraftStore store(ConfigDirPath());
        int slot = store.Active();
        if (slot < 0) slot = 0;
        if (!store.Load(slot, menuCraftDesign)) menuCraftDesign = Authority::CraftDesign();
        menuCraftDesign = Authority::SanitiseCraftDesign(menuCraftDesign);
        menuCraftTexture = SDL_CreateTexture(graphics.GetRenderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kMenuCraftW, kMenuCraftH);
        if (menuCraftTexture != nullptr) SDL_SetTextureBlendMode(menuCraftTexture, SDL_BLENDMODE_BLEND);
    }

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
    const bool tutorialDone = HoverNetTutorial::HasCompleted(ConfigDirPath());
    bool focusFirstButton = true;
    bool running = !pickAuthServerForTest && !pickReplaysForTest;
    int framesShown = 0;
    const bool reducedMotion = LoadReducedMotion();
    const Uint32 menuOpenedAt = SDL_GetTicks();   // the entrance animation counts from here
    // --frames bounds gameplay, not time spent idling at a menu with no human
    // input. One rendered frame verifies this screen in automation, then the
    // documented Local Play default continues into the actual bounded race.
    const int menuFrameLimit = pFrameLimit < 0 ? -1 : 1;

    while (running && (menuFrameLimit < 0 || framesShown < menuFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            MenuProcessEvent(event);
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
                                               : std::min(430.0f, workSize.x * (workSize.x < 820.0f ? 0.5f : 0.42f));   // a small window gives the panel more of its width: the grid's labels must fit
        const float panelHeight = phonePortrait ? std::min(workSize.y * 0.62f, 430.0f)
                                : phoneShort    ? workSize.y - 24.0f
                                                : std::min(580.0f, workSize.y - 84.0f);
        const ImVec2 panelPos = phonePortrait ? ImVec2(16.0f, workSize.y - panelHeight - 16.0f)
                              : phoneShort    ? ImVec2(workSize.x - panelWidth - 12.0f, 12.0f)
                                              : ImVec2(workSize.x - panelWidth - (workSize.x < 820.0f ? 20.0f : 42.0f), 42.0f);
        // Entrance: the craft eases in from the left, the title block settles down from above and the panel slides in from the right and fades up, one after another.
        const bool menuMotion = MenuMotionAllowed() && !reducedMotion;
        const float menuAge = static_cast<float>(SDL_GetTicks() - menuOpenedAt) / 1000.0f;
        const float tCraft = menuMotion ? EaseOutCubic(menuAge / 0.9f) : 1.0f;
        const float tBrand = menuMotion ? EaseOutCubic((menuAge - 0.08f) / 0.6f) : 1.0f;
        const float tPanel = menuMotion ? EaseOutCubic((menuAge - 0.15f) / 0.65f) : 1.0f;
        const float craftX = workPos.x + workSize.x * 0.30f;
        const float craftY = workPos.y + workSize.y * 0.68f;
        const float craftScale = std::min(workSize.x / 1200.0f, workSize.y / 760.0f);
        if (menuCraftTexture != nullptr) {
            // Turn the craft (a few degrees a second; still with reduced motion), redrawn only when the angle moved by two degrees.
            const int yawNow = reducedMotion ? 28 : static_cast<int>(SDL_GetTicks() / 1000.0 * 22.0) % 360 / 2 * 2;
            const Uint32 nowTicks = SDL_GetTicks();
            if (yawNow != menuCraftShownYaw && (nowTicks - menuCraftLastBuild >= 40 || !menuCraftReady)) {
                menuCraftReady = HoverNetCraft::RenderCraftPreview(*gMenuCraftResources, menuCraftDesign.IsPlain() ? nullptr : &menuCraftDesign, menuCraftDesign.craft, yawNow,
                                                                   menuPaletteWords.data(), kMenuCraftW, kMenuCraftH, menuCraftPixels, true);
                if (menuCraftReady) SDL_UpdateTexture(menuCraftTexture, nullptr, menuCraftPixels.data(), kMenuCraftW * 4);
                menuCraftShownYaw = yawNow; menuCraftLastBuild = nowTicks;
            }
        }
        if (menuCraftTexture != nullptr && menuCraftReady) {
            const float availableWidth = phonePortrait ? workSize.x - 8.0f : std::max(220.0f, workSize.x - panelWidth - 66.0f);
            // The craft sits in the lower middle of its picture: show that part, larger.
            const float u0 = 0.10f, u1 = 0.90f, v0 = 0.22f, v1 = 0.86f;
            const float cropAspect = (u1 - u0) * kMenuCraftW / ((v1 - v0) * kMenuCraftH);
            const float width = std::min(availableWidth, workSize.y * 0.6f * cropAspect);
            const float height = width / cropAspect;
            const float x = workPos.x + (phonePortrait ? 4.0f : std::max(8.0f, (availableWidth - width) * 0.5f + 8.0f));
            const float y = phonePortrait ? workPos.y + std::max(40.0f, (panelPos.y - height) * 0.5f) : workPos.y + std::max(40.0f, (workSize.y - height) * 0.5f + workSize.y * 0.06f);
            { const float slide = -80.0f * (1.0f - tCraft); background->AddImage(reinterpret_cast<ImTextureID>(menuCraftTexture), ImVec2(x + slide, y), ImVec2(x + slide + width, y + height), ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32(255, 255, 255, static_cast<int>(255.0f * tCraft))); }
        }
        else if (menuBannerTexture != nullptr) {
            // The banner picture fills the space left of the panel (above the panel on a portrait phone).
            const float availableWidth = phonePortrait ? workSize.x - 8.0f
                                       : std::max(220.0f, workSize.x - panelWidth - 66.0f);
            const float bannerWidth = std::min(availableWidth, workSize.y * 0.9f * menuBannerAspect);
            const float bannerHeight = bannerWidth / menuBannerAspect;
            const float bannerX = workPos.x + (phonePortrait ? 4.0f : std::max(8.0f, (availableWidth - bannerWidth) * 0.5f + 8.0f));
            const float bannerY = phonePortrait ? workPos.y + std::max(40.0f, (panelPos.y - bannerHeight) * 0.5f)
                                                : workPos.y + std::max(40.0f, (workSize.y - bannerHeight) * 0.5f);
            background->AddImage(reinterpret_cast<ImTextureID>(menuBannerTexture),
                                 ImVec2(bannerX, bannerY), ImVec2(bannerX + bannerWidth, bannerY + bannerHeight));
        }
        else if (menuHovercraftTexture != nullptr) {
            // Hero render of the HoverNet craft (a pilot in the red cockpit, a
            // jet engine with a flame) on a transparent background, drawn square.
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

        ImGui::SetCursorPos(phone ? ImVec2(20.0f, 16.0f - 24.0f * (1.0f - tBrand)) : ImVec2(48.0f, 48.0f - 24.0f * (1.0f - tBrand)));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tBrand);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        if (gHoverNetDisplayFont != nullptr) ImGui::PushFont(gHoverNetDisplayFont);
        ImGui::SetWindowFontScale(1.45f);
        ImGui::TextUnformatted("HOVERNET");
        ImGui::SetWindowFontScale(1.0f);
        if (gHoverNetDisplayFont != nullptr) ImGui::PopFont();
        ImGui::TextColored(kHoverNetCoral, "HIGH-SPEED COMBAT RACING");
        ImGui::Spacing();
        ImGui::TextDisabled("FAST, FUN, FREE!");
        ImGui::PopStyleColor();
        {
            // The player's level and progress to the next one (see Profile.h).
            static HoverNetProfile::Profile menuProfile;
            static bool menuProfileLoaded = false;
            if (!menuProfileLoaded || framesShown == 1) { menuProfile.Load(ProfilePath()); menuProfileLoaded = true; }
            if (menuProfile.xp > 0 || menuProfile.races > 0) {
                ImGui::Spacing();
                ImGui::TextColored(kHoverNetCoral, "LEVEL %d  -  %s", menuProfile.Level(), HoverNetCraft::LevelTitle(menuProfile.Level()));
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImDrawList* chip = ImGui::GetWindowDrawList();
                chip->AddRectFilled(at, ImVec2(at.x + 150.0f, at.y + 6.0f), IM_COL32(48, 51, 66, 255), 3.0f);
                chip->AddRectFilled(at, ImVec2(at.x + 150.0f * menuProfile.LevelProgress(), at.y + 6.0f), IM_COL32(250, 115, 122, 255), 3.0f);
                ImGui::Dummy(ImVec2(150.0f, 8.0f));
            }
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar();   // the title block's fade

        ImGui::SetCursorPos(ImVec2(panelPos.x + 70.0f * (1.0f - tPanel), panelPos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, tPanel);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.94f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, phone ? ImVec2(14.0f, 12.0f) : ImVec2(22.0f, 20.0f));
        // The panel never scrolls: its buttons are sized from the room it has. The two main modes are large; the other pages sit in a grid (two columns, three on a
        // short landscape phone), and the heading and hint are dropped when there is no room for them.
        MenuBeginChild("MainMenuPanel", ImVec2(panelWidth, panelHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextColored(kHoverNetCoral, "READY TO RACE?");
        const bool roomyPanel = !phone && panelHeight >= 470.0f;
        if (roomyPanel) ImGui::TextDisabled("Choose your mode");
        ImGui::Separator();
        ImGui::Spacing();

        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        const float gap = ImGui::GetStyle().ItemSpacing.y;
        const bool sideBySidePrimary = phoneShort;
        const int primaryRows = sideBySidePrimary ? 1 : 2;
        const bool showHeading = roomyPanel;
        const bool showOnlinePitch = showHeading && !sideBySidePrimary;
        const bool showHint = roomyPanel && panelHeight >= 540.0f;
        const bool showTutorialHint = false;
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float reserved = (showHeading ? lineHeight + gap * 3.0f + 4.0f : 0.0f) + (showOnlinePitch ? lineHeight * 2.0f + gap : 0.0f) + (showHint ? lineHeight * 2.0f + gap : 0.0f) + (showTutorialHint ? lineHeight * 2.0f : 0.0f) + gap * (primaryRows + 3);
        // Heights in "units": a main button is 1.45 units, a grid button 1 unit (two rows of them).
        const float room = std::max(0.0f, ImGui::GetContentRegionAvail().y - reserved);
        const float unit = room / (primaryRows * 1.45f + 2.0f);
        const float primaryHeight = std::max(34.0f, std::min(70.0f, unit * 1.45f));
        const float secondaryHeight = std::max(32.0f, std::min(52.0f, unit));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (sideBySidePrimary) {
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (HoverNetButton("RACE ONLINE", ImVec2(half, primaryHeight))) { choice = MenuChoice::eOnlineLobby; running = false; }
            if (menuMotion) {   // a soft pulsing glow around the main button, once the panel is in
                const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) / 1000.0f * 2.4f);
                for (int ring = 1; ring <= 4; ++ring) {
                    const float grow = static_cast<float>(ring) * 3.0f;
                    ImGui::GetForegroundDrawList()->AddRect(ImVec2(lo.x - grow, lo.y - grow), ImVec2(hi.x + grow, hi.y + grow), IM_COL32(250, 80, 100, static_cast<int>((34.0f - 7.0f * static_cast<float>(ring)) * pulse * tPanel)), 8.0f + grow, 0, 2.0f);
                }
            }
            ImGui::SameLine();
            if (HoverNetQuietButton("PRACTICE OFFLINE", ImVec2(half, primaryHeight))) { choice = MenuChoice::eLocalPlay; running = false; }
        }
        else {
            if (HoverNetButton("RACE ONLINE", ImVec2(-FLT_MIN, primaryHeight))) { choice = MenuChoice::eOnlineLobby; running = false; }
            if (menuMotion) {   // a soft pulsing glow around the main button, once the panel is in
                const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) / 1000.0f * 2.4f);
                for (int ring = 1; ring <= 4; ++ring) {
                    const float grow = static_cast<float>(ring) * 3.0f;
                    ImGui::GetForegroundDrawList()->AddRect(ImVec2(lo.x - grow, lo.y - grow), ImVec2(hi.x + grow, hi.y + grow), IM_COL32(250, 80, 100, static_cast<int>((34.0f - 7.0f * static_cast<float>(ring)) * pulse * tPanel)), 8.0f + grow, 0, 2.0f);
                }
            }
            if (showOnlinePitch) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(kHoverNetCoral, "Race real players, climb the leaderboards, and show off your craft.");
                ImGui::PopTextWrapPos();
            }
            if (HoverNetQuietButton("PRACTICE OFFLINE", ImVec2(-FLT_MIN, primaryHeight))) { choice = MenuChoice::eLocalPlay; running = false; }
        }
        ImGui::PopStyleVar();
        {
            // Four more doors in a grid: Leaderboards, Garage, Tutorial (marked until it has been done) and Settings.
            const float cell = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (HoverNetButton("Leaderboards", ImVec2(cell, secondaryHeight))) { choice = MenuChoice::eLeaderboards; running = false; }
            ImGui::SameLine();
            if (HoverNetButton("Garage", ImVec2(cell, secondaryHeight))) { choice = MenuChoice::eGarage; running = false; }
            const bool tutorialClicked = tutorialDone ? HoverNetQuietButton("Tutorial", ImVec2(cell, secondaryHeight)) : HoverNetButton("Tutorial", ImVec2(cell, secondaryHeight));
            if (tutorialClicked) { choice = MenuChoice::eTutorial; running = false; }
            ImGui::SameLine();
            if (HoverNetQuietButton("Settings", ImVec2(cell, secondaryHeight))) { choice = MenuChoice::eSettings; running = false; }
        }
        if (showHint) {
            ImGui::PushTextWrapPos(0.0f);
            HoverNetHint("Navigate with arrow keys or D-pad. Select with Enter or A.");
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();   // the panel's fade

        if (!phonePortrait) {
            ImGui::SetCursorPos(ImVec2(22.0f, workSize.y - 30.0f));
            // HOVERNET_VERSION already starts with "v".
            ImGui::TextDisabled("%s%s", HOVERNET_VERSION[0] == 'v' ? "" : "v", HOVERNET_VERSION);
            if (!phone) { ImGui::SameLine(); ImGui::TextDisabled("  |   ESC / B: quit"); }
        }
        ImGui::End();

        HN_SCREEN_FADE();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        CaptureFrameIfRequested(renderer, framesShown, pFrameLimit, "HOVERNET_CAPTURE_MENU");   // test hook: the menu itself, apart from the race that follows
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        HoverNetFrameDelay();
    }

    if (menuHovercraftTexture != nullptr) {
        SDL_DestroyTexture(menuHovercraftTexture);
    }
    if (menuBannerTexture != nullptr) {
        SDL_DestroyTexture(menuBannerTexture);
    }
    if (menuCraftTexture != nullptr) SDL_DestroyTexture(menuCraftTexture);
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

    // A short rumble (strengths 0..1, milliseconds), for impacts and the start. A controller without rumble, or none, ignores it.
    void Rumble(float low, float high, Uint32 milliseconds) const
    {
        if (mController != nullptr) SDL_GameControllerRumble(mController, static_cast<Uint16>(low * 65535.0f), static_cast<Uint16>(high * 65535.0f), milliseconds);
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
    std::vector<std::string> names;   // who drove each slot, where known (empty entries: unknown)
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
    std::vector<std::string> slotNames;   // who sits in each slot, from the lobby roster (saved beside the replay so it can show names)
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

void ReplayTickHook(void* pData, unsigned)
{
    // The tick about to run is replay->tick (the playback steps one tick at a time). The hook's own argument is the simulation time over 16 and is clamped
    // at 0 during the countdown (rules 7 replays start at a negative time), so it cannot say which tick that is.
    ReplayPlayback* replay = static_cast<ReplayPlayback*>(pData);
    const unsigned pTick = replay->tick;
    if (pTick >= replay->data.ticks) return;
    for (int p = 0; p < replay->data.players; ++p) {
        replay->craft[static_cast<std::size_t>(p)]->SetControlState(
            replay->data.inputs[static_cast<std::size_t>(pTick) * replay->data.players + p],
            static_cast<MR_SimulationTime>(pTick) * 16 + static_cast<MR_SimulationTime>(ReplayFormat::StartTimeMs(replay->data.rules)));   // below zero: the countdown (rules 7)
    }
}
#endif

int RunClient(int argc, char** argv)
{
#ifdef HOVERNET_GAME2_PLAYER   // the playtest path and the argument helper exist only in the player build
    gPlaytestPath = ParseStringArg(argc, argv, "--playtest");
#endif
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
        std::fprintf(stderr, "TUTORIAL_COMPLETE=%s\n", (ConfigDirPath() + "/tutorial_complete").c_str());
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
    MenuSliderChangedHook() = &SliderTick;
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
    // Whatever way RunClient returns, the observer is released (an online run's early returns used to leak it, and everything it drew with).
    struct ObserverGuard { MR_Observer* o; ~ObserverGuard() { if (o != nullptr) o->Delete(); } } observerGuard{observer};

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
    // The modern look (see GraphicsSettings): re-read twice a second so a change in the options screen shows at once.
    GraphicsSettings graphicsSettings = LoadGraphicsSettings();
    bool graphicsReducedMotion = LoadReducedMotion();
    auto applyGraphics = [&]() {
        HoverNetGfx::Options options = GraphicsOptionsFor(graphicsSettings, graphicsReducedMotion);
        if (HoverNetVrActive()) options.enabled = false;   // the headset draws two eyes per frame; it keeps the plain picture
        static int loggedPreset = -1;
        if (loggedPreset != static_cast<int>(graphicsSettings.preset) + (options.enabled ? 100 : 0)) {
            loggedPreset = static_cast<int>(graphicsSettings.preset) + (options.enabled ? 100 : 0);
            std::printf("Graphics look: %s%s\n", HoverNetGfx::PresetLabel(graphicsSettings.preset), options.enabled ? "" : " (switched off: headset)");
            std::fflush(stdout);
        }
        graphics.SetModernOptions(options);
        graphics.SetGpuAllowed(graphicsSettings.gpu);
        // With Automatic graphics the presenter decides, frame by frame, how much of the look runs; the observer's shadows and lights and the true-colour textures follow what it used last frame.
        const HoverNetGfx::Options used = (options.automatic && graphics.GetEffectiveModernOptions().enabled) ? graphics.GetEffectiveModernOptions() : options;
        if (options.automatic && options.enabled) {
            static int lastAutomaticLevel = -1;
            if (graphics.GetAutomaticLevel() != lastAutomaticLevel) {   // a few times a minute at most (the governor waits between steps): worth a line for a bug report
                lastAutomaticLevel = graphics.GetAutomaticLevel();
                std::printf("Automatic graphics: level %d of %d (at the change: frames %.1f ms, picture passes %.1f ms)\n", lastAutomaticLevel, HoverNetGfx::Presenter::kMaxLevel, graphics.GetStepFrameMs(), graphics.GetStepCostMs());
                std::fflush(stdout);
            }
        }
#ifdef HOVERNET_GAME2_PLAYER
        if (observer != nullptr) {
            observer->SetModernLighting(options.enabled && used.lighting ? TRUE : FALSE);
            // Filtered true-colour textures cost the rasteriser; when the passes have had to drop to the lowest quality they are switched off too.
            const bool trueColourNow = options.enabled && used.trueColour && graphics.GetEffectiveQuality() >= 1;
            {   // The 8-bit palette, so a player's craft design is baked into the craft's 8-bit pixels and shows whether or not true-colour textures are on.
                static std::uint8_t bakedPalette[768];
                static bool bakedKnown = false;
                const std::uint8_t* shown = graphics.GetRenderPalette();
                if (shown != nullptr && (!bakedKnown || std::memcmp(bakedPalette, shown, sizeof bakedPalette) != 0)) {
                    std::memcpy(bakedPalette, shown, sizeof bakedPalette);
                    bakedKnown = true;
                    MR_ResBitmap::SetBakePalette(bakedPalette);
                    MR_DllObjectFactory::SetCraftPalette(bakedPalette);
                }
            }
            observer->SetModernTextures(trueColourNow ? TRUE : FALSE, graphics.GetRenderPalette());
            {   // Textures in true colour, with the dither and banding of the palette smoothed away: told to this program and to the object library.
                static std::uint8_t lastTexturePalette[768];
                static bool texturesOn = false;
                const std::uint8_t* palette = graphics.GetRenderPalette();
                if (trueColourNow && palette != nullptr) {
                    static float lastBrightness = -1.0f;
                    static bool lastGrade = false;
                    if (!texturesOn || lastBrightness != options.brightness || lastGrade != options.grade || std::memcmp(lastTexturePalette, palette, sizeof lastTexturePalette) != 0) {
                        lastBrightness = options.brightness; lastGrade = options.grade;
                        std::memcpy(lastTexturePalette, palette, sizeof lastTexturePalette);
                        MR_ResBitmap::SetColourPalette(palette, options.brightness, options.grade);
                        MR_DllObjectFactory::SetTexturePalette(palette, options.brightness, options.grade);
                        texturesOn = true;
                    }
                }
                else if (texturesOn) {
                    MR_ResBitmap::SetColourPalette(nullptr);
                    MR_DllObjectFactory::SetTexturePalette(nullptr);
                    texturesOn = false;
                }
            }
            observer->SetModernEffects(options.enabled ? TRUE : FALSE, used.shadows ? TRUE : FALSE, options.reducedMotion ? TRUE : FALSE, graphics.GetShadowLut());
            observer->SetSceneSink(options.enabled ? graphics.SceneSink() : nullptr);   // the card draws the walls, floors and craft when it can
        }
#endif
    };
    applyGraphics();
#endif
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
#ifdef HOVERNET_GAME2_PLAYER
    if (observer != nullptr) {
        // The HUD's words are drawn over each frame in the menu typefaces (the bars, meters and map stay in the picture); the lesson card of the tutorial goes the same way.
        const bool bitmapHudText = std::getenv("HOVERNET_CLASSIC_HUD_TEXT") != nullptr;
        observer->SetExternalHudText(bitmapHudText ? FALSE : TRUE);
        gHudTextInOverlay = !bitmapHudText;
        graphics.SetOverlay([&graphics, observer, bitmapHudText](SDL_Renderer* renderer) { DrawGameOverlay(graphics, renderer, bitmapHudText ? nullptr : &observer->GetHudText()); });
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
    struct FontGuard { MR_SpriteHandle* h; ~FontGuard() { delete h; } } menuFontGuard{menuFontHandle};
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
        ClearRaceCraftDesigns();
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
    HoverNetTutorial::Tutorial tutorial;   // the practice race with a coach (Settings > Tutorial); see Tutorial.h
    bool tutorialStarting = false;
    // Create the first gameplay session only after Local Play has selected it.
    auto loadLocalRace = [&](const std::string& newTrackName, int newLaps, bool newWeapons) -> bool {
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        if (!keepAuthForLoad) endAuthRace();
#endif
        if (!tutorialStarting) {
            tutorial.Stop();
            ShutdownTutorialOverlay();
        }
        ShowLoadingScreen(graphics, newTrackName);
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
#if defined(HOVERNET_GAME2_PLAYER) && defined(HOVERNET_AUTH)
        if (!keepAuthForLoad)
#endif
        {
            // A local race: the player's own craft design (a look only), on the craft they drive.
            ClearRaceCraftDesigns();
            Authority::CraftDesign mine;
            if (mainCharacter != nullptr && HoverNetCraft::CraftStore(ConfigDirPath()).LoadActive(mine)) SetRaceCraftDesign(mainCharacter->GetHoverId(), &mine);
        }
        currentTrackName = newTrackName;
        PlayMusic(MusicTrack::eRace);
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

    // The tutorial: a one-lap practice race on the first track with weapons on and no countdown. The lessons run in the race loop.
    auto startTutorialRace = [&]() -> bool {
        tutorialStarting = true;
        const bool savedRemember = rememberLocal;
        rememberLocal = false;
        const bool loaded = loadLocalRace("ClassicH", 1, true);
        rememberLocal = savedRemember;
        tutorialStarting = false;
        if (!loaded) return false;
        session.SetSimulationTime(0);
        tutorial.Start();
        return true;
    };

    bool tutorialLoaded = false;   // --tutorial: the race is already loaded, skip the main menu
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
        session.SetSimulationTime(static_cast<MR_SimulationTime>(ReplayFormat::StartTimeMs(auth.ack.rules)));   // the countdown is part of the race: pick your craft in it
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
        lobby.SetCraftDesign(HoverNetCraft::CraftStore(ConfigDirPath()).ActiveBytes());   // the player's craft design, sent once the race is joined
        lobby.OnCraftDesign(ApplyRelayedCraftDesign);
        const AuthScreenResult screen = RunAuthServerScreen(graphics, lobby, frameLimit);
        if (screen != AuthScreenResult::eRace) return screen;
        auth.ack = lobby.Ack();
        auth.slotNames.assign(8, std::string());
        for (const auto& member : lobby.GetRoster().members) if (member.first < 8) auth.slotNames[member.first] = member.second;
        if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {
            std::printf("REPLAYTEST roster %zu\n", lobby.GetRoster().members.size());
            std::fflush(stdout);
        }
        auth.backlog = lobby.TakeBacklog();
        auth.spectator = lobby.Spectating();
        auth.conn.Swap(lobby.Connection());
        lobby.AddressOf(auth.host, auth.port, auth.serverKey);
        auth.name = OnlineUsername();
        // The browser tests set these switches after the page has started (see hovernet_test_setenv).
        if (std::getenv("HOVERNET_TEST_AUTH_VERIFY") != nullptr) auth.verify = true;
        if (std::getenv("HOVERNET_TEST_AUTH_SCRIPT") != nullptr) auth.scripted = true;
        const std::string startError = startAuthRace();
        if (!startError.empty()) {
            endAuthRace();
            std::fprintf(stderr, "%s\n", startError.c_str());
            return AuthScreenResult::eBack;
        }
        // Track loading rebuilds the renderers. Apply the complete retained set only after every race craft exists,
        // then set our own saved look explicitly in case its echoed relay follows the start message.
        ClearRaceCraftDesigns();
        for (const auto& relayed : lobby.CraftDesigns()) ApplyRelayedCraftDesign(relayed.first, relayed.second);
        if (!auth.spectator) {
            Authority::CraftDesign mine;
            if (HoverNetCraft::CraftStore(ConfigDirPath()).LoadActive(mine)) SetRaceCraftDesign(auth.ack.player, &mine);
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
    // "NAME, player 3" when the replay's racer in that slot is known, else "player 3".
    auto replayWatchLabel = [&](int slot) -> std::string {
        std::string label = "player " + std::to_string(slot + 1);
        if (replay.active && slot >= 0 && slot < static_cast<int>(replay.names.size()) && !replay.names[static_cast<std::size_t>(slot)].empty()) label = replay.names[static_cast<std::size_t>(slot)] + ", " + label;
        return label;
    };
    // Who drove a slot, for the HUD and the end screen: their name, else "Player 3".
    auto replayRacerName = [&](int slot) -> std::string {
        if (replay.active && slot >= 0 && slot < static_cast<int>(replay.names.size()) && !replay.names[static_cast<std::size_t>(slot)].empty()) return replay.names[static_cast<std::size_t>(slot)];
        return "Player " + std::to_string(slot + 1);
    };
    auto startReplayFile = [&](const std::string& path, int watch, bool& fatal, const std::string& note = std::string()) -> std::string {
        fatal = false;
        ReplayPlayback next;
        next.hashMode = replay.hashMode; next.seekTest = replay.seekTest; next.markerTest = replay.markerTest;   // test modes set on the command line
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!ReadWholeFile(path, bytes)) return "Could not read the replay file " + path;
        if (!ReplayFormat::Decode(bytes, next.data, error)) return "This is not a valid replay: " + error;
        if (!ReplayFormat::RulesSupported(next.data.rules)) {
            return "This replay was recorded with a different version of the game rules and cannot be shown.";
        }
        const std::string trackPath = Tracks().PathFor(next.data.track);
        if (trackPath.empty() || Sha256::HashFile(trackPath) != next.data.trackSha256) {
            return "The track '" + next.data.track + "' on this computer is not the one the replay was recorded on.";
        }
        next.watch = (watch < 0 || watch >= next.data.players) ? 0 : watch;
        next.path = path;
        next.names = std::move(g_nextReplayNames);
        g_nextReplayNames.clear();
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
        session.SetSimulationTime(static_cast<MR_SimulationTime>(ReplayFormat::StartTimeMs(replay.data.rules)));   // rules 7: the race starts with its countdown
        replay.active = true;
        replay.lapsSeen.assign(static_cast<std::size_t>(replay.data.players), 0);
        if (const char* speed = std::getenv("HOVERNET_TEST_REPLAY_SPEED")) replay.speedStep = std::max(0, std::min(4, std::atoi(speed)));   // test hook: start at this speed step
        replay.lastTicks = SDL_GetTicks();
        if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) {
            std::printf("REPLAYTEST following player %d of %d (%s)\n", replay.watch + 1, replay.data.players, note.c_str());
            if (replay.watch < static_cast<int>(replay.names.size()) && !replay.names[static_cast<std::size_t>(replay.watch)].empty())
                std::printf("REPLAYTEST racer %s\n", replay.names[static_cast<std::size_t>(replay.watch)].c_str());
            std::fflush(stdout);
        }
        session.AddMessage(("Following " + replayWatchLabel(replay.watch) + " of " + std::to_string(replay.data.players) + (note.empty() ? std::string() : " (" + note + ")") + ".  P pause, 1-5 speed, [ ] craft, C camera, Esc menu").c_str());
        fatal = false;
        return std::string();
    };
#endif

#ifdef HOVERNET_GAME2_PLAYER
    // The main menu and what it leads to (a local race, the online lobby, Watch a Replay, settings, controls, how to play). Returns true when a race
    // or replay is set up, false when the player quit (or loading failed: mainMenuFailed). Used at start-up and whenever a menu says "main menu".
    bool mainMenuFailed = false;
    bool playtestPending = !gPlaytestPath.empty();
    auto showMainMenu = [&]() -> bool {
        PlayMusic(MusicTrack::eMenu);
        tutorial.Stop();   // back at the menu: the lesson card goes, and its ImGui context with it (the menus make their own)
        ShutdownTutorialOverlay();
        replay.active = false;
        if (playtestPending) {
            // the editor's Playtest: no menus the first time, straight into a race on the track just compiled (--laps N, weapons on); the menus follow afterwards
            playtestPending = false;
            const std::string lapsText = ParseStringArg(argc, argv, "--laps");
            const int playtestLaps = std::max(1, std::min(10, lapsText.empty() ? 1 : std::atoi(lapsText.c_str())));
            if (!loadLocalRace("Playtest", playtestLaps, true)) { std::fprintf(stderr, "Could not load the track to playtest: %s\n", gPlaytestPath.c_str()); mainMenuFailed = true; return false; }
            session.SetSimulationTime(0);
            startingPlayerPosition = mainCharacter->mPosition;
            return true;
        }
    bool pickingMode = true;
        while (pickingMode) {
            pickingMode = false;
#ifdef HOVERNET_WEB
            {   // once per visit, before the main menu: sign in with Outiva or play as a guest (bounded test runs skip it)
                static bool gateShown = false;
                if (!gateShown && frameLimit < 0) { gateShown = true; RunSignInGate(graphics); }
            }
#endif
            gMenuCraftResources = &resources;
            const MenuChoice choice = std::getenv("HOVERNET_OPEN_GARAGE") != nullptr ? MenuChoice::eGarage : RunMainMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
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
            else if (choice == MenuChoice::eSettings || choice == MenuChoice::eControls || choice == MenuChoice::eTutorial) {
                // The Settings hub: game settings, controls, saved replays and the tutorial. Each page returns to the hub; Back returns to the main menu.
                pickingMode = !g_QuitConfirmed;
                HubChoice pick = choice == MenuChoice::eControls ? HubChoice::eControls : choice == MenuChoice::eTutorial ? HubChoice::eTutorial : HubChoice::eBack;
                bool inHub = choice == MenuChoice::eSettings;
                bool first = true;
                while (!g_QuitConfirmed && (inHub || first)) {
                    if (inHub) pick = RunSettingsHub(graphics, frameLimit);
                    first = false;
                    if (pick == HubChoice::eBack) { inHub = false; }
                    else if (pick == HubChoice::eGame) {
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
                    else if (pick == HubChoice::eControls) { RunControlsScreen(graphics, frameLimit); }
                    else if (pick == HubChoice::eReplays) {
                        const bool started = RunReplayBrowser(graphics, frameLimit, [&](const std::string& path, int slot, const std::string& note, bool& fatal) { return startReplayFile(path, slot, fatal, note); });
                        if (started) { inHub = false; pickingMode = false; }
                    }
                    else if (pick == HubChoice::eTutorial) {
                        // A practice race with a coach: the first track, one lap, weapons on, no countdown; the lessons are in the race loop.
                        if (startTutorialRace()) { inHub = false; pickingMode = false; }
                        else std::printf("The tutorial track could not be loaded; back to the menu\n");
                    }
                    if (frameLimit >= 0) inHub = false;   // a bounded (test) run shows each screen once
                }
            }
            else if (choice == MenuChoice::eGarage) {
                RunGarageScreen(graphics, resources, frameLimit);
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
            auth.name = OnlineUsername();
            if (auth.name.empty()) auth.name = HoverNetGuest::Tagged("Player", gAccountsOffered && !gSignedIn);
            std::string error;
            if (!auth.conn.Connect(auth.host, auth.port, error, auth.serverKey.empty() ? nullptr : auth.serverKey.data())) return FatalClientError(error);
            Protocol3::Hello hello;
            hello.name = auth.name.substr(0, 32);
            hello.capabilities = Protocol3::kCapRoster;
            auth.conn.Send(auth.spectator ? Protocol3::kSpectate : Protocol3::kHello, Protocol3::Encode(hello));
            bool gotAck = false, gotStart = false;
            const Uint32 waitStart = SDL_GetTicks();
            while (!(gotAck && (gotStart || auth.spectator))) {
                SDL_PumpEvents();
                std::vector<AuthFrame> frames;
                const bool open = auth.conn.Pump(frames);
                for (const AuthFrame& f : frames) {
                    if (f.type == Protocol3::kHelloAck) gotAck = Protocol3::Decode(f.payload, auth.ack);
                    else if (f.type == Protocol3::kRoster) {
                        Protocol3::Roster roster;
                        if (Protocol3::Decode(f.payload, roster)) {
                            auth.slotNames.assign(8, std::string());
                            for (const auto& member : roster.members) if (member.first < 8) auth.slotNames[member.first] = member.second;
                        }
                    }
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
            if (std::getenv("HOVERNET_TEST_RESULTS_REWARD") != nullptr) {   // screenshot test: what a good race looks like
                HoverNetProfile::RaceResult shot; shot.finished = true; shot.rank = 1; shot.players = 4; shot.laps = 3; shot.newRaceBest = true; shot.online = true;
                HoverNetProfile::Profile profile; profile.xp = 540;
                gRewardView.reward = profile.Apply(shot);
                gRewardView.profile = profile;
                gRewardView.valid = true;
            }
            RunPostRaceScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                              83456, 27341, 1, 4, frameLimit, bestsMode != nullptr ? &sample : nullptr,
                              std::getenv("HOVERNET_TEST_RESULTS_ONLINE") != nullptr);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--tutorial")) {
            // Straight into the tutorial race (CI and screenshots).
            if (!startTutorialRace()) return FatalClientError("Could not load the tutorial track.");
            startingPlayerPosition = mainCharacter->mPosition;
            tutorialLoaded = true;
        }
        else if (HasArgument(argc, argv, "--settings-hub")) {
            int testW = 0, testH = 0;
            const char* testSize = std::getenv("HOVERNET_TEST_WINDOW");
            if (testSize != nullptr && std::sscanf(testSize, "%dx%d", &testW, &testH) == 2) {
                SDL_SetWindowSize(graphics.GetWindow(), testW, testH);
            }
            RunSettingsHub(graphics, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--local-setup-screen")) {
            RunLocalRaceSetup(graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
            g_QuitConfirmed = true;
        }
        bool pickingMode = !g_QuitConfirmed && !replay.active && !tutorialLoaded;
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
    struct GhostCraftGuard { MR_MainCharacter*& c; ~GhostCraftGuard() { delete c; } } ghostCraftGuard{ghostCraft};   // the ghost's craft is made on first use and freed on every way out
    MR_SimulationTime ghostLastTime = 0;  // a time that goes back means the race was restarted
    bool ghostShownReported = false;      // test hook: say once that the ghost was on screen
    bool ghostGapReported = false;        // test hook: say once that the HUD had a gap to the ghost
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
        session.AddMessage(("Following " + replayWatchLabel(*watch) + " of " + std::to_string(count)).c_str());
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
                        const std::string saved = ReplayLibrary(ConfigDirPath() + "/replays").Save(bytes, auth.ack.track, std::time(nullptr), auth.spectator ? -1 : static_cast<int>(auth.ack.player), auth.slotNames);
                        if (!saved.empty()) {
                            session.AddMessage("This race was saved: Watch a Replay in the menu");
                            if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYTEST saved %s %u ticks\n", saved.c_str(), auth.recorder.Ticks()); std::fflush(stdout); }
                        }
                    }
                    else if (std::getenv("HOVERNET_TEST_REPLAY_LIBRARY") != nullptr) { std::printf("REPLAYTEST not saved (the race was not followed from its first tick)\n"); std::fflush(stdout); }
                }
            }
            else if (f.type == kCraftDesign) {
                if (f.payload.size() > 1) ApplyRelayedCraftDesign(f.payload[0], std::vector<std::uint8_t>(f.payload.begin() + 1, f.payload.end()));
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
                std::uint16_t input = auth.scripted ? SimHarnessScripted(auth.ack.player, target)
                                                    : static_cast<std::uint16_t>(controlState & 0xFFFF);
                if (auth.scripted && target < ReplayFormat::kCountdownTicks + 16) input = 0;   // the scripted driver sits out the countdown: its steering would pick another craft
                if (const char* pick = std::getenv("HOVERNET_TEST_PICK_CRAFT")) {   // test hook (scripts/test-craft-pick.sh): N presses of the right steering key in the countdown (each held 6 ticks)
                    // Relative to the first tick this client sends for (a slow machine loads the race late, so fixed ticks could be gone already): the first press
                    // starts 20 ticks after it, each press is held 6 ticks and released 6.
                    static std::uint32_t sFirstTarget = target;
                    const int presses = std::atoi(pick);
                    const std::uint32_t at = target - sFirstTarget;
                    if (target >= sFirstTarget + 20 && at < 20 + 12 * static_cast<std::uint32_t>(presses) && (at - 20) % 12 < 6) input = MR_MainCharacter::eRight;
                }
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
        static double sLastClock = 0;   // the picture's continuous clock (ticks): the craft are drawn between ticks by it, so this, not the whole tick, is what the eye follows
        static double sLastX = 0, sLastY = 0;
        static bool sHave = false;
        if (std::getenv("HOVERNET_TEST_JITTER") != nullptr && !auth.needSnapshot && !auth.resultReceived && !auth.spectator && auth.shownTick > ReplayFormat::kCountdownTicks + 150) {   // only the race itself, once the craft is up to speed: it sits still in the countdown and accelerates from rest after it, and both are real changes of speed that the smoothness measure must not count
            const std::uint32_t displayTick = auth.shownTick;
            const MR_MainCharacter* me = auth.craft[static_cast<std::size_t>(auth.ack.player)];
            const double x = me->mPosition.mX, y = me->mPosition.mY;
            static int sWarmup = 0;
            if (sHave && now > sLastFrame && ++sWarmup > 30) {
                const double expected = (now - sLastFrame) / 16.0;
                // Pace: how far the picture's clock advanced against the real time elapsed. The whole tick is no use for this at a low frame rate (a software
                // renderer, 26 ms a frame): frames then show 1 or 2 new ticks, a pace of 0.62 or 1.23 whatever the motion looks like.
                sRatios.push_back((auth.displayTick - sLastClock) / expected);
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
            sHave = true; sLastFrame = now; sLastDisplay = displayTick; sLastClock = auth.displayTick; sLastX = x; sLastY = y;
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
                char line[96];
                const long long ms = static_cast<long long>(done[i].tick) * 16;
                std::snprintf(line, sizeof line, "%zu. %s   %lld:%02lld.%02lld", i + 1, replayRacerName(done[i].player).c_str(), ms / 60000, (ms % 60000) / 1000, (ms % 1000) / 10);
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
                if (!pauseHintShown && raceClock < 0 && !replay.active) {   // a replay needs no instructions for the driver
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
            if (fire || (autoPlay && std::getenv("HOVERNET_TEST_AUTOFIRE") != nullptr && (SDL_GetTicks() % 2000) < 250)) controlState |= MR_MainCharacter::eFire;   // test hook: an autoplaying craft fires now and then
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
#ifdef HOVERNET_GAME2_PLAYER
            // Race sounds and the start sequence: the countdown beeps, GO, a chime at each lap (a race only: never a replay).
            if (!replay.active) {
                static int lastSecond = -1;
                static int lastLap = -1;
                const MR_SimulationTime clock = session.GetSimulationTime();
                if (clock < 0) {
                    const int second = static_cast<int>((-clock + 999) / 1000);   // 3, 2, 1 for the last three seconds
                    if (second <= 3 && second != lastSecond) {
                        lastSecond = second;
                        gCountdownNumber = second;
                        gCountdownTickAt = SDL_GetTicks();
                        PlaySfx(Sfx::eCountdown, 0, 1.0);
                    }
                    lastLap = -1;
                }
                else {
                    if (lastSecond != -1) {
                        if (lastSecond >= 1) { PlaySfx(Sfx::eGo); gGoShownAt = SDL_GetTicks(); gameController.Rumble(0.3f, 0.5f, 220); }
                        lastSecond = -1;
                        gCountdownNumber = 0;
                    }
                    static bool wasOutOfControl = false;
                    const bool outOfControl = mainCharacter->IsOutOfControl() != FALSE;
                    if (outOfControl && !wasOutOfControl) gameController.Rumble(0.9f, 0.6f, 350);   // an impact: the pad kicks
                    ImpactDuckMusic(outOfControl && !wasOutOfControl);
                    wasOutOfControl = outOfControl;
                    const int lap = mainCharacter->GetLap();
                    static MR_SimulationTime lapStartedAt = 0, bestSoFar = 0;
                    if (lastLap >= 0 && lap > lastLap && lap < mainCharacter->GetTotalLap()) {
                        const bool finalLap = lap == mainCharacter->GetTotalLap() - 1 && mainCharacter->GetTotalLap() > 1;   // the last lap starts: a higher chime and its own banner
                        PlaySfx(Sfx::eLap, 0, finalLap ? 1.26 : 1.0);
                        if (finalLap) PlayMusic(MusicTrack::eFinalLap);   // the faster final-lap track takes over
                        const MR_SimulationTime lapTime = mainCharacter->GetTotalTime() - lapStartedAt;
                        char timeText[32];
                        FormatRaceTime(lapTime, timeText, sizeof(timeText));
                        const bool best = lastLap >= 1 && bestSoFar > 0 && lapTime < bestSoFar;   // the first lap has nothing to beat
                        gLapBannerText = finalLap ? std::string("FINAL LAP") : "LAP " + std::to_string(lastLap + 1) + " DONE";
                        gLapBannerSub = std::string(best ? "BEST LAP  " : "Lap time  ") + timeText;
                        gLapBannerBest = best;
                        gLapBannerAt = SDL_GetTicks();
                        if (bestSoFar <= 0 || lapTime < bestSoFar) bestSoFar = lapTime;
                        lapStartedAt = mainCharacter->GetTotalTime();
                    }
                    else if (lastLap < 0 || lap < lastLap) { lapStartedAt = 0; bestSoFar = 0; }
                    if (gLapBannerAt == 0 && std::getenv("HOVERNET_TEST_LAPBANNER") != nullptr) {   // test hook: show the banner without finishing a lap
                        gLapBannerBest = std::atoi(std::getenv("HOVERNET_TEST_LAPBANNER")) == 2; gLapBannerText = "LAP 2 DONE"; gLapBannerSub = gLapBannerBest ? "BEST LAP  0:31.20" : "Lap time  0:31.20"; gLapBannerAt = SDL_GetTicks();
                    }
                    lastLap = lap;
                }
            }
            else { gCountdownNumber = 0; gGoShownAt = 0; }
            // The tutorial: watch what the player does and show the current lesson (a local race only: never a replay or an online race).
            if (tutorial.Active() && observer != nullptr && !replay.active
#if defined(HOVERNET_AUTH)
                && !auth.active
#endif
                ) {
                static bool previousSkipKey = false;
                const bool skipKey = keyboard[SDL_SCANCODE_N] != 0;
                if (skipKey && !previousSkipKey) tutorial.SkipLesson();
                previousSkipKey = skipKey;
                HoverNetTutorial::Frame frame;
                frame.timeMs = static_cast<long>(SDL_GetTicks());
                frame.controls = controlState;
                frame.speed = mainCharacter->GetNormalisedSpeed();
                frame.lap = mainCharacter->GetLap();
                frame.totalLaps = mainCharacter->GetTotalLap();
                frame.finished = mainCharacter->HasFinish() != FALSE;
                frame.confirm = keyboard[SDL_SCANCODE_RETURN] != 0 || keyboard[SDL_SCANCODE_SPACE] != 0;
                tutorial.Update(frame);
                const HoverNetTutorial::Labels labels = TutorialLabels();
                gTutorialCard.visible = true;
                gTutorialCard.number = tutorial.Number() > HoverNetTutorial::kLessons ? HoverNetTutorial::kLessons : tutorial.Number();
                gTutorialCard.total = HoverNetTutorial::kLessons;
                gTutorialCard.heading = tutorial.Heading();
                gTutorialCard.text = tutorial.Text(labels);
                gTutorialCard.progress = tutorial.Progress();
                gTutorialCard.flash = tutorial.JustCompleted(frame.timeMs);
                gTutorialCard.hint = tutorial.Finished() ? "" : "Skip this lesson: " + labels.skip + "   |   Pause menu: " + labels.pause;
                if (tutorial.Finished()) {
                    static bool saved = false;
                    if (!saved) { HoverNetTutorial::MarkCompleted(ConfigDirPath()); saved = true; }
                }
            }
#endif
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
                PlaySfx(finishBests.newRaceBest || finishBests.newLapBest ? Sfx::eBest : Sfx::eFinish);   // a sting for a new best, the fanfare otherwise
                {   // progress: XP, level and achievements (a count of what happened; nothing here touches the race)
                    HoverNetProfile::Profile profile;
                    profile.Load(ProfilePath());
                    HoverNetProfile::RaceResult result;
                    result.finished = true;
                    result.rank = std::max(1, session.GetRank(mainCharacter));
                    result.players = std::max(1, session.GetNbPlayers());
                    result.laps = std::max(1, session.GetNbLap());
                    result.newRaceBest = finishBests.newRaceBest;
                    result.newLapBest = finishBests.newLapBest;
                    result.online = raceWasOnline;
#if defined(HOVERNET_AUTH)
                    result.online = result.online || (auth.active && !auth.spectator);
#endif
                    result.tutorial = tutorial.Active();
                    gRewardView.reward = profile.Apply(result);
                    if (tutorial.Active()) {
                        const HoverNetProfile::Reward graduate = profile.Unlock(HoverNetProfile::eTutorialGraduate);
                        gRewardView.reward.unlocked.insert(gRewardView.reward.unlocked.end(), graduate.unlocked.begin(), graduate.unlocked.end());
                        gRewardView.reward.xp += graduate.xp;
                        gRewardView.reward.levelAfter = profile.Level();
                        if (gRewardView.reward.breakdown.empty() && graduate.xp > 0) gRewardView.reward.breakdown = "Tutorial complete";
                    }
                    profile.Save(ProfilePath());
                    gRewardView.profile = profile;
                    gRewardView.valid = true;
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
            gFinishPromptVisible = resultsPending && !resultsSeen && !openResults && !replay.active;
            gFinishPromptTouch = HoverNetIsTouchDevice();
            if (openResults && resultsPending) {
#ifdef HOVERNET_WEB
                gHoverNetInRace = 0;
#endif
                PlayMusic(MusicTrack::eMenu);
                const PostRaceChoice resultChoice = RunPostRaceScreen(
                    graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                    finishTotalTime, finishBestLap,
                    session.GetRank(mainCharacter), session.GetNbPlayers(),
                    frameLimit < 0 ? -1 : 2, &finishBests,
                    raceWasOnline
#if defined(HOVERNET_AUTH)
                        || (auth.active && !auth.spectator)
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
                                std::int64_t ghostGap = 0;
                                if (ghostBest.GapMs(ghostTime, mainCharacter->mPosition.mX, mainCharacter->mPosition.mY, mainCharacter->mPosition.mZ,
                                                    mainCharacter->mRoom, ghostGap)) {
                                    observer->SetGhostGapText(GhostRun::FormatGap(ghostGap).c_str());
                                    if (!ghostGapReported && std::getenv("HOVERNET_TEST_GHOST") != nullptr) {
                                        ghostGapReported = true;
                                        std::printf("GHOSTTEST gap %s at %d ms\n", GhostRun::FormatGap(ghostGap).c_str(), static_cast<int>(ghostTime));
                                        std::fflush(stdout);
                                    }
                                }
                                else {
                                    observer->SetGhostGapText(nullptr);
                                }
                                if (!ghostShownReported && std::getenv("HOVERNET_TEST_GHOST") != nullptr) {
                                    ghostShownReported = true;
                                    std::printf("GHOSTTEST ghost shown at %d ms in room %d\n", static_cast<int>(ghostTime), at.room);
                                    std::fflush(stdout);
                                }
                            }
                        }
                        else {
                            observer->SetGhost(nullptr);
                            observer->SetGhostGapText(nullptr);
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
                gReplayLabel.clear();
                gChatPrompt = gHudTextInOverlay ? chatBuffer : std::string();
                if (const char* shown = std::getenv("HOVERNET_TEST_CHAT_PROMPT")) gChatPrompt = shown;   // test hook: look at the chat line without typing
                if (!chatBuffer.empty() && !gHudTextInOverlay && menuFontHandle != nullptr) {
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
                        std::snprintf(label, sizeof label, "%s   %d:%02d / %d:%02d   x%g%s", replayRacerName(replay.watch).c_str(), sec / 60, sec % 60, total / 60, total % 60,
                                      kReplaySpeeds[replay.speedStep], replay.paused ? "   paused" : "");
                        if (gHudTextInOverlay) gReplayLabel = label;
                        else DrawUiText(*menuFontHandle->GetSprite(), barX + barW, barY - 44, label, &viewport, MR_Sprite::eRight, MR_Sprite::eBottom, 1);
                    }
                }
                observer->PlaySoundsSafe(session.GetCurrentLevel(), mainCharacter);
                if (!replay.active) UpdateSpeedSounds(mainCharacter->GetAbsoluteSpeed());
                MR_SoundServer::ApplyContinuousPlay();
            }
            else
#endif
            {
                renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                          SDL_GetTicks(), mainCharacter);
            }
        }
#ifdef HOVERNET_GAME2_PLAYER
        if (framesRendered % 30 == 0) {
            graphicsSettings = LoadGraphicsSettings();
            graphicsReducedMotion = LoadReducedMotion();
        }
        applyGraphics();   // every frame: the shadow table follows the palette, which changes with the track
        graphics.SetFrameHints(observer != nullptr && !debugView ? observer->GetFrameHints() : HoverNetGfx::FrameHints());
#endif
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
