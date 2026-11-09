// The main menu drawn with RmlUi (HTML/CSS-style markup and style sheets with real layout, transitions and animation) instead of Dear ImGui.
// A prototype behind HOVERNET_RMLUI (build) and HOVERNET_RML_MENU=1 (run): see docs/rmlui.md. It knows nothing of the game: it shows what it is given and
// says which door was chosen.
#pragma once

#include <SDL.h>

#include <string>

namespace HoverNetRml
{
struct MainMenuInfo
{
    std::string assetDir;        // NetTarget/LinuxClient/assets, with the ui/ folder and the fonts and pictures in it
    std::string version;         // shown bottom left
    int level = 0;               // 0 = no level line yet (a new player)
    std::string levelTitle;
    float levelProgress = 0.0f;  // 0..1 towards the next level
    bool tutorialDone = false;   // the Tutorial button is quiet once it has been done
};

enum class Choice { eNone, eOnline, eOffline, eLeaderboards, eGarage, eTutorial, eSettings, eBack, eQuitRequested };

// Shows the menu until a door is chosen (or, with frameLimit >= 0, for that many frames and then eNone). The caller keeps its own event loop out of the way:
// this one polls SDL events itself. The renderer's logical size is set to the window's own while it runs and put back to restoreW x restoreH.
Choice RunMainMenu(SDL_Window* window, SDL_Renderer* renderer, const MainMenuInfo& info, int frameLimit, int restoreW, int restoreH);
}
