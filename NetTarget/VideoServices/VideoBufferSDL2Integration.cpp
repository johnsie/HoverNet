// VideoBufferSDL2Integration.cpp
// Integration implementation for SDL2Graphics backend with VideoBuffer

#include "stdafx.h"
#include "VideoBufferSDL2Integration.h"

// Global SDL2 adapter instance
VideoBufferSDL2Adapter* g_SDL2GraphicsAdapter = NULL;

// Helper function to initialize SDL2 graphics
BOOL InitializeSDL2Graphics(HWND pWindow, int pXRes, int pYRes)
{
   // Log to file for debugging

   // Clean up any existing adapter
   if (g_SDL2GraphicsAdapter != NULL)
   {
      delete g_SDL2GraphicsAdapter;
      g_SDL2GraphicsAdapter = NULL;
   }

   try
   {
      // Create new adapter instance
      g_SDL2GraphicsAdapter = new VideoBufferSDL2Adapter();

      // Initialize with window and resolution
      if (g_SDL2GraphicsAdapter->Initialize(pWindow, pXRes, pYRes))
      {
         return TRUE;
      }
      else
      {
         delete g_SDL2GraphicsAdapter;
         g_SDL2GraphicsAdapter = NULL;
         return FALSE;
      }
   }
   catch (const std::exception&)
   {
      if (g_SDL2GraphicsAdapter != NULL)
      {
         delete g_SDL2GraphicsAdapter;
         g_SDL2GraphicsAdapter = NULL;
      }
      return FALSE;
   }
   catch (...)
   {
      if (g_SDL2GraphicsAdapter != NULL)
      {
         delete g_SDL2GraphicsAdapter;
         g_SDL2GraphicsAdapter = NULL;
      }
      return FALSE;
   }
}

// Helper function to shutdown SDL2 graphics
void ShutdownSDL2Graphics()
{
   if (g_SDL2GraphicsAdapter != NULL)
   {
      g_SDL2GraphicsAdapter->Shutdown();
      delete g_SDL2GraphicsAdapter;
      g_SDL2GraphicsAdapter = NULL;
   }
}

// Check if SDL2 graphics is available
BOOL IsSDL2GraphicsAvailable()
{
   // Return TRUE if adapter exists OR if _HAVE_SDL2 is defined (meaning SDL2 support is compiled in)
   #ifdef _HAVE_SDL2
   return TRUE;  // SDL2 support is compiled in
   #else
   return FALSE; // SDL2 support not compiled in
   #endif
}
