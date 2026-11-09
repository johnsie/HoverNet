#include "RmlMenu.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
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

// Test hook HOVERNET_TEST_RML: scripted input for the screen that is opening. One list ("down,enter") serves every screen; "list1;list2;list3" gives each screen, in the order
// they open, its own list (the last list serves any later screen).
std::vector<std::string> ScriptSteps()
{
    std::vector<std::string> steps;
    const char* env = std::getenv("HOVERNET_TEST_RML");
    if (env == nullptr) return steps;
    const std::string all = env;
    std::vector<std::string> sections;
    for (size_t at = 0;;) { const size_t cut = all.find(';', at); sections.push_back(all.substr(at, cut == std::string::npos ? std::string::npos : cut - at)); if (cut == std::string::npos) break; at = cut + 1; }
    static size_t opened = 0;
    const std::string& mine = sections.size() == 1 ? sections[0] : sections[std::min(opened, sections.size() - 1)];
    ++opened;
    std::string step;
    for (char c : mine) { if (c == ',') { if (!step.empty()) steps.push_back(step); step.clear(); } else step += c; }
    if (!step.empty()) steps.push_back(step);
    return steps;
}

void (*gFrameHook)() = nullptr;
void EndFrame(int pFrameLimit)
{
    if (gFrameHook != nullptr) gFrameHook();
    else SDL_Delay(pFrameLimit >= 0 ? 16 : 4);
}

// The screens are drawn twice as large into an off-screen picture and shrunk onto the window with smooth filtering: that smooths the edges of rounded corners and of text,
// which SDL's renderer does not do by itself (Dear ImGui builds smooth edges into its own shapes, so without this the new screens look rougher). HOVERNET_RML_SS=1 turns it off.
struct Supersample
{
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int factor = 2, width = 0, height = 0;
    explicit Supersample(SDL_Renderer* pRenderer) : renderer(pRenderer)
    {
        if (const char* f = std::getenv("HOVERNET_RML_SS")) factor = std::max(1, std::min(4, std::atoi(f)));
    }
    ~Supersample() { if (texture != nullptr) SDL_DestroyTexture(texture); }
    Supersample(const Supersample&) = delete;
    Supersample& operator=(const Supersample&) = delete;
    // Sizes the context and the picture for a window of pWidth x pHeight.
    void Resize(Rml::Context* pContext, int pWidth, int pHeight)
    {
        width = pWidth; height = pHeight;
        pContext->SetDimensions(Rml::Vector2i(pWidth * factor, pHeight * factor));
        pContext->SetDensityIndependentPixelRatio(static_cast<float>(factor));
        if (texture != nullptr) { SDL_DestroyTexture(texture); texture = nullptr; }
        if (factor > 1) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pWidth * factor, pHeight * factor);
            if (texture != nullptr) SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
            else { factor = 1; pContext->SetDimensions(Rml::Vector2i(pWidth, pHeight)); pContext->SetDensityIndependentPixelRatio(1.0f); }
        }
    }
    int W() const { return width * factor; }
    int H() const { return height * factor; }
    void Begin() { if (texture != nullptr) SDL_SetRenderTarget(renderer, texture); }
    void End()
    {
        if (texture == nullptr) return;
        if (const char* dump = std::getenv("HOVERNET_RML_SS_DUMP")) {   // test hook: what was drawn into the large picture
            if (SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, W(), H(), 32, SDL_PIXELFORMAT_ARGB8888)) {
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
                SDL_SaveBMP(surface, dump);
                SDL_FreeSurface(surface);
            }
        }
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_RenderSetClipRect(renderer, nullptr);   // nothing the interface left behind may crop or move the picture
        SDL_RenderSetViewport(renderer, nullptr);
        const SDL_Rect whole = {0, 0, width, height};
        SDL_RenderCopy(renderer, texture, nullptr, &whole);
    }
    // Mouse positions come in window pixels and the context works in picture pixels.
    void Forward(Rml::Context* pContext, SDL_Event event) const
    {
        if (event.type == SDL_MOUSEMOTION) { event.motion.x *= factor; event.motion.y *= factor; event.motion.xrel *= factor; event.motion.yrel *= factor; }
        else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) { event.button.x *= factor; event.button.y *= factor; }
        RmlSDL::InputEventHandler(pContext, event);
    }
};

// A thick line as two triangles (SDL draws 1-pixel lines only): the older menu's grid and speed streaks are 1 to 2 pixels thick, so in the large picture they are factor times that.
void ThickLine(SDL_Renderer* pRenderer, float pX0, float pY0, float pX1, float pY1, float pThickness, SDL_Color pColour)
{
    float dx = pX1 - pX0, dy = pY1 - pY0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001f) return;
    const float nx = -dy / length * pThickness * 0.5f, ny = dx / length * pThickness * 0.5f;
    const SDL_Vertex v[4] = {{{pX0 + nx, pY0 + ny}, pColour, {0, 0}}, {{pX0 - nx, pY0 - ny}, pColour, {0, 0}}, {{pX1 + nx, pY1 + ny}, pColour, {0, 0}}, {{pX1 - nx, pY1 - ny}, pColour, {0, 0}}};
    const int order[6] = {0, 1, 2, 1, 3, 2};
    SDL_RenderGeometry(pRenderer, nullptr, v, 4, order, 6);
}

