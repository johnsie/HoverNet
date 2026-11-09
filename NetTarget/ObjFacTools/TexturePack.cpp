// TexturePack.cpp -- see TexturePack.h.
#include "TexturePack.h"

#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#include "../ThirdParty/stb/stb_image.h"   // stb_image v2.30, public domain or MIT (THIRD_PARTY_NOTICES.md)

namespace HoverNetTex
{
std::string TexturePackPathFor(const char* resourceFile)
{
    std::string path = resourceFile != nullptr ? resourceFile : "";
    const std::string::size_type dot = path.find_last_of('.');
    const std::string::size_type slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) path.resize(dot);
    return path + ".textures";
}

namespace
{
std::uint32_t ReadU32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
bool Fail(std::string* error, const char* why) { if (error != nullptr) *error = why; return false; }
}

bool LoadTexturePack(const char* path, std::vector<PackImage>& images, std::string* error)
{
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return Fail(error, "cannot open the file");
    std::vector<std::uint8_t> data;
    std::uint8_t chunk[65536];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) data.insert(data.end(), chunk, chunk + n);
    std::fclose(f);
    if (data.size() < 12 || std::memcmp(data.data(), "HNTXPK1\n", 8) != 0) return Fail(error, "not a texture pack");
    const std::uint32_t count = ReadU32(data.data() + 8);
    if (count > 4096) return Fail(error, "implausible image count");
    std::size_t at = 12;
    std::vector<PackImage> read;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + 16 > data.size()) return Fail(error, "truncated entry");
        PackImage image;
        image.id = static_cast<int>(ReadU32(data.data() + at));
        const std::uint32_t w = ReadU32(data.data() + at + 4), h = ReadU32(data.data() + at + 8), size = ReadU32(data.data() + at + 12);
        at += 16;
        if (size == 0 || at + size > data.size() || w == 0 || h == 0 || w > 4096 || h > 4096) return Fail(error, "bad entry");
        int pw = 0, ph = 0, channels = 0;
        std::uint8_t* pixels = stbi_load_from_memory(data.data() + at, static_cast<int>(size), &pw, &ph, &channels, 3);
        at += size;
        if (pixels == nullptr) return Fail(error, "an image does not decode");
        if (static_cast<std::uint32_t>(pw) != w || static_cast<std::uint32_t>(ph) != h) { stbi_image_free(pixels); return Fail(error, "an image is not the size its entry says"); }
        image.width = pw; image.height = ph;
        image.words.resize(static_cast<std::size_t>(pw) * ph);
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                const std::uint8_t* p = pixels + (static_cast<std::size_t>(y) * pw + x) * 3;
                image.words[static_cast<std::size_t>(x) * ph + y] = (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
            }
        stbi_image_free(pixels);
        read.push_back(std::move(image));
    }
    images = std::move(read);
    return true;
}
}
