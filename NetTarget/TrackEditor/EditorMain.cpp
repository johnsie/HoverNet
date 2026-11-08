// HoverNet Track Editor: the window. Everything it does to a track is in EditorSession (tested without a window); this file draws the plan, turns the
// mouse and keys into those calls, and shows what the session says. One program for Windows, Linux and (next) the browser: SDL2 and Dear ImGui, the stack of
// the game client (docs/adr/0003-track-editor.md).
//
//   HoverNetTrackEditor [FILE.track.txt] [--new-loop] [--size WxH] [--tools DIR] [--frames N] [--capture OUT.bmp] [--check]
//   --demo-road builds a short winding road from a start room (with the Extend tool's preview showing at its open end), for the picture tests.
//   --frames N and --capture are for tests: draw N frames, save the last as a picture, and leave. --check compiles FILE with the real compiler and validator
//   (the same as Track > Compile and check), prints the answer and leaves; the exit status is 0 only for a track the strict validator calls VALID.
#include "EditorSession.h"
#include "TrackElementCatalogue.h"

#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace HoverNet;

namespace
{
enum class Tool { Select, PlaceElement, PlaceStart, ExtendRoad };

struct View
{
    double scale = 3.0;       // pixels per metre
    double cx = 0, cy = 0;    // the plan point at the middle of the canvas
};

struct App
{
    EditorSession ed;
    View view;
    Tool tool = Tool::Select;
    int placeType = 152;       // the element the Place tool puts down
    float extendLength = 30.0f, extendTurn = 0.0f;   // what the Extend the road tool adds
    std::vector<WallPoint> ghost;        // the room the Extend tool would add under the pointer (empty when there is none)
    int ghostRoom = -1, ghostWall = -1;  // and the edge it grows from
    std::string ghostWhy;                // why nothing would be added there
    bool forcePointer = false; double forceX = 0, forceY = 0;   // tests: pretend the pointer is here (plan coordinates)
    int newTemplate = 0;       // the New dialog: 0 a loop, 1 a start room to build from
    bool panning = false;
    ImVec2 panStart;
    View panView;
    bool fitPending = true;
    // dialogs
    bool openNew = false, openOpen = false, openSaveAs = false, openQuit = false, openCheck = false, openPublish = false, showGrid = true;
    char fileBuffer[512] = "";
    NewTrackOptions newOptions;
    char newName[64] = "My New Track";
    std::string toolsDir;
    std::string checkOutput;
    bool quit = false;
};


#ifdef __EMSCRIPTEN__
// The browser has no file paths: a track is opened through the browser's file picker (the file is copied into the page's own memory file system) and saved
// as a download. The page and these functions are packaging/web/editor-shell.html.
EM_JS(void, hn_pick_file, (), {
    var input = document.createElement('input');
    input.type = 'file';
    input.accept = '.txt,.track,.track.txt,text/plain';
    input.onchange = function () {
        var f = input.files[0];
        if (!f) return;
        var r = new FileReader();
        r.onload = function () { FS.writeFile('/upload.track.txt', new Uint8Array(r.result)); Module.ccall('hn_file_ready', null, ['string'], [f.name]); };
        r.readAsArrayBuffer(f);
    };
    input.click();
});
EM_JS(void, hn_download, (const char* path, const char* name), {
    var data = FS.readFile(UTF8ToString(path));
    var blob = new Blob([data], { type: 'text/plain' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = UTF8ToString(name);
    document.body.appendChild(a);
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
});
// Publishing (docs/adr/0004-track-marketplace.md, docs/specs/track-market.md section 1b): the page already holds the author's Outiva sign-in (an HttpOnly cookie on
// this site), so the editor can send the track to the market itself; the service compiles it, proves it can be driven round and checks it.
EM_JS(void, hn_fetch_info, (), {
    var done = function (a, b, c, d, e) { Module.ccall('hn_info_done', null, ['number', 'number', 'number', 'string', 'string'], [a, b, c, d, e]); };
    fetch('/market/info', { credentials: 'same-origin' }).then(function (r) {
        return r.json().then(function (j) { var you = j.you || {}; done(r.status, you.signedIn ? 1 : 0, you.invited ? 1 : 0, j.declaration || '', j.declarationText || ''); });
    }).catch(function () { done(0, 0, 0, '', ''); });
});
EM_JS(void, hn_publish_js, (const char* source, const char* name, const char* author, const char* licence, const char* declaration, const char* description), {
    var fd = new FormData();
    fd.append('source', new Blob([UTF8ToString(source)], { type: 'text/plain' }), 'track.track.txt');
    fd.append('name', UTF8ToString(name)); fd.append('author', UTF8ToString(author)); fd.append('licence', UTF8ToString(licence));
    fd.append('declaration', UTF8ToString(declaration)); fd.append('description', UTF8ToString(description));
    try { localStorage.setItem('hn_author', UTF8ToString(author)); } catch (e) {}
    var done = function (status, text) { Module.ccall('hn_publish_done', null, ['number', 'string'], [status, text]); };
    fetch('/market/submit-source', { method: 'POST', body: fd, credentials: 'same-origin' }).then(function (r) {
        return r.json().catch(function () { return {}; }).then(function (j) {
            var msg = '';
            if (j.error) msg = j.error;
            else if (j.reasons && j.reasons.length) msg = 'Turned down:\n' + j.reasons.join('\n');
            else if (j.status) msg = 'Sent. ' + (j.status === 'listed' ? 'It is listed: players can find it in the Community tracks list.' : 'It is ' + j.status + ': a moderator looks at an author\'s first track before it is listed.');
            else msg = 'The Track Market answered with status ' + r.status;
            done(r.status, msg);
        });
    }).catch(function () { done(0, 'Could not reach the Track Market: check your connection and try again.'); });
});
EM_JS(void, hn_open_signin, (), { window.open('/account/login', '_blank');   /* signs in in a new tab and ends on the game page; the editor tab keeps the work */ });
// Autosave: the browser keeps a copy of the track being edited (in this site's local storage) a moment after every change, and offers it back the next time the
// editor opens, so a closed tab or a crash does not lose an author's work. Saving to a file (a download) is still how a track is kept for good.
EM_JS(void, hn_autosave_put, (const char* text), { try { localStorage.setItem('hn_autosave', UTF8ToString(text)); } catch (e) {} });
EM_JS(void, hn_autosave_clear, (), { try { localStorage.removeItem('hn_autosave'); } catch (e) {} });
std::string gUploadName;
std::string gRestoreText;            // a copy found at start-up, waiting for the person's answer
bool gRestoreAsked = false;
bool gRestoreVisible = false;
std::string gLastAutosaved;
ImVec2 gRestoreBtn(-1, -1), gFreshBtn(-1, -1);   // where the two answers were drawn (the browser test clicks them)
bool gUploadReady = false;
int gResizeW = 0, gResizeH = 0;
struct PublishState
{
    bool infoLoaded = false, infoAsked = false, signedIn = false, invited = false, reachable = true;
    std::string declaration, declarationText;
    bool working = false;
    int lastHttp = -1;
    std::string message;
    char name[64] = "", author[48] = "", description[208] = "";
    int licence = 0;
    bool accepted = false;
};
PublishState gPublish;
extern "C" {
EMSCRIPTEN_KEEPALIVE void hn_info_done(int status, int signedIn, int invited, const char* declaration, const char* text)
{
    gPublish.infoLoaded = true; gPublish.reachable = status == 200;
    gPublish.signedIn = signedIn != 0; gPublish.invited = invited != 0;
    gPublish.declaration = declaration ? declaration : ""; gPublish.declarationText = text ? text : "";
}
EMSCRIPTEN_KEEPALIVE void hn_publish_done(int status, const char* text) { gPublish.working = false; gPublish.lastHttp = status; gPublish.message = text ? text : ""; }
EMSCRIPTEN_KEEPALIVE void hn_autosave_found(const char* text) { if (text && text[0]) { gRestoreText = text; gRestoreAsked = true; } }
EMSCRIPTEN_KEEPALIVE void hn_author_known(const char* saved) { if (saved && gPublish.author[0] == '\0') std::snprintf(gPublish.author, sizeof(gPublish.author), "%s", saved); }
EMSCRIPTEN_KEEPALIVE void hn_file_ready(const char* name) { gUploadName = name ? name : "track.track.txt"; gUploadReady = true; }
EMSCRIPTEN_KEEPALIVE void hn_resize(int w, int h) { gResizeW = w; gResizeH = h; }
}
#endif


// ---- the look: the game's (Inter for text, Oxanium for headings, near-black panels with one red accent), so the editor and the game feel like one product ----
ImFont* gDisplayFont = nullptr;
const ImVec4 kRed(0.86f, 0.20f, 0.27f, 1.0f), kRedHover(0.95f, 0.32f, 0.38f, 1.0f), kRedActive(0.70f, 0.12f, 0.18f, 1.0f), kCoral(0.98f, 0.45f, 0.48f, 1.0f);

std::string AssetPath(const char* name)
{
#ifdef __EMSCRIPTEN__
    return std::string("/assets/") + name;
#else
    return std::string(HOVERNET_SOURCE_DIR "/NetTarget/LinuxClient/assets/") + name;
#endif
}

void ApplyTheme(ImGuiIO& io)
{
    ImFontConfig body;
    body.SizePixels = 17.0f; body.OversampleH = 2; body.OversampleV = 2; body.RasterizerMultiply = 1.08f;
    ImFont* bodyFont = io.Fonts->AddFontFromFileTTF(AssetPath("Inter-Variable.ttf").c_str(), body.SizePixels, &body);
    if (!bodyFont) io.Fonts->AddFontDefault();
    ImFontConfig display;
    display.SizePixels = 21.0f; display.OversampleH = 2; display.OversampleV = 2; display.RasterizerMultiply = 1.1f;
    gDisplayFont = io.Fonts->AddFontFromFileTTF(AssetPath("Oxanium-Variable.ttf").c_str(), display.SizePixels, &display);
    if (!gDisplayFont) gDisplayFont = bodyFont;

    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(16, 14); st.FramePadding = ImVec2(10, 6); st.ItemSpacing = ImVec2(10, 9); st.ScrollbarSize = 13;
    st.WindowRounding = 0; st.ChildRounding = 10; st.FrameRounding = 7; st.GrabRounding = 7; st.PopupRounding = 10; st.ScrollbarRounding = 8; st.TabRounding = 7;
    st.WindowBorderSize = 0; st.PopupBorderSize = 1; st.FrameBorderSize = 0;
    ImVec4* c = st.Colors;
    const ImVec4 panel(0.075f, 0.080f, 0.105f, 1.0f), field(0.045f, 0.050f, 0.070f, 1.0f), border(0.20f, 0.22f, 0.29f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.65f, 0.72f, 1.0f);
    c[ImGuiCol_WindowBg] = panel; c[ImGuiCol_ChildBg] = panel; c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Border] = border; c[ImGuiCol_Separator] = border;
    c[ImGuiCol_FrameBg] = field; c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.13f, 0.18f, 1.0f); c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_CheckMark] = kCoral; c[ImGuiCol_SliderGrab] = kRed; c[ImGuiCol_SliderGrabActive] = kRedActive;
    c[ImGuiCol_Button] = kRed; c[ImGuiCol_ButtonHovered] = kRedHover; c[ImGuiCol_ButtonActive] = kRedActive;
    c[ImGuiCol_Header] = ImVec4(0.86f, 0.20f, 0.27f, 0.35f); c[ImGuiCol_HeaderHovered] = ImVec4(0.86f, 0.20f, 0.27f, 0.55f); c[ImGuiCol_HeaderActive] = ImVec4(0.86f, 0.20f, 0.27f, 0.75f);
    c[ImGuiCol_TitleBg] = panel; c[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.12f, 0.17f, 1.0f); c[ImGuiCol_MenuBarBg] = ImVec4(0.035f, 0.040f, 0.060f, 1.0f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, 0.01f, 0.02f, 0.72f); c[ImGuiCol_NavHighlight] = kCoral;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.040f, 0.060f, 1.0f); c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.34f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.075f, 0.080f, 0.105f, 0.98f);
}

