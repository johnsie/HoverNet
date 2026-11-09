#include "RmlMenu.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_SDL.h"

namespace HoverNetRml
{
namespace
{
// One listener for every button: a click records which button, by its id.
class ChooseListener : public Rml::EventListener
{
public:
    explicit ChooseListener(Choice* pResult) : mResult(pResult) {}
    void ProcessEvent(Rml::Event& pEvent) override
    {
        const Rml::String id = pEvent.GetCurrentElement()->GetId();
        if (id == "online") *mResult = Choice::eOnline;
        else if (id == "offline") *mResult = Choice::eOffline;
        else if (id == "leaderboards") *mResult = Choice::eLeaderboards;
        else if (id == "garage") *mResult = Choice::eGarage;
        else if (id == "tutorial") *mResult = Choice::eTutorial;
        else if (id == "settings") *mResult = Choice::eSettings;
        if (*mResult != Choice::eNone && std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST chose %s\n", id.c_str()); std::fflush(stdout); }
    }
private:
    Choice* mResult;
};

const char* const kButtons[] = {"online", "offline", "leaderboards", "garage", "tutorial", "settings"};   // in the order the arrow keys walk them
constexpr int kButtonCount = 6;

void MoveFocus(Rml::ElementDocument* pDocument, int pStep)
{
    int at = -1;
    Rml::Element* focused = pDocument->GetFocusLeafNode();
    for (int i = 0; i < kButtonCount; ++i) {
        Rml::Element* button = pDocument->GetElementById(kButtons[i]);
        if (button != nullptr && (button == focused)) at = i;
    }
    const int next = at < 0 ? 0 : (at + pStep + kButtonCount) % kButtonCount;
    if (Rml::Element* button = pDocument->GetElementById(kButtons[next])) button->Focus();
}
}  // namespace

Choice RunMainMenu(SDL_Window* pWindow, SDL_Renderer* pRenderer, const MainMenuInfo& pInfo, int pFrameLimit, int pRestoreW, int pRestoreH)
{
    SDL_RenderSetLogicalSize(pRenderer, 0, 0);
    SystemInterface_SDL system;
    system.SetWindow(pWindow);
    RenderInterface_SDL renderInterface(pRenderer);
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderInterface);
    if (!Rml::Initialise()) { SDL_RenderSetLogicalSize(pRenderer, pRestoreW, pRestoreH); return Choice::eNone; }

    Choice result = Choice::eNone;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(pRenderer, &width, &height);
    Rml::Context* context = Rml::CreateContext("hovernet-main-menu", Rml::Vector2i(width, height));
    Rml::LoadFontFace(pInfo.assetDir + "/Inter-Variable.ttf", true);       // the body face (also the fallback for any name)
    Rml::LoadFontFace(pInfo.assetDir + "/Oxanium-Variable.ttf", false);    // the display face
    Rml::ElementDocument* document = context != nullptr ? context->LoadDocument(pInfo.assetDir + "/ui/main-menu.rml") : nullptr;
    if (document == nullptr) {
        std::fprintf(stderr, "RmlUi main menu: could not load %s/ui/main-menu.rml\n", pInfo.assetDir.c_str());
        Rml::Shutdown();
        SDL_RenderSetLogicalSize(pRenderer, pRestoreW, pRestoreH);
        return Choice::eNone;
    }
    if (Rml::Element* version = document->GetElementById("version")) version->SetInnerRML(Rml::String(pInfo.version[0] == 'v' ? "" : "v") + pInfo.version);
    if (Rml::Element* level = document->GetElementById("level")) {
        if (pInfo.level > 0) {
            if (Rml::Element* text = document->GetElementById("leveltext")) text->SetInnerRML("LEVEL " + std::to_string(pInfo.level) + "  -  " + pInfo.levelTitle);
            if (Rml::Element* fill = document->GetElementById("barfill")) fill->SetProperty("width", std::to_string(static_cast<int>(std::max(0.0f, std::min(1.0f, pInfo.levelProgress)) * 100.0f)) + "%");
        }
        else level->SetProperty("display", "none");
    }
    if (pInfo.tutorialDone) if (Rml::Element* tutorial = document->GetElementById("tutorial")) tutorial->SetClass("quiet", true);

