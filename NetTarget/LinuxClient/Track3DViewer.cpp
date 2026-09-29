#ifdef _WIN32
#include <afxwin.h>
#include <afxtempl.h>
#endif

#include "../GraphicsSDL2/SDL2Graphics.h"
#include "../Game2/ClientSession.h"
#ifdef HOVERNET_GAME2_PLAYER
#include "../Game2/Observer.h"
#include "../VideoServices/SoundServer.h"
#include "RaceServerClient.h"
#include "../ThirdParty/imgui/imgui.h"
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
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <map>
#include <vector>

namespace
{
constexpr int kWidth = 1024;
constexpr int kHeight = 768;

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
constexpr const char* kDefaultLobbyHost = "outiva.com";
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
// Tracks the server will actually accept (kept in sync with the whitelist in
// ServerSocket.cpp's eRSMsgHostRace handler).
const char* const kHostableTracks[] = {"ClassicH", "Steeplechase", "Switchback", "The Alley2", "The River"};
constexpr int kHostableTrackCount = sizeof(kHostableTracks) / sizeof(kHostableTracks[0]);

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
};
static_assert(sizeof(kTrackGuides) / sizeof(kTrackGuides[0]) == kHostableTrackCount,
              "Every hostable track needs setup guidance");

int FindHostableTrack(const std::string& trackName)
{
    for (int index = 0; index < kHostableTrackCount; ++index) {
        if (trackName == kHostableTracks[index]) return index;
    }
    return -1;
}

void HoverNetSectionHeading(const char* label);

struct TrackPreview
{
    SDL_Texture* mTexture = nullptr;
    int mWidth = 0;
    int mHeight = 0;
    bool mLoaded = false;
};

TrackPreview LoadTrackPreview(SDL_Renderer* renderer, const char* trackName)
{
    TrackPreview preview;
    preview.mLoaded = true;
    if (renderer == nullptr || trackName == nullptr) return preview;

    try {
        MR_RecordFile track;
        const std::string path = SourcePath((std::string("NetTarget/Tracks/") + trackName + ".trk").c_str());
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
        const char* unavailable = "Map preview unavailable";
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
    int mTrackIndex = 0;
    int mLaps = 5;
    bool mWeapons = false;
};

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
    std::ifstream in(HostPrefsPath());
    int track = 0, laps = 0, weapons = 0;
    if (in >> track >> laps >> weapons) {
        if (track >= 0 && track < kHostableTrackCount) { prefs.mTrackIndex = track; }
        if (laps >= 1 && laps <= 20) { prefs.mLaps = laps; }
        prefs.mWeapons = weapons != 0;
    }
    return prefs;
}

void SaveHostPrefs(const HostPrefs& prefs)
{
    std::ofstream out(HostPrefsPath());
    out << prefs.mTrackIndex << ' ' << prefs.mLaps << ' ' << (prefs.mWeapons ? 1 : 0) << '\n';
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
    std::ifstream in(LocalRacePrefsPath());
    int track = 0, laps = 0, weapons = 0;
    if (in >> track >> laps >> weapons) {
        if (track >= 0 && track < kHostableTrackCount) { prefs.mTrackIndex = track; }
        if (laps >= 1 && laps <= 20) { prefs.mLaps = laps; }
        prefs.mWeapons = weapons != 0;
    }
    return prefs;
}

void SaveLocalRacePrefs(const HostPrefs& prefs)
{
    std::ofstream out(LocalRacePrefsPath());
    out << prefs.mTrackIndex << ' ' << prefs.mLaps << ' ' << (prefs.mWeapons ? 1 : 0) << '\n';
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

void ApplyHoverNetLobbyStyle(float scale, bool highContrast)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = highContrast ? 1.0f : 0.0f;

    // High contrast removes subtle near-neighbour shades: surfaces become much
    // darker, secondary text and borders brighter, and action red darker under
    // white labels. The standard palette retains its softer layered appearance.
    const ImVec4 windowBg = highContrast ? ImVec4(0.01f, 0.01f, 0.01f, 1.00f)
                                         : ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    const ImVec4 panelBg = highContrast ? ImVec4(0.045f, 0.045f, 0.055f, 1.00f)
                                        : ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
    const ImVec4 fieldBg = highContrast ? ImVec4(0.00f, 0.00f, 0.00f, 1.00f)
                                        : ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    const ImVec4 softBorder = highContrast ? ImVec4(0.82f, 0.82f, 0.86f, 1.00f)
                                           : ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    const ImVec4 text = highContrast ? ImVec4(1.00f, 1.00f, 1.00f, 1.00f)
                                     : ImVec4(0.93f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textMuted = highContrast ? ImVec4(0.82f, 0.82f, 0.86f, 1.00f)
                                          : ImVec4(0.56f, 0.56f, 0.60f, 1.00f);
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

// Section titles ("Game list", "Chat section", ...) in a coral accent, legible on
// the dark panel background, distinguishing them from ordinary body text without
// another loud color block.
void HoverNetSectionHeading(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetCoral);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
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
    eLeaveRace,
    eOnlineLobby,
    eSettings,
    eControls,
    eQuit,
};

PauseChoice RunPauseMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                         MR_3DViewPort& viewport, const MR_Sprite& font, bool pIsOnline,
                         int pFrameLimit = -1);

enum class PostRaceChoice
{
    eContinue,
    eNewLocalRace,
    eOnlineLobby,
    eQuit,
};