// The main menu's background, as the older menu draws it: a four-corner gradient, a darker floor under a horizon, a perspective grid running to a vanishing point and 18 short
// diagonal speed streaks that drift sideways (still when the player has chosen reduced motion). pFactor: the large picture's scale; pTick: milliseconds / 8, or 0 when still.
void DrawMenuBackdrop(SDL_Renderer* pRenderer, int pWidth, int pHeight, int pFactor, unsigned pTick)
{
    SDL_SetRenderDrawBlendMode(pRenderer, SDL_BLENDMODE_BLEND);
    const float f = static_cast<float>(pFactor), w = static_cast<float>(pWidth), h = static_cast<float>(pHeight);
    const float windowW = w / f, windowH = h / f;
    SDL_SetRenderDrawColor(pRenderer, 13, 13, 19, 255);
    SDL_RenderClear(pRenderer);
    const SDL_Color tl = {13, 13, 19, 255}, tr = {25, 10, 18, 255}, bl = {7, 7, 11, 255}, br = {12, 6, 10, 255};
    const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, tl, {0, 0}}, {{w, 0.0f}, tr, {0, 0}}, {{0.0f, h}, bl, {0, 0}}, {{w, h}, br, {0, 0}}};
    const int order[6] = {0, 1, 2, 1, 3, 2};
    SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
    const float horizon = h * 0.54f, vx = w * 0.35f;
    SDL_SetRenderDrawColor(pRenderer, 9, 9, 14, 235);
    const SDL_FRect floorRect = {0.0f, horizon, w, h - horizon};
    SDL_RenderFillRectF(pRenderer, &floorRect);
    for (int line = -8; line <= 12; ++line) ThickLine(pRenderer, vx, horizon, w * (static_cast<float>(line) / 10.0f), h, 1.5f * f, SDL_Color{202, 32, 57, 72});
    for (float row : {0.04f, 0.10f, 0.19f, 0.31f, 0.47f, 0.67f, 0.88f}) { const float y = horizon + (h - horizon) * row; ThickLine(pRenderer, 0.0f, y, w, y, 1.0f * f, SDL_Color{240, 53, 75, 52}); }
    for (int streak = 0; streak < 18; ++streak) {
        const float x = static_cast<float>((static_cast<unsigned>(streak * 193) + pTick) % static_cast<unsigned>(std::max(1.0f, windowW))) * f;
        const float y = (125.0f + static_cast<float>((streak * 47) % static_cast<int>(std::max(1.0f, windowH * 0.38f)))) * f;
        const float length = (35.0f + static_cast<float>((streak * 17) % 90)) * f;
        ThickLine(pRenderer, x, y, x + length, y - 8.0f * f, 2.0f * f, SDL_Color{255, 91, 108, 70});
    }
}

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
    Supersample ss(pRenderer);
    if (context != nullptr) ss.Resize(context, width, height);
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
    if (pInfo.reducedMotion) document->SetClass("still", true);
    document->Show();
    if (Rml::Element* first = document->GetElementById("online")) first->Focus();

    // Test hook HOVERNET_TEST_RML="down,down,enter" or "click:garage": scripted input through the real event path, one step every few frames once the opening animation is over.
    std::vector<std::string> script;
    script = ScriptSteps();
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
                    scripted.type = SDL_MOUSEMOTION; scripted.motion.x = static_cast<int>(at.x / static_cast<float>(ss.factor)); scripted.motion.y = static_cast<int>(at.y / static_cast<float>(ss.factor)); SDL_PushEvent(&scripted);
                    SDL_zero(scripted); scripted.type = SDL_MOUSEBUTTONDOWN; scripted.button.button = SDL_BUTTON_LEFT; scripted.button.state = SDL_PRESSED; scripted.button.clicks = 1; scripted.button.x = static_cast<int>(at.x / static_cast<float>(ss.factor)); scripted.button.y = static_cast<int>(at.y / static_cast<float>(ss.factor)); SDL_PushEvent(&scripted);
                    SDL_zero(scripted); scripted.type = SDL_MOUSEBUTTONUP; scripted.button.button = SDL_BUTTON_LEFT; scripted.button.state = SDL_RELEASED; scripted.button.clicks = 1; scripted.button.x = static_cast<int>(at.x / static_cast<float>(ss.factor)); scripted.button.y = static_cast<int>(at.y / static_cast<float>(ss.factor)); SDL_PushEvent(&scripted);
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
            ss.Forward(context, event);
        }
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; ss.Resize(context, width, height); }
        context->Update();
        // Under the interface: the background (a gradient the renderer draws itself, so the craft can sit between it and the markup) and the player's craft.
        ss.Begin();
        {
            DrawMenuBackdrop(pRenderer, ss.W(), ss.H(), ss.factor, pInfo.reducedMotion ? 0u : SDL_GetTicks() / 8u);
            if (pInfo.craftFrame) {
                const Uint32 now = SDL_GetTicks();
                const int yaw = pInfo.reducedMotion ? 28 : static_cast<int>(now / 1000.0 * 22.0) % 360 / 2 * 2;
                SDL_Rect crop = {0, 0, 0, 0};
                SDL_Texture* craft = pInfo.craftFrame(yaw, crop);
                Rml::Element* hero = document->GetElementById("hero");
                if (craft != nullptr && hero != nullptr && crop.w > 0 && crop.h > 0) {
                    if (!craftShown) { craftShown = true; craftStart = now; if (Rml::Element* img = hero->GetFirstChild()) img->SetProperty("display", "none"); }
                    // Placed and sized as the older menu does: as wide as the room left of the panel allows, but no wider than 60% of the window's height, centred in that room and
                    // a little below the middle (window units, then scaled to the large picture).
                    const float f = static_cast<float>(ss.factor), windowW = static_cast<float>(width), windowH = static_cast<float>(height);
                    const float panelW = std::min(430.0f, windowW * (windowW < 820.0f ? 0.5f : 0.42f));
                    const float room = std::max(220.0f, windowW - panelW - 66.0f);
                    const float aspect = static_cast<float>(crop.w) / static_cast<float>(crop.h);
                    const float cw = std::min(room, windowH * 0.6f * aspect), ch = cw / aspect;
                    const Rml::Vector2f origin(std::max(8.0f, (room - cw) * 0.5f + 8.0f) * f, std::max(40.0f, (windowH - ch) * 0.5f + windowH * 0.06f) * f), size(cw * f, ch * f);
                    float w = size.x, h = size.y;
                    const float t = pInfo.reducedMotion ? 1.0f : std::min(1.0f, static_cast<float>(now - craftStart) / 900.0f);
                    const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);   // the same curve as the markup's cubic-out
                    SDL_FRect to = {origin.x - 70.0f * static_cast<float>(ss.factor) * (1.0f - ease), origin.y, w, h};
                    SDL_SetTextureAlphaMod(craft, static_cast<Uint8>(255.0f * ease));
                    SDL_RenderCopyF(pRenderer, craft, &crop, &to);
                }
            }
        }
        context->Render();
        ss.End();
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
        EndFrame(pFrameLimit);
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
    Supersample ss(pRenderer);
    if (context != nullptr) ss.Resize(context, width, height);
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
    script = ScriptSteps();
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
            ss.Forward(context, event);
        }
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; ss.Resize(context, width, height); }
        context->Update();
        ss.Begin();
        SDL_SetRenderDrawColor(pRenderer, 18, 12, 20, 255);
        SDL_RenderClear(pRenderer);
        const SDL_Color top = {18, 12, 20, 255}, bottom = {69, 13, 28, 255};   // the same dark red wash as the ImGui results screen
        const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, top, {0, 0}}, {{static_cast<float>(ss.W()), 0.0f}, top, {0, 0}},
                                    {{0.0f, static_cast<float>(ss.H())}, bottom, {0, 0}}, {{static_cast<float>(ss.W()), static_cast<float>(ss.H())}, bottom, {0, 0}}};
        const int order[6] = {0, 1, 2, 1, 3, 2};
        SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
        context->Render();
        ss.End();
        if (const char* shot = std::getenv("HOVERNET_CAPTURE_RML")) {
            if (pFrameLimit >= 0 && frames == pFrameLimit) {
                if (SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888)) {
                    SDL_RenderReadPixels(pRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
                    SDL_SaveBMP(surface, shot);
                    SDL_FreeSurface(surface);
                }
            }
        }
        SDL_RenderPresent(pRenderer);
        EndFrame(pFrameLimit);
    }
    document->Close();
    Rml::Shutdown();
    return leave(result);
}

