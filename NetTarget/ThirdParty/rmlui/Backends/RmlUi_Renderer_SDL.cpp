/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "RmlUi_Renderer_SDL.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Types.h>
#include <SDL.h>
#include "../../stb/stb_image.h"   // HoverNet, 2026-11-09: images are decoded with the vendored stb_image (implemented once, in TexturePack.cpp) instead of SDL_image

RenderInterface_SDL::RenderInterface_SDL(SDL_Renderer* renderer) : renderer(renderer) {}

void RenderInterface_SDL::BeginFrame()
{
	SDL_RenderSetViewport(renderer, nullptr);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void RenderInterface_SDL::EndFrame() {}

void RenderInterface_SDL::RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices, const Rml::TextureHandle texture,
	const Rml::Vector2f& translation)
{
	SDL_FPoint* positions = new SDL_FPoint[num_vertices];

	for (int i = 0; i < num_vertices; i++)
	{
		positions[i].x = vertices[i].position.x + translation.x;
		positions[i].y = vertices[i].position.y + translation.y;
	}

	SDL_Texture* sdl_texture = (SDL_Texture*)texture;

	SDL_RenderGeometryRaw(renderer, sdl_texture, &positions[0].x, sizeof(SDL_FPoint), (const SDL_Color*)&vertices->colour, sizeof(Rml::Vertex),
		&vertices->tex_coord.x, sizeof(Rml::Vertex), num_vertices, indices, num_indices, 4);

	delete[] positions;
}

void RenderInterface_SDL::EnableScissorRegion(bool enable)
{
	if (enable)
		SDL_RenderSetClipRect(renderer, &rect_scissor);
	else
		SDL_RenderSetClipRect(renderer, nullptr);

	scissor_region_enabled = enable;
}

void RenderInterface_SDL::SetScissorRegion(int x, int y, int width, int height)
{
	rect_scissor.x = x;
	rect_scissor.y = y;
	rect_scissor.w = width;
	rect_scissor.h = height;

	if (scissor_region_enabled)
		SDL_RenderSetClipRect(renderer, &rect_scissor);
}

bool RenderInterface_SDL::LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	char* buffer = new char[buffer_size];
	file_interface->Read(buffer, buffer_size, file_handle);
	file_interface->Close(file_handle);

	// HoverNet, 2026-11-09: decoded with stb_image (any format it reads) into a 32-bit RGBA surface; the original used SDL_image.
	int image_w = 0, image_h = 0, image_channels = 0;
	unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(buffer), int(buffer_size), &image_w, &image_h, &image_channels, 4);
	SDL_Surface* surface = nullptr;
	if (pixels)
	{
		surface = SDL_CreateRGBSurfaceWithFormat(0, image_w, image_h, 32, SDL_PIXELFORMAT_ABGR8888);   // R,G,B,A byte order in memory, as stb gives it
		if (surface)
			for (int y = 0; y < image_h; ++y)
				SDL_memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch, pixels + y * image_w * 4, size_t(image_w) * 4);
		stbi_image_free(pixels);
	}

	bool success = false;

	if (surface)
	{
		SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

		if (texture)
		{
			texture_handle = (Rml::TextureHandle)texture;
			texture_dimensions = Rml::Vector2i(surface->w, surface->h);
			success = true;
		}

		SDL_FreeSurface(surface);
	}

	delete[] buffer;

	return success;
}

bool RenderInterface_SDL::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source, const Rml::Vector2i& source_dimensions)
{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Uint32 rmask = 0xff000000;
	Uint32 gmask = 0x00ff0000;
	Uint32 bmask = 0x0000ff00;
	Uint32 amask = 0x000000ff;
#else
	Uint32 rmask = 0x000000ff;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x00ff0000;
	Uint32 amask = 0xff000000;
#endif

	SDL_Surface* surface =
		SDL_CreateRGBSurfaceFrom((void*)source, source_dimensions.x, source_dimensions.y, 32, source_dimensions.x * 4, rmask, gmask, bmask, amask);
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	SDL_FreeSurface(surface);
	texture_handle = (Rml::TextureHandle)texture;
	return true;
}

void RenderInterface_SDL::ReleaseTexture(Rml::TextureHandle texture_handle)
{
	SDL_DestroyTexture((SDL_Texture*)texture_handle);
}
