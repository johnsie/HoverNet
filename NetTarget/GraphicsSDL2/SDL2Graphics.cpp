#include "SDL2Graphics.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// EM_JS compiles these into the page's script. emscripten_run_script*() would eval() a string,
// which the site's Content-Security-Policy forbids (it throws EvalError and stops startup).
EM_JS(int, hovernet_js_force_software, (), {
    try { return localStorage.getItem('hn-renderer') === 'software' ? 1 : 0; } catch (e) { return 0; }
});
#endif

#ifdef _HAVE_SDL2
#include <cstring>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#endif

// Only HoverNetGame2Player's CMake target defines this (from `git describe`, see
// the root CMakeLists.txt) -- the other SDL2Graphics-linking binaries (track
// viewers, the bootstrap smoke test) are internal dev tools, not the shipped
// client, so a plain fallback title is fine for them.
#ifndef HOVERNET_VERSION
#define HOVERNET_VERSION "dev"
#endif

SDL2GraphicsBackend::SDL2GraphicsBackend()
    : m_window(nullptr)
    , m_renderer(nullptr)
    , m_texture(nullptr)
    , m_paletteRGB(nullptr)
    , m_sdlPalette(nullptr)
    , m_width(0)
    , m_height(0)
    , m_initialized(false)
{
}

SDL2GraphicsBackend::~SDL2GraphicsBackend()
{
    Shutdown();
}

bool SDL2GraphicsBackend::Initialize(void* windowHandle, int width, int height)
{
    if (m_initialized) return false;
    m_width = width; m_height = height;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    { return false; }

    const std::string windowTitle = std::string("HoverNet ") + HOVERNET_VERSION;
#ifdef _WIN32
    // The legacy client embeds SDL in its existing HWND. The shared client has
    // no Win32 host window and must let SDL create the top-level window, just
    // as it does on Linux. Supporting both paths lets migration happen without
    // keeping separate render backends.
    HWND hwnd = static_cast<HWND>(windowHandle);
    if (hwnd != nullptr) {
        m_window = SDL_CreateWindowFrom(hwnd);
    } else {
        m_window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    }
#else
    (void)windowHandle;
    m_window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (!m_window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return false; }

    // Create renderer with VSYNC enabled to synchronize with monitor refresh rate
    // This prevents flickering by ensuring Present() waits for the next vertical blank
#ifdef __EMSCRIPTEN__
    // In the browser the software renderer rasterises every pixel on the CPU and copies the whole
    // canvas each frame, which is far too slow for a page-sized canvas. Use the WebGL renderer
    // (SDL converts the RGB24 game texture itself); fall back to software on a fresh window if
    // WebGL is unavailable. No vsync flag: the browser paces presents.
    // The page sets localStorage "hn-renderer" = "software" when WebGL failed to produce a frame
    // on this browser (see shell.html), so such browsers skip the accelerated renderer.
    const bool forceSoftware = hovernet_js_force_software() != 0;
    if (!forceSoftware) m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer && !forceSoftware) {
        SDL_DestroyWindow(m_window);
        m_window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!m_window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return false; }
    }
#endif
#ifndef __EMSCRIPTEN__
    {
        // The graphics card draws the menus and scales the picture when there is one (OpenGL): the picture passes and the finished picture then stay on the card
        // (see GpuFinisher.h). HOVERNET_RENDERER=software keeps the old processor-only renderer; the dummy video driver (tests) has no accelerated renderer and falls through.
        const char* wanted = std::getenv("HOVERNET_RENDERER");
        if (wanted == nullptr || std::strcmp(wanted, "software") != 0) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
            m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!m_renderer) m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        }
    }
