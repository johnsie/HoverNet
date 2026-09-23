#ifndef SDL2_GRAPHICS_H
#define SDL2_GRAPHICS_H

// SDL2 Graphics Backend for HoverRace
// Uses SDL2 software rendering for compatibility
// Maintains backward compatibility with existing game code

// Optional SDL2 support - if SDL2 is not available, this backend is disabled
#ifdef _HAVE_SDL2
    #include <SDL.h>
#else
    // Forward declarations for when SDL2 is not available
    typedef void* SDL_Window;
    typedef void* SDL_Renderer;
    typedef void* SDL_Texture;
    typedef void* SDL_Color;
#endif

#include <cstdint>
#include <vector>

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

    // Properties
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsInitialized() const { return m_initialized; }

private:
    // SDL2 objects
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture* m_texture;
    
    // Palette for color conversion
    uint8_t* m_paletteRGB;  // 256 * 3 bytes (RGB format)
    SDL_Color* m_sdlPalette;  // SDL format palette
    std::vector<uint8_t> m_rgbBuffer;
    
    // Resolution
    int m_width;
    int m_height;
    bool m_initialized;

    // Helper methods
    bool InitializeSDL2Rendering();
    bool CreateSDLPalette();
};

#endif // SDL2_GRAPHICS_H
