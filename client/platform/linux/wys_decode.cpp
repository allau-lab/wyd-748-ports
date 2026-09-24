#include "wys_decode.h"
#include "asset_paths.h"
#include "asset_resolve.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

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

	void Decode565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
	{
		r = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
		g = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
		b = static_cast<uint8_t>((c & 31) * 255 / 31);
	}

	bool DecodeDxt1(const uint8_t* src, size_t srcSize, uint32_t w, uint32_t h,
		std::vector<uint8_t>& rgba)
	{
		const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 8u;
		if (srcSize < need)
			return false;
		rgba.assign(static_cast<size_t>(w) * h * 4u, 0);
		size_t off = 0;
		for (uint32_t by = 0; by < h; by += 4)
		{
			for (uint32_t bx = 0; bx < w; bx += 4)
			{
				const uint16_t c0 = static_cast<uint16_t>(src[off] | (src[off + 1] << 8));
				const uint16_t c1 = static_cast<uint16_t>(src[off + 2] | (src[off + 3] << 8));
				const uint32_t bits = static_cast<uint32_t>(src[off + 4]) |
					(static_cast<uint32_t>(src[off + 5]) << 8) |
					(static_cast<uint32_t>(src[off + 6]) << 16) |
					(static_cast<uint32_t>(src[off + 7]) << 24);
				off += 8;

				uint8_t cr[4], cg[4], cb[4], ca[4];
				Decode565(c0, cr[0], cg[0], cb[0]);
				Decode565(c1, cr[1], cg[1], cb[1]);
				if (c0 > c1)
				{
					cr[2] = static_cast<uint8_t>((2 * cr[0] + cr[1]) / 3);
					cg[2] = static_cast<uint8_t>((2 * cg[0] + cg[1]) / 3);
					cb[2] = static_cast<uint8_t>((2 * cb[0] + cb[1]) / 3);
					cr[3] = static_cast<uint8_t>((cr[0] + 2 * cr[1]) / 3);
					cg[3] = static_cast<uint8_t>((cg[0] + 2 * cg[1]) / 3);
					cb[3] = static_cast<uint8_t>((cb[0] + 2 * cb[1]) / 3);
					ca[0] = ca[1] = ca[2] = ca[3] = 255;
				}
				else
				{
					cr[2] = static_cast<uint8_t>((cr[0] + cr[1]) / 2);
					cg[2] = static_cast<uint8_t>((cg[0] + cg[1]) / 2);
					cb[2] = static_cast<uint8_t>((cb[0] + cb[1]) / 2);
					cr[3] = cg[3] = cb[3] = 0;
					ca[0] = ca[1] = ca[2] = 255;
					ca[3] = 0;
				}

				for (uint32_t py = 0; py < 4; ++py)
				{
					for (uint32_t px = 0; px < 4; ++px)
					{
						const uint32_t x = bx + px;
						const uint32_t y = by + py;
						if (x >= w || y >= h)
							continue;
						const uint32_t idx = (bits >> (2 * (py * 4 + px))) & 3u;
						const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
						rgba[di + 0] = cr[idx];
						rgba[di + 1] = cg[idx];
						rgba[di + 2] = cb[idx];
						rgba[di + 3] = ca[idx];
					}
				}
			}
		}
		return true;
	}

	bool DecodeDxt3(const uint8_t* src, size_t srcSize, uint32_t w, uint32_t h,
		std::vector<uint8_t>& rgba)
	{
		const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16u;
		if (srcSize < need)
			return false;
		rgba.assign(static_cast<size_t>(w) * h * 4u, 0);
		size_t off = 0;
		for (uint32_t by = 0; by < h; by += 4)
		{
			for (uint32_t bx = 0; bx < w; bx += 4)
			{
				uint8_t a[16];
				for (int i = 0; i < 8; ++i)
				{
					const uint8_t byte = src[off + static_cast<size_t>(i)];
					a[i * 2] = static_cast<uint8_t>((byte & 0x0F) * 17);
					a[i * 2 + 1] = static_cast<uint8_t>(((byte >> 4) & 0x0F) * 17);
				}
				off += 8;

				const uint16_t c0 = static_cast<uint16_t>(src[off] | (src[off + 1] << 8));
				const uint16_t c1 = static_cast<uint16_t>(src[off + 2] | (src[off + 3] << 8));
				const uint32_t bits = static_cast<uint32_t>(src[off + 4]) |
					(static_cast<uint32_t>(src[off + 5]) << 8) |
					(static_cast<uint32_t>(src[off + 6]) << 16) |
					(static_cast<uint32_t>(src[off + 7]) << 24);
				off += 8;

				uint8_t cr[4], cg[4], cb[4];
				Decode565(c0, cr[0], cg[0], cb[0]);
				Decode565(c1, cr[1], cg[1], cb[1]);
				cr[2] = static_cast<uint8_t>((2 * cr[0] + cr[1]) / 3);
				cg[2] = static_cast<uint8_t>((2 * cg[0] + cg[1]) / 3);
				cb[2] = static_cast<uint8_t>((2 * cb[0] + cb[1]) / 3);
				cr[3] = static_cast<uint8_t>((cr[0] + 2 * cr[1]) / 3);
				cg[3] = static_cast<uint8_t>((cg[0] + 2 * cg[1]) / 3);
				cb[3] = static_cast<uint8_t>((cb[0] + 2 * cb[1]) / 3);

				for (uint32_t py = 0; py < 4; ++py)
				{
					for (uint32_t px = 0; px < 4; ++px)
					{
						const uint32_t x = bx + px;
						const uint32_t y = by + py;
						if (x >= w || y >= h)
							continue;
						const uint32_t idx = (bits >> (2 * (py * 4 + px))) & 3u;
						const size_t di = (static_cast<size_t>(y) * w + x) * 4u;
						rgba[di + 0] = cr[idx];
						rgba[di + 1] = cg[idx];
						rgba[di + 2] = cb[idx];
						rgba[di + 3] = a[py * 4 + px];
					}
				}
			}
		}
		return true;
	}
}

