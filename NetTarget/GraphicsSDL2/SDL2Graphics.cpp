#include "SDL2Graphics.h"

#ifdef _HAVE_SDL2
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
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
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(windowHandle);
    if (!hwnd) return false;
#endif

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    { return false; }

#ifdef _WIN32
    m_window = SDL_CreateWindowFrom(hwnd);
#else
    (void)windowHandle;
    m_window = SDL_CreateWindow("HoverNet", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, SDL_WINDOW_SHOWN);
#endif
    if (!m_window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); return false; }

    // Create renderer with VSYNC enabled to synchronize with monitor refresh rate
    // This prevents flickering by ensuring Present() waits for the next vertical blank
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
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
    return CreateSDLPalette(); 
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
    
    // IMPORTANT: buffer is assumed to have stride == width (linear)
    // This must match mLineLen from VideoBuffer!
    
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
            rgb_buffer[offset + 0] = m_paletteRGB[index*3 + 0];  // R
            rgb_buffer[offset + 1] = m_paletteRGB[index*3 + 1];  // G
            rgb_buffer[offset + 2] = m_paletteRGB[index*3 + 2];  // B
        }
    }
    
    SDL_UpdateTexture(m_texture, nullptr, rgb_buffer, pitch);
    
    SDL_RenderClear(m_renderer);
    SDL_Rect r = {0, 0, width, height};
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &r);
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
bool SDL2GraphicsBackend::CreateSDLPalette() { return false; }
#endif