namespace
{
// The two backdrops of the older screens. Style 0 (the pause menu): a dark gradient with lines running to a vanishing point on the left and a few horizontal rows. Style 1 (Settings and the
// other screens): a four-corner gradient with a centred perspective grid whose horizon sits low. Drawn into the large picture, so a line is pFactor pixels thick for one window pixel.
void DrawGridBackdrop(SDL_Renderer* pRenderer, int pWidth, int pHeight, int pStyle = 0, int pFactor = 1)
{
    SDL_SetRenderDrawBlendMode(pRenderer, SDL_BLENDMODE_BLEND);
    const float w = static_cast<float>(pWidth), h = static_cast<float>(pHeight);
    const int order[6] = {0, 1, 2, 1, 3, 2};
    auto line = [&](float x0, float y0, float x1, float y1, bool steep) {
        for (int k = 0; k < std::max(1, pFactor); ++k) SDL_RenderDrawLineF(pRenderer, x0 + (steep ? static_cast<float>(k) : 0.0f), y0 + (steep ? 0.0f : static_cast<float>(k)), x1 + (steep ? static_cast<float>(k) : 0.0f), y1 + (steep ? 0.0f : static_cast<float>(k)));
    };
    if (pStyle == 1) {
        SDL_SetRenderDrawColor(pRenderer, 9, 12, 22, 255);
        SDL_RenderClear(pRenderer);
        const SDL_Color tl = {9, 12, 22, 255}, tr = {31, 10, 22, 255}, bl = {5, 8, 15, 255}, br = {13, 7, 15, 255};
        const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, tl, {0, 0}}, {{w, 0.0f}, tr, {0, 0}}, {{0.0f, h}, bl, {0, 0}}, {{w, h}, br, {0, 0}}};
        SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
        const float horizon = h * 0.72f;
        SDL_SetRenderDrawColor(pRenderer, 224, 42, 67, 34);
        for (int l = -8; l <= 8; ++l) line(w * 0.5f, horizon, w * (0.5f + static_cast<float>(l) * 0.14f), h, true);
        SDL_SetRenderDrawColor(pRenderer, 224, 42, 67, 28);
        for (int row = 1; row <= 4; ++row) line(0.0f, horizon + (h - horizon) * static_cast<float>(row * row) / 16.0f, w, horizon + (h - horizon) * static_cast<float>(row * row) / 16.0f, false);
        return;
    }
    SDL_SetRenderDrawColor(pRenderer, 13, 13, 19, 255);
    SDL_RenderClear(pRenderer);
    const SDL_Color top = {13, 13, 19, 255}, bottom = {30, 9, 16, 255};
    const SDL_Vertex quad[4] = {{{0.0f, 0.0f}, top, {0, 0}}, {{w, 0.0f}, top, {0, 0}}, {{0.0f, h}, bottom, {0, 0}}, {{w, h}, bottom, {0, 0}}};
    SDL_RenderGeometry(pRenderer, nullptr, quad, 4, order, 6);
    const float horizon = h * 0.60f, vx = w * 0.28f;
    SDL_SetRenderDrawColor(pRenderer, 227, 43, 66, 55);
    for (int l = -5; l <= 10; ++l) line(vx, horizon, w * (static_cast<float>(l) / 7.0f), h, true);
    SDL_SetRenderDrawColor(pRenderer, 240, 53, 75, 48);
    for (float row : {0.10f, 0.24f, 0.43f, 0.68f, 0.94f}) { const float y = horizon + (h - horizon) * row; line(0.0f, y, w, y, false); }
}