#endif
    if (!m_renderer) m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) { 
        // Fallback: try without VSYNC
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
        if (!m_renderer) { 
            SDL_DestroyWindow(m_window); 
            SDL_QuitSubSystem(SDL_INIT_VIDEO); 
            return false; 
        }
    }
    
    SDL_RenderSetLogicalSize(m_renderer, width, height);
    m_presenter.UseWallClock(true);   // particles age by real time in the game
    {   // did the renderer get vsync (the display paces the loop)? The game loops then need no pause of their own.
        SDL_RendererInfo info;
        HoverNetGfx::SetVsyncActive(SDL_GetRendererInfo(m_renderer, &info) == 0 && (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0 && std::getenv("HOVERNET_NO_VSYNC_PACING") == nullptr);
    }
    // Use RGB24 (3 bytes per pixel, no padding) instead of RGB888 to ensure correct pitch handling
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!m_texture) { 
        SDL_DestroyRenderer(m_renderer); 
        SDL_DestroyWindow(m_window); 
        SDL_QuitSubSystem(SDL_INIT_VIDEO); 
        return false; 
    }
    m_rgbBuffer.resize(static_cast<size_t>(width) * height * 3);
    
    // Allocate palette buffer but don't initialize with any data - wait for SetPalette()
    // to provide the real game palette loaded from track files
    if (!m_paletteRGB) { 
        m_paletteRGB = new uint8_t[768];
        memset(m_paletteRGB, 0, 768);  // Initialize to zero - will be filled by SetPalette
    }
    
    // Don't call CreateSDLPalette yet - we don't have valid palette data
    // CreateSDLPalette will be called from SetPalette once real palette is available
    
    m_initialized = true;
    return true;
}

void SDL2GraphicsBackend::Shutdown()
{
    if (m_gpuTexture) { SDL_DestroyTexture(m_gpuTexture); m_gpuTexture = nullptr; }
    if (m_texture) { SDL_DestroyTexture(m_texture); m_texture = nullptr; }
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (m_paletteRGB) { delete[] m_paletteRGB; m_paletteRGB = nullptr; }
    if (m_sdlPalette) { delete[] m_sdlPalette; m_sdlPalette = nullptr; }
    m_rgbBuffer.clear();
    m_initialized = false;
}

bool SDL2GraphicsBackend::AllocateBuffer(int width, int height, uint8_t*& outBuffer)
{ outBuffer = new uint8_t[width*height]; memset(outBuffer, 0, width*height); return true; }

void SDL2GraphicsBackend::FreeBuffer(uint8_t* buffer)
{ if (buffer) delete[] buffer; }

bool SDL2GraphicsBackend::SetPalette(const uint8_t* palette, int paletteSize)
{ 
    if (!palette || paletteSize < 768) {
        return false; 
    }
    
    memcpy(m_paletteRGB, palette, 768);
    m_shadowLutValid = false;
    m_gradedPaletteValid = false;
    return CreateSDLPalette(); 
}

bool SDL2GraphicsBackend::ConvertToRGBA(const uint8_t* buffer, int width, int height, uint8_t* out) const
{
    if (!m_paletteRGB || !buffer || !out) return false;
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* rgb = m_paletteRGB + buffer[i] * 3;
        out[i * 4 + 0] = rgb[0];
        out[i * 4 + 1] = rgb[1];
        out[i * 4 + 2] = rgb[2];
        out[i * 4 + 3] = 255;
    }
    return true;
}

const uint8_t* SDL2GraphicsBackend::GetRenderPalette()
{
    if (!m_paletteRGB) return nullptr;
    if (m_modernOptions.enabled && m_modernOptions.grade) {
        if (!m_gradedPaletteValid || m_gradedBrightness != m_modernOptions.brightness) {
            HoverNetGfx::Presenter::GradePalette(m_paletteRGB, m_gradedPalette, m_modernOptions.brightness);
            m_gradedBrightness = m_modernOptions.brightness;
            m_gradedPaletteValid = true;
        }
        return m_gradedPalette;
    }
    return m_paletteRGB;
}

const uint8_t* SDL2GraphicsBackend::GetShadowLut()
{
    if (!m_paletteRGB) return nullptr;
    if (!m_shadowLutValid) {
        HoverNetGfx::BuildShadowLut(m_paletteRGB, 87, m_shadowLut);   // each of the shadow's five rings darkens by this much, so the core is about half as bright
        m_shadowLutValid = true;
    }
    return m_shadowLut;
}

