// The main menu drawn with RmlUi (HTML/CSS-style markup and style sheets with real layout, transitions and animation) instead of Dear ImGui.
// A prototype behind HOVERNET_RMLUI (build) and HOVERNET_RML_MENU=1 (run): see docs/rmlui.md. It knows nothing of the game: it shows what it is given and
// says which door was chosen.
#pragma once

#include <SDL.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace HoverNetRml
{
enum class Sound { eHover, eClick, eBack };

// Called once at the end of every frame of every screen here (the game's own frame counter and pacing; without it a screen paces itself with a short delay).
void SetFrameHook(void (*hook)());

// The player's UI Scale setting (1.0 = 100%): every screen here is laid out that much larger or smaller.
void SetUiScale(float scale);

struct MainMenuInfo
{
    std::string assetDir;        // NetTarget/LinuxClient/assets, with the ui/ folder and the fonts and pictures in it
    std::string version;         // shown bottom left
    int level = 0;               // 0 = no level line yet (a new player)
    std::string levelTitle;
    float levelProgress = 0.0f;  // 0..1 towards the next level
    bool tutorialDone = false;   // the Tutorial button is quiet once it has been done
    // The player's own craft, turning: called every frame with the angle in degrees, it returns the picture to draw (null = none, and the stock picture in the markup
    // is shown instead) and sets cropOut to the part of it to show. The caller owns the texture. reducedMotion keeps the entrance animations of the pictures still.
    std::function<SDL_Texture*(int, SDL_Rect&)> craftFrame;
    bool reducedMotion = false;
    std::function<void(Sound)> sound;   // the game's interface sounds (null = silent)
};

enum class Choice { eNone, eOnline, eOffline, eLeaderboards, eGarage, eTutorial, eSettings, eBack, eQuitRequested };

// Shows the menu until a door is chosen (or, with frameLimit >= 0, for that many frames and then eNone). The caller keeps its own event loop out of the way:
// this one polls SDL events itself. The renderer's logical size is set to the window's own while it runs and put back to restoreW x restoreH.
Choice RunMainMenu(SDL_Window* window, SDL_Renderer* renderer, const MainMenuInfo& info, int frameLimit, int restoreW, int restoreH);

// The results screen after a race: the card eases in and its parts appear one after another, the XP counts up and the level bar fills.
struct ResultsInfo
{
    std::string assetDir;
    std::string heading, subtitle;                        // VICTORY! / PODIUM FINISH / RACE COMPLETE, and the line under it
    bool victory = false;
    std::string position, finishTime, bestLap;            // the three numbers
    std::vector<std::pair<std::string, bool>> bests;      // the player's records on this track; true = a new one (drawn hot)
    bool hasReward = false;
    int xp = 0;
    std::string breakdown, levelLine, opened, unlocked;   // the XP line, "Level 3 - Title", what the level opened in the Garage, the achievements earned
    bool levelUp = false;
    float levelProgress = 0.0f;                           // 0..1 on the level bar
    bool online = false;                                  // offers RETURN TO LOBBY as well
    bool reducedMotion = false;
    std::function<void(Sound)> sound;
};

enum class ResultsChoice { eNone, eLobby, eMainMenu, eQuitRequested };

// Runs until a door is chosen (or, with frameLimit >= 0, for that many frames, all parts shown at once). Same event and logical-size rules as RunMainMenu.
ResultsChoice RunResults(SDL_Window* window, SDL_Renderer* renderer, const ResultsInfo& info, int frameLimit, int restoreW, int restoreH);

// A column of buttons beside a title block: the pause menu, and any other screen that is a list of choices.
struct ChoiceOption
{
    std::string label;
    bool primary = false;           // the one red button
    std::string headingBefore;      // a small heading above this button (empty: none)
    std::string detail;             // a smaller line under the label (empty: none)
};

struct ChoiceInfo
{
    std::string assetDir;
    std::string title, eyebrow, subtitle;   // the block at the top left
    std::string panelTitle;                 // above the buttons
    std::vector<ChoiceOption> options;
    int backdrop = 0;                       // 0: the pause menu's backdrop, 1: the one Settings and most other screens have
    bool reducedMotion = false;
    std::function<void(Sound)> sound;
};

// Returns the index of the option chosen; kBack for Esc / B / Start; kClose when the window was closed; kNone when a bounded run ended or the screen could not load.
constexpr int kChoiceBack = -1, kChoiceClose = -2, kChoiceNone = -3;
int RunChoiceMenu(SDL_Window* window, SDL_Renderer* renderer, const ChoiceInfo& info, int frameLimit, int restoreW, int restoreH);

// ---- Forms: tabs of labelled controls with Save and Cancel (Settings, and screens like it) -----------------------------------------------------------------
// The caller keeps its own state and side effects: it describes the fields, and every frame hears what the player changed.
struct FormField
{
    enum class Type { eHeading, eNote, eText, eToggle, eSlider, eSelect, eButton, eChooser };
    Type type = Type::eNote;
    std::string id;                  // events and updates name a field by this
    std::string label;               // the label (a heading's or note's own text)
    std::string hint;                // a small line under the control, updated in place with SetHint
    std::string text;                // eText: the value
    bool on = false;                 // eToggle
    float value = 0.0f, min = 0.0f, max = 100.0f, step = 1.0f;   // eSlider
    std::string suffix;              // eSlider: shown after the number, for example "%"
    std::vector<std::string> options; int selected = 0;           // eSelect
    bool quiet = false;              // eButton: the quieter style
    std::vector<std::string> items;  // eChooser: every choice (a filter box narrows a long list to the first 60 matches); `selected` is the current one, `label` above it
};

struct FormEvent
{
    std::string id;                  // the field, or "tab" for a tab chosen (index = the tab)
    std::string text;
    bool on = false;
    float value = 0.0f;
    int index = 0;
};

enum class FormOutcome { eNone, eSave, eCancel, eClose };

class FormScreen
{
public:
    FormScreen(SDL_Window* window, SDL_Renderer* renderer, const std::string& assetDir, const std::string& title, bool reducedMotion, std::function<void(Sound)> sound,
               int restoreW, int restoreH);
    ~FormScreen();
    FormScreen(const FormScreen&) = delete;
    FormScreen& operator=(const FormScreen&) = delete;
    bool Ok() const;                                                  // false: the screen could not load (use the older one)
    void SetTabs(const std::vector<std::string>& names, int selected);
    void SetFields(const std::vector<FormField>& fields);             // replaces the controls (a tab changed, or a section opened)
    void SetHint(const std::string& id, const std::string& text);
    void SetSaveEnabled(bool enabled);
    void SetText(const std::string& id, const std::string& text);
    void SetToggle(const std::string& id, bool on);
    void SetSlider(const std::string& id, float value);
    void SetSelect(const std::string& id, int index, const std::string& shown);
    void SetLayout(bool wide, bool tabs);                                // wide: a second column on the right for a picture; tabs: show the tab bar
    void SetFooter(const std::string& saveLabel, const std::string& cancelLabel);   // an empty cancel label leaves one wide button
    // While capturing, the next key (or controller button) is reported as a "capturekey" (index = SDL scancode) or "capturebutton" (index = SDL button) event instead of moving the
    // focus; Escape reports "capturecancel". For screens that remap controls.
    void SetCapture(bool keys, bool buttons);
    // A picture beside the controls (the track map): called every frame, returns the texture to draw (null = none) and sets crop to the part to show. The caller owns it.
    void SetSideImage(std::function<SDL_Texture*(SDL_Rect&)> image);
    // A box over everything with a message, an optional progress bar (fraction < 0: none) and one button (a "busy" event when pressed); empty title hides it.
    void SetBusy(const std::string& title, const std::string& line, float fraction, const std::string& buttonLabel);
    // One frame: input, update, drawing. Fills `events` with what the player changed.
    FormOutcome Frame(std::vector<FormEvent>& events, int frameNumber, int frameLimit);
private:
    struct Impl;
    Impl* mImpl;
};
}