class OptionListener : public Rml::EventListener
{
public:
    OptionListener(int* pResult, std::function<void(Sound)> pSound) : mResult(pResult), mSound(std::move(pSound)) {}
    void ProcessEvent(Rml::Event& pEvent) override
    {
        if (pEvent.GetId() == Rml::EventId::Mouseover) { if (mSound) mSound(Sound::eHover); return; }
        if (mSound) mSound(Sound::eClick);
        *mResult = std::atoi(pEvent.GetCurrentElement()->GetId().c_str() + 3);   // ids are opt0, opt1, ...
        if (std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST choice %d\n", *mResult); std::fflush(stdout); }
    }
private:
    int* mResult;
    std::function<void(Sound)> mSound;
};
}  // namespace

int RunChoiceMenu(SDL_Window* pWindow, SDL_Renderer* pRenderer, const ChoiceInfo& pInfo, int pFrameLimit, int pRestoreW, int pRestoreH)
{
    SDL_RenderSetLogicalSize(pRenderer, 0, 0);
    SystemInterface_SDL system;
    system.SetWindow(pWindow);
    RenderInterface_SDL renderInterface(pRenderer);
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderInterface);
    auto leave = [&](int pResult) { SDL_RenderSetLogicalSize(pRenderer, pRestoreW, pRestoreH); return pResult; };
    if (!Rml::Initialise()) return leave(kChoiceNone);
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(pRenderer, &width, &height);
    Rml::Context* context = Rml::CreateContext("hovernet-choice", Rml::Vector2i(width, height));
    Supersample ss(pRenderer);
    if (context != nullptr) ss.Resize(context, width, height);
    Rml::LoadFontFace(pInfo.assetDir + "/Inter-Variable.ttf", true);
    Rml::LoadFontFace(pInfo.assetDir + "/Oxanium-Variable.ttf", false);
    Rml::ElementDocument* document = context != nullptr ? context->LoadDocument(pInfo.assetDir + "/ui/choice.rml") : nullptr;
    if (document == nullptr) {
        std::fprintf(stderr, "RmlUi choice menu: could not load %s/ui/choice.rml\n", pInfo.assetDir.c_str());
        Rml::Shutdown();
        return leave(kChoiceNone);
    }
    auto set = [&](const char* id, const std::string& text) { if (Rml::Element* e = document->GetElementById(id)) e->SetInnerRML(Escaped(text)); };
    set("title", pInfo.title); set("eyebrow", pInfo.eyebrow); set("subtitle", pInfo.subtitle); set("paneltitle", pInfo.panelTitle);
    Rml::String html;
    for (size_t i = 0; i < pInfo.options.size(); ++i) {
        const ChoiceOption& option = pInfo.options[i];
        if (!option.headingBefore.empty()) html += "<p class=\"group\">" + Escaped(option.headingBefore) + "</p>";
        html += "<button id=\"opt" + std::to_string(i) + "\"" + (option.primary ? " class=\"primary\"" : "") + ">" + Escaped(option.label) + (option.detail.empty() ? "" : "<br/><span class=\"detail\">" + Escaped(option.detail) + "</span>") + "</button>";
    }
    if (Rml::Element* list = document->GetElementById("options")) list->SetInnerRML(html);
    int result = kChoiceNone;
    OptionListener listener(&result, pInfo.sound);
    const int count = static_cast<int>(pInfo.options.size());
    for (int i = 0; i < count; ++i) if (Rml::Element* button = document->GetElementById("opt" + std::to_string(i))) { button->AddEventListener("click", &listener); button->AddEventListener("mouseover", &listener); }
    document->Show();
    if (Rml::Element* first = document->GetElementById("opt0")) first->Focus();
    auto move = [&](int step) {
        if (pInfo.sound) pInfo.sound(Sound::eHover);
        int at = -1;
        Rml::Element* focused = document->GetFocusLeafNode();
        for (int i = 0; i < count; ++i) if (document->GetElementById("opt" + std::to_string(i)) == focused) at = i;
        const int next = at < 0 ? 0 : (at + step + count) % count;
        if (Rml::Element* button = document->GetElementById("opt" + std::to_string(next))) button->Focus();
    };
    std::vector<std::string> script;
    script = ScriptSteps();
    bool decided = false;
    int frames = 0;
    while (!decided && (pFrameLimit < 0 || frames < pFrameLimit)) {
        ++frames;
        if (result != kChoiceNone) { decided = true; break; }
        if (frames >= 60 && (frames - 60) % 4 == 0 && static_cast<size_t>((frames - 60) / 4) < script.size()) {
            const std::string& step = script[static_cast<size_t>((frames - 60) / 4)];
            SDL_Event scripted; SDL_zero(scripted);
            scripted.type = SDL_KEYDOWN; scripted.key.state = SDL_PRESSED;
            scripted.key.keysym.sym = step == "down" ? SDLK_DOWN : step == "up" ? SDLK_UP : step == "enter" ? SDLK_RETURN : SDLK_ESCAPE;
            SDL_PushEvent(&scripted);
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) { result = kChoiceClose; decided = true; break; }
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) { if (pInfo.sound) pInfo.sound(Sound::eBack); result = kChoiceBack; decided = true; break; }
                if (key == SDLK_DOWN) { move(1); continue; }
                if (key == SDLK_UP) { move(-1); continue; }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const int b = event.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START) { if (pInfo.sound) pInfo.sound(Sound::eBack); result = kChoiceBack; decided = true; break; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { move(1); continue; }
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { move(-1); continue; }
                if (b == SDL_CONTROLLER_BUTTON_A) { if (Rml::Element* f = document->GetFocusLeafNode()) f->Click(); continue; }
            }
            ss.Forward(context, event);
        }
        if (decided) break;
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(pRenderer, &w, &h);
        if (w != width || h != height) { width = w; height = h; ss.Resize(context, width, height); }
        context->Update();
        ss.Begin();
        DrawGridBackdrop(pRenderer, ss.W(), ss.H(), pInfo.backdrop, ss.factor);
        context->Render();
        ss.End();
        if (const char* shot = std::getenv("HOVERNET_CAPTURE_RML")) {
            if ((pFrameLimit >= 0 && frames == pFrameLimit) || (pFrameLimit < 0 && frames == 62)) {
                if (SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888)) {
                    SDL_RenderReadPixels(pRenderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
                    SDL_SaveBMP(surface, shot);
                    SDL_FreeSurface(surface);
                }
            }
        }
        SDL_RenderPresent(pRenderer);
        EndFrame(pFrameLimit);
    }
    document->Close();
    Rml::Shutdown();
    return leave(result);
}

