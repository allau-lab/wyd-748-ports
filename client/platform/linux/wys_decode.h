#pragma once

// Decoder WYS 7.48: magic "WS10" + DDS ofuscado (TextureManager).
// Reconstrução: skip 1º byte, prefixo "DDS", fourCC offset 84 → DXT1/DXT3.
// Saída: RGBA8 (mip 0) para upload D3D9/Vulkan.

#include "wyt_decode.h" // reusa WYDWytImage

#include <cstdint>
#include <string>

bool WYD_DecodeWysRgba(const uint8_t* data, size_t size, WYDWytImage& out);

// relative: ex. "Effect/spark01.wys"
bool WYD_LoadWysRgba(const char* relative, WYDWytImage& out);

// hint curto do MSA (ex. "spark01") → Effect|mesh|Mesh + .wys/.wyt
bool WYD_LoadTextureHintRgba(const char* hint, WYDWytImage& out);
