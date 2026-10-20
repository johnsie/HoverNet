// Gamepad menu input: B must act as Escape (go back) on every menu screen, but not while a text field or a popup needs it, nor while the
// Controls screen waits for a button to bind. Runs the production MenuProcessEvent (MenuInput.h) under SDL_VIDEODRIVER=dummy.
#include "MenuInput.h"

#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

static int sFailures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++sFailures;
}

static SDL_Event Button(Uint8 button)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = button;
    e.cbutton.state = SDL_PRESSED;
    return e;
}

static void Frame(SDL_Renderer* renderer, void (*body)())
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("t");
    if (body) body();
    ImGui::End();
    ImGui::Render();
    (void)renderer;
}

static char sText[32] = "";
static void TextBody() { ImGui::SetKeyboardFocusHere(); ImGui::InputText("name", sText, sizeof(sText)); }
static void PopupBody()
{
    ImGui::Button("open");
    ImGui::OpenPopup("p");
    if (ImGui::BeginPopup("p")) { ImGui::TextUnformatted("x"); ImGui::EndPopup(); }
}

// Presses a navigation key for one frame (down, then up). The arrow keys feed the same ImGui navigation as the D-pad (a headless run has no
// gamepad, so the SDL backend would not report one); MenuSliderStep reads both.
static void Tap(SDL_Renderer* renderer, ImGuiKey key, void (*body)())
{
    ImGui::GetIO().AddKeyEvent(key, true);
    Frame(renderer, body);
    ImGui::GetIO().AddKeyEvent(key, false);
    Frame(renderer, body);
}

static int sLaps = 5;
static bool sFirstFocused = false, sSecondFocused = false, sFocusFirst = true;
static void SliderBody()
{
    // Focus a button, then reach the slider by navigating down, as a player does (SetKeyboardFocusHere on a slider would start typing in it).
    if (sFocusFirst) { ImGui::SetKeyboardFocusHere(); sFocusFirst = false; }
    ImGui::Button("above");
    MenuSliderInt("##laps", &sLaps, 1, 20);
}
template <bool Flattened>
static void BlocksBody()
{
    auto block = [](const char* id, const char* button, bool& focused) {
        if (Flattened) MenuBeginChild(id, ImVec2(0, 100), true); else ImGui::BeginChild(id, ImVec2(0, 100), true);
        ImGui::Button(button);
        focused = ImGui::IsItemFocused();
        ImGui::EndChild();
    };
    if (sFocusFirst) { ImGui::SetKeyboardFocusHere(); sFocusFirst = false; }
    block("blockA", "first", sFirstFocused);
    block("blockB", "second", sSecondFocused);
}

int main()
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* window = SDL_CreateWindow("MenuInputSmoke", 0, 0, 400, 300, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!renderer) { std::fprintf(stderr, "no renderer: %s\n", SDL_GetError()); return 1; }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // No context yet would be a crash in a real client only if a menu ran before ImGui started; here the context exists.
    Frame(renderer, nullptr); Frame(renderer, nullptr);
    SDL_Event e = Button(SDL_CONTROLLER_BUTTON_B);
    MenuProcessEvent(e);
    Check(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE, "B on a plain menu is Escape (back)");

    e = Button(SDL_CONTROLLER_BUTTON_A);
    MenuProcessEvent(e);
    Check(e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_A, "A is left alone (ImGui activates with it)");
    e = Button(SDL_CONTROLLER_BUTTON_START);
    MenuProcessEvent(e);
    Check(e.type == SDL_CONTROLLERBUTTONDOWN, "Start is left alone (the pause screens handle it)");

    g_MenuGamepadCapture = true;
    e = Button(SDL_CONTROLLER_BUTTON_B);
    MenuProcessEvent(e);
    Check(e.type == SDL_CONTROLLERBUTTONDOWN, "B is not Escape while a button is being bound");
    g_MenuGamepadCapture = false;

    // A text field being edited keeps B.
    for (int i = 0; i < 3; ++i) Frame(renderer, TextBody);
    ImGui::GetIO().WantTextInput = true;   // the SDL backend sets this from the active widget; force it in case no widget claimed it headless
    Check(ImGui::GetIO().WantTextInput, "setup: a text field wants input");
    e = Button(SDL_CONTROLLER_BUTTON_B);
    MenuProcessEvent(e);
    Check(e.type == SDL_CONTROLLERBUTTONDOWN, "B stays B while typing (it only leaves the field)");
    ImGui::GetIO().WantTextInput = false;

    // An open popup keeps B.
    for (int i = 0; i < 3; ++i) Frame(renderer, PopupBody);
    Check(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel), "setup: a popup is open");
    e = Button(SDL_CONTROLLER_BUTTON_B);
    MenuProcessEvent(e);
    Check(e.type == SDL_CONTROLLERBUTTONDOWN, "B stays B while a popup is open (it closes the popup first)");

    // A focused slider steps with the D-pad straight away (no "activate first").
    sFocusFirst = true;
    for (int i = 0; i < 4; ++i) Frame(renderer, SliderBody);
    Tap(renderer, ImGuiKey_DownArrow, SliderBody);
    const int before = sLaps;
    Tap(renderer, ImGuiKey_RightArrow, SliderBody);
    Check(sLaps == before + 1, "D-pad right on a focused slider adds one");
    Tap(renderer, ImGuiKey_LeftArrow, SliderBody);
    Tap(renderer, ImGuiKey_LeftArrow, SliderBody);
    Check(sLaps == before - 1, "D-pad left takes one off, per press");
    for (int i = 0; i < 30; ++i) Tap(renderer, ImGuiKey_LeftArrow, SliderBody);
    Check(sLaps == 1, "the slider stops at its minimum");

    // Option blocks: the D-pad reaches the next block (plain child windows keep it inside the first one).
    sFocusFirst = true; sFirstFocused = sSecondFocused = false;
    for (int i = 0; i < 4; ++i) Frame(renderer, BlocksBody<false>);
    Tap(renderer, ImGuiKey_DownArrow, BlocksBody<false>);
    Tap(renderer, ImGuiKey_DownArrow, BlocksBody<false>);
    const bool plainReachesSecond = sSecondFocused;
    sFocusFirst = true; sFirstFocused = sSecondFocused = false;
    for (int i = 0; i < 4; ++i) Frame(renderer, BlocksBody<true>);
    Check(sFirstFocused, "setup: the first block's button has the focus");
    Tap(renderer, ImGuiKey_DownArrow, BlocksBody<true>);
    Tap(renderer, ImGuiKey_DownArrow, BlocksBody<true>);
    Check(sSecondFocused, "D-pad down moves from one option block to the next");
    std::printf("note plain BeginChild reaches the second block: %s (the reason for MenuBeginChild)\n", plainReachesSecond ? "yes" : "no");

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return sFailures == 0 ? 0 : 1;
}
