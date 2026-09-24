#include "field_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	void Put(std::uint8_t* rgba, uint32_t w, uint32_t h,
		int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= w || static_cast<uint32_t>(y) >= h)
			return;
		const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4;
		rgba[i] = r;
		rgba[i + 1] = g;
		rgba[i + 2] = b;
		rgba[i + 3] = 255;
	}

	void Fill(std::uint8_t* rgba, uint32_t w, uint32_t h,
		int x0, int y0, int x1, int y1,
		std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				Put(rgba, w, h, x, y, r, g, b);
	}

	// Fonte 5×7 mínima (mesma ideia do selchar).
	using Glyph = unsigned char[7];
	const Glyph* G(char c)
	{
		static const Glyph blank = {};
		static const Glyph g0 = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
		static const Glyph g1 = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
		static const Glyph g2 = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
		static const Glyph g3 = {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E};
		static const Glyph g4 = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
		static const Glyph g5 = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E};
		static const Glyph g6 = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E};
		static const Glyph g7 = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
		static const Glyph g8 = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
		static const Glyph g9 = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C};
		static const Glyph gA = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
		static const Glyph gC = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
		static const Glyph gD = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
		static const Glyph gE = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
		static const Glyph gF = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
		static const Glyph gI = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E};
		static const Glyph gL = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
		static const Glyph gM = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
		static const Glyph gN = {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
		static const Glyph gO = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
		static const Glyph gP = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
		static const Glyph gR = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
		static const Glyph gS = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
		static const Glyph gT = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
		static const Glyph gU = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
		static const Glyph gW = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
		static const Glyph gX = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
		static const Glyph gY = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
		static const Glyph gDash = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
		static const Glyph gColon = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
		static const Glyph gComma = {0x00,0x00,0x00,0x00,0x04,0x04,0x08};
		static const Glyph gSlash = {0x01,0x02,0x04,0x08,0x10,0x00,0x00};
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
		switch (c) {
		case '0': return &g0; case '1': return &g1; case '2': return &g2;
		case '3': return &g3; case '4': return &g4; case '5': return &g5;
		case '6': return &g6; case '7': return &g7; case '8': return &g8;
		case '9': return &g9; case 'A': return &gA; case 'C': return &gC;
		case 'D': return &gD; case 'E': return &gE; case 'F': return &gF;
		case 'I': return &gI; case 'L': return &gL; case 'M': return &gM;
		case 'N': return &gN; case 'O': return &gO; case 'P': return &gP;
		case 'R': return &gR; case 'S': return &gS; case 'T': return &gT;
		case 'U': return &gU; case 'W': return &gW; case 'X': return &gX;
		case 'Y': return &gY; case '-': return &gDash; case ':': return &gColon;
		case ',': return &gComma; case '/': return &gSlash; case ' ': return &blank;
		default: return &blank;
		}
	}

	void Text(std::uint8_t* rgba, uint32_t w, uint32_t h,
		int x, int y, const char* s, int scale,
		std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		for (int i = 0; s[i]; ++i)
		{
			const Glyph* gl = G(s[i]);
			for (int row = 0; row < 7; ++row)
			{
				const unsigned char bits = (*gl)[row];
				for (int col = 0; col < 5; ++col)
				{
					if (((bits >> (4 - col)) & 1) == 0) continue;
					for (int dy = 0; dy < scale; ++dy)
						for (int dx = 0; dx < scale; ++dx)
							Put(rgba, w, h,
								x + i * 6 * scale + col * scale + dx,
								y + row * scale + dy, r, g, b);
				}
			}
		}
	}
}

void WYD_FieldViewResetFromLogin(WYDFieldView& view, const WYDLoginSession& login)
{
	const int zoneX = static_cast<int>(login.pos_x) >> 7;
	const int zoneY = static_cast<int>(login.pos_y) >> 7;
	const int localWorldX = static_cast<int>(login.pos_x) - (zoneX << 7);
	const int localWorldY = static_cast<int>(login.pos_y) - (zoneY << 7);
	view.local_x = static_cast<float>(localWorldX) * 0.5f;
	view.local_y = static_cast<float>(localWorldY) * 0.5f;
	view.local_x = std::clamp(view.local_x, 1.f, 62.f);
	view.local_y = std::clamp(view.local_y, 1.f, 62.f);
	view.dirty = true;
}

void WYD_FieldViewMove(WYDFieldView& view, float dx, float dy)
{
	if (dx == 0.f && dy == 0.f)
		return;
	view.local_x = std::clamp(view.local_x + dx, 1.f, 62.f);
	view.local_y = std::clamp(view.local_y + dy, 1.f, 62.f);
	view.dirty = true;
}

