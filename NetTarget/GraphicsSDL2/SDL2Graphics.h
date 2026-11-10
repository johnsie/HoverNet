#ifndef SDL2_GRAPHICS_H
#define SDL2_GRAPHICS_H

// SDL2 Graphics Backend for HoverRace
// Uses SDL2 software rendering for compatibility
// Maintains backward compatibility with existing game code

// Optional SDL2 support - if SDL2 is not available, this backend is disabled
#ifdef _HAVE_SDL2
    #include <functional>
#include <SDL.h>
#else
    // Forward declarations for when SDL2 is not available
    typedef void* SDL_Window;
    typedef void* SDL_Renderer;
    typedef void* SDL_Texture;
    typedef void* SDL_Color;
#endif

#include <chrono>
#include <cstdint>
#include <vector>

#include "../VideoServices/ModernGraphics.h"
#include "../VideoServices/GpuFinisher.h"
#include <memory>

class SDL2GraphicsBackend
{
public:
    SDL2GraphicsBackend();
    ~SDL2GraphicsBackend();

    // Initialization
    bool Initialize(void* windowHandle, int width, int height);
    void Shutdown();

    // Buffer management
    bool AllocateBuffer(int width, int height, uint8_t*& outBuffer);
    void FreeBuffer(uint8_t* buffer);

    // Palette management (for 8-bit color support)
    bool SetPalette(const uint8_t* palette, int paletteSize);
    
    // Rendering
    bool Present(const uint8_t* buffer, int width, int height);
    bool Clear(uint8_t color);

    // The optional modern look (VideoServices/ModernGraphics.h): picture passes and effects applied to each frame after the palette.
    // Disabled options (the default) leave the classic picture byte-for-byte as before. The hints come from the observer for the frame
    // about to be presented and are used by that one Present() call.
    void SetModernOptions(const HoverNetGfx::Options& options) { m_modernOptions = options; }
    const HoverNetGfx::Options& GetModernOptions() const { return m_modernOptions; }
    void SetFrameHints(const HoverNetGfx::FrameHints& hints) { m_frameHints = hints; }
    void ResetModernEffects() { m_presenter.Reset(); }
    // 256 entries: each palette index mapped to the nearest colour at about 70% brightness (for the observer's craft shadows); rebuilt when
    // the palette changes. Null before a palette has been set.
    const uint8_t* GetShadowLut();
    // The palette the frame is shown through (256 RGB triples): the graded one when the look grades colour, otherwise the game's. Null before one is set.
    const uint8_t* GetRenderPalette();
    // The quality the picture passes are really running at (lower than the setting while they are too slow for their time budget).
    int GetEffectiveQuality() const { return m_presenter.EffectiveQuality(); }
    int GetAutomaticLevel() const { return m_presenter.Level(); }
    double GetPassCostMs() const { return m_presenter.PassCostMs(); }
    double GetFrameMs() const { return m_presenter.FrameMs(); }   // Automatic graphics: how much of the look is running now (0 to Presenter::kMaxLevel)
    const HoverNetGfx::Options& GetEffectiveModernOptions() const { return m_presenter.EffectiveOptions(); }   // the options as limited by the automatic level
    // True while the picture passes run on the graphics card; the card's name, and how long the last frame took there (ms).
    bool GpuActive() const { return m_gpuAllowed && m_presenter.FinisherActive(); }
    // Where the viewport hands the scene when the card draws it (null while it cannot: no card, switched off, or it failed). Set it on the observer every frame.
    MR_SceneSink* SceneSink() { return GpuActive() && m_gpu && m_gpu->SceneWorks() && m_gpuSceneAllowed ? &m_gpu->Scene() : nullptr; }
    void SetGpuAllowed(bool allowed) { m_gpuAllowed = allowed; }   // the player's choice (Settings > Graphics); the CPU passes run when it is off
    const char* GpuName() const { return m_gpu ? m_gpu->Renderer().c_str() : ""; }
    double GpuMilliseconds() const { return m_gpu ? m_gpu->LastMilliseconds() : 0.0; }

    // Palette-expand an indexed frame to RGBA (VR eye images); out must hold width*height*4 bytes.
    bool ConvertToRGBA(const uint8_t* buffer, int width, int height, uint8_t* out) const;

    // Properties
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsInitialized() const { return m_initialized; }

    // Raw SDL handles, for overlay UI (e.g. Dear ImGui) drawn via the same
    // renderer this backend already owns. Null until Initialize() succeeds.
    // Something drawn over each finished frame just before it is shown (the tutorial's lesson card): called with the renderer after the picture has been copied
    // to it. It must leave the renderer's logical size as it found it. Pass an empty function to remove it.
    void SetOverlay(std::function<void(SDL_Renderer*)> overlay) { m_overlay = std::move(overlay); }
    SDL_Window* GetWindow() const { return m_window; }
    SDL_Renderer* GetRenderer() const { return m_renderer; }

private:
    std::function<void(SDL_Renderer*)> m_overlay;
    // SDL2 objects
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture* m_texture;
    
    // Palette for color conversion
    uint8_t* m_paletteRGB;  // 256 * 3 bytes (RGB format)
    SDL_Color* m_sdlPalette;  // SDL format palette
    std::vector<uint8_t> m_rgbBuffer;
    uint8_t m_shadowLut[256];
    bool m_shadowLutValid = false;
    uint8_t m_gradedPalette[768];
    bool m_gradedPaletteValid = false;
    std::chrono::steady_clock::time_point m_lastPresentTime;
    bool m_havePresentTime = false;
    float m_gradedBrightness = 1.0f;
    HoverNetGfx::Presenter m_presenter;
    SDL_Texture* m_gpuTexture = nullptr;   // the finished picture, written by the card straight into a texture of the renderer (no read-back), when the renderer is OpenGL
    std::unique_ptr<HoverNetGfx::GpuFinisher> m_gpu;   // the finishing passes on the graphics card, when this machine has one (HOVERNET_GPU=0 turns it off)
    bool m_gpuTried = false;
    bool m_gpuAllowed = true;
    bool m_gpuSceneAllowed = true;   // HOVERNET_GPU_SCENE=0 keeps the software rasteriser and puts only the picture passes on the card
    HoverNetGfx::Options m_modernOptions;
    HoverNetGfx::FrameHints m_frameHints;
    
    // Resolution
    int m_width;
    int m_height;
    bool m_initialized;

    // Helper methods
    bool InitializeSDL2Rendering();
    bool CreateSDLPalette();
};

#endif // SDL2_GRAPHICS_H