// ---- FormScreen ----------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
// Every control reports to one listener, which turns the DOM event into a FormEvent for the caller.
class FormListener : public Rml::EventListener
{
public:
    std::vector<FormEvent> pending;
    FormOutcome outcome = FormOutcome::eNone;
    bool saveEnabled = true;
    std::function<void(Sound)> sound;
    // select options, so the arrows can step through them: id -> (current index, count)
    std::map<std::string, std::pair<int, int>> selects;

    void ProcessEvent(Rml::Event& pEvent) override
    {
        Rml::Element* element = pEvent.GetCurrentElement();
        const Rml::String id = element->GetId();
        if (pEvent.GetId() == Rml::EventId::Mouseover) { if (sound && (element->IsClassSet("act") || element->IsClassSet("tab") || element->IsClassSet("toggle") || element->IsClassSet("arrow"))) sound(Sound::eHover); return; }
        if (pEvent.GetId() == Rml::EventId::Change) {
            FormEvent e;
            e.id = id.size() > 2 && id.compare(0, 2, "f_") == 0 ? id.substr(2) : id;
            if (element->GetTagName() == "input" && element->GetAttribute<Rml::String>("type", "") == "range") {
                e.value = pEvent.GetParameter<float>("value", element->GetAttribute<float>("value", 0.0f));
                if (Rml::Element* shown = element->GetOwnerDocument()->GetElementById("v_" + e.id)) shown->SetInnerRML(FormatNumber(e.value) + element->GetAttribute<Rml::String>("data-suffix", ""));
            }
            else e.text = pEvent.GetParameter<Rml::String>("value", "");
            pending.push_back(e);
            return;
        }
        // clicks
        if (id == "save") { if (saveEnabled) { if (sound) sound(Sound::eClick); outcome = FormOutcome::eSave; } return; }
        if (id == "cancel") { if (sound) sound(Sound::eBack); outcome = FormOutcome::eCancel; return; }
        if (id.compare(0, 3, "tab") == 0 && id.size() > 3 && std::isdigit(static_cast<unsigned char>(id[3]))) {
            if (sound) sound(Sound::eClick);
            FormEvent e; e.id = "tab"; e.index = std::atoi(id.c_str() + 3); pending.push_back(e);
            return;
        }
        if (sound) sound(Sound::eClick);
        if (element->IsClassSet("toggle")) {
            const bool on = !element->IsClassSet("on");
            element->SetClass("on", on);
            FormEvent e; e.id = id.substr(2); e.on = on; pending.push_back(e);
        }
        else if (element->IsClassSet("arrow")) {   // f_<id>_prev / f_<id>_next
            const bool next = id.size() > 5 && id.compare(id.size() - 5, 5, "_next") == 0;
            const std::string base = id.substr(2, id.size() - 2 - (next ? 5 : 5));
            auto found = selects.find(base);
            if (found != selects.end() && found->second.second > 0) {
                const int count = found->second.second;
                const int index = (found->second.first + (next ? 1 : -1) + count) % count;
                found->second.first = index;
                FormEvent e; e.id = base; e.index = index; pending.push_back(e);
            }
        }
        else if (element->IsClassSet("act")) { FormEvent e; e.id = id.substr(2); pending.push_back(e); }
    }

