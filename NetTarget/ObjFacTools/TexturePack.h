// TexturePack.h -- the AI-enhanced textures (docs/texture-pipeline.md).
//
// A texture pack is one file, next to ObjFac1.dat (ObjFac1.textures), that holds an improved true-colour image for some of the game's textures: each is the
// texture run through an AI upscaler (tools/texture-ai/), as a PNG. Format, little-endian: the 8 bytes "HNTXPK1\n", a u32 count, then per image a u32 texture
// id, a u32 width, a u32 height, a u32 PNG size and the PNG. The pack is optional: without it the game improves textures itself (TextureEnhance.h).
#ifndef HOVERNET_TEXTURE_PACK_H
#define HOVERNET_TEXTURE_PACK_H

#include <cstdint>
#include <string>
#include <vector>

namespace HoverNetTex
{
struct PackImage
{
    int id = 0;
    int width = 0, height = 0;
    std::vector<std::uint32_t> words;   // 0x00RRGGBB, column-major like the game's textures: texel (x, y) at [x * height + y]
};

// Reads a pack. Returns false (and says why in *error) when the file cannot be read or is malformed; images read before the problem are not kept.
bool LoadTexturePack(const char* path, std::vector<PackImage>& images, std::string* error = nullptr);

// The pack's path for a resource file: the same name with ".textures" in place of its extension.
std::string TexturePackPathFor(const char* resourceFile);
}

#endif