bool WYD_DecodeWysRgba(const uint8_t* data, size_t size, WYDWytImage& out)
{
	out = {};
	if (!data || size < 128)
	{
		out.error = "buffer WYS curto demais";
		return false;
	}
	if (data[0] != 'W' || data[1] != 'S' || data[2] != '1' || data[3] != '0')
	{
		out.error = "magic inválido (esperado WS10)";
		return false;
	}

	// Espelha TextureManager: descarta 1 byte, prefixa "DDS", corrige fourCC@84.
	std::vector<uint8_t> dds(size - 1);
	std::memcpy(dds.data(), data + 1, size - 1);
	dds[0] = 'D';
	dds[1] = 'D';
	dds[2] = 'S';
	if (dds.size() < 128)
	{
		out.error = "DDS reconstruído curto";
		return false;
	}
	if (dds[84] == '2')
		std::memcpy(&dds[84], "DXT1", 4);
	else
		std::memcpy(&dds[84], "DXT3", 4);

	const uint32_t height =
		static_cast<uint32_t>(dds[12] | (dds[13] << 8) | (dds[14] << 16) | (dds[15] << 24));
	const uint32_t width =
		static_cast<uint32_t>(dds[16] | (dds[17] << 8) | (dds[18] << 16) | (dds[19] << 24));
	if (width == 0 || height == 0 || width > 4096 || height > 4096)
	{
		out.error = "dimensão DDS inválida";
		return false;
	}

	const uint8_t* payload = dds.data() + 128;
	const size_t payloadSize = dds.size() - 128;
	const bool isDxt1 = (std::memcmp(&dds[84], "DXT1", 4) == 0);
	std::vector<uint8_t> rgba;
	const bool ok = isDxt1
		? DecodeDxt1(payload, payloadSize, width, height, rgba)
		: DecodeDxt3(payload, payloadSize, width, height, rgba);
	if (!ok)
	{
		out.error = isDxt1 ? "DXT1 truncado" : "DXT3 truncado";
		return false;
	}

	out.width = width;
	out.height = height;
	out.rgba = std::move(rgba);
	return true;
}

bool WYD_LoadWysRgba(const char* relative, WYDWytImage& out)
{
	out = {};
	const std::string path = WYD_JoinAssetPath(relative);
	std::vector<uint8_t> bytes;
	if (!ReadAll(path, bytes))
	{
		out.error = "não abriu (case-sensitive?): " + path;
		return false;
	}
	if (!WYD_DecodeWysRgba(bytes.data(), bytes.size(), out))
	{
		if (out.error.find(path) == std::string::npos)
			out.error += " @ " + path;
		return false;
	}
	std::fprintf(stderr, "[WYDLINUX][wys] %s → %ux%u RGBA\n",
		relative, out.width, out.height);
	return true;
}

bool WYD_LoadTextureHintRgba(const char* hint, WYDWytImage& out)
{
	out = {};
	if (!hint || !hint[0])
	{
		out.error = "hint vazio";
		return false;
	}

	std::string name = hint;
	// remove extensão se já veio com .wys/.wyt
	const auto dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name.resize(dot);

	const char* folders[] = {"Effect/", "effect/", "mesh/", "Mesh/", ""};
	const char* exts[] = {".wys", ".wyt", ".WYS", ".WYT"};
	for (const char* folder : folders)
	{
		for (const char* ext : exts)
		{
			const std::string rel = std::string(folder) + name + ext;
			const std::string abs = WYD_ResolveAssetAbsolute(rel.c_str());
			if (abs.empty())
				continue;
			std::vector<uint8_t> bytes;
			if (!ReadAll(abs, bytes) || bytes.size() < 4)
				continue;
			if (bytes[0] == 'W' && bytes[1] == 'S' && bytes[2] == '1' && bytes[3] == '0')
			{
				if (WYD_DecodeWysRgba(bytes.data(), bytes.size(), out))
				{
					std::fprintf(stderr, "[WYDLINUX][tex] hint '%s' → %s (%ux%u)\n",
						hint, rel.c_str(), out.width, out.height);
					return true;
				}
			}
			else if (bytes[0] == 'W' && bytes[1] == 'T' && bytes[2] == '1' && bytes[3] == '0')
			{
				if (WYD_DecodeWytRgba(bytes.data(), bytes.size(), out))
				{
					std::fprintf(stderr, "[WYDLINUX][tex] hint '%s' → %s (%ux%u)\n",
						hint, rel.c_str(), out.width, out.height);
					return true;
				}
			}
		}
	}
	out.error = std::string("textura não encontrada para hint '") + hint + "'";
	return false;
}