    static std::string FormatNumber(float pValue)
    {
        char text[32];
        if (pValue == static_cast<float>(static_cast<int>(pValue))) std::snprintf(text, sizeof text, "%d", static_cast<int>(pValue)); else std::snprintf(text, sizeof text, "%.1f", pValue);
        return text;
    }
};
}  // namespace

struct FormScreen::Impl
{
    SystemInterface_SDL system;
    std::unique_ptr<RenderInterface_SDL> render;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    FormListener listener;
    std::unique_ptr<Supersample> ss;
    bool ok = false, initialised = false;
    int width = 0, height = 0, restoreW = 0, restoreH = 0;
    std::vector<std::string> script;     // test hook HOVERNET_TEST_RML: "tab:2", "click:name", "text:name=Tester", "enter", "down" ...
    int tabCount = 0;
};

FormScreen::FormScreen(SDL_Window* pWindow, SDL_Renderer* pRenderer, const std::string& pAssetDir, const std::string& pTitle, bool, std::function<void(Sound)> pSound,
                       int pRestoreW, int pRestoreH)
    : mImpl(new Impl)
{
    Impl& m = *mImpl;
    m.window = pWindow; m.renderer = pRenderer; m.restoreW = pRestoreW; m.restoreH = pRestoreH;
    m.listener.sound = std::move(pSound);
    SDL_RenderSetLogicalSize(pRenderer, 0, 0);
    m.system.SetWindow(pWindow);
    m.render.reset(new RenderInterface_SDL(pRenderer));
    Rml::SetSystemInterface(&m.system);
    Rml::SetRenderInterface(m.render.get());
    if (!Rml::Initialise()) return;
    m.initialised = true;
    SDL_GetRendererOutputSize(pRenderer, &m.width, &m.height);
    m.context = Rml::CreateContext("hovernet-form", Rml::Vector2i(m.width, m.height));
    m.ss.reset(new Supersample(pRenderer));
    if (m.context != nullptr) m.ss->Resize(m.context, m.width, m.height);
    Rml::LoadFontFace(pAssetDir + "/Inter-Variable.ttf", true);
    Rml::LoadFontFace(pAssetDir + "/Oxanium-Variable.ttf", false);
    m.document = m.context != nullptr ? m.context->LoadDocument(pAssetDir + "/ui/form.rml") : nullptr;
    if (m.document == nullptr) { std::fprintf(stderr, "RmlUi form: could not load %s/ui/form.rml\n", pAssetDir.c_str()); return; }
    if (Rml::Element* title = m.document->GetElementById("title")) title->SetInnerRML(Escaped(pTitle));
    for (const char* id : {"save", "cancel"}) if (Rml::Element* button = m.document->GetElementById(id)) { button->AddEventListener("click", &m.listener); button->AddEventListener("mouseover", &m.listener); }
    m.document->Show();
    m.script = ScriptSteps();
    m.ok = true;
}

FormScreen::~FormScreen()
{
    Impl& m = *mImpl;
    if (m.document != nullptr) m.document->Close();
    m.ss.reset();
    if (m.initialised) Rml::Shutdown();
    SDL_RenderSetLogicalSize(m.renderer, m.restoreW, m.restoreH);
    delete mImpl;
}

bool FormScreen::Ok() const { return mImpl->ok; }

void FormScreen::SetTabs(const std::vector<std::string>& pNames, int pSelected)
{
    Impl& m = *mImpl;
    Rml::Element* tabs = m.document->GetElementById("tabs");
    if (tabs == nullptr) return;
    m.tabCount = static_cast<int>(pNames.size());
    Rml::String html;
    for (size_t i = 0; i < pNames.size(); ++i) html += "<button id=\"tab" + std::to_string(i) + "\" class=\"tab" + (static_cast<int>(i) == pSelected ? " sel" : "") + "\">" + Escaped(pNames[i]) + "</button>";
    tabs->SetInnerRML(html);
    for (size_t i = 0; i < pNames.size(); ++i) if (Rml::Element* tab = m.document->GetElementById("tab" + std::to_string(i))) { tab->AddEventListener("click", &m.listener); tab->AddEventListener("mouseover", &m.listener); }
}