bool SDL2GraphicsBackend::Present(const uint8_t* buffer, int width, int height)
{
    if (!m_renderer || !m_texture || !m_paletteRGB || !buffer) return false;
    
    // Verify dimensions match texture
    if (width != m_width || height != m_height) {
        return false;
    }
    
    // Convert 8-bit indexed palette data to 24-bit RGB for rendering
    // CRITICAL: Use exact pitch (width * 3) with NO padding - SDL_PIXELFORMAT_RGB24 expects contiguous data
    int pitch = width * 3;  // RGB24 with exactly 3 bytes per pixel, no padding
    if (m_rgbBuffer.size() != static_cast<size_t>(pitch) * height) {
        m_rgbBuffer.resize(static_cast<size_t>(pitch) * height);
    }
    uint8_t* rgb_buffer = m_rgbBuffer.data();

    // The modern look's tone curve and extra colour are applied to the 256 palette colours here, not to every pixel afterwards.
    const uint8_t* palette = GetRenderPalette();

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t index = buffer[y * width + x];  // Linear stride = width
            
            // SAFETY: Check palette index is valid and clamp to valid range
            if (index >= 256) {
                index = 0;  // Default to black for invalid indices
            }
            
            // EXTRA FIX: Detect isolated corruption artifacts (single pixels surrounded by different colors)
            // These are typically from ObjFac1 bitmap rendering buffer overruns
            // Pattern: if a pixel is very different from neighbors AND appears sporadically, it's likely an artifact
            bool is_isolated_artifact = false;
            if (x > 0 && x < width-1 && y > 0 && y < height-1) {
                int left = buffer[y * width + (x-1)];
                int right = buffer[y * width + (x+1)];
                int up = buffer[y * width - width + x];
                int down = buffer[y * width + width + x];
                
                // If all 4 neighbors are the same color but this pixel is very different, it's likely an artifact
                if (left == right && up == down && left == up && left != 0 && left < 256) {
                    int diff_left = (index >= left) ? (index - left) : (left - index);
                    int diff_right = (index >= right) ? (index - right) : (right - index);
                    int diff_up = (index >= up) ? (index - up) : (up - index);
                    int diff_down = (index >= down) ? (index - down) : (down - index);
                    
                    // If differences are extreme (> 40 palette entries away), likely corruption
                    if (diff_left > 40 && diff_right > 40 && diff_up > 40 && diff_down > 40) {
                        is_isolated_artifact = true;
                    }
                }
            }
            
            // Replace artifact with neighbor color to smooth out corruption
            if (is_isolated_artifact) {
                if (x > 0) index = buffer[y * width + (x-1)];  // Use left neighbor
            }
            
            int offset = y * pitch + x * 3;
            rgb_buffer[offset + 0] = palette[index*3 + 0];  // R
            rgb_buffer[offset + 1] = palette[index*3 + 1];  // G
            rgb_buffer[offset + 2] = palette[index*3 + 2];  // B
        }
    }
    
    {   // how long whole frames take, for the adaptive quality
        const auto now = std::chrono::steady_clock::now();
        double workMs = 0.0;
        if (HoverNetGfx::MillisecondsSinceFrameStart(workMs)) m_presenter.NoteWorkMs(workMs);   // the loop says where its frame began: judge the work, not the pause and the display's wait
        else if (m_havePresentTime) m_presenter.NoteFrameInterval(std::chrono::duration<double, std::milli>(now - m_lastPresentTime).count());
        m_lastPresentTime = now;
        m_havePresentTime = true;
    }
    if (m_modernOptions.enabled) {
        HoverNetGfx::Presenter::ApplyTrueColour(rgb_buffer, buffer, width, height, m_frameHints);
        if (!m_gpuTried) {
            // The graphics card does the picture passes when it can (a hidden OpenGL context of its own: the renderer and the menus are not touched).
            m_gpuTried = true;
            const char* choice = std::getenv("HOVERNET_GPU");
            if (choice == nullptr || std::strcmp(choice, "0") != 0) {
                m_gpu.reset(new HoverNetGfx::GpuFinisher);
                SDL_RendererInfo rendererInfo;
                const bool glRenderer = SDL_GetRendererInfo(m_renderer, &rendererInfo) == 0 && rendererInfo.name != nullptr && std::strcmp(rendererInfo.name, "opengl") == 0;
                if (glRenderer) SDL_RenderFlush(m_renderer);   // the renderer's context is the current one now: the finisher's shares with it
                if (m_gpu->Init()) {
                    if (glRenderer) {
                        m_gpuTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, width, height);
                        SDL_RenderFlush(m_renderer);
                        if (m_gpuTexture != nullptr && !m_gpu->AdoptTarget(m_gpuTexture, width, height)) { SDL_DestroyTexture(m_gpuTexture); m_gpuTexture = nullptr; }
                    }
                    HoverNetGfx::GpuFinisher* gpu = m_gpu.get();
                    m_presenter.SetFinisher([gpu](std::uint8_t* rgb, int w, int h, const HoverNetGfx::Options& o, const HoverNetGfx::FrameHints& hints, const std::uint8_t* mask) { return gpu->Process(rgb, w, h, o, hints, mask); });
                    std::printf("Picture passes on the graphics card: %s%s\n", m_gpu->Renderer().c_str(), m_gpuTexture != nullptr ? " (the picture stays on the card)" : "");
                }
                else std::printf("Picture passes on the processor (%s)\n", m_gpu->Error().c_str());
                std::fflush(stdout);
            }
        }
        if (m_gpu) m_gpu->SetPalette(m_paletteRGB);
        if (const char* sceneChoice = std::getenv("HOVERNET_GPU_SCENE")) m_gpuSceneAllowed = std::strcmp(sceneChoice, "0") != 0;
        HoverNetGfx::Options frameOptions = m_modernOptions;
        frameOptions.gpuFinish = m_gpuAllowed && m_presenter.FinisherActive();
        m_presenter.Process(rgb_buffer, width, height, frameOptions, m_frameHints);
    }

    const bool onCard = m_gpuTexture != nullptr && m_gpu && m_gpu->OutputIsShared() && m_modernOptions.enabled && m_gpuAllowed;
    if (!onCard) SDL_UpdateTexture(m_texture, nullptr, rgb_buffer, pitch);   // (with the picture finished on the card it is already in its texture)

    SDL_RenderClear(m_renderer);
    SDL_Rect r = {0, 0, width, height};
    SDL_RenderCopy(m_renderer, onCard ? m_gpuTexture : m_texture, nullptr, &r);
    if (m_overlay) m_overlay(m_renderer);
    SDL_RenderPresent(m_renderer);
    return true;
}

