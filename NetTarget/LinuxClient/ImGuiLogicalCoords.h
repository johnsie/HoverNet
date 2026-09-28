#pragma once

// WindowToLogicalPoint/RewriteMouseEventToLogical convert real window-pixel
// mouse coordinates into the fixed logical coordinate space a
// SDL_RenderSetLogicalSize'd renderer letterboxes into, for the raw
// bitmap-font screens (ConfirmQuit, RunPauseMenu in Track3DViewer.cpp) that
// still draw at that fixed size. Pulled into its own header, rather than
// staying private to Track3DViewer.cpp, specifically so
// HoverNetImGuiLogicalMouseSmoke can exercise the exact same code the real
// client runs instead of a hand-duplicated copy that could quietly drift
// from it.
//
// The ImGui-driven screens (Lobby, Local Race Setup, Settings) don't need
// this at all -- they disable the renderer's logical size for the duration
// of their own frame instead (see Track3DViewer.cpp), so ImGui draws 1:1
// against the real window and every mouse coordinate SDL reports is already
// in the right space. That's the design that actually held up: three
// straight attempts at correcting ImGui's mouse position for the letterbox
// mismatch instead (v0.1.73 through v0.1.76) each looked right and wasn't --
// see git log for what didn't work and why.

#include <SDL.h>

// Delegates to SDL's own SDL_RenderWindowToLogical (SDL 2.0.18+) rather than
// reimplementing the letterbox scale/offset math by hand -- a from-scratch
// version based on SDL_GetWindowSize alone was tried first and didn't hold
// up, most likely because it can't account for cases where the renderer's
// actual output size (what SDL_RenderSetLogicalSize's viewport math is
// really based on) differs from window size, e.g. HiDPI/fractional display
// scaling. SDL already tracks whatever mapping it set up internally, so ask
// it directly instead of guessing.
inline void WindowToLogicalPoint(SDL_Renderer* pRenderer, float pWindowX, float pWindowY,
                                 float& pOutLogicalX, float& pOutLogicalY)
{
    if (pRenderer == nullptr) {
        pOutLogicalX = pWindowX;
        pOutLogicalY = pWindowY;
        return;
    }
    SDL_RenderWindowToLogical(pRenderer, static_cast<int>(pWindowX), static_cast<int>(pWindowY),
                             &pOutLogicalX, &pOutLogicalY);
}

// Call on every SDL_Event right after SDL_PollEvent, before any raw
// SDL_MOUSEBUTTONDOWN/SDL_MOUSEMOTION handling -- ConfirmQuit and
// RunPauseMenu in Track3DViewer.cpp.
inline void RewriteMouseEventToLogical(SDL_Event& pEvent, SDL_Renderer* pRenderer)
{
    float lLogicalX = 0.0f, lLogicalY = 0.0f;
    if (pEvent.type == SDL_MOUSEMOTION) {
        WindowToLogicalPoint(pRenderer, static_cast<float>(pEvent.motion.x),
                             static_cast<float>(pEvent.motion.y), lLogicalX, lLogicalY);
        pEvent.motion.x = static_cast<Sint32>(lLogicalX);
        pEvent.motion.y = static_cast<Sint32>(lLogicalY);
    }
    else if (pEvent.type == SDL_MOUSEBUTTONDOWN || pEvent.type == SDL_MOUSEBUTTONUP) {
        WindowToLogicalPoint(pRenderer, static_cast<float>(pEvent.button.x),
                             static_cast<float>(pEvent.button.y), lLogicalX, lLogicalY);
        pEvent.button.x = static_cast<Sint32>(lLogicalX);
        pEvent.button.y = static_cast<Sint32>(lLogicalY);
    }
}
