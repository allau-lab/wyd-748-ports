#include "field_map.h"
#include "asset_paths.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

bool WYD_FieldMapLoad(WYDFieldMap& out, const char* relative_trn)
{
	out = {};
	if (!relative_trn || !relative_trn[0])
	{
		out.error = "path vazio";
		return false;
	}

	out.path = WYD_JoinAssetPath(relative_trn);
	std::ifstream in(out.path, std::ios::binary);
	if (!in)
	{
		out.error = "não abriu " + out.path;
		std::fprintf(stderr, "[WYDLINUX][field] %s\n", out.error.c_str());
		return false;
	}

	std::vector<unsigned char> data(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	if (data.size() < 3 + 12 * 4096)
	{
		out.error = "trn curto";
		return false;
	}

	const int nameLen = data[0] > 127 ? 127 : data[0];
	if (static_cast<size_t>(1 + nameLen + 2 + 12 * 4096) > data.size())
	{
		out.error = "trn header inválido";
		return false;
	}

	std::memcpy(out.map_name, data.data() + 1, static_cast<size_t>(nameLen));
	out.map_name[nameLen] = '\0';
	out.zone_x = data[1 + nameLen];
	out.zone_y = data[2 + nameLen];

	const unsigned char* tiles = data.data() + 1 + nameLen + 2;
	for (int i = 0; i < 4096; ++i)
	{
		const unsigned char* t = tiles + i * 12;
		const int y = i / 64;
		const int x = i % 64;
		out.tiles[y][x].height = static_cast<std::int8_t>(t[0]);
		out.tiles[y][x].tile_index = t[1];
		std::memcpy(&out.tiles[y][x].color, t + 8, 4);
	}

	out.loaded = true;
	std::fprintf(stderr,
		"[WYDLINUX][field] OK %s name='%s' zone=%d,%d tiles=4096\n",
		relative_trn, out.map_name, out.zone_x, out.zone_y);
	return true;
}

bool WYD_FieldMapLoadForWorldPos(WYDFieldMap& out, short world_x, short world_y)
{
	const int zx = static_cast<int>(world_x) >> 7;
	const int zy = static_cast<int>(world_y) >> 7;
	char rel[64];
	std::snprintf(rel, sizeof(rel), "Env/Field%02d%02d.trn", zx, zy);
	return WYD_FieldMapLoad(out, rel);
}

void WYD_FieldMapUnload(WYDFieldMap& map)
{
	map = {};
}
