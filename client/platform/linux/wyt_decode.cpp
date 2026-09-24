#include "wyt_decode.h"
#include "asset_paths.h"

#include <cstdio>
#include <fstream>

namespace
{
	bool ReadAll(const std::string& path, std::vector<uint8_t>& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return false;
		in.seekg(0, std::ios::end);
		const auto n = in.tellg();
		if (n <= 0)
			return false;
		in.seekg(0, std::ios::beg);
		out.resize(static_cast<size_t>(n));
		in.read(reinterpret_cast<char*>(out.data()), n);
		return true;
	}
}

bool WYD_DecodeWytRgba(const uint8_t* data, size_t size, WYDWytImage& out)
{
	out = {};
	if (!data || size < 4 + 18)
	{
		out.error = "buffer WYT curto demais";
		return false;
	}
	if (data[0] != 'W' || data[1] != 'T' || data[2] != '1' || data[3] != '0')
	{
		out.error = "magic inválido (esperado WT10)";
		return false;
	}

	const uint8_t* tga = data + 4;
	const size_t tgaSize = size - 4;
	const uint8_t idLen = tga[0];
	const uint8_t cmapType = tga[1];
	const uint8_t imageType = tga[2];
	if (cmapType != 0)
	{
		out.error = "TGA com color map não suportado";
		return false;
	}
	if (imageType != 2 && imageType != 10)
	{
		out.error = "TGA type != 2/10 (truecolor)";
		return false;
	}

	const uint32_t width = static_cast<uint32_t>(tga[12] | (tga[13] << 8));
	const uint32_t height = static_cast<uint32_t>(tga[14] | (tga[15] << 8));
	const uint8_t bpp = tga[16];
	const uint8_t desc = tga[17];
	if (width == 0 || height == 0 || (bpp != 24 && bpp != 32))
	{
		out.error = "dimensão/bpp inválidos";
		return false;
	}

	const size_t headerSkip = 18u + idLen;
	if (tgaSize < headerSkip)
	{
		out.error = "header TGA truncado";
		return false;
	}

	const size_t bytesPerPixel = bpp / 8u;
	const size_t expected = static_cast<size_t>(width) * height * bytesPerPixel;
	const uint8_t* pixels = tga + headerSkip;

	// Type 10 = RLE — não implementado neste lote (logo1/Inven01 são type 2).
	if (imageType == 10)
	{
		out.error = "TGA RLE (type 10) ainda não suportado neste port";
		return false;
	}
	if (tgaSize < headerSkip + expected)
	{
		out.error = "payload TGA truncado";
		return false;
	}

	const bool originTop = (desc & 0x20) != 0;
	out.width = width;
	out.height = height;
	out.rgba.resize(static_cast<size_t>(width) * height * 4u);

	for (uint32_t y = 0; y < height; ++y)
	{
		const uint32_t srcY = originTop ? y : (height - 1u - y);
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t si = (static_cast<size_t>(srcY) * width + x) * bytesPerPixel;
			const size_t di = (static_cast<size_t>(y) * width + x) * 4u;
			const uint8_t b = pixels[si + 0];
			const uint8_t g = pixels[si + 1];
			const uint8_t r = pixels[si + 2];
			const uint8_t a = (bytesPerPixel == 4) ? pixels[si + 3] : 255;
			out.rgba[di + 0] = r;
			out.rgba[di + 1] = g;
			out.rgba[di + 2] = b;
			out.rgba[di + 3] = a;
		}
	}
	return true;
}

bool WYD_LoadWytRgba(const char* relative, WYDWytImage& out)
{
	out = {};
	const std::string path = WYD_JoinAssetPath(relative);
	std::vector<uint8_t> bytes;
	if (!ReadAll(path, bytes))
	{
		out.error = "não abriu (case-sensitive?): " + path;
		return false;
	}
	if (!WYD_DecodeWytRgba(bytes.data(), bytes.size(), out))
	{
		if (out.error.find(path) == std::string::npos)
			out.error += " @ " + path;
		return false;
	}
	std::fprintf(stderr, "[WYDLINUX][wyt] %s → %ux%u RGBA\n",
		relative, out.width, out.height);
	return true;
}