    ChooseListener listener(&result);
    for (const char* id : kButtons) if (Rml::Element* button = document->GetElementById(id)) button->AddEventListener("click", &listener);
    document->Show();
    if (Rml::Element* first = document->GetElementById("online")) first->Focus();

    // Test hook HOVERNET_TEST_RML="down,down,enter" or "click:garage": scripted input through the real event path, one step every few frames once the opening animation is over.
    std::vector<std::string> script;
    if (const char* steps = std::getenv("HOVERNET_TEST_RML")) for (std::string step; *steps != 0; ++steps) { if (*steps == ',') { script.push_back(step); step.clear(); } else step += *steps; if (steps[1] == 0) script.push_back(step); }
    int frames = 0;
    while (result == Choice::eNone && (pFrameLimit < 0 || frames < pFrameLimit)) {
        ++frames;
        if (frames >= 60 && (frames - 60) % 4 == 0 && static_cast<size_t>((frames - 60) / 4) < script.size()) {
            const std::string& step = script[static_cast<size_t>((frames - 60) / 4)];
            SDL_Event scripted; SDL_zero(scripted);
            if (step.compare(0, 6, "click:") == 0) {
                Rml::Element* target = document->GetElementById(step.substr(6));
                if (target != nullptr) {
                    const Rml::Vector2f at = target->GetAbsoluteOffset(Rml::Box::BORDER) + target->GetBox().GetSize(Rml::Box::BORDER) * 0.5f;
                    scripted.type = SDL_MOUSEMOTION; scripted.motion.x = static_cast<int>(at.x); scripted.motion.y = static_cast<int>(at.y); SDL_PushEvent(&scripted);
                    SDL_zero(scripted); scripted.type = SDL_MOUSEBUTTONDOWN; scripted.button.button = SDL_BUTTON_LEFT; scripted.button.state = SDL_PRESSED; scripted.button.clicks = 1; scripted.button.x = static_cast<int>(at.x); scripted.button.y = static_cast<int>(at.y); SDL_PushEvent(&scripted);
                    SDL_zero(scripted); scripted.type = SDL_MOUSEBUTTONUP; scripted.button.button = SDL_BUTTON_LEFT; scripted.button.state = SDL_RELEASED; scripted.button.clicks = 1; scripted.button.x = static_cast<int>(at.x); scripted.button.y = static_cast<int>(at.y); SDL_PushEvent(&scripted);
                }
            }
            else {
                scripted.type = SDL_KEYDOWN; scripted.key.state = SDL_PRESSED;
                scripted.key.keysym.sym = step == "down" ? SDLK_DOWN : step == "up" ? SDLK_UP : step == "enter" ? SDLK_RETURN : SDLK_ESCAPE;
                SDL_PushEvent(&scripted);
            }
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { result = Choice::eQuitRequested; break; }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) { result = Choice::eBack; break; }
                if (key == SDLK_DOWN) { MoveFocus(document, 1); continue; }
                if (key == SDLK_UP) { MoveFocus(document, -1); continue; }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = event.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_B) { result = Choice::eBack; break; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { MoveFocus(document, 1); continue; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { MoveFocus(document, -1); continue; }
                if (b == SDL_CONTROLLER_BUTTON_A) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            RmlSDL::InputEventHandler(context, event);
        }
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; context->SetDimensions(Rml::Vector2i(width, height)); }
        context->Update();
        SDL_SetRenderDrawColor(pRenderer, 18, 20, 28, 255);
        SDL_RenderClear(pRenderer);
        context->Render();
        if (const char* shot = std::getenv("HOVERNET_CAPTURE_MENU")) {   // the same capture hook as the ImGui menu: the last frame of a bounded run
            if (pFrameLimit >= 0 && frames == pFrameLimit) {
                if (SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888)) {
                    SDL_RenderReadPixels(pRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
                    SDL_SaveBMP(surface, shot);
                    SDL_FreeSurface(surface);
                }
            }
        }
        SDL_RenderPresent(pRenderer);
        SDL_Delay(pFrameLimit >= 0 ? 16 : 4);
    }
    document->Close();
    Rml::Shutdown();
    SDL_RenderSetLogicalSize(pRenderer, pRestoreW, pRestoreH);
    return result;
}
}  // namespace HoverNetRml