// A heading with the game's red bar, in the display face.
void Heading(const char* label)
{
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + 4.0f, at.y + h), ImGui::ColorConvertFloat4ToU32(kRed), 2.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kCoral);
    if (gDisplayFont) ImGui::PushFont(gDisplayFont);
    ImGui::TextUnformatted(label);
    if (gDisplayFont) ImGui::PopFont();
    ImGui::PopStyleColor();
}

// A secondary action: a quiet, outlined button, so a dialog has one red button (the thing to press) and the rest recede.
bool QuietButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.11f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.12f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.10f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.31f, 0.33f, 0.41f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

// The primary button, with a little depth (a highlight across the top, a shade along the bottom).
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float r = ImGui::GetStyle().FrameRounding;
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(lo, ImVec2(hi.x, lo.y + (hi.y - lo.y) * 0.5f), IM_COL32(255, 255, 255, ImGui::IsItemHovered() ? 38 : 20), r, ImDrawFlags_RoundCornersTop);
    d->AddRectFilled(ImVec2(lo.x, hi.y - 3.0f), hi, IM_COL32(0, 0, 0, 46), r, ImDrawFlags_RoundCornersBottom);
    return pressed;
}

ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

ImVec2 ToScreen(const View& v, const ImVec2& origin, const ImVec2& size, double x, double y)
{
    return ImVec2(static_cast<float>(origin.x + size.x / 2 + (x - v.cx) * v.scale), static_cast<float>(origin.y + size.y / 2 - (y - v.cy) * v.scale));
}

void ToPlan(const View& v, const ImVec2& origin, const ImVec2& size, const ImVec2& p, double& x, double& y)
{
    x = v.cx + (p.x - origin.x - size.x / 2) / v.scale;
    y = v.cy - (p.y - origin.y - size.y / 2) / v.scale;
}

