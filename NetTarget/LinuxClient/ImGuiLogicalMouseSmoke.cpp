// Regression test for the fullscreen click-misalignment bug (v0.1.73 through
// v0.1.76): every ImGui menu screen used to lay out and hit-test in the
// fixed kLogicalWidth x kLogicalHeight space SDL_RenderSetLogicalSize
// letterboxes into, while mouse coordinates from SDL stayed in real window
// pixels -- and three straight attempts at correcting for that mismatch
// each shipped looking right and wasn't (see Track3DViewer.cpp's git log
// for what didn't work and why). The design that actually holds up doesn't
// try to correct mouse coordinates at all: it disables the renderer's
// logical size for the duration of each ImGui frame, so ImGui draws 1:1
// against the real window and every mouse coordinate SDL reports is already
// in the right space.
//
// This test forces exactly the scenario that broke every previous attempt
// -- a window whose size doesn't match kLogicalWidth x kLogicalHeight, at a
// different aspect ratio, as fullscreen always produces -- and checks, via
// ImGui's own IsItemHovered(), that a synthetic click physically positioned
// over a button is actually seen as hovering it, using the exact same
// SDL_RenderSetLogicalSize(renderer, 0, 0) pattern Track3DViewer.cpp uses
// for its ImGui screens.
//
// Runs under SDL_VIDEODRIVER=dummy like the other headless smoke tests --
// window/renderer creation and mouse-state queries all work under it, so no
// real display is needed.

#include "ImGuiLogicalCoords.h"

#include "../ThirdParty/imgui/imgui.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdl2.h"
#include "../ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

namespace
{
// The legacy framebuffer's fixed resolution -- irrelevant to this test's
// ImGui screen itself, but exercised here as the size SDL_RenderSetLogicalSize
// is restored to afterwards, matching what Track3DViewer.cpp's raw
// bitmap-font screens (ConfirmQuit, RunPauseMenu) need.
constexpr int kLegacyLogicalWidth = 1024;
constexpr int kLegacyLogicalHeight = 768;
// 16:9, deliberately a different aspect ratio than kLegacyLogicalWidth x
// kLegacyLogicalHeight's 4:3 -- exactly what a fullscreen resolution
// mismatch produces, and what broke every previous fix attempt.
constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
}  // namespace