void FormScreen::SetFields(const std::vector<FormField>& pFields)
{
    Impl& m = *mImpl;
    Rml::Element* host = m.document->GetElementById("fields");
    if (host == nullptr) return;
    m.listener.selects.clear();
    Rml::String html;
    for (const FormField& f : pFields) {
        const Rml::String id = Escaped(f.id);
        switch (f.type) {
        case FormField::Type::eHeading: html += "<p class=\"heading\">" + Escaped(f.label) + "</p>"; break;
        case FormField::Type::eNote: html += "<p class=\"note\">" + Escaped(f.label) + "</p>"; break;
        case FormField::Type::eText:
            html += "<div class=\"field\"><p class=\"label\">" + Escaped(f.label) + "</p><input type=\"text\" class=\"text\" id=\"f_" + id + "\" value=\"" + Escaped(f.text) + "\"/><p class=\"hint\" id=\"h_" + id + "\">" + Escaped(f.hint) + "</p></div>";
            break;
        case FormField::Type::eToggle:
            html += "<div class=\"toggle" + Rml::String(f.on ? " on" : "") + "\" id=\"f_" + id + "\"><span class=\"box\"></span><span class=\"tlabel\">" + Escaped(f.label) + "</span></div>";
            if (!f.hint.empty()) html += "<p class=\"note\">" + Escaped(f.hint) + "</p>";
            break;
        case FormField::Type::eSlider:
            html += "<div class=\"field\"><p class=\"label\">" + Escaped(f.label) + "<span class=\"val\" id=\"v_" + id + "\">" + FormListener::FormatNumber(f.value) + Escaped(f.suffix) + "</span></p><input type=\"range\" class=\"range\" id=\"f_" + id + "\" min=\"" +
                    std::to_string(f.min) + "\" max=\"" + std::to_string(f.max) + "\" step=\"" + std::to_string(f.step) + "\" value=\"" + std::to_string(f.value) + "\" data-suffix=\"" + Escaped(f.suffix) + "\"/>" +
                    (f.hint.empty() ? Rml::String() : "<p class=\"note\">" + Escaped(f.hint) + "</p>") + "</div>";
            break;
        case FormField::Type::eSelect: {
            const int count = static_cast<int>(f.options.size());
            const int at = count > 0 ? std::max(0, std::min(count - 1, f.selected)) : 0;
            m.listener.selects[f.id] = std::make_pair(at, count);
            html += "<div class=\"field\"><p class=\"label\">" + Escaped(f.label) + "</p><div class=\"select\" id=\"f_" + id + "\"><button class=\"arrow\" id=\"f_" + id + "_prev\">&lt;</button><span class=\"sval\" id=\"v_" + id + "\">" +
                    Escaped(count > 0 ? f.options[static_cast<size_t>(at)] : "") + "</span><button class=\"arrow\" id=\"f_" + id + "_next\">&gt;</button></div>" +
                    (f.hint.empty() ? Rml::String() : "<p class=\"note\">" + Escaped(f.hint) + "</p>") + "</div>";
            break;
        }
        case FormField::Type::eButton:
            html += "<button class=\"act" + Rml::String(f.quiet ? " quiet" : "") + "\" id=\"f_" + id + "\">" + Escaped(f.label) + "</button>";
            break;
        }
    }
    host->SetInnerRML(html);
    for (const FormField& f : pFields) {
        Rml::Element* e = m.document->GetElementById("f_" + f.id);
        if (e == nullptr) continue;
        switch (f.type) {
        case FormField::Type::eText: case FormField::Type::eSlider: e->AddEventListener("change", &m.listener); break;
        case FormField::Type::eToggle: case FormField::Type::eButton: e->AddEventListener("click", &m.listener); e->AddEventListener("mouseover", &m.listener); break;
        case FormField::Type::eSelect:
            for (const char* suffix : {"_prev", "_next"}) if (Rml::Element* arrow = m.document->GetElementById("f_" + f.id + suffix)) { arrow->AddEventListener("click", &m.listener); arrow->AddEventListener("mouseover", &m.listener); }
            break;
        default: break;
        }
    }
    host->SetScrollTop(0.0f);
}

void FormScreen::SetHint(const std::string& pId, const std::string& pText)
{
    if (Rml::Element* e = mImpl->document->GetElementById("h_" + pId)) e->SetInnerRML(Escaped(pText));
}

void FormScreen::SetSaveEnabled(bool pEnabled)
{
    mImpl->listener.saveEnabled = pEnabled;
    if (Rml::Element* save = mImpl->document->GetElementById("save")) save->SetClass("disabled", !pEnabled);
}

void FormScreen::SetText(const std::string& pId, const std::string& pText)
{
    if (Rml::Element* e = mImpl->document->GetElementById("f_" + pId)) if (auto* control = dynamic_cast<Rml::ElementFormControl*>(e)) control->SetValue(pText);
}

void FormScreen::SetToggle(const std::string& pId, bool pOn)
{
    if (Rml::Element* e = mImpl->document->GetElementById("f_" + pId)) e->SetClass("on", pOn);
}

void FormScreen::SetSlider(const std::string& pId, float pValue)
{
    if (Rml::Element* e = mImpl->document->GetElementById("f_" + pId)) {
        e->SetAttribute("value", pValue);
        if (Rml::Element* shown = mImpl->document->GetElementById("v_" + pId)) shown->SetInnerRML(FormListener::FormatNumber(pValue) + e->GetAttribute<Rml::String>("data-suffix", ""));
    }
}

void FormScreen::SetSelect(const std::string& pId, int pIndex, const std::string& pShown)
{
    Impl& m = *mImpl;
    auto found = m.listener.selects.find(pId);
    if (found != m.listener.selects.end()) found->second.first = pIndex;
    if (Rml::Element* shown = m.document->GetElementById("v_" + pId)) shown->SetInnerRML(Escaped(pShown));
}