void Fit(App& a, const ImVec2& size)
{
    const PlanBounds b = ModelBounds(a.ed.model);
    if (!b.valid) return;
    a.view.cx = (b.minX + b.maxX) / 2; a.view.cy = (b.minY + b.maxY) / 2;
    const double w = std::max(1.0, b.maxX - b.minX), h = std::max(1.0, b.maxY - b.minY);
    a.view.scale = std::max(0.2, std::min((size.x - 120) / w, (size.y - 120) / h));   // a comfortable margin, so the end of a road is never at the edge
}

std::vector<ImVec2> gElementScreen;
ImVec2 gMenuTrack(-1, -1), gMenuPublish(-1, -1);   // where the Track menu and its Publish item were last drawn (the browser test clicks there)
bool gPublishVisible = false;
ImVec2 gCanvasOrigin(0, 0), gCanvasSize(0, 0);      // the plan's last position on screen (the browser test works out where to click)   // where each element was last drawn (the browser test clicks there)

struct ElementLook { ImU32 colour; const char* letter; };
ElementLook LookOf(int type)
{
    switch (type) {
    case 202: return {Col(255, 255, 255), "F"};
    case 203: return {Col(255, 220, 80), "1"};
    case 204: return {Col(255, 220, 80), "2"};
    case 152: return {Col(90, 220, 120), "P"};
    case 200: return {Col(90, 170, 255), "U"};
    case 201: return {Col(255, 150, 60), "S"};
    case 170: return {Col(200, 120, 255), "B"};
    default: return {Col(200, 200, 200), "?"};
    }
}

std::string ElementName(int type)
{
    const TrackElementInfo* info = FindTrackElement(static_cast<unsigned short>(type));
    return info ? info->name : "unknown";
}

void DrawPlan(App& a, const ImVec2& origin, const ImVec2& size)
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    d->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y), Col(12, 15, 26), Col(24, 11, 22), Col(8, 10, 17), Col(12, 8, 14));
    const View& v = a.view;
    if (a.showGrid && v.scale > 1.2) {
        const double step = v.scale > 6 ? 10.0 : 50.0;
        double x0, y0, x1, y1;
        ToPlan(v, origin, size, origin, x0, y1);
        ToPlan(v, origin, size, ImVec2(origin.x + size.x, origin.y + size.y), x1, y0);
        for (double gx = std::floor(x0 / step) * step; gx <= x1; gx += step)
            d->AddLine(ToScreen(v, origin, size, gx, y0), ToScreen(v, origin, size, gx, y1), Col(30, 30, 42));
        for (double gy = std::floor(y0 / step) * step; gy <= y1; gy += step)
            d->AddLine(ToScreen(v, origin, size, x0, gy), ToScreen(v, origin, size, x1, gy), Col(30, 30, 42));
    }
    // which room edges are joins to another room (drawn thin) and which are walls (drawn bold)
    std::set<std::pair<int, int>> joined;
    for (const TrackConnection& c : a.ed.model.connections) { joined.insert({c.room1, c.wall1}); joined.insert({c.room2, c.wall2}); }
    const EditorSession& ed = a.ed;
    for (std::size_t i = 0; i < ed.model.rooms.size(); ++i) {
        const TrackRoom& r = ed.model.rooms[i];
        if (r.walls.size() < 3) continue;
        std::vector<ImVec2> pts;
        for (const WallPoint& w : r.walls) pts.push_back(ToScreen(v, origin, size, w.x, w.y));
        const bool selected = (ed.selection.kind == Selection::Room || ed.selection.kind == Selection::Corner) && ed.selection.index == static_cast<int>(i);
        const bool problem = false;
        d->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), r.isFeature ? Col(150, 90, 190, 120) : (selected ? Col(78, 82, 132) : Col(44, 49, 78)));
        for (std::size_t k = 0; k < pts.size(); ++k) {
            const bool isJoin = joined.count({r.id, static_cast<int>(k)}) != 0 && !r.isFeature;
            d->AddLine(pts[k], pts[(k + 1) % pts.size()], isJoin ? Col(70, 74, 100) : Col(235, 235, 245), isJoin ? 1.0f : 2.0f);
        }
        if (selected) {
            for (std::size_t k = 0; k < pts.size(); ++k) {
                const bool picked = ed.selection.kind == Selection::Corner && ed.selection.corner == static_cast<int>(k);
                d->AddCircleFilled(pts[k], picked ? 6.0f : 4.5f, picked ? Col(255, 120, 130) : Col(255, 210, 90));
            }
        }
        (void)problem;
        if (v.scale > 2.2 && !r.isFeature) {
            ImVec2 mid(0, 0);
            for (const ImVec2& p : pts) { mid.x += p.x / pts.size(); mid.y += p.y / pts.size(); }
            char label[16];
            std::snprintf(label, sizeof(label), "%d", r.id);
            d->AddText(ImVec2(mid.x - 6, mid.y - 6), Col(110, 114, 150), label);
        }
    }
    // starts: an arrow along the heading, with the team number
    for (std::size_t i = 0; i < ed.model.starts.size(); ++i) {
        const TrackStart& s = ed.model.starts[i];
        const ImVec2 p = ToScreen(v, origin, size, s.x, s.y);
        const double rad = s.orientation * 3.14159265358979 / 180.0;
        const ImVec2 dir(static_cast<float>(std::cos(rad)), static_cast<float>(-std::sin(rad)));
        const ImVec2 side(-dir.y, dir.x);
        const float L = 9.0f, W = 5.0f;
        const bool picked = ed.selection.kind == Selection::Start && ed.selection.index == static_cast<int>(i);
        d->AddTriangleFilled(ImVec2(p.x + dir.x * L, p.y + dir.y * L), ImVec2(p.x - dir.x * L + side.x * W, p.y - dir.y * L + side.y * W),
                             ImVec2(p.x - dir.x * L - side.x * W, p.y - dir.y * L - side.y * W), picked ? Col(255, 120, 130) : Col(120, 200, 255));
        if (v.scale > 3.0) { char t[8]; std::snprintf(t, sizeof(t), "%d", s.team); d->AddText(ImVec2(p.x + 6, p.y + 4), Col(160, 210, 255), t); }
    }
    // elements: a coloured disc with a letter
    gElementScreen.assign(ed.model.freeElements.size(), ImVec2(-1, -1));
    for (std::size_t i = 0; i < ed.model.freeElements.size(); ++i) {
        const TrackFreeElement& e = ed.model.freeElements[i];
        const ImVec2 p = ToScreen(v, origin, size, e.x, e.y);
        gElementScreen[i] = p;
        const ElementLook look = LookOf(e.elementType);
        const bool picked = ed.selection.kind == Selection::Element && ed.selection.index == static_cast<int>(i);
        d->AddCircleFilled(p, 8.0f, Col(10, 10, 14));
        d->AddCircle(p, 8.0f, picked ? Col(255, 120, 130) : look.colour, 0, picked ? 3.0f : 2.0f);
        d->AddText(ImVec2(p.x - 3.5f, p.y - 7), look.colour, look.letter);
    }
    // the room the Extend tool would add, and the edge it grows from
    if (a.tool == Tool::ExtendRoad && !a.ghost.empty()) {
        std::vector<ImVec2> pts;
        for (const WallPoint& w : a.ghost) pts.push_back(ToScreen(v, origin, size, w.x, w.y));
        d->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), Col(70, 210, 140, 70));
        for (std::size_t k = 0; k < pts.size(); ++k) d->AddLine(pts[k], pts[(k + 1) % pts.size()], Col(110, 235, 165), 2.0f);
        if (a.ghostRoom >= 0 && a.ghostRoom < static_cast<int>(ed.model.rooms.size()) && a.ghost.size() >= 4) {
            const WallPoint& p = a.ghost[0]; const WallPoint& q = a.ghost[3];   // the shared edge
            d->AddLine(ToScreen(v, origin, size, p.x, p.y), ToScreen(v, origin, size, q.x, q.y), Col(255, 230, 120), 4.0f);
        }
    }
    d->PopClipRect();
}

