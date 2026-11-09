// GpuFinisher.h -- the finishing passes of the Modern look, run on the graphics card (OpenGL 3.3 core or OpenGL ES 3.0, loaded through SDL).
//
// The software rasteriser still draws the scene and the CPU passes still do the lighting that needs the depth buffer (atmosphere, lights, craft, reflections,
// effects, halos); this does what only needs the finished picture: de-banding with dither, edge smoothing, sharpening, bloom, the highlight roll-off, the dark
// edge beside HUD text and the vignette, as fragment shaders. The HUD's own pixels are left exactly as drawn. The picture goes up as one texture and comes back
// as one read, so it costs a few milliseconds whatever the passes' quality (bloom runs at three sizes with real Gaussian blurs).
//
// It makes its own hidden window and context, so the game's SDL renderer and Dear ImGui are not touched. Where no context can be made (no video device, the
// "dummy" driver, a browser) Init returns false and the CPU passes run as before. Nothing here is read by the simulation, and it never changes the camera.
#ifndef HOVERNET_GPU_FINISHER_H
#define HOVERNET_GPU_FINISHER_H

#include "ModernGraphics.h"

#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Texture;

namespace HoverNetGfx
{
class GpuFinisher
{
public:
    GpuFinisher();
    ~GpuFinisher();
    // Makes the context and compiles the shaders. False (and Error() says why) when this machine cannot. When the SDL renderer's own OpenGL context is current
    // when this is called, the new context shares objects with it, so the finished picture can be written straight into one of the renderer's textures
    // (AdoptTarget) and never comes back to the processor.
    bool Init();
    // With the renderer's context current: makes this texture (RGBA, the picture's size, made by the renderer as a render target) the place the picture is written. False
    // when the renderer is not OpenGL or the contexts do not share; the picture is then read back as before.
    bool AdoptTarget(SDL_Texture* texture, int width, int height);
    bool OutputIsShared() const;       // the last Process wrote into the adopted texture: the caller must not copy the CPU frame over it
    bool Ready() const { return mReady; }
    const std::string& Error() const { return mError; }
    const std::string& Renderer() const { return mRenderer; }   // the GPU's name, once ready

    // Finishes an RGB24 frame in place. hudMask: null or width*height bytes. False when something failed (the caller then runs the CPU passes).
    bool Process(std::uint8_t* rgb, int width, int height, const Options& options, const FrameHints& hints, const std::uint8_t* hudMask);

    double LastMilliseconds() const { return mLastMs; }

private:
    struct Impl;
    Impl* mImpl;
    bool mReady = false;
    std::string mError, mRenderer;
    double mLastMs = 0.0;
};
}

#endif