FormOutcome FormScreen::Frame(std::vector<FormEvent>& pEvents, int pFrame, int pFrameLimit)
{
    Impl& m = *mImpl;
    // scripted input (tests): steps start after the opening animation, four frames apart
    if (pFrame >= 60 && (pFrame - 60) % 4 == 0 && static_cast<size_t>((pFrame - 60) / 4) < m.script.size()) {
        const std::string& step = m.script[static_cast<size_t>((pFrame - 60) / 4)];
        SDL_Event scripted; SDL_zero(scripted);
        if (step.compare(0, 4, "tab:") == 0) { if (Rml::Element* e = m.document->GetElementById("tab" + step.substr(4))) e->Click(); }
        else if (step.compare(0, 6, "click:") == 0) { if (Rml::Element* e = m.document->GetElementById(step.substr(6))) e->Click(); }
        else if (step.compare(0, 5, "text:") == 0) {
            const size_t eq = step.find('=');
            if (eq != std::string::npos) if (Rml::Element* e = m.document->GetElementById("f_" + step.substr(5, eq - 5))) if (auto* control = dynamic_cast<Rml::ElementFormControl*>(e)) {
                control->SetValue(step.substr(eq + 1));
                FormEvent event; event.id = step.substr(5, eq - 5); event.text = step.substr(eq + 1); m.listener.pending.push_back(event);
            }
        }
        else {
            scripted.type = SDL_KEYDOWN; scripted.key.state = SDL_PRESSED;
            scripted.key.keysym.sym = step == "down" ? SDLK_DOWN : step == "up" ? SDLK_UP : step == "enter" ? SDLK_RETURN : SDLK_ESCAPE;
            SDL_PushEvent(&scripted);
        }
    }
    auto focusStep = [&](bool pBackwards) {
        if (m.listener.sound) m.listener.sound(Sound::eHover);
        m.context->ProcessKeyDown(Rml::Input::KI_TAB, pBackwards ? Rml::Input::KM_SHIFT : 0);
        m.context->ProcessKeyUp(Rml::Input::KI_TAB, pBackwards ? Rml::Input::KM_SHIFT : 0);
    };
    auto arrowOnSelect = [&](bool pNext) -> bool {   // Left and Right on a focused select choose the neighbouring option
        Rml::Element* focused = m.document->GetFocusLeafNode();
        if (focused == nullptr || !focused->IsClassSet("select")) return false;
        if (Rml::Element* arrow = m.document->GetElementById(focused->GetId() + (pNext ? "_next" : "_prev"))) arrow->Click();
        return true;
    };
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return FormOutcome::eClose;
        if (event.type == SDL_KEYDOWN) {
            const SDL_Keycode key = event.key.keysym.sym;
            Rml::Element* focused = m.document->GetFocusLeafNode();
            const bool typing = focused != nullptr && focused->GetTagName() == "input" && focused->GetAttribute<Rml::String>("type", "text") == "text";
            if (key == SDLK_ESCAPE) { if (m.listener.sound) m.listener.sound(Sound::eBack); return FormOutcome::eCancel; }
            if (key == SDLK_DOWN) { focusStep(false); continue; }
            if (key == SDLK_UP) { focusStep(true); continue; }
            if (!typing && key == SDLK_LEFT && arrowOnSelect(false)) continue;
            if (!typing && key == SDLK_RIGHT && arrowOnSelect(true)) continue;
            if (!typing && (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)) { if (focused != nullptr) focused->Click(); continue; }
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            const int b = event.cbutton.button;
            if (b == SDL_CONTROLLER_BUTTON_B) { if (m.listener.sound) m.listener.sound(Sound::eBack); return FormOutcome::eCancel; }
            if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { focusStep(false); continue; }
            if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { focusStep(true); continue; }
            if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT && arrowOnSelect(false)) continue;
            if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && arrowOnSelect(true)) continue;
            if (b == SDL_CONTROLLER_BUTTON_A) { if (Rml::Element* f = m.document->GetFocusLeafNode()) f->Click(); continue; }
        }
        m.ss->Forward(m.context, event);
    }
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(m.renderer, &w, &h);
    if (w != m.width || h != m.height) { m.width = w; m.height = h; m.ss->Resize(m.context, w, h); }
    m.context->Update();
    m.ss->Begin();
    DrawGridBackdrop(m.renderer, m.ss->W(), m.ss->H(), 1, m.ss->factor);
    m.context->Render();
    m.ss->End();
    if (const char* shot = std::getenv("HOVERNET_CAPTURE_RML")) {
        if ((pFrameLimit >= 0 && pFrame == pFrameLimit) || (pFrameLimit < 0 && pFrame == 62)) {
            if (SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, m.width, m.height, 32, SDL_PIXELFORMAT_ARGB8888)) {
                SDL_RenderReadPixels(m.renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
                SDL_SaveBMP(surface, shot);
                SDL_FreeSurface(surface);
            }
        }
    }
    SDL_RenderPresent(m.renderer);
    EndFrame(pFrameLimit);
    for (const FormEvent& e : m.listener.pending) {
        if (std::getenv("HOVERNET_TEST_RML") != nullptr) { std::printf("RMLTEST form %s text=[%s] on=%d value=%g index=%d\n", e.id.c_str(), e.text.c_str(), e.on ? 1 : 0, e.value, e.index); std::fflush(stdout); }
        pEvents.push_back(e);
    }
    m.listener.pending.clear();
    const FormOutcome out = m.listener.outcome;
    m.listener.outcome = FormOutcome::eNone;
    return out;
}
void SetFrameHook(void (*pHook)()) { gFrameHook = pHook; }
}  // namespace HoverNetRml
