#pragma once

// Decoder do formato WYT 7.48: prefixo "WT10" + corpo TGA (sem footer).
// Espelha TextureManager: descarta 4 bytes e trata o restante como TGA.
// Saída: RGBA8 top-left origin para upload Vulkan.

#include <cstdint>
#include <string>
#include <vector>

struct WYDWytImage
{
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba; // width*height*4
	std::string error;
};

// relative: ex. "UI/logo1.wyt" (casing exato).
bool WYD_LoadWytRgba(const char* relative, WYDWytImage& out);

// Decodifica buffer já lido (inclui magic WT10).
bool WYD_DecodeWytRgba(const uint8_t* data, size_t size, WYDWytImage& out);
