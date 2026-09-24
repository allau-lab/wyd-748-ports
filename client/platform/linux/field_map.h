#pragma once

// Loader mínimo de Env/FieldXXYY.trn (64×64 tiles × 12 B) — Fase F.
#include "wyt_decode.h"

#include <cstdint>
#include <string>

struct WYDFieldTile
{
	std::int8_t height = 0;
	std::uint8_t tile_index = 0;
	std::uint32_t color = 0xFFFFFFFF;
};

struct WYDFieldMap
{
	bool loaded = false;
	int zone_x = 0;
	int zone_y = 0;
	char map_name[128] {};
	WYDFieldTile tiles[64][64] {};
	std::string path;
	std::string error;
};

// worldX/Y do 0x114 → zona = coord >> 7 → Env/Field%02d%02d.trn
bool WYD_FieldMapLoadForWorldPos(WYDFieldMap& out, short world_x, short world_y);

// Reaproveita path explícito (casing Env/).
bool WYD_FieldMapLoad(WYDFieldMap& out, const char* relative_trn);

void WYD_FieldMapUnload(WYDFieldMap& map);