void RunCheck(App& a)
{
    a.checkOutput.clear();
#ifdef __EMSCRIPTEN__
    a.checkOutput = "Compile and check is not in the browser editor yet (the compiler runs as its own module there; coming next). Save the track and use the desktop editor to check it.";
#else
    if (a.ed.dirty || a.ed.path.empty()) { a.checkOutput = "Save the track first (File > Save), then check it."; return; }
    const std::string out = a.ed.path + ".check.trk";
    auto run = [&](const std::string& command) {
        std::string text;
        FILE* pipe = popen((command + " 2>&1").c_str(), "r");
        if (!pipe) return std::string("could not start ") + command;
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe)) text += buffer;
        const int code = pclose(pipe);
        return text + (code == 0 ? "" : "\n(exit status " + std::to_string(code) + ")");
    };
    const std::string q = "\"";
    const std::string compile = run(q + a.toolsDir + "HoverNetMazeCompiler" + q + " " + q + out + q + " " + q + a.ed.path + q);
    std::string text = "Compiler:\n";
    // the compiler prints a lot of resource noise; keep the lines that matter
    std::string line;
    for (char c : compile) { if (c == '\n') { if (line.find("ERROR") != std::string::npos || line.find("WARNING") != std::string::npos || line.find("exit status") != std::string::npos) text += "  " + line + "\n"; line.clear(); } else line += c; }
    FILE* f = std::fopen(out.c_str(), "rb");
    if (!f) { a.checkOutput = text + "  no track was made: the compiler refused it.\n"; return; }
    std::fclose(f);
    text += "  compiled.\n\nValidator (strict):\n  ";
    text += run(q + a.toolsDir + "HoverNetTrackValidator" + q + " " + q + out + q + " check");
    std::remove(out.c_str());
    a.checkOutput = text;
    if (!a.ed.problems.empty()) {   // the compiler's "cannot load" says little: the editor can say what it sees
        a.checkOutput += "\nThe editor also sees:\n";
        for (std::size_t i = 0; i < a.ed.problems.size() && i < 12; ++i) a.checkOutput += "  - " + a.ed.problems[i] + "\n";
        if (a.ed.problems.size() > 12) a.checkOutput += "  ... and " + std::to_string(a.ed.problems.size() - 12) + " more\n";
    }
#endif
    a.openCheck = true;
}

// Save: on a computer, to the file; in the browser, to a download of that name (the browser decides where it goes).
void DoSave(App& a)
{
#ifdef __EMSCRIPTEN__
    std::string name = a.ed.path.empty() ? "MyTrack.track.txt" : a.ed.path;
    const std::size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (a.ed.SaveAs("/work.track.txt")) { hn_download("/work.track.txt", name.c_str()); a.ed.path = name; a.ed.status = "Saved as a download: " + name; hn_autosave_clear(); gLastAutosaved = a.ed.Text(); }
#else
    if (!a.ed.path.empty()) a.ed.Save();
    else { std::snprintf(a.fileBuffer, sizeof(a.fileBuffer), "MyTrack.track.txt"); a.openSaveAs = true; }
#endif
}

void Menus(App& a)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New loop...")) a.openNew = true;
#ifdef __EMSCRIPTEN__
            if (ImGui::MenuItem("Open from your computer...")) hn_pick_file();
            if (ImGui::MenuItem("Save to your computer", "Ctrl+S")) DoSave(a);
#else
            if (ImGui::MenuItem("Open...")) { std::snprintf(a.fileBuffer, sizeof(a.fileBuffer), "%s", a.ed.path.c_str()); a.openOpen = true; }
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !a.ed.path.empty())) a.ed.Save();
            if (ImGui::MenuItem("Save as...")) { std::snprintf(a.fileBuffer, sizeof(a.fileBuffer), "%s", a.ed.path.empty() ? "MyTrack.track.txt" : a.ed.path.c_str()); a.openSaveAs = true; }
#endif
            ImGui::Separator();
#ifndef __EMSCRIPTEN__
            if (ImGui::MenuItem("Quit")) { if (a.ed.dirty) a.openQuit = true; else a.quit = true; }