int main()
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("ImGuiLogicalMouseSmoke", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    // Matches SDL2Graphics.cpp's Initialize(): the renderer normally sits
    // letterboxed to the legacy framebuffer's fixed size, same as it would
    // be before the player ever opens an ImGui screen.
    SDL_RenderSetLogicalSize(renderer, kLegacyLogicalWidth, kLegacyLogicalHeight);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // A point clearly inside the button we'll place, in real window pixels
    // -- with logical size disabled during the ImGui frame, this is exactly
    // the space ImGui's DisplaySize and mouse position both live in, with no
    // conversion of any kind.
    constexpr float kTargetWindowX = 900.0f;
    constexpr float kTargetWindowY = 500.0f;

    // Sanity check: this point must fall outside the legacy 1024x768 area to
    // actually prove logical size was disabled rather than coincidentally
    // still working -- with a 1600x900 window it's within bounds either way,
    // so assert the more telling case: it's off the 4:3 area's right edge
    // once mapped as if logical size were still 1024x768 relative to a
    // 16:9 window (the vertical letterbox bars would otherwise place logical
    // (900, 500) well to the right of the visible 4:3 content).
    int legacyMappedX = 0, legacyMappedY = 0;
    SDL_RenderLogicalToWindow(renderer, kTargetWindowX, kTargetWindowY, &legacyMappedX, &legacyMappedY);
    if (legacyMappedX == static_cast<int>(kTargetWindowX) && legacyMappedY == static_cast<int>(kTargetWindowY)) {
        std::fprintf(stderr,
                     "Test setup didn't actually produce a window/logical mismatch "
                     "(window=%dx%d legacy_logical=%dx%d) -- test would pass trivially\n",
                     kWindowWidth, kWindowHeight, kLegacyLogicalWidth, kLegacyLogicalHeight);
        return 1;
    }

    SDL_WarpMouseInWindow(window, static_cast<int>(kTargetWindowX), static_cast<int>(kTargetWindowY));
    SDL_PumpEvents();

    // Two frames, not one: a brand-new ImGui window's hover state is decided
    // by NewFrame() against the window RECT FROM THE PREVIOUS FRAME (there
    // isn't one yet the very first time Begin() is called for it), so
    // hit-testing it in the same frame it's created can never work -- that's
    // a quirk of how ImGui schedules hover detection, not something specific
    // to this bug. The real game's screens run in a loop, exactly like this,
    // so replicating that here (create on frame 1, assert on frame 2) is
    // what actually matches production instead of a same-frame artifact.
    // A distinct clear color and button color, read back after presenting,
    // so this test also catches a purely visual regression (the original
    // "lobby filled the screen but overflowed, so you can't see any of the
    // buttons" report) -- IsItemHovered() alone only proves ImGui's internal
    // layout math is self-consistent, not that the pixels actually end up
    // where the player can see and click them.
    constexpr Uint8 kClearColor[3] = {10, 10, 10};
    constexpr Uint8 kButtonColor[3] = {0, 200, 0};
    const ImVec4 buttonColorF(kButtonColor[0] / 255.0f, kButtonColor[1] / 255.0f, kButtonColor[2] / 255.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, buttonColorF);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonColorF);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColorF);

    bool hovered = false;
    bool buttonPixelFound = false;
    for (int frame = 0; frame < 2; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        // The actual fix under test: disable logical size so ImGui draws
        // 1:1 against the real window, exactly as Track3DViewer.cpp's
        // RunLobbyScreen/RunLocalRaceSetup/RunSettingsScreen do.
        SDL_RenderSetLogicalSize(renderer, 0, 0);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Test", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorPos(ImVec2(kTargetWindowX - 40.0f, kTargetWindowY - 15.0f));
        ImGui::Button("Target", ImVec2(80.0f, 30.0f));
        if (frame == 1) {
            hovered = ImGui::IsItemHovered();
        }
        ImGui::End();
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, kClearColor[0], kClearColor[1], kClearColor[2], 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        if (frame == 1) {
            Uint8 pixel[4] = {0, 0, 0, 0};
            SDL_Rect readRect{static_cast<int>(kTargetWindowX), static_cast<int>(kTargetWindowY), 1, 1};
            if (SDL_RenderReadPixels(renderer, &readRect, SDL_PIXELFORMAT_RGB24, pixel, 3) == 0) {
                buttonPixelFound = pixel[0] < 50 && pixel[1] > 150 && pixel[2] < 50;
            }
            else {
                std::fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
            }
        }

        SDL_RenderPresent(renderer);
        // Restored before any subsequent raw-framebuffer rendering (or, in
        // this test, before the loop's next PollEvent -- matching where
        // Track3DViewer.cpp restores it, ahead of any ConfirmQuit call).
        SDL_RenderSetLogicalSize(renderer, kLegacyLogicalWidth, kLegacyLogicalHeight);
    }
    ImGui::PopStyleColor(3);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!hovered) {
        std::fprintf(stderr,
                     "Button at window position (%.0f, %.0f) was not detected as hovered after moving "
                     "the mouse there in a %dx%d window -- fullscreen/resized-window clicks on ImGui "
                     "screens would land in the wrong place\n",
                     kTargetWindowX, kTargetWindowY, kWindowWidth, kWindowHeight);
        return 1;
    }
    if (!buttonPixelFound) {
        std::fprintf(stderr,
                     "No button-colored pixel at window position (%.0f, %.0f) after presenting in a "
                     "%dx%d window -- the button is logically hoverable but not actually visible/drawn "
                     "where the player would look for it (the original \"screen overflowed, can't see "
                     "any buttons\" report)\n",
                     kTargetWindowX, kTargetWindowY, kWindowWidth, kWindowHeight);
        return 1;
    }

    std::printf(
        "ImGui logical mouse smoke test passed: window=%dx%d target=(%.0f,%.0f) hovered=yes drawn=yes\n",
        kWindowWidth, kWindowHeight, kTargetWindowX, kTargetWindowY);
    return 0;
}
