#pragma once
// Console-style menu input. Every menu screen runs its own event loop and used to decide for itself what a gamepad's B button did
// (about half of the screens ignored it), so menus behaved differently from one screen to the next. All of them now feed events through
// MenuProcessEvent: ImGui gets the event (D-pad / left stick move, A selects), and B becomes the same Escape press the keyboard sends, so B
// goes back everywhere Escape does -- except while ImGui itself needs B (a text field is being edited, or a drop-down / popup is open: B
// closes that first, as on a console) and while the Controls screen is waiting for a button to bind (g_MenuGamepadCapture).

#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/imgui_internal.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"

#include <SDL.h>

// A function-local static keeps this header C++14-compatible while still giving
// every menu translation unit the same capture flag.  An inline variable would
// require C++17 (the Windows client intentionally still builds as C++14).
inline bool& MenuGamepadCapture()
{
    static bool capture = false;
    return capture;
}

inline void MenuProcessEvent(SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);
    if (event.type != SDL_CONTROLLERBUTTONDOWN || event.cbutton.button != SDL_CONTROLLER_BUTTON_B || MenuGamepadCapture()) return;
    if (ImGui::GetCurrentContext() == nullptr) return;
    if (ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    SDL_Event escape;
    SDL_zero(escape);
    escape.type = SDL_KEYDOWN;
    escape.key.state = SDL_PRESSED;
    escape.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.keysym.sym = SDLK_ESCAPE;
    event = escape;
}

// Option blocks. A plain child window is a separate navigation scope in ImGui: the D-pad cannot leave one block for the next, and a
// gamepad user has to "enter" a block with A and leave it with B. NavFlattened makes every block share the screen's focus scope, so the
// D-pad moves through all the options of a screen in reading order, like a console menu. Same two forms as ImGui::BeginChild.
inline bool MenuBeginChild(const char* id, const ImVec2& size, ImGuiChildFlags childFlags, ImGuiWindowFlags windowFlags = 0)
{
    return ImGui::BeginChild(id, size, childFlags | ImGuiChildFlags_NavFlattened, windowFlags);
}
inline bool MenuBeginChild(const char* id, const ImVec2& size, bool border, ImGuiWindowFlags windowFlags = 0)
{
    return MenuBeginChild(id, size, border ? ImGuiChildFlags_Border : ImGuiChildFlags_None, windowFlags);
}

// Sliders. ImGui's slider only changes with the D-pad after A "activates" it, which nobody finds. A focused slider now steps with
// left/right straight away (held = repeats), the way console option menus work. A mouse drag, or ImGui's own A-then-D-pad, still works.
// Returns -1, 0 or +1 and swallows the move so the focus stays on the slider.
inline int MenuSliderStep()
{
    if (!ImGui::IsItemFocused() || ImGui::IsItemActive()) return 0;
    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) step = -1;
    else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) step = 1;
    if (step != 0) ImGui::NavMoveRequestCancel();
    return step;
}

inline bool MenuSliderInt(const char* label, int* value, int lo, int hi, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
    bool changed = ImGui::SliderInt(label, value, lo, hi, format, flags);
    if (const int step = MenuSliderStep()) {
        const int next = *value + step < lo ? lo : (*value + step > hi ? hi : *value + step);
        if (next != *value) { *value = next; changed = true; }
    }
    return changed;
}

inline bool MenuSliderFloat(const char* label, float* value, float lo, float hi, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
    bool changed = ImGui::SliderFloat(label, value, lo, hi, format, flags);
    if (const int step = MenuSliderStep()) {
        const float next = *value + step * (hi - lo) / 20.0f;
        const float clamped = next < lo ? lo : (next > hi ? hi : next);
        if (clamped != *value) { *value = clamped; changed = true; }
    }
    return changed;
}
