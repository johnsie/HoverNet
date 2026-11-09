// TextureEnhance.h -- turning the game's 8-bit palette textures into better true-colour ones (HoverNet 3.0, stage 3; docs/graphics-engine.md).
//
// A texture in the resource file is a grid of palette indices (at most 256 colours, often dithered or banded) with a few smaller copies of it
// (mip levels) made by picking indices. These functions build a true-colour copy: a colour per texel with 8 bits per channel, from the palette, with
// the dither and banding of the original smoothed away without blurring its edges, and the smaller copies made by averaging the large one in linear
// light, so a distant wall keeps its brightness and does not shimmer. No SDL, no platform code: the same source runs natively, in the browser and in
// the tests. Textures tile, so the neighbours of a texel at the edge are on the opposite side.
//
// Texel layout is the game's: column-major, texel (x, y) at [x * yres + y]. Colour is 0x00RRGGBB.
#ifndef HOVERNET_TEXTURE_ENHANCE_H
#define HOVERNET_TEXTURE_ENHANCE_H

#include <cstdint>

namespace HoverNetTex
{
// The palette's colour for each texel, then (when enhance is true) the dither and banding smoothed with an edge-preserving filter and the result
// sharpened a little. xres and yres must each be at least 2. out holds xres * yres words.
//
// detail (0 or more) adds a subtle surface variation, strongest where the texture is a flat field and absent at edges: a fine grain and a soft
// mottling of about +-3% brightness at detail 1, worked out from the texel position only (so it is the same every time and tiles seamlessly). Flat
// colour is what makes a texture look painted; a trace of variation makes it look like a surface.
void EnhanceBase(const std::uint8_t* indices, int xres, int yres, const std::uint8_t* palette768, std::uint32_t* out, bool enhance, int detail = 0);

// Just the surface variation of EnhanceBase's detail, applied to a true-colour texture (a word 0x00RRGGBB per texel, in place): about +-3% brightness at detail 1,
// on flat fields only. Used for the AI-enhanced images too.
void AddSurfaceDetail(std::uint32_t* texels, int xres, int yres, int detail);

// A smaller copy of a true-colour texture: each output texel is the average, in linear light, of the block of source texels it covers. The source
// size must be a whole multiple of the destination size in both directions.
bool Downsample(const std::uint32_t* src, int srcXRes, int srcYRes, std::uint32_t* dst, int dstXRes, int dstYRes);

// A larger copy: twice the size in both directions, edge-directed (the diagonal staircases of an edge become staircases of half-texels; flat areas
// and gradients are copied, a straight edge is untouched). dst holds 2 * srcXRes * 2 * srcYRes words.
void Upscale2x(const std::uint32_t* src, int srcXRes, int srcYRes, std::uint32_t* dst);
}

#endif
