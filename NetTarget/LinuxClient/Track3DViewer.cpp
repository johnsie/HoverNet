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

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <map>
#include <vector>

namespace
{
constexpr int kWidth = 1024;
constexpr int kHeight = 768;

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

struct UiRect
{
    int x;
    int y;
    int w;
    int h;

    bool Contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

constexpr MR_UInt8 kUiPanelColor = MR_RESERVED_COLORS_BEGINNING + 15;
constexpr MR_UInt8 kUiPanelAltColor = MR_RESERVED_COLORS_BEGINNING + 14;
constexpr MR_UInt8 kUiBorderColor = MR_RESERVED_COLORS_BEGINNING + 10;
constexpr MR_UInt8 kUiSelectionColor = MR_RESERVED_COLORS_BEGINNING + 43;
constexpr MR_UInt8 kUiButtonColor = MR_RESERVED_COLORS_BEGINNING + 12;
constexpr MR_UInt8 kUiButtonActiveColor = MR_RESERVED_COLORS_BEGINNING + 40;

void FillUiRect(MR_VideoBuffer& buffer, const UiRect& rect, MR_UInt8 color)
{
    const int left = std::max(0, rect.x);
    const int right = std::min(buffer.GetXRes(), rect.x + rect.w);
    const int top = std::max(0, rect.y);
    const int bottom = std::min(buffer.GetYRes(), rect.y + rect.h);
    if (left >= right || top >= bottom) {
        return;
    }
    MR_UInt8* pixels = buffer.GetBuffer();
    const int stride = buffer.GetLineLen();
    for (int y = top; y < bottom; ++y) {
        std::memset(pixels + y * stride + left, color, static_cast<std::size_t>(right - left));
    }
}

void OutlineUiRect(MR_VideoBuffer& buffer, const UiRect& rect, MR_UInt8 color)
{
    FillUiRect(buffer, UiRect{rect.x, rect.y, rect.w, 1}, color);
    FillUiRect(buffer, UiRect{rect.x, rect.y + rect.h - 1, rect.w, 1}, color);
    FillUiRect(buffer, UiRect{rect.x, rect.y, 1, rect.h}, color);
    FillUiRect(buffer, UiRect{rect.x + rect.w - 1, rect.y, 1, rect.h}, color);
}

void DrawUiPanel(MR_VideoBuffer& buffer, const UiRect& rect)
{
    FillUiRect(buffer, rect, kUiPanelColor);
    OutlineUiRect(buffer, rect, kUiBorderColor);
}

void DrawUiButton(MR_VideoBuffer& buffer, const MR_Sprite& font, MR_3DViewPort& viewport,
                  const UiRect& rect, const char* label, bool active = false)
{
    FillUiRect(buffer, rect, active ? kUiButtonActiveColor : kUiButtonColor);
    OutlineUiRect(buffer, rect, active ? kUiButtonActiveColor : kUiBorderColor);
    DrawUiText(font, rect.x + rect.w / 2, rect.y + rect.h / 2, label, &viewport,
               MR_Sprite::eCenter, MR_Sprite::eCenter, 2);
}

// Word-wraps text to fit the viewport's width (this font is fixed-width, so pixel
// width is exact from character count) instead of relying on every caller to hand-
// trim its own strings to fit -- that approach already broke three separate times
// as lines were extended, each only noticed from a screenshot after the fact.
// Draws left-aligned starting at (x, y) and returns the y position after the last
// wrapped line, so callers can keep laying out content below it.
int DrawUiTextWrapped(const MR_Sprite& font, int x, int y, int lineHeight, const char* text,
                      MR_3DViewPort* dest)
{
    // MR_Sprite::StrBlt advances by mWidth*3/4 per character (see Sprite.cpp), not
    // the full glyph cell width -- match that exactly or this under-estimates how
    // many characters actually fit and wraps too early.
    const int charWidth = std::max(1, font.GetItemWidth() * 3 / 4);
    const int maxChars = std::max(1, (dest->GetXRes() - x) / charWidth);

    std::string remaining(text);
    while (!remaining.empty()) {
        if (static_cast<int>(remaining.size()) <= maxChars) {
            DrawUiText(font, x, y, remaining.c_str(), dest);
            y += lineHeight;
            break;
        }
        // Break at the last space within the limit, so words don't get split.
        std::size_t breakAt = remaining.rfind(' ', static_cast<std::size_t>(maxChars));
        if (breakAt == std::string::npos || breakAt == 0) {
            breakAt = static_cast<std::size_t>(maxChars);
        }
        DrawUiText(font, x, y, remaining.substr(0, breakAt).c_str(), dest);
        y += lineHeight;
        const std::size_t nextStart = remaining.find_first_not_of(' ', breakAt);
        remaining = (nextStart == std::string::npos) ? std::string() : remaining.substr(nextStart);
    }
    return y;
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
const char* const kHostableTracks[] = {"ClassicH", "Steeplechase", "The Alley2", "The River"};
constexpr int kHostableTrackCount = 4;

struct HostPrefs
{
    int mTrackIndex = 0;
    int mLaps = 5;
    bool mWeapons = false;
};

std::string HostPrefsPath()
{
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/.hovernet_host_prefs";
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

enum class LobbyPhase
{
    eEnteringName, // First time in the lobby with no saved username yet
    eBrowsing,    // Picking or naming a race
    eWaitingRoom, // Joined a race, waiting for its creator to start it
};

std::string UsernamePath()
{
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/.hovernet_username";
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

void ApplyHoverNetLobbyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
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
    style.FrameBorderSize = 0.0f;

    // A slightly lighter shade per surface (window < panel < input field) is what
    // makes a flat dark UI read as layered instead of as one big black rectangle.
    const ImVec4 windowBg{0.09f, 0.09f, 0.11f, 1.00f};
    const ImVec4 panelBg{0.14f, 0.14f, 0.17f, 1.00f};
    const ImVec4 fieldBg{0.11f, 0.11f, 0.13f, 1.00f};
    const ImVec4 softBorder{0.24f, 0.24f, 0.28f, 1.00f};
    const ImVec4 text{0.93f, 0.93f, 0.95f, 1.00f};
    const ImVec4 textMuted{0.56f, 0.56f, 0.60f, 1.00f};
    const ImVec4 selection{0.86f, 0.20f, 0.27f, 0.35f};

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
    colors[ImGuiCol_CheckMark] = kHoverNetCoral;
    colors[ImGuiCol_SliderGrab] = kHoverNetRed;
    colors[ImGuiCol_SliderGrabActive] = kHoverNetRedActive;
    colors[ImGuiCol_Button] = kHoverNetRed;
    colors[ImGuiCol_ButtonHovered] = kHoverNetRedHover;
    colors[ImGuiCol_ButtonActive] = kHoverNetRedActive;
    colors[ImGuiCol_Header] = selection;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.86f, 0.20f, 0.27f, 0.55f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.86f, 0.20f, 0.27f, 0.75f);
    colors[ImGuiCol_Separator] = softBorder;
    colors[ImGuiCol_SeparatorHovered] = kHoverNetRedHover;
    colors[ImGuiCol_SeparatorActive] = kHoverNetRedActive;
    colors[ImGuiCol_ResizeGrip] = ImVec4(kHoverNetRed.x, kHoverNetRed.y, kHoverNetRed.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = kHoverNetRedHover;
    colors[ImGuiCol_ResizeGripActive] = kHoverNetRedActive;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kHoverNetRed.x, kHoverNetRed.y, kHoverNetRed.z, 0.40f);
    colors[ImGuiCol_ScrollbarBg] = windowBg;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = kHoverNetRedHover;
    colors[ImGuiCol_ScrollbarGrabActive] = kHoverNetRedActive;
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

// pClient is caller-owned (not constructed here) and deliberately left connected
// when this returns true: a joined-and-started race needs to keep talking to the
// server afterward (player position sync), so the connection can't be scoped to
// just this function the way it could when all it did was browse/chat.
bool RunLobbyScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                    const MR_Sprite& font, RaceServerClient& pClient, const std::string& host, unsigned port,
                    std::string& outJoinedName, std::string& outTrackName, int& outNumLaps,
                    int& outLocalClientId, std::vector<RaceServerPeer>& outPeers, int pFrameLimit)
{
    // The lobby screen renders through Dear ImGui directly against the SDL_Renderer
    // graphics already owns, not the paletted MR_VideoBuffer the 3D game view uses --
    // buffer/viewport/font are unused here now, kept only so the call site (shared
    // with the old rendering path) doesn't need to change.
    (void)buffer;
    (void)viewport;
    (void)font;
    RaceServerClient& client = pClient;

    if (!client.Connect(host, port)) {
        std::fprintf(stderr, "Lobby screen: could not connect to RaceServer at %s:%u\n", host.c_str(), port);
        return false;
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
    std::vector<std::string> raceMembers;
    int framesShown = 0;
    bool hostPopupOpen = false;
    bool requestOpenHostPopup = false;
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

    auto refreshGames = [&]() {
        gamesBeingListed.clear();
        client.SendMessage(eRSMsgListGames, nullptr, 0);
        lastRefresh = SDL_GetTicks();
        statusText = "Refreshing race list...";
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

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // This is a fixed-purpose screen, not a user-arranged workspace -- don't leave
    // an imgui.ini behind in whatever directory the game happens to run from.
    io.IniFilename = nullptr;
    // ImGui's default font renders at 13px, which looks cramped blown up across a
    // 1024x768 screen -- build the atlas at a larger fixed size instead of scaling
    // the default bitmap font (which just blurs it).
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 19.0f;
    io.Fonts->AddFontDefault(&fontConfig);
    ApplyHoverNetLobbyStyle();
    ImGui_ImplSDL2_InitForSDLRenderer(graphics.GetWindow(), graphics.GetRenderer());
    ImGui_ImplSDLRenderer2_Init(graphics.GetRenderer());

    SDL_StartTextInput();
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
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

        if (phase == LobbyPhase::eEnteringName) {
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
                ImGui::Text("Track name:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::TextUnformatted(trackName.empty() ? "-" : trackName.c_str());
                ImGui::Text("Laps:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::Text("%d", laps);
                ImGui::Text("Availability:");
                ImGui::SameLine(labelColumnWidth);
                ImGui::TextWrapped("%s", availability.empty() ? "-" : availability.c_str());
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
        if (ImGui::BeginPopupModal("HostRace", &hostPopupOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Combo("Track", &hostPrefs.mTrackIndex, kHostableTracks, kHostableTrackCount);
            ImGui::SliderInt("Laps", &hostPrefs.mLaps, 1, 20);
            ImGui::Checkbox("Weapons", &hostPrefs.mWeapons);
            ImGui::Spacing();
            if (HoverNetButton("Create Race", ImVec2(140, 32))) {
                hostRaceNow();
                hostPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (HoverNetButton("Cancel", ImVec2(120, 32))) {
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
        SDL_Delay(16);
    }
    SDL_StopTextInput();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    return joined;
}

enum class PauseChoice
{
    eResume,
    eLeaveRace,
    eQuit,
};

PauseChoice RunPauseMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer,
                         MR_3DViewPort& viewport, const MR_Sprite& font)
{
    const char* options[] = {"Resume", "Leave Race", "Quit HoverNet"};
    constexpr int optionCount = 3;
    int selected = 0;
    const int panelWidth = std::min(520, viewport.GetXRes() - 48);
    const int panelHeight = 330;
    const UiRect panel{(viewport.GetXRes() - panelWidth) / 2,
                       (viewport.GetYRes() - panelHeight) / 2, panelWidth, panelHeight};
    const int buttonWidth = panelWidth - 80;
    const int buttonHeight = 48;
    const int firstButtonY = panel.y + 112;

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                return PauseChoice::eQuit;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    return PauseChoice::eResume;
                }
                if (event.key.keysym.sym == SDLK_UP) {
                    selected = (selected + optionCount - 1) % optionCount;
                }
                else if (event.key.keysym.sym == SDLK_DOWN) {
                    selected = (selected + 1) % optionCount;
                }
                else if (event.key.keysym.sym == SDLK_RETURN) {
                    return static_cast<PauseChoice>(selected);
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                for (int index = 0; index < optionCount; ++index) {
                    const UiRect button{panel.x + 40, firstButtonY + index * (buttonHeight + 12),
                                        buttonWidth, buttonHeight};
                    if (button.Contains(event.button.x, event.button.y)) {
                        return static_cast<PauseChoice>(index);
                    }
                }
            }
            else if (event.type == SDL_MOUSEMOTION) {
                for (int index = 0; index < optionCount; ++index) {
                    const UiRect button{panel.x + 40, firstButtonY + index * (buttonHeight + 12),
                                        buttonWidth, buttonHeight};
                    if (button.Contains(event.motion.x, event.motion.y)) {
                        selected = index;
                    }
                }
            }
        }

        DrawUiPanel(buffer, panel);
        DrawUiText(font, panel.x + panel.w / 2, panel.y + 24, "GAME PAUSED", &viewport,
                   MR_Sprite::eCenter, MR_Sprite::eTop, 1);
        for (int index = 0; index < optionCount; ++index) {
            const UiRect button{panel.x + 40, firstButtonY + index * (buttonHeight + 12),
                                buttonWidth, buttonHeight};
            DrawUiButton(buffer, font, viewport, button, options[index], index == selected);
        }
        graphics.Present(buffer.GetBuffer(), kWidth, kHeight);
        SDL_Delay(16);
    }
}

enum class MenuChoice
{
    eLocalPlay,
    eOnlineLobby,
};

// The very first screen in player mode: pick local play or the online lobby. Bounded
// by pFrameLimit exactly like RunLobbyScreen, and defaults to eLocalPlay if the
// player never chooses (Escape, or the frame budget runs out under an automated/
// headless run) -- this is what keeps every existing --frames-bounded test working
// unchanged even though the menu is now always shown first in player mode.
MenuChoice RunMainMenu(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                       const MR_Sprite& font, int pFrameLimit)
{
    const int lineHeight = std::max(1, font.GetItemHeight());
    int selected = 0;
    const char* options[] = {"Local Play", "Online Lobby"};
    const int optionCount = 2;

    bool running = true;
    MenuChoice choice = MenuChoice::eLocalPlay;
    int framesShown = 0;

    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    running = false;
                }
                else if (key == SDLK_UP) {
                    selected = (selected + optionCount - 1) % optionCount;
                }
                else if (key == SDLK_DOWN) {
                    selected = (selected + 1) % optionCount;
                }
                else if (key == SDLK_RETURN) {
                    choice = (selected == 1) ? MenuChoice::eOnlineLobby : MenuChoice::eLocalPlay;
                    running = false;
                }
            }
        }

        viewport.Clear(0);
        int y = lineHeight * 3;
        DrawUiText(font, viewport.GetXRes() / 2, y, "HOVERNET", &viewport, MR_Sprite::eCenter, MR_Sprite::eTop);
        y += lineHeight * 3;
        for (int index = 0; index < optionCount; ++index) {
            char line[64];
            std::snprintf(line, sizeof(line), "%s%s", index == selected ? "> " : "  ", options[index]);
            DrawUiText(font, viewport.GetXRes() / 2, y, line, &viewport, MR_Sprite::eCenter, MR_Sprite::eTop);
            y += lineHeight;
        }
        y += lineHeight;
        DrawUiText(font, viewport.GetXRes() / 2, y, "Up/Down   Enter: confirm", &viewport,
                   MR_Sprite::eCenter, MR_Sprite::eTop);

        graphics.Present(buffer.GetBuffer(), kWidth, kHeight);
        SDL_Delay(16);
    }

    return choice;
}
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

bool IsPlayerMode(int argc, char** argv)
{
#ifdef HOVERNET_GAME2_PLAYER
    return true;
#else
    return HasArgument(argc, argv, "--play");
#endif
}
}

int main(int argc, char** argv)
{
    MR_InitTrigoTables();
    MR_MainCharacter::RegisterFactory();
    struct FactoryCleanup
    {
        ~FactoryCleanup() { MR_DllObjectFactory::Clean(FALSE); }
    } factoryCleanup;
#ifdef HOVERNET_GAME2_PLAYER
    MR_SoundServer::Init(nullptr);
    struct SoundServerCleanup
    {
        ~SoundServerCleanup() { MR_SoundServer::Close(); }
    } soundServerCleanup;
#endif

    MR_RecordFile* track = new MR_RecordFile;
    const std::string trackPath = SourcePath("NetTarget/Tracks/ClassicH.trk");
    if (!track->OpenForRead(trackPath.c_str())) {
        std::fprintf(stderr, "Could not open ClassicH.trk\n");
        delete track;
        return 1;
    }

    const bool playerMode = IsPlayerMode(argc, argv);
    MR_VideoBuffer buffer(nullptr, 1.0, 0.5, 0.5);
    if (!buffer.SetVideoMode(kWidth, kHeight) || !buffer.Lock()) {
        std::fprintf(stderr, "Could not create 3D framebuffer\n");
        return 1;
    }
    MR_ClientSession session;
    const BOOL allowWeapons = playerMode ? TRUE : FALSE;
    if (!session.LoadNew("ClassicH", track, 1, allowWeapons, &buffer) || session.GetCurrentLevel() == nullptr) {
        std::fprintf(stderr, "Could not load ClassicH.trk\n");
        return 1;
    }

    if (playerMode && !session.CreateMainCharacter()) {
        std::fprintf(stderr, "Could not create the local player\n");
        return 1;
    }
    if (playerMode) {
        session.SetSimulationTime(-6000);
    }

    const MR_Level* level = session.GetCurrentLevel();
    MR_MainCharacter* mainCharacter = session.GetMainCharacter();
    const bool autoPlay = playerMode && HasArgument(argc, argv, "--autoplay");
    MR_3DCoordinate startingPlayerPosition;
    if (autoPlay) {
        session.SetSimulationTime(0);
        startingPlayerPosition = mainCharacter->mPosition;
    }
    const int player = 0;
    int room = level->GetStartingRoom(player);
    if (room < 0 || room >= level->GetRoomCount()) {
        std::fprintf(stderr, "ClassicH.trk has no valid starting room\n");
        return 1;
    }

    const std::string resourcePath = SourcePath("NetTarget/ObjFac1.dat");
    MR_ResourceLib resources(resourcePath.c_str());

    MR_3DViewPort viewport;
    viewport.Setup(&buffer, 0, 0, kWidth, kHeight, 800);
#ifdef HOVERNET_GAME2_PLAYER
    MR_Observer* observer = playerMode ? MR_Observer::New() : nullptr;
    bool debugView = playerMode && HasArgument(argc, argv, "--debug");
    if (playerMode && observer == nullptr) {
        std::fprintf(stderr, "Could not create the Game2 observer\n");
        return 1;
    }
#endif
    MR_3DCoordinate camera = level->GetStartingPos(player);
    camera.mZ += 700;
    MR_Angle orientation = level->GetStartingOrientation(player);
    if (HasArgument(argc, argv, "--powerup")) {
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
    ClampCameraHeight(*level, room, camera);
    RenderStats renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                          SDL_GetTicks(), mainCharacter);
#ifdef HOVERNET_GAME2_PLAYER
    if (playerMode) {
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
    const int nonZeroPixels = static_cast<int>(std::count_if(buffer.GetBuffer(),
        buffer.GetBuffer() + kWidth * kHeight, [](MR_UInt8 pixel) { return pixel != 0; }));
    if (renderStats.surfacesRendered == 0 || nonZeroPixels == 0) {
        std::fprintf(stderr, "ClassicH starting room did not render visible surfaces\n");
        return 1;
    }

    SDL2GraphicsBackend graphics;
    if (!graphics.Initialize(nullptr, kWidth, kHeight)) {
        return 1;
    }
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

    const int frameLimit = ParseFrameCount(argc, argv);

#ifdef HOVERNET_GAME2_PLAYER
    // When set, onlineClient stays connected for the rest of the run: the main
    // loop below sends this player's position each frame and applies updates from
    // remotePlayers (one MR_MainCharacter per other racer, keyed by their server-
    // assigned client id -- see RaceServerClient::SendPlayerState/ParsePlayerState
    // for why that id has to be threaded through explicitly rather than assumed).
    RaceServerClient onlineClient;
    OnlineElementBroadcastContext elementBroadcastContext{&onlineClient};
    int localClientId = -1;
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
    ParseLobbyArg(argc, argv, lobbyHost, lobbyPort);

    auto resetRaceSession = [&]() -> bool {
        session.SetElementCreationBroadcastHook(nullptr, nullptr);
        onlineClient.Disconnect();
        remotePlayers.clear();
        raceNames.clear();
        localClientId = -1;

        MR_RecordFile* freshTrack = new MR_RecordFile;
        if (!freshTrack->OpenForRead(trackPath.c_str())) {
            delete freshTrack;
            std::fprintf(stderr, "Could not reopen ClassicH.trk while leaving race\n");
            return false;
        }
        if (!session.LoadNew("ClassicH", freshTrack, 1, allowWeapons, &buffer) ||
            !session.CreateMainCharacter()) {
            std::fprintf(stderr, "Could not reset the gameplay session while leaving race\n");
            return false;
        }

        level = session.GetCurrentLevel();
        mainCharacter = session.GetMainCharacter();
        if (level == nullptr || mainCharacter == nullptr) {
            return false;
        }
        session.SetSimulationTime(-6000);
        room = level->GetStartingRoom(0);
        camera = mainCharacter->mPosition;
        camera.mZ += 700;
        orientation = mainCharacter->GetCabinOrientation();
        renderStats = RenderScene(*level, room, camera, orientation, viewport, resources,
                                  SDL_GetTicks(), mainCharacter);
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

        // The level loaded at startup (or left over from a previous online race) is
        // whatever track happened to be current before this -- reload with the
        // track this race actually uses. LoadNew() invalidates the previous
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
            remotePlayers.clear();
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
        return true;
    };

    if (playerMode && menuFontHandle != nullptr) {
        const MenuChoice choice = RunMainMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                                              frameLimit);
        if (choice == MenuChoice::eOnlineLobby && !joinOnlineRace()) {
            std::printf("Lobby skipped or unavailable; continuing with local play\n");
        }
    }
#endif

    std::printf("ClassicH 3D view: room=%d surfaces=%d actors=%d pixels=%d\n",
                room, renderStats.surfacesRendered, renderStats.actorsRendered, nonZeroPixels);
    int framesRendered = 0;
    bool running = true;
    bool cockpitView = false;
    bool missileSeen = false;
    // In-race chat, scoped to whatever race this connection is in (see the
    // eRSMsgChatMessage handling below) -- text input only actually does
    // anything once online, but it's harmless to leave enabled for local play.
    std::string chatBuffer;
    SDL_StartTextInput();
    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
        bool sceneChanged = renderStats.actorsRendered > 0;
        bool horizontalMovement = false;
        const MR_3DCoordinate previousCamera = camera;
        bool leaveForLobby = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
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
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
#ifdef HOVERNET_GAME2_PLAYER
                if (playerMode && onlineClient.IsConnected() && menuFontHandle != nullptr) {
                    const PauseChoice pauseChoice = RunPauseMenu(
                        graphics, buffer, viewport, *menuFontHandle->GetSprite());
                    if (pauseChoice == PauseChoice::eQuit) {
                        running = false;
                    }
                    else if (pauseChoice == PauseChoice::eLeaveRace) {
                        leaveForLobby = true;
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
            if (!resetRaceSession() || !joinOnlineRace()) {
                running = false;
                break;
            }
            continue;
        }
#endif

        const Uint8* keyboard = SDL_GetKeyboardState(nullptr);
        const bool turnLeft = keyboard[SDL_SCANCODE_LEFT] || (!playerMode && keyboard[SDL_SCANCODE_A]);
        const bool turnRight = keyboard[SDL_SCANCODE_RIGHT] || (!playerMode && keyboard[SDL_SCANCODE_D]);
        const bool moveForward = playerMode ? keyboard[SDL_SCANCODE_LSHIFT] || keyboard[SDL_SCANCODE_RSHIFT]
                            : keyboard[SDL_SCANCODE_UP] || keyboard[SDL_SCANCODE_W];
        const bool moveBackward = playerMode ? keyboard[SDL_SCANCODE_DOWN]
                             : keyboard[SDL_SCANCODE_DOWN] || keyboard[SDL_SCANCODE_S];
        const bool moveUp = keyboard[SDL_SCANCODE_Q];
        const bool moveDown = keyboard[SDL_SCANCODE_E];
        const bool fire = keyboard[SDL_SCANCODE_LCTRL] || keyboard[SDL_SCANCODE_RCTRL] ||
                  HasArgument(argc, argv, "--fire");
        const bool jump = keyboard[SDL_SCANCODE_UP] || HasArgument(argc, argv, "--jump");
        const bool selectWeapon = keyboard[SDL_SCANCODE_TAB] || HasArgument(argc, argv, "--select-weapon");

#ifdef HOVERNET_GAME2_PLAYER
        if (playerMode) {
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

        if (playerMode) {
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
                const MR_ElementNetState localState = mainCharacter->GetNetState();
                onlineClient.SendPlayerState(localClientId, localState.mData, localState.mDataLen);

                RaceServerMessage netMessage;
                while (onlineClient.PollMessage(netMessage, 0)) {
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
                    observer->RenderNormalDisplay(&buffer, &session, mainCharacter, session.GetSimulationTime(),
                                                  session.GetBackImage());
                }
                if (!chatBuffer.empty() && menuFontHandle != nullptr) {
                    const std::string prompt = "Chat: " + chatBuffer + "_";
                    DrawUiText(*menuFontHandle->GetSprite(), 20, kHeight - 40, prompt.c_str(), &viewport,
                               MR_Sprite::eLeft, MR_Sprite::eTop, 2);
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
        SDL_Delay(16);
    }

    if (autoPlay && mainCharacter->mPosition == startingPlayerPosition) {
        std::fprintf(stderr, "Autoplay did not move the main character\n");
        return 1;
    }
    if (autoPlay) {
        std::printf("Autoplay player position=(%d,%d,%d)\n", mainCharacter->mPosition.mX,
                    mainCharacter->mPosition.mY, mainCharacter->mPosition.mZ);
    }
    if (playerMode && HasArgument(argc, argv, "--fire") &&
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