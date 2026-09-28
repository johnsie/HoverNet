#include "VideoBufferSDL2Adapter.h"
#include <cstring>

VideoBufferSDL2Adapter::VideoBufferSDL2Adapter()
    : m_buffer(nullptr)
    , m_width(0)
    , m_height(0)
    , m_locked(false)
{
}

VideoBufferSDL2Adapter::~VideoBufferSDL2Adapter()
{
    Shutdown();
}

bool VideoBufferSDL2Adapter::Initialize(void* windowHandle, int width, int height)
{
    m_width = width;
    m_height = height;

    // Initialize SDL2 backend
    if (!m_backend.Initialize(windowHandle, width, height))
    {
        return false;
    }


    // Allocate buffer
    if (!m_backend.AllocateBuffer(width, height, m_buffer))
    {
        return false;
    }

    return true;
}

void VideoBufferSDL2Adapter::Shutdown()
{
    if (m_buffer)
    {
        m_backend.FreeBuffer(m_buffer);
        m_buffer = nullptr;
    }
    m_backend.Shutdown();
}

bool VideoBufferSDL2Adapter::Lock(uint8_t*& outBuffer)
{
    // If buffer is already locked, return immediately without clearing
    // This handles legitimate cases where Lock() is called multiple times
    // before Unlock() (e.g., during viewport setup or exception handling)
    if (m_locked)
    {
        // Already locked - just return the existing buffer pointer
        // Do NOT clear the buffer or modify m_locked state
        if (!m_buffer)
            return false;
        outBuffer = m_buffer;
        
        
        return true;
    }
    
    if (!m_buffer)
        return false;

    // DEFENSIVE CHECK: Verify buffer size is valid before memset
    // If width or height is invalid, this could cause massive memory write
    if (m_width <= 0 || m_height <= 0)
    {
        return false;
    }

    // Calculate total buffer size - use long long to detect overflow
    long long totalSize = (long long)m_width * (long long)m_height;
    if (totalSize > 1000000)  // Sanity check - reasonable max for retro game
    {
        return false;
    }

    // Clear the buffer to black (index 0) at the start of each frame
    // This ensures no garbage from previous frames persists
    // while keeping the same buffer pointer for rendering consistency
    try {
        memset(m_buffer, 0, (size_t)totalSize);
        
    }
    catch(...) {
        return false;
    }

    m_locked = true;
    outBuffer = m_buffer;
    return true;
}

bool VideoBufferSDL2Adapter::Unlock(uint8_t* pBuffer)
{
    if (!m_locked)
        return false;

    m_locked = false;

    // Use provided buffer if given (from VideoBuffer.mBuffer), otherwise use internal buffer
    uint8_t* bufferToPresent = (pBuffer != nullptr) ? pBuffer : m_buffer;
    
    if (!bufferToPresent)
        return false;

    bool result = m_backend.Present(bufferToPresent, m_width, m_height);
    
    
    return result;
}

bool VideoBufferSDL2Adapter::SetPalette(const uint8_t* paletteRGB)
{
    if (!paletteRGB)
        return false;

    return m_backend.SetPalette(paletteRGB, 768);  // 256 colors * 3 bytes
}
