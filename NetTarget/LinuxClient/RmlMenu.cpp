#include "RmlMenu.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    ChooseListener(Choice* pResult, std::function<void(Sound)> pSound) : mResult(pResult), mSound(std::move(pSound)) {}
    void ProcessEvent(Rml::Event& pEvent) override
    {
        const Rml::String id = pEvent.GetCurrentElement()->GetId();
        if (pEvent.GetId() == Rml::EventId::Mouseover) { if (mSound) mSound(Sound::eHover); return; }
        if (mSound) mSound(Sound::eClick);
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
    std::function<void(Sound)> mSound;
};

const char* const kButtons[] = {"online", "offline", "leaderboards", "garage", "tutorial", "settings"};   // in the order the arrow keys walk them
constexpr int kButtonCount = 6;

void MoveFocus(Rml::ElementDocument* pDocument, int pStep, const std::function<void(Sound)>& pSound)
{
    if (pSound) pSound(Sound::eHover);
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

    ChooseListener listener(&result, pInfo.sound);
    for (const char* id : kButtons) if (Rml::Element* button = document->GetElementById(id)) { button->AddEventListener("click", &listener); button->AddEventListener("mouseover", &listener); }
    document->Show();
    if (Rml::Element* first = document->GetElementById("online")) first->Focus();

    // Test hook HOVERNET_TEST_RML="down,down,enter" or "click:garage": scripted input through the real event path, one step every few frames once the opening animation is over.
    std::vector<std::string> script;
    if (const char* steps = std::getenv("HOVERNET_TEST_RML")) for (std::string step; *steps != 0; ++steps) { if (*steps == ',') { script.push_back(step); step.clear(); } else step += *steps; if (steps[1] == 0) script.push_back(step); }
    bool craftShown = false;
    Uint32 craftStart = 0;
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
                if (key == SDLK_ESCAPE) { if (pInfo.sound) pInfo.sound(Sound::eBack); result = Choice::eBack; break; }
                if (key == SDLK_DOWN) { MoveFocus(document, 1, pInfo.sound); continue; }
                if (key == SDLK_UP) { MoveFocus(document, -1, pInfo.sound); continue; }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = event.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_B) { result = Choice::eBack; break; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { MoveFocus(document, 1, pInfo.sound); continue; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { MoveFocus(document, -1, pInfo.sound); continue; }
                if (b == SDL_CONTROLLER_BUTTON_A) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            RmlSDL::InputEventHandler(context, event);
        }
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; context->SetDimensions(Rml::Vector2i(width, height)); }
        context->Update();
        // Under the interface: the background (a gradient the renderer draws itself, so the craft can sit between it and the markup) and the player's craft.
        {
            SDL_SetRenderDrawColor(pRenderer, 18, 20, 28, 255);
            SDL_RenderClear(pRenderer);
            const SDL_Color top = {18, 20, 28, 255}, bottom = {38, 16, 26, 255};
            const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, top, {0, 0}}, {{static_cast<float>(width), 0.0f}, top, {0, 0}},
                                        {{0.0f, static_cast<float>(height)}, bottom, {0, 0}}, {{static_cast<float>(width), static_cast<float>(height)}, bottom, {0, 0}}};
            const int order[6] = {0, 1, 2, 1, 3, 2};
            SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
            if (pInfo.craftFrame) {
                const Uint32 now = SDL_GetTicks();
                const int yaw = pInfo.reducedMotion ? 28 : static_cast<int>(now / 1000.0 * 22.0) % 360 / 2 * 2;
                SDL_Rect crop = {0, 0, 0, 0};
                SDL_Texture* craft = pInfo.craftFrame(yaw, crop);
                Rml::Element* hero = document->GetElementById("hero");
                if (craft != nullptr && hero != nullptr && crop.w > 0 && crop.h > 0) {
                    if (!craftShown) { craftShown = true; craftStart = now; if (Rml::Element* img = hero->GetFirstChild()) img->SetProperty("display", "none"); }
                    const Rml::Vector2f origin = hero->GetAbsoluteOffset(Rml::Box::BORDER), size = hero->GetBox().GetSize(Rml::Box::BORDER);
                    const float aspect = static_cast<float>(crop.w) / static_cast<float>(crop.h);
                    float w = size.x, h = w / aspect;
                    if (h > size.y) { h = size.y; w = h * aspect; }
                    const float t = pInfo.reducedMotion ? 1.0f : std::min(1.0f, static_cast<float>(now - craftStart) / 900.0f);
                    const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);   // the same curve as the markup's cubic-out
                    SDL_FRect to = {origin.x + (size.x - w) * 0.5f - 70.0f * (1.0f - ease), origin.y + (size.y - h) * 0.5f, w, h};
                    SDL_SetTextureAlphaMod(craft, static_cast<Uint8>(255.0f * ease));
                    SDL_RenderCopyF(pRenderer, craft, &crop, &to);
                }
            }
        }
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