#endif
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, a.ed.CanUndo())) a.ed.Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, a.ed.CanRedo())) a.ed.Redo();
            if (ImGui::MenuItem("Delete", "Delete")) a.ed.DeleteSelected();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit the whole track", "F")) a.fitPending = true;
            ImGui::MenuItem("Grid", nullptr, &a.showGrid);
            ImGui::EndMenu();
        }
        const bool trackMenuOpen = ImGui::BeginMenu("Track");
        { const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax(); gMenuTrack = ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); }
        if (trackMenuOpen) {
            if (ImGui::MenuItem("Compile and check...")) RunCheck(a);
            if (ImGui::MenuItem("Publish to the community...")) a.openPublish = true;
            { const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax(); gMenuPublish = ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void Inspector(App& a)
{
    EditorSession& ed = a.ed;
    ImGui::PushTextWrapPos(0.0f);        // hints wrap inside the panel instead of running off its edge
    ImGui::PushItemWidth(170.0f);        // and a slider leaves room for its label
    Heading("Tool");
    int tool = static_cast<int>(a.tool);
    ImGui::RadioButton("Select / move", &tool, 0);
    ImGui::RadioButton("Place an element", &tool, 1);
    ImGui::RadioButton("Place a start", &tool, 2);
    ImGui::RadioButton("Extend the road", &tool, 3);
    a.tool = static_cast<Tool>(tool);
    if (a.tool == Tool::ExtendRoad) {
        ImGui::SliderFloat("Length (m)", &a.extendLength, 12.0f, 120.0f, "%.0f");
        ImGui::SliderFloat("Turn (degrees)", &a.extendTurn, -90.0f, 90.0f, "%.0f");
        ImGui::TextDisabled("Point at the open end of a room: the new room (green) is joined to it. Left turns are positive.");
        if (!a.ghostWhy.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", a.ghostWhy.c_str());
    }
    if (a.tool == Tool::PlaceElement) {
        if (ImGui::BeginCombo("##placetype", ElementName(a.placeType).c_str())) {
            for (std::size_t i = 0; i < kTrackElementCatalogueSize; ++i) {
                const TrackElementInfo& info = kTrackElementCatalogue[i];
                if (info.kind != TrackElementKind::PlacedElement) continue;
                if (ImGui::Selectable(info.name, a.placeType == info.id)) a.placeType = info.id;
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Click on the road to place it.");
    }
    else if (a.tool == Tool::PlaceStart) ImGui::TextDisabled("Click on the road to place a start.");
    ImGui::Separator();

    const Selection s = ed.selection;
    auto surfaceCombo = [&](const char* label, SurfaceSlot slot, int current) {
        const TrackElementInfo* now = FindTrackElement(static_cast<unsigned short>(current));
        if (ImGui::BeginCombo(label, now ? now->name : "?")) {
            for (std::size_t i = 0; i < kTrackElementCatalogueSize; ++i) {
                const TrackElementInfo& info = kTrackElementCatalogue[i];
                if (info.kind != TrackElementKind::Surface) continue;
                if (ImGui::Selectable(info.name, info.id == current)) ed.SetSurface(slot, info.id);
            }
            ImGui::EndCombo();
        }
    };
    if ((s.kind == Selection::Room || s.kind == Selection::Corner) && s.index >= 0 && s.index < static_cast<int>(ed.model.rooms.size())) {
        const TrackRoom& r = ed.model.rooms[static_cast<std::size_t>(s.index)];
        Heading((std::string(r.isFeature ? "Feature " : "Room ") + std::to_string(r.id)).c_str());
        if (r.isFeature) ImGui::Text("in room %d", r.parent);
        float floorH = static_cast<float>(r.floor.height), ceilH = static_cast<float>(r.ceiling.height);
        if (ImGui::InputFloat("Floor height (m)", &floorH, 0.5f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) ed.SetHeight(SurfaceSlot::Floor, floorH);
        if (ImGui::InputFloat("Ceiling height (m)", &ceilH, 0.5f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) ed.SetHeight(SurfaceSlot::Ceiling, ceilH);
        surfaceCombo("Floor", SurfaceSlot::Floor, r.floor.surface);
        surfaceCombo("Ceiling", SurfaceSlot::Ceiling, r.ceiling.surface);
        surfaceCombo("All walls", SurfaceSlot::Wall, r.walls.empty() ? 52 : r.walls[0].surface);
        ImGui::TextDisabled("Drag a yellow corner to reshape; rooms that share it follow.");
        if (QuietButton("Delete this room")) ed.DeleteSelected();
    }
    else if (s.kind == Selection::Start && s.index >= 0 && s.index < static_cast<int>(ed.model.starts.size())) {
        const TrackStart& st = ed.model.starts[static_cast<std::size_t>(s.index)];
        Heading((std::string("Start, team ") + std::to_string(st.team)).c_str());
        ImGui::Text("in room %d at %.1f, %.1f", st.section, st.x, st.y);
        float o = static_cast<float>(st.orientation);
        if (ImGui::InputFloat("Facing (degrees)", &o, 5.0f, 45.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) ed.SetOrientationOfSelected(o);
        ImGui::TextDisabled("0 is along +x; turning is anticlockwise.");
        if (QuietButton("Delete this start")) ed.DeleteSelected();
    }
    else if (s.kind == Selection::Element && s.index >= 0 && s.index < static_cast<int>(ed.model.freeElements.size())) {
        const TrackFreeElement& e = ed.model.freeElements[static_cast<std::size_t>(s.index)];
        Heading(ElementName(e.elementType).c_str());
        ImGui::Text("in room %d at %.1f, %.1f", e.section, e.x, e.y);
        float o = static_cast<float>(e.orientation);
        if (ImGui::InputFloat("Facing (degrees)", &o, 5.0f, 45.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) ed.SetOrientationOfSelected(o);
        if (QuietButton("Delete this element")) ed.DeleteSelected();
    }
    else {
        Heading("This track");
        ImGui::Text("%d rooms, %d features", ed.model.CountRooms(), ed.model.CountFeatures());
        ImGui::Text("%zu starts, %zu elements", ed.model.starts.size(), ed.model.freeElements.size());
        ImGui::Spacing();
        ImGui::TextWrapped("Click a room, a start or an element to change it. Drag starts and elements to move them. Right-drag or hold Space to move the view; the wheel zooms.");
    }
    ImGui::PopItemWidth();
    ImGui::PopTextWrapPos();
}

void Dialogs(App& a)
{
    if (a.openNew) { ImGui::OpenPopup("New loop"); a.openNew = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(620, std::max(200.0f, ImGui::GetIO().DisplaySize.y - 40.0f)));
    if (ImGui::BeginPopupModal("New loop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", a.newName, sizeof(a.newName));
        ImGui::RadioButton("A loop, ready to race", &a.newTemplate, 0);
        ImGui::RadioButton("A start room: I will build the road myself", &a.newTemplate, 1);
        if (a.newTemplate == 1) ImGui::TextDisabled("One room with the eight starts and the finish line. Extend the road from its open end.");
        ImGui::BeginDisabled(a.newTemplate == 1);
        ImGui::SliderInt("Rooms", &a.newOptions.rooms, 8, 96);
        float radius = static_cast<float>(a.newOptions.radius), width = static_cast<float>(a.newOptions.halfWidth * 2.0);
        if (ImGui::SliderFloat("Radius (m)", &radius, 40.0f, 600.0f)) a.newOptions.radius = radius;
        if (ImGui::SliderFloat("Road width (m)", &width, 12.0f, 80.0f)) a.newOptions.halfWidth = width / 2.0;
        ImGui::Checkbox("With pickups, fuel and a speed pad", &a.newOptions.pickups);
        ImGui::EndDisabled();
        ImGui::TextDisabled(a.newTemplate == 0 ? "Makes a complete track: eight starts, the finish line and both checkpoints." : "It plays as free play until you place both checkpoints.");
        if (PrimaryButton("Make it")) {
            a.newOptions.name = a.newName;
            if (a.newTemplate == 1 ? a.ed.NewStartRoom(a.newName) : a.ed.NewLoop(a.newOptions)) { a.fitPending = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (QuietButton("Cancel")) ImGui::CloseCurrentPopup();
        if (!a.ed.status.empty()) ImGui::TextWrapped("%s", a.ed.status.c_str());
        ImGui::EndPopup();
    }
    auto fileDialog = [&](const char* title, bool& flag, bool save) {
        if (flag) { ImGui::OpenPopup(title); flag = false; }
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(420);
            const bool enter = ImGui::InputText("File", a.fileBuffer, sizeof(a.fileBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            if (PrimaryButton(save ? "Save" : "Open") || enter) {
                if (save ? a.ed.SaveAs(a.fileBuffer) : a.ed.Open(a.fileBuffer)) { if (!save) a.fitPending = true; ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (QuietButton("Cancel")) ImGui::CloseCurrentPopup();
            if (!a.ed.status.empty()) ImGui::TextWrapped("%s", a.ed.status.c_str());
            ImGui::EndPopup();
        }
    };
    fileDialog("Open", a.openOpen, false);
    fileDialog("Save as", a.openSaveAs, true);
    if (a.openQuit) { ImGui::OpenPopup("Unsaved changes"); a.openQuit = false; }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The track has changes that are not saved.");
        if (QuietButton("Leave without saving")) a.quit = true;
        ImGui::SameLine();
        if (PrimaryButton("Keep editing")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (a.openPublish) {
        ImGui::OpenPopup("Publish");
        a.openPublish = false;
#ifdef __EMSCRIPTEN__
        gPublish.infoLoaded = false; gPublish.message.clear(); gPublish.accepted = false;
        if (gPublish.name[0] == '\0') {   // a name to start from: the file's, without its ending
            std::string base = a.ed.path.empty() ? std::string("") : a.ed.path;
            const std::size_t slash = base.find_last_of('/'); if (slash != std::string::npos) base = base.substr(slash + 1);
            for (const char* ending : {".track.txt", ".txt"}) { const std::string e = ending; if (base.size() > e.size() && base.compare(base.size() - e.size(), e.size(), e) == 0) { base.resize(base.size() - e.size()); break; } }
            std::snprintf(gPublish.name, sizeof(gPublish.name), "%s", base.c_str());
        }
        hn_fetch_info();
#endif
    }
    gPublishVisible = false;
    // centred, and never taller than the window (a small browser window scrolls the dialog instead of losing its buttons off the bottom)
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(620, std::max(200.0f, ImGui::GetIO().DisplaySize.y - 40.0f)));
    if (ImGui::BeginPopupModal("Publish", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        gPublishVisible = true;
#ifdef __EMSCRIPTEN__
        PublishState& p = gPublish;
        ImGui::PushTextWrapPos(560.0f);
        if (!p.infoLoaded) ImGui::TextUnformatted("Asking the Track Market...");
        else if (!p.reachable) ImGui::TextUnformatted("The Track Market cannot be reached from here. Try again later; your track is safe in this window, and File > Save to your computer keeps a copy.");
        else if (!p.signedIn) {
            ImGui::TextUnformatted("Sign in with your Outiva account to publish. It opens in a new tab; come back here afterwards and press Check again. Your work stays in this tab.");
            if (PrimaryButton("Sign in (new tab)")) hn_open_signin();
            ImGui::SameLine();
            if (QuietButton("Check again")) { p.infoLoaded = false; hn_fetch_info(); }
        }
        else if (!p.invited) ImGui::TextUnformatted("You are signed in, but publishing is in a private beta for invited authors for now. Anyone can already download community tracks. Ask the person who runs HoverNet to invite you.");
        else {
            const char* licences[] = {"CC-BY-4.0", "CC0-1.0", "CC-BY-SA-4.0"};
            const char* explain[] = {"Anyone may use and share it if they credit you (the game shows your name next to the track).", "No conditions at all: anyone may do anything with it.", "As CC BY, and changed versions must be shared the same way."};
            ImGui::InputText("Track name", p.name, sizeof(p.name));
            ImGui::InputText("Your name (shown as the author)", p.author, sizeof(p.author));
            ImGui::InputText("Short description", p.description, sizeof(p.description));
            ImGui::Combo("Licence", &p.licence, licences, 3);
            ImGui::TextDisabled("%s", explain[p.licence]);
            ImGui::Spacing();
            ImGui::TextUnformatted(p.declarationText.c_str());
            ImGui::Checkbox("I agree to this", &p.accepted);
            ImGui::Spacing();
            if (!a.ed.problems.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "The editor sees %zu thing(s) to look at first (see the list below the plan).", a.ed.problems.size());
            ImGui::BeginDisabled(p.working || !p.accepted || p.name[0] == '\0' || p.author[0] == '\0');
            if (PrimaryButton(p.working ? "Sending... (a minute at most)" : "Publish")) {
                p.working = true; p.message.clear();
                hn_publish_js(a.ed.Text().c_str(), p.name, p.author, licences[p.licence], p.declaration.c_str(), p.description);
            }
            ImGui::EndDisabled();
        }
        if (!p.message.empty()) { ImGui::Spacing(); ImGui::TextUnformatted(p.message.c_str()); }
        ImGui::PopTextWrapPos();
#else
        ImGui::TextWrapped("Publishing is done from the browser editor at https://hovernet.outiva.com/editor/ : you sign in there with your Outiva account. Save this track (File > Save) and open it in the browser editor with File > Open from your computer.");
#endif
        if (QuietButton("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#ifdef __EMSCRIPTEN__
    if (gRestoreAsked) { ImGui::OpenPopup("Restore your work"); gRestoreAsked = false; }
    gRestoreVisible = false;
    if (ImGui::BeginPopupModal("Restore your work", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        gRestoreVisible = true;
        ImGui::PushTextWrapPos(520.0f);
        ImGui::TextUnformatted("The editor kept a copy of a track you were working on in this browser, and it was not saved to a file. Do you want it back?");
        ImGui::PopTextWrapPos();
        const bool restoreClicked = PrimaryButton("Restore my work");
        { const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax(); gRestoreBtn = ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); }
        if (restoreClicked) {
            if (a.ed.OpenText(gRestoreText, "Restored your unsaved work from this browser: save it to a file to keep it.")) { a.fitPending = true; gLastAutosaved = a.ed.Text(); }
            gRestoreText.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const bool freshClicked = QuietButton("Start fresh");
        { const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax(); gFreshBtn = ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2); }
        if (freshClicked) { hn_autosave_clear(); gRestoreText.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
#endif
    if (a.openCheck) { ImGui::OpenPopup("Compile and check"); a.openCheck = false; }
    if (ImGui::BeginPopupModal("Compile and check", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(a.checkOutput.c_str());
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Frame(App& a, int width, int height)
{
    ImGuiIO& io = ImGui::GetIO();
    Menus(a);
    const float menuH = ImGui::GetFrameHeight();
    const float sideW = 340.0f, bottomH = 150.0f;
    const ImVec2 canvasOrigin(0, menuH), canvasSize(width - sideW, height - menuH - bottomH);

    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    // the plan
    ImGui::SetNextWindowPos(canvasOrigin);
    ImGui::SetNextWindowSize(canvasSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Plan", nullptr, fixed | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    gCanvasOrigin = canvasOrigin; gCanvasSize = canvasSize;
    if (a.fitPending) { Fit(a, canvasSize); a.fitPending = false; }
    DrawPlan(a, canvasOrigin, canvasSize);
    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("plan-input", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    double wx = 0, wy = 0;
    ToPlan(a.view, canvasOrigin, canvasSize, mouse, wx, wy);
    const double hit = 9.0 / a.view.scale;   // a pointer within nine pixels counts as on it
    // zoom about the pointer
    if (hovered && io.MouseWheel != 0.0f) {
        const double before = a.view.scale;
        a.view.scale = std::max(0.2, std::min(60.0, a.view.scale * std::pow(1.15, io.MouseWheel)));
        a.view.cx = wx - (mouse.x - canvasOrigin.x - canvasSize.x / 2) / a.view.scale;
        a.view.cy = wy + (mouse.y - canvasOrigin.y - canvasSize.y / 2) / a.view.scale;
        (void)before;
    }
    // pan: right or middle button, or Space with the left
    const bool wantPan = hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)));
    if (wantPan) { a.panning = true; a.panStart = mouse; a.panView = a.view; }
    if (a.panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            a.view.cx = a.panView.cx - (mouse.x - a.panStart.x) / a.view.scale;
            a.view.cy = a.panView.cy + (mouse.y - a.panStart.y) / a.view.scale;
        } else a.panning = false;
    }
    else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (a.tool == Tool::ExtendRoad) {
            if (a.ed.ExtendRoadAt(wx, wy, hit * 1.6, a.extendLength, a.extendTurn)) {
                // keep building: if the new room runs off the edge of the plan, bring the whole road back into view
                const TrackRoom& added = a.ed.model.rooms[static_cast<std::size_t>(a.ed.selection.index)];
                for (const WallPoint& w : added.walls) {
                    const ImVec2 p = ToScreen(a.view, canvasOrigin, canvasSize, w.x, w.y);
                    if (p.x < canvasOrigin.x + 30 || p.y < canvasOrigin.y + 30 || p.x > canvasOrigin.x + canvasSize.x - 30 || p.y > canvasOrigin.y + canvasSize.y - 30) a.fitPending = true;
                }
            }
        }
        else if (a.tool == Tool::Select) a.ed.BeginDrag(wx, wy, hit);
        else if (a.tool == Tool::PlaceElement) a.ed.PlaceElement(a.placeType, wx, wy);
        else if (a.tool == Tool::PlaceStart) a.ed.PlaceStart(wx, wy);
    }
    // the ghost of the room the Extend tool would add under the pointer
    a.ghost.clear(); a.ghostWhy.clear(); a.ghostRoom = a.ghostWall = -1;
    if (a.tool == Tool::ExtendRoad && (hovered || a.forcePointer)) {
        const double px = a.forcePointer ? a.forceX : wx, py = a.forcePointer ? a.forceY : wy;
        a.ed.PreviewExtend(px, py, hit * 1.6, a.extendLength, a.extendTurn, a.ghost, a.ghostRoom, a.ghostWall, a.ghostWhy);
    }
    if (a.ed.Dragging()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) a.ed.Drag(wx, wy);
        else a.ed.EndDrag();
    }
    ImGui::End();

    // the inspector
    ImGui::SetNextWindowPos(ImVec2(width - sideW, menuH));
    ImGui::SetNextWindowSize(ImVec2(sideW, height - menuH - bottomH));
    ImGui::Begin("Properties", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
    Inspector(a);
    ImGui::End();

    // problems and status
    ImGui::SetNextWindowPos(ImVec2(0, height - bottomH));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), bottomH));
    ImGui::Begin("Problems", nullptr, fixed | ImGuiWindowFlags_NoTitleBar);
    Heading("Problems");
    ImGui::TextColored(a.ed.problems.empty() ? ImVec4(0.5f, 0.9f, 0.6f, 1) : ImVec4(1.0f, 0.8f, 0.4f, 1), "%s",
                       a.ed.problems.empty() ? "No problems the editor can see." : (std::to_string(a.ed.problems.size()) + " thing(s) to look at:").c_str());
    for (std::size_t i = 0; i < a.ed.problems.size() && i < 6; ++i) ImGui::BulletText("%s", a.ed.problems[i].c_str());
    if (a.ed.problems.size() > 6) ImGui::TextDisabled("... and %zu more", a.ed.problems.size() - 6);
    ImGui::Separator();
    ImGui::TextDisabled("%s%s    %s", a.ed.path.empty() ? "(not saved yet)" : a.ed.path.c_str(), a.ed.dirty ? "  *unsaved*" : "", a.ed.status.c_str());
    ImGui::End();

    Dialogs(a);

    // keys (not while typing in a box)
    if (!io.WantTextInput) {
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) a.ed.Redo(); else a.ed.Undo(); }
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) a.ed.Redo();
        else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) DoSave(a);
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) a.ed.DeleteSelected();
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { a.tool = Tool::Select; a.ed.Select(Selection()); }
        else if (ImGui::IsKeyPressed(ImGuiKey_F)) a.fitPending = true;
    }
}

bool SaveCapture(SDL_Renderer* renderer, const std::string& file)
{
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(renderer, &w, &h) != 0) return false;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    const bool ok = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0 && SDL_SaveBMP(surface, file.c_str()) == 0;
    SDL_FreeSurface(surface);
    return ok;
}
App* gApp = nullptr;
}  // namespace

#ifdef __EMSCRIPTEN__
// What the browser test asks the editor, to prove that an upload or a click changed the real track (not just the picture).
extern "C" {
EMSCRIPTEN_KEEPALIVE int hn_rooms() { return gApp ? gApp->ed.model.CountRooms() : -1; }
EMSCRIPTEN_KEEPALIVE int hn_elements() { return gApp ? static_cast<int>(gApp->ed.model.freeElements.size()) : -1; }
EMSCRIPTEN_KEEPALIVE void hn_publish_test(const char* name, const char* author, const char* licence, const char* declaration, const char* description)
{ if (gApp) { gPublish.working = true; hn_publish_js(gApp->ed.Text().c_str(), name, author, licence, declaration, description); } }
EMSCRIPTEN_KEEPALIVE int hn_publish_http() { return gPublish.lastHttp; }
EMSCRIPTEN_KEEPALIVE int hn_publish_working() { return gPublish.working ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int hn_menu_track_x() { return static_cast<int>(gMenuTrack.x); }
EMSCRIPTEN_KEEPALIVE int hn_menu_track_y() { return static_cast<int>(gMenuTrack.y); }
EMSCRIPTEN_KEEPALIVE int hn_menu_publish_x() { return static_cast<int>(gMenuPublish.x); }
EMSCRIPTEN_KEEPALIVE int hn_menu_publish_y() { return static_cast<int>(gMenuPublish.y); }
EMSCRIPTEN_KEEPALIVE int hn_publish_visible() { return gPublishVisible ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void hn_test_new_start_room() { if (gApp) { gApp->ed.NewStartRoom("Road test"); gApp->fitPending = true; gApp->tool = Tool::Select; } }
EMSCRIPTEN_KEEPALIVE void hn_test_tool_extend() { if (gApp) { gApp->tool = Tool::ExtendRoad; gApp->extendLength = 30.0f; gApp->extendTurn = 0.0f; } }
// where the open end of the last room is on screen (the middle of its far edge), so a test can click it with a real mouse
EMSCRIPTEN_KEEPALIVE int hn_open_end_px() { if (!gApp || gApp->ed.model.rooms.empty()) return -1; const TrackRoom& r = gApp->ed.model.rooms.back(); return static_cast<int>(ToScreen(gApp->view, gCanvasOrigin, gCanvasSize, (r.walls[1].x + r.walls[2].x) / 2, (r.walls[1].y + r.walls[2].y) / 2).x); }
EMSCRIPTEN_KEEPALIVE int hn_open_end_py() { if (!gApp || gApp->ed.model.rooms.empty()) return -1; const TrackRoom& r = gApp->ed.model.rooms.back(); return static_cast<int>(ToScreen(gApp->view, gCanvasOrigin, gCanvasSize, (r.walls[1].x + r.walls[2].x) / 2, (r.walls[1].y + r.walls[2].y) / 2).y); }
EMSCRIPTEN_KEEPALIVE int hn_all_rooms() { return gApp ? static_cast<int>(gApp->ed.model.rooms.size()) : -1; }
EMSCRIPTEN_KEEPALIVE int hn_restore_visible() { return gRestoreVisible ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int hn_restore_btn_x() { return static_cast<int>(gRestoreBtn.x); }
EMSCRIPTEN_KEEPALIVE int hn_restore_btn_y() { return static_cast<int>(gRestoreBtn.y); }
EMSCRIPTEN_KEEPALIVE int hn_fresh_btn_x() { return static_cast<int>(gFreshBtn.x); }
EMSCRIPTEN_KEEPALIVE int hn_fresh_btn_y() { return static_cast<int>(gFreshBtn.y); }
EMSCRIPTEN_KEEPALIVE int hn_dirty() { return gApp && gApp->ed.dirty ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int hn_problems() { return gApp ? static_cast<int>(gApp->ed.problems.size()) : -1; }
EMSCRIPTEN_KEEPALIVE int hn_element_px(int i) { return i >= 0 && i < static_cast<int>(gElementScreen.size()) ? static_cast<int>(gElementScreen[static_cast<std::size_t>(i)].x) : -1; }
EMSCRIPTEN_KEEPALIVE int hn_element_py(int i) { return i >= 0 && i < static_cast<int>(gElementScreen.size()) ? static_cast<int>(gElementScreen[static_cast<std::size_t>(i)].y) : -1; }
EMSCRIPTEN_KEEPALIVE int hn_element_section(int i) { return gApp && i >= 0 && i < static_cast<int>(gApp->ed.model.freeElements.size()) ? gApp->ed.model.freeElements[static_cast<std::size_t>(i)].section : -1; }
}
#endif

int main(int argc, char** argv)
{
    App app;
    gApp = &app;
    int width = 1280, height = 800, frames = -1;
    std::string capture, openFile;
    bool newLoop = false, checkOnly = false, demoRoad = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--new-loop") newLoop = true;
        else if (arg == "--check") checkOnly = true;
        else if (arg == "--demo-road") demoRoad = true;
        else if (arg == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (arg == "--capture" && i + 1 < argc) capture = argv[++i];
        else if (arg == "--tools" && i + 1 < argc) { app.toolsDir = argv[++i]; if (!app.toolsDir.empty() && app.toolsDir.back() != '/') app.toolsDir += '/'; }
        else if (arg == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &width, &height);
        else if (arg.rfind("--", 0) != 0) openFile = arg;
        else { std::fprintf(stderr, "unknown option %s\nusage: %s [FILE.track.txt] [--new-loop] [--size WxH] [--tools DIR] [--frames N] [--capture OUT.bmp]\n", arg.c_str(), argv[0]); return 2; }
    }
    if (checkOnly) {
        // no window: the same compile-and-check as the menu, for scripts and tests
        if (openFile.empty()) { std::fprintf(stderr, "--check needs a track file\n"); return 2; }
        if (SDL_Init(0) != 0) return 1;
        if (app.toolsDir.empty()) { char* base = SDL_GetBasePath(); if (base) { app.toolsDir = base; SDL_free(base); } }
        if (!app.ed.Open(openFile)) { std::fprintf(stderr, "%s\n", app.ed.status.c_str()); return 1; }
        RunCheck(app);
        std::printf("%s\n", app.checkOutput.c_str());
        return app.checkOutput.find("VALID ") != std::string::npos && app.checkOutput.find("INVALID") == std::string::npos ? 0 : 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    if (app.toolsDir.empty()) {
        char* base = SDL_GetBasePath();
        if (base) { app.toolsDir = base; SDL_free(base); }
    }
    SDL_Window* window = SDL_CreateWindow("HoverNet Track Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
    if (!window) { std::fprintf(stderr, "window failed: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, frames >= 0 ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) { std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError()); return 1; }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ApplyTheme(io);
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    if (demoRoad) {
        app.ed.NewStartRoom("Demo road");
        const double turns[] = {0, 22, 22, -25, -25, 20};
        int from = 0;
        for (double t : turns) { std::string why; from = ExtendRoad(app.ed.model, from, 1, 32.0, t, why); if (from < 0) break; }
        app.ed.Refresh();
        app.tool = Tool::ExtendRoad; app.extendLength = 32.0f; app.extendTurn = 20.0f;
        if (from >= 0) {   // the pointer rests on the open end of the last room
            const TrackRoom& last = app.ed.model.rooms.back();
            app.forcePointer = true; app.forceX = (last.walls[1].x + last.walls[2].x) / 2; app.forceY = (last.walls[1].y + last.walls[2].y) / 2;
        }
    }
    else if (!openFile.empty()) app.ed.Open(openFile);
    else if (newLoop || true) { app.newOptions.name = "My New Track"; if (!app.ed.NewLoop(app.newOptions)) std::fprintf(stderr, "%s\n", app.ed.status.c_str()); }

    int drawn = 0;
    while (!app.quit) {
#ifdef __EMSCRIPTEN__
        if (gResizeW > 0 && gResizeH > 0) { SDL_SetWindowSize(window, gResizeW, gResizeH); gResizeW = gResizeH = 0; }
        if (gUploadReady) {
            gUploadReady = false;
            if (app.ed.Open("/upload.track.txt")) { app.ed.path = gUploadName; app.ed.dirty = false; app.fitPending = true; app.ed.status = "Opened " + gUploadName; }
        }
#endif
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) { if (app.ed.dirty && frames < 0) app.openQuit = true; else app.quit = true; }
        }
        SDL_GetWindowSize(window, &width, &height);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        Frame(app, width, height);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 14, 14, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
#ifdef __EMSCRIPTEN__
        {   // a moment after a change, keep a copy in the browser (not while the person is still deciding about an earlier copy)
            static Uint32 lastCheck = 0;
            const Uint32 now = SDL_GetTicks();
            if (now - lastCheck > 700 && gRestoreText.empty() && !gRestoreVisible) {
                lastCheck = now;
                if (app.ed.dirty && (app.ed.CanUndo() || app.ed.CanRedo())) {   // not a track nobody has touched
                    const std::string text = app.ed.Text();
                    if (text != gLastAutosaved) { hn_autosave_put(text.c_str()); gLastAutosaved = text; }
                }
            }
        }
#endif
        ++drawn;
        if (frames >= 0 && drawn >= frames) {
            if (!capture.empty() && !SaveCapture(renderer, capture)) std::fprintf(stderr, "could not save the picture\n");
            break;
        }
        SDL_RenderPresent(renderer);
#ifdef __EMSCRIPTEN__
        SDL_Delay(1);   // gives the browser its turn (the build uses ASYNCIFY, as the game does)
#endif
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