bool WYD_FieldViewBuild(WYDFieldView& view, const WYDFieldMap& map,
	const WYDLoginSession& login, const WYDFieldWorld& world,
	uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return false;

	view.image.width = width;
	view.image.height = height;
	view.image.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
	std::uint8_t* px = view.image.rgba.data();

	Fill(px, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height), 12, 16, 22);

	const int hudH = 48;
	const int mapH = static_cast<int>(height) - hudH - 8;
	const int mapW = static_cast<int>(width) - 16;
	const int ox = 8;
	const int oy = hudH;
	const int zoneOriginX = map.zone_x << 7;
	const int zoneOriginY = map.zone_y << 7;

	if (map.loaded)
	{
		for (int ty = 0; ty < 64; ++ty)
		{
			for (int tx = 0; tx < 64; ++tx)
			{
				const auto& t = map.tiles[ty][tx];
				const int h = static_cast<int>(t.height);
				std::uint8_t r = static_cast<std::uint8_t>(std::clamp(40 + h * 3, 0, 255));
				std::uint8_t g = static_cast<std::uint8_t>(std::clamp(70 + h * 4, 0, 255));
				std::uint8_t b = static_cast<std::uint8_t>(std::clamp(35 + h * 2, 0, 255));
				if (t.color != 0 && t.color != 0xFFFFFFFFu)
				{
					r = static_cast<std::uint8_t>((t.color >> 16) & 0xFF);
					g = static_cast<std::uint8_t>((t.color >> 8) & 0xFF);
					b = static_cast<std::uint8_t>(t.color & 0xFF);
				}
				const int x0 = ox + (tx * mapW) / 64;
				const int x1 = ox + ((tx + 1) * mapW) / 64;
				const int y0 = oy + (ty * mapH) / 64;
				const int y1 = oy + ((ty + 1) * mapH) / 64;
				Fill(px, width, height, x0, y0, x1, y1, r, g, b);
			}
		}
	}
	else
	{
		Fill(px, width, height, ox, oy, ox + mapW, oy + mapH, 30, 40, 50);
		Text(px, width, height, ox + 20, oy + 20, "MAP MISSING", 2, 200, 80, 80);
	}

	auto WorldToScreen = [&](short wx, short wy, int& sx, int& sy) {
		const float lx = (static_cast<float>(wx - zoneOriginX)) * 0.5f;
		const float ly = (static_cast<float>(wy - zoneOriginY)) * 0.5f;
		sx = ox + static_cast<int>((lx * mapW) / 64.f);
		sy = oy + static_cast<int>((ly * mapH) / 64.f);
	};

	// Mobs (não-self)
	for (int i = 0; i < WYD_FIELD_MAX_MOBS; ++i)
	{
		const auto& m = world.mobs[i];
		if (!m.alive || m.is_self)
			continue;
		if (m.world_x < zoneOriginX || m.world_x >= zoneOriginX + 128 ||
			m.world_y < zoneOriginY || m.world_y >= zoneOriginY + 128)
			continue;

		int sx = 0, sy = 0;
		WorldToScreen(m.world_x, m.world_y, sx, sy);
		Fill(px, width, height, sx - 3, sy - 3, sx + 4, sy + 4, 80, 160, 255);
		Fill(px, width, height, sx - 1, sy - 1, sx + 2, sy + 2, 30, 60, 120);
		if (m.name[0])
			Text(px, width, height, sx + 5, sy - 6, m.name, 1, 200, 220, 255);
	}

	// Player (vista local; preferir self mob se existir)
	int px0 = ox + static_cast<int>((view.local_x * mapW) / 64.f) - 4;
	int py0 = oy + static_cast<int>((view.local_y * mapH) / 64.f) - 4;
	Fill(px, width, height, px0, py0, px0 + 9, py0 + 9, 255, 230, 60);
	Fill(px, width, height, px0 + 2, py0 + 2, px0 + 7, py0 + 7, 220, 60, 40);

	char line1[96];
	std::snprintf(line1, sizeof(line1), "FIELD %s ID:%u",
		login.field_mob_name[0] ? login.field_mob_name : "?",
		login.client_id);
	Text(px, width, height, 8, 8, line1, 2, 230, 230, 240);

	const int wx = zoneOriginX + static_cast<int>(view.local_x * 2.f);
	const int wy = zoneOriginY + static_cast<int>(view.local_y * 2.f);
	char line2[96];
	std::snprintf(line2, sizeof(line2), "POS %d,%d  ZONE %02d%02d  MOBS %d  WASD",
		wx, wy, map.zone_x, map.zone_y, world.count);
	Text(px, width, height, 8, 28, line2, 1, 180, 200, 180);

	view.dirty = false;
	view.image.error.clear();
	return true;
}

void WYD_FieldViewDestroy(WYDFieldView& view)
{
	view.image = {};
	view.dirty = true;
}