namespace
{
// A click on either results button.
class ResultsListener : public Rml::EventListener
{
public:
    ResultsListener(ResultsChoice* pResult, std::function<void(Sound)> pSound) : mResult(pResult), mSound(std::move(pSound)) {}
    void ProcessEvent(Rml::Event& pEvent) override
    {
        const Rml::String id = pEvent.GetCurrentElement()->GetId();
        if (pEvent.GetId() == Rml::EventId::Mouseover) { if (mSound) mSound(Sound::eHover); return; }
        if (mSound) mSound(Sound::eClick);
        if (id == "lobby") *mResult = ResultsChoice::eLobby;
        else if (id == "menu") *mResult = ResultsChoice::eMainMenu;
        if (*mResult != ResultsChoice::eNone && std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST results chose %s\n", id.c_str()); std::fflush(stdout); }
    }
private:
    ResultsChoice* mResult;
    std::function<void(Sound)> mSound;
};

Rml::String Escaped(const std::string& pText)
{
    Rml::String out;
    for (char c : pText) { if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '&') out += "&amp;"; else out += c; }
    return out;
}
}  // namespace

ResultsChoice RunResults(SDL_Window* pWindow, SDL_Renderer* pRenderer, const ResultsInfo& pInfo, int pFrameLimit, int pRestoreW, int pRestoreH)
{
    SDL_RenderSetLogicalSize(pRenderer, 0, 0);
    SystemInterface_SDL system;
    system.SetWindow(pWindow);
    RenderInterface_SDL renderInterface(pRenderer);
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderInterface);
    auto leave = [&](ResultsChoice pResult) { SDL_RenderSetLogicalSize(pRenderer, pRestoreW, pRestoreH); return pResult; };
    if (!Rml::Initialise()) return leave(ResultsChoice::eNone);

    ResultsChoice result = ResultsChoice::eNone;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(pRenderer, &width, &height);
    Rml::Context* context = Rml::CreateContext("hovernet-results", Rml::Vector2i(width, height));
    Rml::LoadFontFace(pInfo.assetDir + "/Inter-Variable.ttf", true);
    Rml::LoadFontFace(pInfo.assetDir + "/Oxanium-Variable.ttf", false);
    Rml::ElementDocument* document = context != nullptr ? context->LoadDocument(pInfo.assetDir + "/ui/results.rml") : nullptr;
    if (document == nullptr) {
        std::fprintf(stderr, "RmlUi results: could not load %s/ui/results.rml\n", pInfo.assetDir.c_str());
        Rml::Shutdown();
        return leave(ResultsChoice::eNone);
    }
    auto set = [&](const char* id, const std::string& text) { if (Rml::Element* e = document->GetElementById(id)) e->SetInnerRML(Escaped(text)); };
    set("heading", pInfo.heading);
    if (pInfo.victory) if (Rml::Element* h = document->GetElementById("heading")) h->SetClass("victory", true);
    set("subtitle", pInfo.subtitle);
    set("position", pInfo.position);
    set("finish", pInfo.finishTime);
    set("bestlap", pInfo.bestLap);
    if (Rml::Element* bests = document->GetElementById("bests")) {
        if (pInfo.bests.empty()) bests->SetProperty("display", "none");
        for (const auto& line : pInfo.bests) bests->SetInnerRML(bests->GetInnerRML() + (line.second ? "<p class=\"hot\">" : "<p>") + Escaped(line.first) + "</p>");
    }
    if (Rml::Element* reward = document->GetElementById("reward")) {
        if (!pInfo.hasReward) reward->SetProperty("display", "none");
    }
    set("breakdown", pInfo.breakdown);
    set("level", pInfo.levelLine);
    if (pInfo.levelUp) { if (Rml::Element* e = document->GetElementById("level")) e->SetClass("up", true); if (Rml::Element* e = document->GetElementById("barfill")) e->SetClass("up", true); }
    set("opened", pInfo.opened);
    set("unlocked", pInfo.unlocked);
    if (!pInfo.online) if (Rml::Element* lobby = document->GetElementById("lobby")) lobby->SetProperty("display", "none");

    ResultsListener listener(&result, pInfo.sound);
    for (const char* id : {"lobby", "menu"}) if (Rml::Element* button = document->GetElementById(id)) { button->AddEventListener("click", &listener); button->AddEventListener("mouseover", &listener); }
    document->Show();
    if (Rml::Element* first = document->GetElementById(pInfo.online ? "lobby" : "menu")) first->Focus();

    Rml::ElementList reveal;
    document->GetElementsByClassName(reveal, "reveal");
    const bool instant = pInfo.reducedMotion || pFrameLimit >= 0;   // a bounded (test) run and Reduced motion show everything at once
    if (instant) document->SetClass("instant", true);
    const Uint32 start = SDL_GetTicks();
    size_t revealed = 0;
    bool cardShown = false, barFilled = false;
    Uint32 rewardAt = 0;

    std::vector<std::string> script;   // test hook HOVERNET_TEST_RML, as in the main menu: scripted keys through the real event path
    if (const char* steps = std::getenv("HOVERNET_TEST_RML")) for (std::string step; *steps != 0; ++steps) { if (*steps == ',') { script.push_back(step); step.clear(); } else step += *steps; if (steps[1] == 0) script.push_back(step); }
    int frames = 0;
    while (result == ResultsChoice::eNone && (pFrameLimit < 0 || frames < pFrameLimit)) {
        ++frames;
        const Uint32 age = SDL_GetTicks() - start;
        if (!cardShown && (instant || age >= 0)) { cardShown = true; if (Rml::Element* card = document->GetElementById("card")) card->SetClass("show", true); }
        // The parts appear one after another, a fifth of a second apart, from a third of a second in.
        while (revealed < reveal.size() && (instant || age >= 320 + 190 * revealed)) {
            reveal[revealed]->SetClass("show", true);
            if (reveal[revealed]->GetId() == "reward") rewardAt = SDL_GetTicks();
            ++revealed;
        }
        if (pInfo.hasReward) {   // the XP counts up over a second, easing out, and then the bar fills
            float t = 1.0f;
            if (!instant) t = rewardAt == 0 ? 0.0f : std::min(1.0f, static_cast<float>(SDL_GetTicks() - rewardAt) / 1000.0f);
            const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            static int lastShown = -1;
            const int shown = static_cast<int>(pInfo.xp * eased + 0.5f);
            if (shown != lastShown || frames == 1) { lastShown = shown; set("xp", "+" + std::to_string(shown) + " XP"); }
            if (!barFilled && rewardAt != 0 || (instant && !barFilled)) {
                barFilled = true;
                if (Rml::Element* fill = document->GetElementById("barfill")) fill->SetProperty("width", std::to_string(static_cast<int>(std::max(0.0f, std::min(1.0f, pInfo.levelProgress)) * 100.0f)) + "%");
            }
        }
        if (frames >= 60 && (frames - 60) % 4 == 0 && static_cast<size_t>((frames - 60) / 4) < script.size()) {
            const std::string& step = script[static_cast<size_t>((frames - 60) / 4)];
            SDL_Event scripted; SDL_zero(scripted);
            scripted.type = SDL_KEYDOWN; scripted.key.state = SDL_PRESSED;
            scripted.key.keysym.sym = step == "down" ? SDLK_DOWN : step == "up" ? SDLK_UP : step == "enter" ? SDLK_RETURN : SDLK_ESCAPE;
            SDL_PushEvent(&scripted);
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { result = ResultsChoice::eQuitRequested; break; }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) { if (pInfo.sound) pInfo.sound(Sound::eBack); result = ResultsChoice::eMainMenu; break; }
                if ((key == SDLK_DOWN || key == SDLK_UP) && pInfo.online) {
                    Rml::Element* focused = document->GetFocusLeafNode();
                    const char* other = (focused != nullptr && focused->GetId() == "lobby") ? "menu" : "lobby";
                    if (Rml::Element* e = document->GetElementById(other)) e->Focus();
                    continue;
                }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = event.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_B) { result = ResultsChoice::eMainMenu; break; }
                if ((b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == SDL_CONTROLLER_BUTTON_DPAD_UP) && pInfo.online) {
                    Rml::Element* focused = document->GetFocusLeafNode();
                    if (Rml::Element* e = document->GetElementById((focused != nullptr && focused->GetId() == "lobby") ? "menu" : "lobby")) e->Focus();
                    continue;
                }
                if (b == SDL_CONTROLLER_BUTTON_A) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            RmlSDL::InputEventHandler(context, event);
        }
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; context->SetDimensions(Rml::Vector2i(width, height)); }
        context->Update();
        SDL_SetRenderDrawColor(pRenderer, 18, 12, 20, 255);
        SDL_RenderClear(pRenderer);
        const SDL_Color top = {18, 12, 20, 255}, bottom = {69, 13, 28, 255};   // the same dark red wash as the ImGui results screen
        const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, top, {0, 0}}, {{static_cast<float>(width), 0.0f}, top, {0, 0}},
                                    {{0.0f, static_cast<float>(height)}, bottom, {0, 0}}, {{static_cast<float>(width), static_cast<float>(height)}, bottom, {0, 0}}};
        const int order[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
        context->Render();
        if (const char* shot = std::getenv("HOVERNET_CAPTURE_FRAME")) {
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
    return leave(result);
}
}  // namespace HoverNetRml