PostRaceChoice RunPostRaceScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                                 MR_3DViewPort& viewport, const MR_Sprite& font,
                                 MR_SimulationTime pFinishTime, MR_SimulationTime pBestLap,
                                 int pRank, int pPlayerCount, int pFrameLimit = -1);

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
                    std::string& outJoinedName, std::string& outTrackName, int& outNumLaps,
                    int& outLocalClientId, std::vector<RaceServerPeer>& outPeers, int pFrameLimit)
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
        constexpr std::size_t kMaxChatLines = 40;
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
    int selected = 0;
    LobbyPhase phase = LobbyPhase::eBrowsing;
    Uint32 lastRefresh = SDL_GetTicks();
    Uint32 lastPing = SDL_GetTicks();
    std::string statusText = "Connected to the shared HoverNet lobby.";
    bool running = true;
    bool joined = false;
    bool isHost = false;
    bool connectionLossReported = false;
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
    std::array<TrackPreview, kHostableTrackCount> lobbyTrackPreviews{};

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
    auto joinSelected = [&]() {
        if (!games.empty() && selected >= 0 && selected < static_cast<int>(games.size()) &&
            !games[static_cast<std::size_t>(selected)].mStarted) {
            const RaceServerGameInfo& game = games[static_cast<std::size_t>(selected)];
            if (client.JoinGameById(game.mRaceId)) {
                outJoinedName = game.mName;
                outTrackName = game.mTrack;
                outNumLaps = std::max(1, game.mNumLaps);
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
        const std::string raceName = kHostableTracks[hostPrefs.mTrackIndex];
        outTrackName = kHostableTracks[hostPrefs.mTrackIndex];
        outNumLaps = hostPrefs.mLaps;
        if (client.HostRace(raceName, kHostableTracks[hostPrefs.mTrackIndex],
                             hostPrefs.mLaps, hostPrefs.mWeapons)) {
            outJoinedName = raceName;
            phase = LobbyPhase::eWaitingRoom;
            raceMembers.clear();
            SaveHostPrefs(hostPrefs);
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
    // ImGui's default font renders at 13px, which looks cramped blown up across a
    // 1024x768 screen -- build the atlas at a larger fixed size instead of scaling
    // the default bitmap font (which just blurs it).
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 19.0f;
    io.Fonts->AddFontDefault(&fontConfig);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    SDL_StartTextInput();
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
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
                // Used to just close the lobby screen outright -- the same
                // pause menu Escape brings up everywhere else in the client,
                // so there's a way back out of an accidental press, and a way
                // to reach Settings without leaving first.
                const PauseChoice pauseChoice = RunPauseMenu(graphics, buffer, viewport, font, false, pFrameLimit);
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
                    SaveUsername(username);
                    std::snprintf(usernameBuf, sizeof(usernameBuf), "%s", username.c_str());
                    statusText = "That name was taken -- you're now '" + username + "'";
                }
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

        // Header banner: a solid accent-color strip reads as a title, not just
        // another line of body text -- everything below it stays on the neutral
        // panel palette so the red isn't fighting itself across the whole screen.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, 76), false);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted("HOVERNET LOBBY");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.90f, 0.91f, 1.0f));
        ImGui::TextUnformatted(statusText.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (!client.IsConnected()) {
            const float panelWidth = std::min(520.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 std::max(0.0f, (ImGui::GetContentRegionAvail().x - panelWidth) * 0.5f));
            ImGui::BeginChild("ConnectionLost", ImVec2(panelWidth, 275), true);
            HoverNetSectionHeading("CONNECTION LOST");
            ImGui::TextWrapped("The RaceServer connection closed. Race and player details shown before the disconnect may no longer be valid.");
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
            ImGui::Spacing();
            ImGui::TextUnformatted("Welcome to HoverNet -- enter a username:");
            ImGui::SetNextItemWidth(320);
            const bool submitted = ImGui::InputText("##username", usernameBuf, sizeof(usernameBuf),
                                                     ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            const bool clicked = HoverNetButton("Continue");
            if ((submitted || clicked) && usernameBuf[0] != '\0') {
                username = usernameBuf;
                SaveUsername(username);
                client.SetPlayerName(username);
                client.ListLobbyUsers();
                phase = LobbyPhase::eBrowsing;
                statusText = "Connected to the shared HoverNet lobby.";
            }
        }
        else {
            // Panel grid matches the Windows client's IDD_INTERNET_MEETING dialog:
            // game list (top-left), game details + player list (top-middle), the
            // action buttons (top-right), then the lobby roster and chat below.
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float availHeight = ImGui::GetContentRegionAvail().y;
            // Race list stays short (there are only ever a handful of open races),
            // but the lobby roster and chat below need real room -- in practice
            // there can be far more users and chat history than races to show.
            const float topHeight = availHeight * 0.45f;
            const float leftColWidth = availWidth * 0.29f;
            const float detailsColWidth = availWidth * 0.55f;

            const RaceServerGameInfo* selectedGame =
                (phase == LobbyPhase::eBrowsing && !games.empty() && selected >= 0 &&
                 selected < static_cast<int>(games.size()))
                    ? &games[static_cast<std::size_t>(selected)]
                    : nullptr;

            ImGui::BeginChild("GameListPanel", ImVec2(leftColWidth, topHeight), true);
            HoverNetSectionHeading("Game list");
            if (phase == LobbyPhase::eWaitingRoom) {
                ImGui::TextDisabled("You're in a race -- see Game details.");
            }
            else if (ImGui::BeginListBox("##races", ImVec2(-FLT_MIN, -FLT_MIN))) {
                if (games.empty()) {
                    ImGui::TextDisabled("No open races - host one to get started");
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

            ImGui::SameLine();
            ImGui::BeginChild("GameDetailsPanel", ImVec2(detailsColWidth, topHeight), true);
            HoverNetSectionHeading("Game details");
            {
                const std::string trackName = (phase == LobbyPhase::eWaitingRoom)
                    ? outTrackName
                    : (selectedGame != nullptr ? selectedGame->mTrack : std::string());
                const int laps = (phase == LobbyPhase::eWaitingRoom)
                    ? outNumLaps
                    : (selectedGame != nullptr ? selectedGame->mNumLaps : 0);
                const std::string availability = (phase == LobbyPhase::eWaitingRoom)
                    ? statusText
                    : (selectedGame != nullptr
                           ? (selectedGame->mStarted ? "Race in progress" : "Waiting for players")
                           : std::string());

                // Column offset sized to the widest label ("Availability:") plus a
                // gap, rather than a guessed pixel count -- a fixed 120px fit the
                // shorter labels but "Availability:" ran past it and overlapped the
                // value that followed on the same line.
                const float labelColumnWidth = ImGui::CalcTextSize("Availability:").x + 12.0f;
                const int trackIndex = FindHostableTrack(trackName);
                const float mapWidth = trackIndex >= 0 ? 145.0f : 0.0f;
                ImGui::BeginChild("SelectedRaceMetadata",
                                  ImVec2(ImGui::GetContentRegionAvail().x - mapWidth,
                                         122.0f), false);
                ImGui::Text("Track name:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::TextUnformatted(trackName.empty() ? "-" : trackName.c_str());
                ImGui::Text("Laps:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::Text("%d", laps);
                ImGui::Text("Availability:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::TextWrapped("%s", availability.empty() ? "-" : availability.c_str());
                ImGui::EndChild();
                if (trackIndex >= 0) {
                    TrackPreview& preview = lobbyTrackPreviews[trackIndex];
                    if (!preview.mLoaded) {
                        preview = LoadTrackPreview(graphics.GetRenderer(),
                                                   kHostableTracks[trackIndex]);
                    }
                    ImGui::SameLine();
                    ImGui::BeginChild("SelectedRaceMap", ImVec2(0, 122.0f), false);
                    DrawTrackPreview(preview, 110.0f, false);
                    ImGui::EndChild();
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Players list:");
                ImGui::BeginChild("PlayersListBox", ImVec2(-FLT_MIN, 90), true);
                if (phase == LobbyPhase::eWaitingRoom) {
                    ImGui::BulletText("You");
                    for (const std::string& member : raceMembers) {
                        ImGui::BulletText("%s", member.c_str());
                    }
                }
                else if (selectedGame != nullptr) {
                    ImGui::Text("%d player%s", selectedGame->mNumPlayers,
                                selectedGame->mNumPlayers == 1 ? "" : "s");
                }
                ImGui::EndChild();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("ActionsPanel", ImVec2(0, topHeight), true);
            if (phase == LobbyPhase::eWaitingRoom) {
                if (isHost) {
                    if (HoverNetButton("Start Race", ImVec2(-FLT_MIN, 0))) {
                        client.StartRace();
                        statusText = "Starting race...";
                    }
                }
                else {
                    ImGui::TextDisabled("Waiting for host...");
                }
                ImGui::Spacing();
                if (HoverNetButton("Cancel", ImVec2(-FLT_MIN, 0))) {
                    cancelWaitingRoom();
                }
            }
            else {
                const bool canJoin = selectedGame != nullptr && !selectedGame->mStarted;
                ImGui::BeginDisabled(!canJoin);
                if (HoverNetButton("Join Game...", ImVec2(-FLT_MIN, 0))) {
                    joinSelected();
                }
                ImGui::EndDisabled();
                ImGui::Spacing();
                if (HoverNetButton("Host Race...", ImVec2(-FLT_MIN, 0))) {
                    requestOpenHostPopup = true;
                }
            }
            // Push Quit to the bottom of the panel, mirroring the Windows dialog's
            // spaced-out button column.
            ImGui::SetCursorPosY(topHeight - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y);
            if (HoverNetButton("Quit", ImVec2(-FLT_MIN, 0))) {
                running = false;
            }
            ImGui::EndChild();

            ImGui::BeginChild("UsersListPanel", ImVec2(leftColWidth, 0), true);
            HoverNetSectionHeading("Users list");
            if (ImGui::BeginListBox("##users", ImVec2(-FLT_MIN, -FLT_MIN))) {
                for (const RaceServerPeer& user : lobbyUsers) {
                    ImGui::Selectable(user.mName.c_str());
                }
                ImGui::EndListBox();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("ChatPanel", ImVec2(0, 0), true);
            HoverNetSectionHeading("Chat section");
            ImGui::BeginChild("ChatLog", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()),
                              true);
            for (const std::string& chatLine : chatLog) {
                ImGui::TextWrapped("%s", chatLine.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            if (focusChatInput) {
                ImGui::SetKeyboardFocusHere();
                focusChatInput = false;
            }
            ImGui::SetNextItemWidth(-90);
            bool sendChat = ImGui::InputText("##chat", chatBuf, sizeof(chatBuf),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            sendChat = HoverNetButton("Send", ImVec2(-FLT_MIN, 0)) || sendChat;
            if (sendChat && chatBuf[0] != '\0') {
                const std::string outgoing(chatBuf);
                if (client.SendMessage(eRSMsgChatMessage, outgoing.data(), outgoing.size())) {
                    pushChat(username + ": " + outgoing);
                }
                chatBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::EndChild();
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
            ImGui::Combo("##HostTrack", &hostPrefs.mTrackIndex,
                         kHostableTracks, kHostableTrackCount);
            const TrackGuide& guide = kTrackGuides[hostPrefs.mTrackIndex];
            ImGui::Spacing();
            HoverNetSectionHeading("Course briefing");
            ImGui::Text("Style: %s", guide.mCharacter);
            ImGui::SameLine();
            ImGui::TextDisabled("  Difficulty: %s", guide.mDifficulty);
            ImGui::TextWrapped("%s", guide.mDescription);
            ImGui::Spacing();
            ImGui::TextUnformatted("Laps");
            ImGui::SameLine();
            ImGui::TextDisabled("Suggested: %d", guide.mSuggestedLaps);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderInt("##HostLaps", &hostPrefs.mLaps, 1, 20);
            ImGui::Checkbox("Weapons enabled", &hostPrefs.mWeapons);
            ImGui::EndChild();

            TrackPreview& preview = lobbyTrackPreviews[hostPrefs.mTrackIndex];
            if (!preview.mLoaded) {
                preview = LoadTrackPreview(graphics.GetRenderer(),
                                           kHostableTracks[hostPrefs.mTrackIndex]);
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

            if (HoverNetButton("Create Race", ImVec2(160, 36))) {
                hostRaceNow();
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (HoverNetButton("Cancel", ImVec2(120, 36))) {
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
        ImGui::Render();

        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
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
        SDL_Delay(16);
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
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 19.0f;
        io.Fonts->AddFontDefault(&fontConfig);
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
        SDL_Delay(16);
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
                         int pFrameLimit)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 19.0f;
        io.Fonts->AddFontDefault(&fontConfig);
        ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
        ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
        ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());
    }

    const char* options[] = {"RESUME RACE", pIsOnline ? "LEAVE RACE" : "NEW LOCAL RACE",
                             "ONLINE MULTIPLAYER", "SETTINGS", "CONTROLS", "QUIT HOVERNET"};
    constexpr int optionCount = sizeof(options) / sizeof(options[0]);
    PauseChoice choice = PauseChoice::eResume;
    bool focusFirstButton = true;
    bool running = true;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
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
        ImGui::SetWindowFontScale(2.1f);
        ImGui::TextUnformatted("GAME PAUSED");
        ImGui::SetWindowFontScale(1.0f);
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
            const float height = index < 3 ? 54.0f : 42.0f;
            if (HoverNetButton(options[index], ImVec2(-FLT_MIN, height))) {
                choice = static_cast<PauseChoice>(index);
                running = false;
            }
            if (index == 2) {
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
        SDL_Delay(16);
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
                                 int pRank, int pPlayerCount, int pFrameLimit)
{
    const bool ownsImGuiContext = ImGui::GetCurrentContext() == nullptr;
    if (ownsImGuiContext) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 19.0f;
        io.Fonts->AddFontDefault(&fontConfig);
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
        const float cardHeight = std::min(620.0f, workSize.y - 48.0f);
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        const char* actions[] = {"KEEP DRIVING", "NEW LOCAL RACE", "ONLINE MULTIPLAYER", "QUIT HOVERNET"};
        for (int index = 0; index < 4; ++index) {
            if (HoverNetButton(actions[index], ImVec2(-FLT_MIN, index == 0 ? 54.0f : 44.0f))) {
                choice = static_cast<PostRaceChoice>(index);
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
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        SDL_Delay(16);
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

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 19.0f;
    io.Fonts->AddFontDefault(&fontConfig);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    bool running = true;
    bool cancelled = false;
    int framesShown = 0;
    std::array<TrackPreview, kHostableTrackCount> trackPreviews{};

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

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, 76), false);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted("LOCAL RACE SETUP");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Spacing();

        const float panelWidth = std::min(900.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
        ImGui::BeginChild("LocalRaceSetupPanel", ImVec2(panelWidth, 0), true);

        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const bool sideBySide = contentWidth >= 610.0f;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float controlsWidth = sideBySide ? contentWidth * 0.52f : contentWidth;
        ImGui::BeginChild("RaceOptions", ImVec2(controlsWidth, sideBySide ? 0.0f : 390.0f), false);

        ImGui::PushItemWidth(-1.0f);
        ImGui::TextUnformatted("Track");
        ImGui::Combo("##Track", &prefs.mTrackIndex, kHostableTracks, kHostableTrackCount);
        ImGui::PopItemWidth();

        const TrackGuide& guide = kTrackGuides[prefs.mTrackIndex];
        ImGui::Spacing();
        HoverNetSectionHeading("Course briefing");
        ImGui::Text("Style: %s", guide.mCharacter);
        ImGui::SameLine();
        ImGui::TextDisabled("  Difficulty: %s", guide.mDifficulty);
        ImGui::TextWrapped("%s", guide.mDescription);
        ImGui::Spacing();

        ImGui::TextUnformatted("Laps");
        ImGui::SameLine();
        ImGui::TextDisabled("Suggested: %d", guide.mSuggestedLaps);
        ImGui::PushItemWidth(-1.0f);
        ImGui::SliderInt("##Laps", &prefs.mLaps, 1, 20);
        ImGui::PopItemWidth();
        ImGui::Spacing();
        ImGui::Checkbox("Weapons enabled", &prefs.mWeapons);
        ImGui::Spacing();
        ImGui::Spacing();

        if (HoverNetButton("Start Race", ImVec2(-FLT_MIN, 40))) {
            running = false;
        }
        ImGui::Spacing();
        if (HoverNetButton("Cancel", ImVec2(-FLT_MIN, 36))) {
            cancelled = true;
            running = false;
        }
        ImGui::EndChild();

        TrackPreview& preview = trackPreviews[prefs.mTrackIndex];
        if (!preview.mLoaded) {
            preview = LoadTrackPreview(graphics.GetRenderer(), kHostableTracks[prefs.mTrackIndex]);
        }
        if (sideBySide) {
            ImGui::SameLine(0.0f, gap);
            ImGui::BeginChild("TrackPreview", ImVec2(0, 0), false);
            DrawTrackPreview(preview);
            ImGui::EndChild();
        }
        else {
            ImGui::Spacing();
            ImGui::BeginChild("TrackPreview", ImVec2(0, 240.0f), false);
            DrawTrackPreview(preview);
            ImGui::EndChild();
        }

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        SDL_Delay(16);
    }

    for (TrackPreview& preview : trackPreviews) {
        if (preview.mTexture != nullptr) SDL_DestroyTexture(preview.mTexture);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    LocalRaceSetup result;
    result.confirmed = !cancelled;
    result.trackName = kHostableTracks[prefs.mTrackIndex];
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
    float volumePercent = static_cast<float>(currentVolume * 100.0);
    bool muted = currentMuted;
    const float currentUiScale = LoadUiScale();
    float uiScalePercent = currentUiScale * 100.0f;
    bool applyUiScale = false;
    bool largeHudText = LoadLargeHudText();
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
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 19.0f;
        io.Fonts->AddFontDefault(&fontConfig);
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

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, 76), false);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(1.35f);
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
        ImGui::BeginChild("SettingsPanel", ImVec2(centerWidth, 0), true);

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
        if (ImGui::Checkbox("High contrast menus", &highContrast)) {
            applyUiScale = true;
        }
        ImGui::TextDisabled("Strengthens text, borders, fields, and focus states.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

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
            volumePercent = 100.0f;
            muted = false;
            uiScalePercent = 100.0f;
            largeHudText = false;
            reducedMotion = false;
            highContrast = false;
            selectedResolution = 0;
            windowWidth = resolutions[0].width;
            windowHeight = resolutions[0].height;
            fullscreen = false;

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

        const bool canSave = !usernameBlank && !hostBlank && portValid;
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

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        SDL_Renderer* renderer = graphics.GetRenderer();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        if (applyUiScale) {
            ApplyHoverNetLobbyStyle(uiScalePercent / 100.0f, highContrast);
            applyUiScale = false;
        }
        SDL_Delay(16);
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
        if (!currentFullscreen) {
            SDL_SetWindowSize(graphics.GetWindow(), currentWindowWidth, currentWindowHeight);
            SDL_SetWindowPosition(graphics.GetWindow(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        if (!ownsImGuiContext) {
            ApplyHoverNetLobbyStyle(currentUiScale, currentHighContrast);
        }
    }

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
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 19.0f;
    io.Fonts->AddFontDefault(&fontConfig);
    ApplyHoverNetLobbyStyle(LoadUiScale(), LoadHighContrast());
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    const char* titles[] = {
        "CHOOSE A RACE", "DRIVE YOUR HOVERCRAFT", "READ THE RACE", "MAKE IT YOURS"
    };
    const char* bodies[] = {
        "Local Play lets you choose a track, lap count, and whether weapons are enabled. "
        "Online Lobby connects to the shared RaceServer, where you can join an open race "
        "or host one for other players.",
        "Use Left/Right to steer, Shift to accelerate, Down to brake or reverse, and Up "
        "to jump. Ctrl fires, Tab selects a weapon, and Escape opens the pause menu. "
        "A standard controller uses the left stick, triggers, A, X, Y, and Start.",
        "The top HUD shows elapsed time and lap progress. The status line reports speed, "
        "fuel, weapon readiness, and network latency. The minimap tracks racers. Open "
        "Controls from the main or pause menu whenever you need to review or remap inputs.",
        "Settings controls your display name, RaceServer, sound, window mode, resolution, "
        "UI scale, and accessibility options. Large HUD text keeps race information and "
        "chat readable; Reduced motion and High contrast adjust the menus. Changes preview "
        "live, Save keeps them, and Cancel restores your previous setup."
    };
    constexpr int pageCount = sizeof(titles) / sizeof(titles[0]);
    static_assert(pageCount == sizeof(bodies) / sizeof(bodies[0]),
                  "Every onboarding title needs body text");
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

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, 76), false);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted("WELCOME TO HOVERNET");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        const float panelWidth = std::min(680.0f, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
        ImGui::BeginChild("OnboardingPanel", ImVec2(panelWidth, -60.0f), true);
        ImGui::Text("STEP %d OF %d", page + 1, pageCount);
        HoverNetSectionHeading(titles[page]);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(bodies[page]);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (page > 0) {
            if (HoverNetButton("Back", ImVec2(130, 40))) --page;
            ImGui::SameLine();
        }
        const char* nextLabel = page == pageCount - 1 ? "Finish" : "Next";
        if (HoverNetButton(nextLabel, ImVec2(130, 40))) {
            if (page == pageCount - 1) {
                completed = true;
                running = false;
            }
            else {
                ++page;
            }
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        SDL_Delay(16);
    }

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
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 19.0f;
        io.Fonts->AddFontDefault(&fontConfig);
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

        ImGui::PushStyleColor(ImGuiCol_ChildBg, kHoverNetRed);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
        ImGui::BeginChild("HeaderBar", ImVec2(0, 76), false);
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(1.35f);
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
        SDL_Delay(16);
    }

    if (ownsImGuiContext) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
}

enum class MenuChoice
{
    eLocalPlay,
    eOnlineLobby,
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
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 19.0f;
    io.Fonts->AddFontDefault(&fontConfig);
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

    const char* options[] = {"Local Play", "Online Lobby", "Settings", "Controls", "How to Play"};
    constexpr int optionCount = sizeof(options) / sizeof(options[0]);
    MenuChoice choice = MenuChoice::eLocalPlay;
    bool focusFirstButton = true;
    bool running = true;
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

        const float panelWidth = std::min(430.0f, workSize.x * 0.42f);
        const float craftX = workPos.x + workSize.x * 0.30f;
        const float craftY = workPos.y + workSize.y * 0.68f;
        const float craftScale = std::min(workSize.x / 1200.0f, workSize.y / 760.0f);
        if (menuHovercraftTexture != nullptr) {
            // This hero render is derived from HoverNet's original round-skirt
            // craft: oversized turbines and a visible pilot give the menu a
            // stronger arcade-racing identity without under-body lighting.
            const float availableWidth = std::max(220.0f, workSize.x - panelWidth - 66.0f);
            const float craftSize = std::min(availableWidth, workSize.y * 0.72f);
            const float imageX = workPos.x + std::max(8.0f, (availableWidth - craftSize) * 0.5f);
            const float imageY = workPos.y + std::max(155.0f, workSize.y - craftSize - 18.0f);
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

        ImGui::SetCursorPos(ImVec2(48.0f, 48.0f));
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, kHoverNetWhite);
        ImGui::SetWindowFontScale(2.35f);
        ImGui::TextUnformatted("HOVERNET");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(kHoverNetCoral, "HIGH-SPEED COMBAT RACING");
        ImGui::Spacing();
        ImGui::TextDisabled("RACE THE LINE.  OWN THE TRACK.");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        const float panelHeight = std::min(580.0f, workSize.y - 84.0f);
        ImGui::SetCursorPos(ImVec2(workSize.x - panelWidth - 42.0f, 42.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 0.94f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 20.0f));
        ImGui::BeginChild("MainMenuPanel", ImVec2(panelWidth, panelHeight), true);
        ImGui::TextColored(kHoverNetCoral, "READY TO RACE?");
        ImGui::TextDisabled("Choose your mode");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (focusFirstButton) {
            ImGui::SetKeyboardFocusHere();
            focusFirstButton = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        if (HoverNetButton("LOCAL RACE", ImVec2(-FLT_MIN, 62))) {
            choice = MenuChoice::eLocalPlay;
            running = false;
        }
        ImGui::Spacing();
        if (HoverNetButton("ONLINE MULTIPLAYER", ImVec2(-FLT_MIN, 62))) {
            choice = MenuChoice::eOnlineLobby;
            running = false;
        }
        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextDisabled("GARAGE & SUPPORT");
        ImGui::Separator();
        ImGui::Spacing();
        for (int index = 2; index < optionCount; ++index) {
            if (HoverNetButton(options[index], ImVec2(-FLT_MIN, 42))) {
                choice = static_cast<MenuChoice>(index);
                running = false;
            }
            ImGui::Spacing();
        }
        ImGui::PushTextWrapPos(0.0f);
        HoverNetHint("Navigate with arrow keys or D-pad. Select with Enter or A.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        ImGui::SetCursorPos(ImVec2(22.0f, workSize.y - 30.0f));
        ImGui::TextDisabled("v%s   |   ESC / B: quit", HOVERNET_VERSION);
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 23, 23, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
        SDL_Delay(16);
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

int RunClient(int argc, char** argv)
{
#ifdef HOVERNET_GAME2_PLAYER
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
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    struct FactoryCleanup
    {
        ~FactoryCleanup() { MR_DllObjectFactory::Clean(FALSE); }
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
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(kWidth, kHeight) || !buffer.Lock()) {
        return FatalClientError("Could not create the 3D framebuffer.");
    }
    MR_ClientSession session;
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
    viewport.Setup(&buffer, 0, 0, kWidth, kHeight, 800);
#ifdef HOVERNET_GAME2_PLAYER
    MR_Observer* observer = playerMode ? MR_Observer::New() : nullptr;
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
        const std::string trackPath = SourcePath(("NetTarget/Tracks/" + initialTrackName + ".trk").c_str());
        if (!track->OpenForRead(trackPath.c_str()) ||
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
    std::string lobbyHost = kDefaultLobbyHost;
    unsigned lobbyPort = kDefaultLobbyPort;
    LoadServerUrl(lobbyHost, lobbyPort);  // a Settings change from a previous run, if any
    ParseLobbyArg(argc, argv, lobbyHost, lobbyPort);  // --lobby always wins, for dev/testing

    auto resetRaceSession = [&]() -> bool {
        session.SetElementCreationBroadcastHook(nullptr, nullptr);
        onlineClient.Disconnect();
        remotePlayers.clear();
        raceNames.clear();
        localClientId = -1;
        raceWasOnline = false;

        return true;
    };

    // Create the first gameplay session only after Local Play has selected it.
    auto loadLocalRace = [&](const std::string& newTrackName, int newLaps, bool newWeapons) -> bool {
        session.SetElementCreationBroadcastHook(nullptr, nullptr);
        onlineClient.Disconnect();
        remotePlayers.clear();
        raceNames.clear();
        localClientId = -1;
        raceWasOnline = false;

        MR_RecordFile* newTrackFile = new MR_RecordFile;
        const std::string newTrackPath = SourcePath(("NetTarget/Tracks/" + newTrackName + ".trk").c_str());
        if (!newTrackFile->OpenForRead(newTrackPath.c_str()) ||
            !session.LoadNew(newTrackName.c_str(), newTrackFile, newLaps, newWeapons ? TRUE : FALSE, &buffer) ||
            session.GetCurrentLevel() == nullptr || !session.CreateMainCharacter()) {
            std::fprintf(stderr, "Could not load '%s' for local play\n", newTrackName.c_str());
            return false;
        }
        level = session.GetCurrentLevel();
        mainCharacter = session.GetMainCharacter();
        currentTrackName = newTrackName;
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

    auto joinOnlineRace = [&]() -> bool {
        std::string joinedRace;
        std::string joinedTrack;
        int joinedLaps = 1;
        std::vector<RaceServerPeer> knownPeers;
        localClientId = -1;
        if (menuFontHandle == nullptr ||
            !RunLobbyScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(), onlineClient,
                            lobbyHost, lobbyPort, joinedRace, joinedTrack, joinedLaps, localClientId,
                            knownPeers, frameLimit)) {
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
            const std::string joinedTrackPath = SourcePath(("NetTarget/Tracks/" + joinedTrack + ".trk").c_str());
            if (!joinedTrackFile->OpenForRead(joinedTrackPath.c_str()) ||
                !session.LoadNew(joinedTrack.c_str(), joinedTrackFile, joinedLaps, allowWeapons, &buffer) ||
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

    if (playerMode && menuFontHandle == nullptr) {
        return FatalClientError("Could not load the menu font from ObjFac1.dat.");
    }
    if (playerMode) {
        if (HasArgument(argc, argv, "--lobby-screen")) {
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
        else if (HasArgument(argc, argv, "--pause-menu")) {
            RunPauseMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(), false, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--quit-confirmation")) {
            ConfirmQuit(graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--results-screen")) {
            RunPostRaceScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                              83456, 27341, 1, 4, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (HasArgument(argc, argv, "--onboarding")) {
            RunOnboardingScreen(graphics, frameLimit);
            g_QuitConfirmed = true;
        }
        else if (frameLimit < 0 && !HasCompletedOnboarding()) {
            if (RunOnboardingScreen(graphics, frameLimit)) SaveOnboardingComplete();
        }
        bool pickingMode = !g_QuitConfirmed;
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
                if (!joinOnlineRace()) {
                    std::printf("Lobby skipped or unavailable; returning to the main menu\n");
                    pickingMode = !g_QuitConfirmed;
                }
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
            else if (choice == MenuChoice::eControls) {
                RunControlsScreen(graphics, frameLimit);
                pickingMode = !g_QuitConfirmed;
            }
            else if (choice == MenuChoice::eHowToPlay) {
                if (RunOnboardingScreen(graphics, frameLimit)) SaveOnboardingComplete();
                pickingMode = !g_QuitConfirmed;
            }
            else if (autoPlay) {
                if (!loadLocalRace(initialTrackName, 1, true)) return 1;
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
    SDL_StartTextInput();
    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
        gameController.Refresh();
        bool sceneChanged = renderStats.actorsRendered > 0;
        bool horizontalMovement = false;
        const MR_3DCoordinate previousCamera = camera;
        bool leaveForLobby = false;
        bool startNewLocalRace = false;
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
            else if (event.type == SDL_TEXTINPUT && onlineClient.IsConnected()) {
                if (chatBuffer.size() < 60) {
                    chatBuffer += event.text.text;
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE &&
                     onlineClient.IsConnected() && !chatBuffer.empty()) {
                chatBuffer.pop_back();
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN &&
                     onlineClient.IsConnected() && !chatBuffer.empty()) {
                const std::string wireChat = RaceServerClient::EncodeInRaceChat(chatBuffer);
                onlineClient.SendMessage(eRSMsgChatMessage, wireChat.data(), wireChat.size());
                session.AddMessage(("You: " + chatBuffer).c_str());
                chatBuffer.clear();
            }
#endif
            else if ((event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) ||
                     (event.type == SDL_CONTROLLERBUTTONDOWN &&
                      event.cbutton.button == SDL_CONTROLLER_BUTTON_START)) {
#ifdef HOVERNET_GAME2_PLAYER
                // Used to only show this menu online -- offline play just quit
                // immediately on Escape, with no way back to it short of
                // relaunching, and no way to start a different local race
                // without doing so. Same menu either way now; only the
                // "Leave Race"/"New Local Race" label and what it does differ.
                if (playerMode && menuFontHandle != nullptr) {
                    const bool isOnline = onlineClient.IsConnected();
                    const PauseChoice pauseChoice = RunPauseMenu(
                        graphics, buffer, viewport, *menuFontHandle->GetSprite(), isOnline, frameLimit);
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
        if (leaveForLobby) {
            missileSeen = false;
            finishAnnounced = false;
            if (!resetRaceSession() || !joinOnlineRace()) {
                running = false;
                break;
            }
            wasOnlineConnected = onlineClient.IsConnected();
            continue;
        }
        if (startNewLocalRace) {
            const LocalRaceSetup setup = RunLocalRaceSetup(graphics, buffer, viewport,
                                                            *menuFontHandle->GetSprite(), frameLimit);
            if (setup.confirmed) {
                missileSeen = false;
                finishAnnounced = false;
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
        const bool turnLeft = keyboard[gKeyboardBindings.steerLeft] || controllerLeft ||
                              (!playerMode && keyboard[SDL_SCANCODE_A]);
        const bool turnRight = keyboard[gKeyboardBindings.steerRight] || controllerRight ||
                               (!playerMode && keyboard[SDL_SCANCODE_D]);
        const bool moveForward = playerMode ?
            keyboard[gKeyboardBindings.accelerate] ||
            (gKeyboardBindings.accelerate == SDL_SCANCODE_LSHIFT && keyboard[SDL_SCANCODE_RSHIFT]) ||
            controllerAccelerate :
            keyboard[SDL_SCANCODE_UP] || keyboard[SDL_SCANCODE_W];
        const bool moveBackward = playerMode ?
            keyboard[gKeyboardBindings.brake] || controllerBrake :
            keyboard[SDL_SCANCODE_DOWN] || keyboard[SDL_SCANCODE_S];
        const bool moveUp = keyboard[SDL_SCANCODE_Q];
        const bool moveDown = keyboard[SDL_SCANCODE_E];
        const bool fire = keyboard[gKeyboardBindings.fire] ||
                  (gKeyboardBindings.fire == SDL_SCANCODE_LCTRL && keyboard[SDL_SCANCODE_RCTRL]) ||
                  gameController.Button(gControllerBindings.fire) ||
                  HasArgument(argc, argv, "--fire");
        const bool jump = keyboard[gKeyboardBindings.jump] ||
                          gameController.Button(gControllerBindings.jump) ||
                          HasArgument(argc, argv, "--jump");
        const bool selectWeapon = keyboard[gKeyboardBindings.selectWeapon] ||
                                  gameController.Button(gControllerBindings.selectWeapon) ||
                                  HasArgument(argc, argv, "--select-weapon");

#ifdef HOVERNET_GAME2_PLAYER
        if (playerMode && mainCharacter != nullptr && level != nullptr) {
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
            int controlState = 0;
            if (autoPlay || moveForward) controlState |= MR_MainCharacter::eMotorOn;
            if (turnLeft) controlState |= MR_MainCharacter::eLeft;
            if (turnRight) controlState |= MR_MainCharacter::eRight;
            if (moveBackward) controlState |= MR_MainCharacter::eBreakDirection;
            if (fire) controlState |= MR_MainCharacter::eFire;
            if (jump) controlState |= MR_MainCharacter::eJump;
            if (selectWeapon) controlState |= MR_MainCharacter::eSelectWeapon;
            session.SetControlState(controlState, 0);
            session.Process();

#ifdef HOVERNET_GAME2_PLAYER
            if (wasOnlineConnected && !onlineClient.IsConnected()) {
                wasOnlineConnected = false;
                session.AddMessage("Connection to server lost -- continuing offline");
            }
            if (!finishAnnounced && mainCharacter->HasFinish()) {
                finishAnnounced = true;
                const PostRaceChoice resultChoice = RunPostRaceScreen(
                    graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                    mainCharacter->GetTotalTime(), mainCharacter->GetBestLapDuration(),
                    session.GetRank(mainCharacter), session.GetNbPlayers(),
                    frameLimit < 0 ? -1 : 2);
                if (resultChoice == PostRaceChoice::eQuit) {
                    running = false;
                    continue;
                }
                if (resultChoice == PostRaceChoice::eOnlineLobby) {
                    missileSeen = false;
                    finishAnnounced = false;
                    if (!resetRaceSession() || !joinOnlineRace()) {
                        running = false;
                    }
                    wasOnlineConnected = onlineClient.IsConnected();
                    continue;
                }
                if (resultChoice == PostRaceChoice::eNewLocalRace) {
                    const LocalRaceSetup setup = RunLocalRaceSetup(
                        graphics, buffer, viewport, *menuFontHandle->GetSprite(), frameLimit);
                    if (setup.confirmed) {
                        missileSeen = false;
                        finishAnnounced = false;
                        if (!loadLocalRace(setup.trackName, setup.laps, setup.weapons)) {
                            running = false;
                        }
                        wasOnlineConnected = onlineClient.IsConnected();
                    }
                    continue;
                }
                session.AddMessage("Race complete -- press ESC for race options");
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
                    else {
                        observer->SetNetworkLatency(-2);
                    }
                    observer->RenderNormalDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                                  session.GetBackImage());
                }
                if (!chatBuffer.empty() && menuFontHandle != nullptr) {
                    const std::string prompt = "Chat: " + chatBuffer + "_";
                    DrawUiText(*menuFontHandle->GetSprite(), 20, kHeight - 40, prompt.c_str(), &viewport,
                               MR_Sprite::eLeft, MR_Sprite::eTop, LoadLargeHudText() ? 1 : 2);
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

        SDL_Delay(16);
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
