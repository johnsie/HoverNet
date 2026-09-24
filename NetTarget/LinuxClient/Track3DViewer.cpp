#include "../GraphicsSDL2/SDL2Graphics.h"
#include "../Game2/ClientSession.h"
#ifdef HOVERNET_GAME2_PLAYER
#include "../Game2/Observer.h"
#include "../VideoServices/SoundServer.h"
#include "RaceServerClient.h"
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
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
constexpr int kWidth = 1024;
constexpr int kHeight = 768;

#ifdef HOVERNET_GAME2_PLAYER
// The production RaceServer, deployed by the GitLab pipeline. --lobby overrides this.
constexpr const char* kDefaultLobbyHost = "192.168.10.181";
constexpr unsigned kDefaultLobbyPort = 9600;
#endif

#ifndef HOVERNET_SOURCE_DIR
#define HOVERNET_SOURCE_DIR "."
#endif

std::string SourcePath(const char* relativePath)
{
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
                MR_Sprite::eAlignment hAlign = MR_Sprite::eLeft, MR_Sprite::eAlignment vAlign = MR_Sprite::eTop)
{
    font.StrBlt(x, y, Ascii2Simple(text), dest, hAlign, vAlign);
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
enum class LobbyInputMode
{
    eNone,
    eHostRace,
    eChat,
};

enum class LobbyPhase
{
    eBrowsing,    // Picking or naming a race
    eWaitingRoom, // Joined a race, waiting for its creator to start it
};

bool RunLobbyScreen(SDL2GraphicsBackend& graphics, MR_VideoBuffer& buffer, MR_3DViewPort& viewport,
                    const MR_Sprite& font, const std::string& host, unsigned port,
                    std::string& outJoinedName, int pFrameLimit)
{
    const int lineHeight = std::max(1, font.GetItemHeight());

    RaceServerClient client;
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
        constexpr std::size_t kMaxChatLines = 8;
        if (chatLog.size() > kMaxChatLines) {
            chatLog.erase(chatLog.begin());
        }
    };

    int selected = 0;
    LobbyInputMode inputMode = LobbyInputMode::eNone;
    LobbyPhase phase = LobbyPhase::eBrowsing;
    std::string inputBuffer;
    Uint32 lastRefresh = SDL_GetTicks();
    bool running = true;
    bool joined = false;
    bool isHost = false;
    std::vector<std::string> raceMembers;
    int framesShown = 0;

    SDL_StartTextInput();
    while (running && (pFrameLimit < 0 || framesShown < pFrameLimit)) {
        ++framesShown;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (inputMode != LobbyInputMode::eNone && event.type == SDL_TEXTINPUT) {
                if (inputBuffer.size() < 60) {
                    inputBuffer += event.text.text;
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    if (inputMode != LobbyInputMode::eNone) {
                        inputMode = LobbyInputMode::eNone;
                        inputBuffer.clear();
                    }
                    else {
                        running = false;
                    }
                }
                else if (inputMode == LobbyInputMode::eHostRace) {
                    if (key == SDLK_BACKSPACE && !inputBuffer.empty()) {
                        inputBuffer.pop_back();
                    }
                    else if (key == SDLK_RETURN && !inputBuffer.empty()) {
                        if (client.JoinGame(inputBuffer)) {
                            outJoinedName = inputBuffer;
                            phase = LobbyPhase::eWaitingRoom;
                            inputMode = LobbyInputMode::eNone;
                            raceMembers.clear();
                        }
                        inputBuffer.clear();
                    }
                }
                else if (inputMode == LobbyInputMode::eChat) {
                    if (key == SDLK_BACKSPACE && !inputBuffer.empty()) {
                        inputBuffer.pop_back();
                    }
                    else if (key == SDLK_RETURN && !inputBuffer.empty()) {
                        if (client.SendMessage(eRSMsgChatMessage, inputBuffer.data(), inputBuffer.size())) {
                            pushChat("You: " + inputBuffer);
                        }
                        inputBuffer.clear();  // Stay in chat mode -- keep the conversation going
                    }
                }
                else if (key == SDLK_t) {
                    inputMode = LobbyInputMode::eChat;
                    inputBuffer.clear();
                }
                else if (phase == LobbyPhase::eBrowsing) {
                    if (key == SDLK_UP && !games.empty()) {
                        selected = (selected + static_cast<int>(games.size()) - 1) % static_cast<int>(games.size());
                    }
                    else if (key == SDLK_DOWN && !games.empty()) {
                        selected = (selected + 1) % static_cast<int>(games.size());
                    }
                    else if (key == SDLK_r) {
                        gamesBeingListed.clear();
                        client.SendMessage(eRSMsgListGames, nullptr, 0);
                        lastRefresh = SDL_GetTicks();
                    }
                    else if (key == SDLK_n) {
                        inputMode = LobbyInputMode::eHostRace;
                        inputBuffer.clear();
                    }
                    else if (key == SDLK_RETURN && !games.empty()) {
                        if (client.JoinGame(games[static_cast<std::size_t>(selected)].mName)) {
                            outJoinedName = games[static_cast<std::size_t>(selected)].mName;
                            phase = LobbyPhase::eWaitingRoom;
                            raceMembers.clear();
                        }
                    }
                }
                else if (phase == LobbyPhase::eWaitingRoom) {
                    if (key == SDLK_s && isHost) {
                        client.StartRace();
                    }
                }
            }
        }

        if (running && SDL_GetTicks() - lastRefresh > 3000) {
            gamesBeingListed.clear();
            client.SendMessage(eRSMsgListGames, nullptr, 0);
            lastRefresh = SDL_GetTicks();
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
                }
            }
            else if (message.mType == eRSMsgChatMessage) {
                pushChat(std::string(message.mData.begin(), message.mData.end()));
            }
            else if (message.mType == eRSMsgJoinedRace) {
                RaceServerJoinAck ack;
                if (RaceServerClient::ParseJoinedRace(message, ack)) {
                    isHost = ack.mIsHost;
                }
            }
            else if (message.mType == eRSMsgConnNameSet) {
                RaceServerPeer peer;
                if (RaceServerClient::ParsePeer(message, peer)) {
                    raceMembers.push_back(peer.mName);
                    pushChat("* " + peer.mName + " joined");
                }
            }
            else if (message.mType == eRSMsgRaceStarted) {
                joined = true;
                running = false;
            }
        }

        viewport.Clear(0);
        int y = lineHeight;

        if (phase == LobbyPhase::eWaitingRoom) {
            char titleLine[64];
            std::snprintf(titleLine, sizeof(titleLine), "IN RACE: %s", outJoinedName.c_str());
            DrawUiText(font, viewport.GetXRes() / 2, y, titleLine, &viewport, MR_Sprite::eCenter, MR_Sprite::eTop);
            y += lineHeight * 2;

            if (raceMembers.empty()) {
                DrawUiText(font, 20, y, "(waiting for other players...)", &viewport, MR_Sprite::eLeft,
                           MR_Sprite::eTop);
                y += lineHeight;
            }
            else {
                for (const std::string& member : raceMembers) {
                    DrawUiText(font, 20, y, member.c_str(), &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
                    y += lineHeight;
                }
            }
            y += lineHeight;
            DrawUiText(font, 20, y,
                       isHost ? "You are the host." : "Waiting for the host to start the race...",
                       &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
            y += lineHeight;
        }
        else {
            DrawUiText(font, viewport.GetXRes() / 2, y, "HOVERRACE LOBBY", &viewport, MR_Sprite::eCenter,
                       MR_Sprite::eTop);
            y += lineHeight * 2;

            if (games.empty()) {
                DrawUiText(font, 20, y, "(no open races -- press N to host one)", &viewport, MR_Sprite::eLeft,
                           MR_Sprite::eTop);
                y += lineHeight;
            }
            else {
                for (std::size_t index = 0; index < games.size(); ++index) {
                    const RaceServerGameInfo& game = games[index];
                    char line[128];
                    std::snprintf(line, sizeof(line), "%s%-24s track=%-12s laps=%d players=%d%s",
                                  static_cast<int>(index) == selected ? "> " : "  ", game.mName.c_str(),
                                  game.mTrack.c_str(), game.mNumLaps, game.mNumPlayers,
                                  game.mStarted ? " (in progress)" : "");
                    DrawUiText(font, 20, y, line, &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
                    y += lineHeight;
                }
            }
        }
        y += lineHeight;

        DrawUiText(font, 20, y, "-- Chat --", &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
        y += lineHeight;
        for (const std::string& chatLine : chatLog) {
            DrawUiText(font, 20, y, chatLine.c_str(), &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
            y += lineHeight;
        }
        y += lineHeight;

        if (inputMode == LobbyInputMode::eHostRace) {
            char line[80];
            std::snprintf(line, sizeof(line), "New race name: %s_", inputBuffer.c_str());
            DrawUiText(font, 20, y, line, &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
            y += lineHeight;
            DrawUiText(font, 20, y, "Enter: host it   Esc: cancel", &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
        }
        else if (inputMode == LobbyInputMode::eChat) {
            char line[80];
            std::snprintf(line, sizeof(line), "Say: %s_", inputBuffer.c_str());
            DrawUiText(font, 20, y, line, &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
            y += lineHeight;
            DrawUiText(font, 20, y, "Enter: send   Esc: stop chatting", &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
        }
        else if (phase == LobbyPhase::eWaitingRoom) {
            if (isHost) {
                DrawUiText(font, 20, y, "S: start the race   T: chat   Esc: leave", &viewport, MR_Sprite::eLeft,
                           MR_Sprite::eTop);
            }
            else {
                DrawUiText(font, 20, y, "T: chat   Esc: leave", &viewport, MR_Sprite::eLeft, MR_Sprite::eTop);
            }
        }
        else {
            DrawUiText(font, 20, y, "Up/Down: select   Enter: join   Esc: skip", &viewport, MR_Sprite::eLeft,
                       MR_Sprite::eTop);
            y += lineHeight;
            DrawUiText(font, 20, y, "N: host a race   T: chat   R: refresh", &viewport, MR_Sprite::eLeft,
                       MR_Sprite::eTop);
        }

        graphics.Present(buffer.GetBuffer(), kWidth, kHeight);
        SDL_Delay(16);
    }
    SDL_StopTextInput();

    return joined;
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
        DrawUiText(font, viewport.GetXRes() / 2, y, "HOVERRACE", &viewport, MR_Sprite::eCenter, MR_Sprite::eTop);
        y += lineHeight * 3;
        for (int index = 0; index < optionCount; ++index) {
            char line[64];
            std::snprintf(line, sizeof(line), "%s%s", index == selected ? "> " : "  ", options[index]);
            DrawUiText(font, viewport.GetXRes() / 2, y, line, &viewport, MR_Sprite::eCenter, MR_Sprite::eTop);
            y += lineHeight;
        }
        y += lineHeight;
        DrawUiText(font, viewport.GetXRes() / 2, y, "Up/Down: select   Enter: confirm", &viewport,
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

    const MR_Level& level = *session.GetCurrentLevel();
    MR_MainCharacter* mainCharacter = session.GetMainCharacter();
    const bool autoPlay = playerMode && HasArgument(argc, argv, "--autoplay");
    MR_3DCoordinate startingPlayerPosition;
    if (autoPlay) {
        session.SetSimulationTime(0);
        startingPlayerPosition = mainCharacter->mPosition;
    }
    const int player = 0;
    int room = level.GetStartingRoom(player);
    if (room < 0 || room >= level.GetRoomCount()) {
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
    MR_3DCoordinate camera = level.GetStartingPos(player);
    camera.mZ += 700;
    MR_Angle orientation = level.GetStartingOrientation(player);
    if (HasArgument(argc, argv, "--powerup")) {
        bool powerUpFound = false;
        for (int candidateRoom = 0; candidateRoom < level.GetRoomCount() && !powerUpFound; ++candidateRoom) {
            MR_FreeElementHandle handle = level.GetFirstFreeElement(candidateRoom);
            while (handle != nullptr) {
                MR_FreeElement* element = level.GetFreeElement(handle);
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
                handle = level.GetNextFreeElement(handle);
            }
        }
        if (!powerUpFound) {
            std::fprintf(stderr, "ClassicH does not contain a power-up\n");
            return 1;
        }
    }
    ClampCameraHeight(level, room, camera);
    RenderStats renderStats = RenderScene(level, room, camera, orientation, viewport, resources,
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
    if (playerMode) {
        MR_SpriteHandle* menuFontHandle = LoadUiFont();
        if (menuFontHandle != nullptr) {
            const MenuChoice choice = RunMainMenu(graphics, buffer, viewport, *menuFontHandle->GetSprite(),
                                                  frameLimit);
            if (choice == MenuChoice::eOnlineLobby) {
                std::string lobbyHost = kDefaultLobbyHost;
                unsigned lobbyPort = kDefaultLobbyPort;
                ParseLobbyArg(argc, argv, lobbyHost, lobbyPort);  // --lobby overrides the default

                std::string joinedRace;
                if (RunLobbyScreen(graphics, buffer, viewport, *menuFontHandle->GetSprite(), lobbyHost,
                                   lobbyPort, joinedRace, frameLimit)) {
                    std::printf("Joined race '%s' via the lobby\n", joinedRace.c_str());
                }
                else {
                    std::printf("Lobby skipped or unavailable; continuing with local play\n");
                }
            }
        }
        delete menuFontHandle;
    }
#endif

    std::printf("ClassicH 3D view: room=%d surfaces=%d actors=%d pixels=%d\n",
                room, renderStats.surfacesRendered, renderStats.actorsRendered, nonZeroPixels);
    int framesRendered = 0;
    bool running = true;
    bool cockpitView = false;
    bool missileSeen = false;
    while (running && (frameLimit < 0 || framesRendered < frameLimit)) {
        bool sceneChanged = renderStats.actorsRendered > 0;
        bool horizontalMovement = false;
        const MR_3DCoordinate previousCamera = camera;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            running = event.type != SDL_QUIT &&
                !(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
        }

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
            room = mainCharacter->mRoom;
            camera = mainCharacter->mPosition;
            camera.mZ += 700;
            orientation = mainCharacter->GetCabinOrientation();
            sceneChanged = true;

            if (fire && !missileSeen && ContainsFreeElementType(level, 1, 150)) {
                missileSeen = true;
            }
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
                const int cameraRoom = level.FindRoomForPoint(cameraPosition, room);
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
            ClampCameraHeight(level, room, camera);
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
                observer->PlaySoundsSafe(session.GetCurrentLevel(), mainCharacter);
                MR_SoundServer::ApplyContinuousPlay();
            }
            else
#endif
            {
                renderStats = RenderScene(level, room, camera, orientation, viewport, resources,
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
    observer->Delete();
#endif

    return 0;
}