bool SDL2GraphicsBackend::Clear(uint8_t color)
{
    if (!m_renderer) return false;
    SDL_Color c = {0, 0, 0, 255};
    if (m_paletteRGB && color < 256) { c.r = m_paletteRGB[color*3]; c.g = m_paletteRGB[color*3+1]; c.b = m_paletteRGB[color*3+2]; }
    SDL_SetRenderDrawColor(m_renderer, c.r, c.g, c.b, 255);
    SDL_RenderClear(m_renderer);
    SDL_RenderPresent(m_renderer);
    return true;
}

bool SDL2GraphicsBackend::CreateSDLPalette()
{
    if (!m_paletteRGB) return false;
    if (m_sdlPalette) delete[] m_sdlPalette;
    m_sdlPalette = new SDL_Color[256];
    if (!m_sdlPalette) return false;
    for (int i = 0; i < 256; i++)
    { m_sdlPalette[i].r = m_paletteRGB[i*3]; m_sdlPalette[i].g = m_paletteRGB[i*3+1]; m_sdlPalette[i].b = m_paletteRGB[i*3+2]; m_sdlPalette[i].a = 255; }
    SDL_Palette* palette = SDL_AllocPalette(256);
    if (palette == nullptr) return false;
    const bool success = SDL_SetPaletteColors(palette, m_sdlPalette, 0, 256) == 0;
    SDL_FreePalette(palette);
    return success;
}

#else
SDL2GraphicsBackend::SDL2GraphicsBackend() : m_window(nullptr), m_renderer(nullptr), m_texture(nullptr), m_paletteRGB(nullptr), m_sdlPalette(nullptr), m_width(0), m_height(0), m_initialized(false) {}
SDL2GraphicsBackend::~SDL2GraphicsBackend() {}
bool SDL2GraphicsBackend::Initialize(void* windowHandle, int width, int height) { return false; }
void SDL2GraphicsBackend::Shutdown() {}
bool SDL2GraphicsBackend::AllocateBuffer(int width, int height, uint8_t*& outBuffer) { return false; }
void SDL2GraphicsBackend::FreeBuffer(uint8_t* buffer) {}
bool SDL2GraphicsBackend::SetPalette(const uint8_t* palette, int paletteSize) { return false; }
bool SDL2GraphicsBackend::Present(const uint8_t* buffer, int width, int height) { return false; }
bool SDL2GraphicsBackend::Clear(uint8_t color) { return false; }
bool SDL2GraphicsBackend::ConvertToRGBA(const uint8_t*, int, int, uint8_t*) const { return false; }
bool SDL2GraphicsBackend::CreateSDLPalette() { return false; }
#endif
