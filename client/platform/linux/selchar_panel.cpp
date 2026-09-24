#include "selchar_panel.h"

#include <cstdio>
#include <cstring>
#include <cstdint>

namespace
{
	constexpr int kGlyphW = 5;
	constexpr int kGlyphH = 7;

	// 5 bits por linha, 7 linhas — índice ASCII 32..90
	using Glyph = unsigned char[7];

	const Glyph* LookupGlyph(char c)
	{
		static const Glyph blank = {0, 0, 0, 0, 0, 0, 0};
		static const Glyph g0 = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
		static const Glyph g1 = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
		static const Glyph g2 = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
		static const Glyph g3 = {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E};
		static const Glyph g4 = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
		static const Glyph g5 = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
		static const Glyph g6 = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
		static const Glyph g7 = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
		static const Glyph g8 = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
		static const Glyph g9 = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
		static const Glyph gA = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
		static const Glyph gB = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
		static const Glyph gC = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
		static const Glyph gD = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
		static const Glyph gE = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
		static const Glyph gF = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
		static const Glyph gG = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
		static const Glyph gH = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
		static const Glyph gI = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
		static const Glyph gJ = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C};
		static const Glyph gK = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
		static const Glyph gL = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
		static const Glyph gM = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
		static const Glyph gN = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
		static const Glyph gO = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
		static const Glyph gP = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
		static const Glyph gQ = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
		static const Glyph gR = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
		static const Glyph gS = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
		static const Glyph gT = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
		static const Glyph gU = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
		static const Glyph gV = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
		static const Glyph gW = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
		static const Glyph gX = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
		static const Glyph gY = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
		static const Glyph gZ = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
		static const Glyph gDash = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
		static const Glyph gColon = {0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00};
		static const Glyph gDot = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00};
		static const Glyph gLB = {0x06, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06};
		static const Glyph gRB = {0x0C, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0C};
		static const Glyph gUnd = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};

		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		switch (c)
		{
		case '0': return &g0; case '1': return &g1; case '2': return &g2;
		case '3': return &g3; case '4': return &g4; case '5': return &g5;
		case '6': return &g6; case '7': return &g7; case '8': return &g8;
		case '9': return &g9;
		case 'A': return &gA; case 'B': return &gB; case 'C': return &gC;
		case 'D': return &gD; case 'E': return &gE; case 'F': return &gF;
		case 'G': return &gG; case 'H': return &gH; case 'I': return &gI;
		case 'J': return &gJ; case 'K': return &gK; case 'L': return &gL;
		case 'M': return &gM; case 'N': return &gN; case 'O': return &gO;
		case 'P': return &gP; case 'Q': return &gQ; case 'R': return &gR;
		case 'S': return &gS; case 'T': return &gT; case 'U': return &gU;
		case 'V': return &gV; case 'W': return &gW; case 'X': return &gX;
		case 'Y': return &gY; case 'Z': return &gZ;
		case '-': return &gDash; case ':': return &gColon; case '.': return &gDot;
		case '[': return &gLB; case ']': return &gRB; case '_': return &gUnd;
		case ' ': return &blank;
		default: return &gDot;
		}
	}

	void PutPixel(std::uint8_t* rgba, uint32_t w, uint32_t h,
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

	void FillRect(std::uint8_t* rgba, uint32_t w, uint32_t h,
		int x0, int y0, int x1, int y1,
		std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				PutPixel(rgba, w, h, x, y, r, g, b);
	}

	void DrawText(std::uint8_t* rgba, uint32_t w, uint32_t h,
		int x, int y, const char* text, int scale,
		std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		for (int i = 0; text[i]; ++i)
		{
			const Glyph* gl = LookupGlyph(text[i]);
			for (int row = 0; row < kGlyphH; ++row)
			{
				const unsigned char bits = (*gl)[row];
				for (int col = 0; col < kGlyphW; ++col)
				{
					if (((bits >> (4 - col)) & 1) == 0)
						continue;
					for (int dy = 0; dy < scale; ++dy)
						for (int dx = 0; dx < scale; ++dx)
							PutPixel(rgba, w, h,
								x + i * (kGlyphW + 1) * scale + col * scale + dx,
								y + row * scale + dy, r, g, b);
				}
			}
		}
	}

	void SlotRect(uint32_t width, uint32_t height, int slot,
		int& x0, int& y0, int& x1, int& y1)
	{
		const int margin = 24;
		const int gap = 16;
		const int top = 72;
		const int usableW = static_cast<int>(width) - margin * 2 - gap * 3;
		const int cardW = usableW / 4;
		const int cardH = static_cast<int>(height) - top - margin;
		x0 = margin + slot * (cardW + gap);
		y0 = top;
		x1 = x0 + cardW;
		y1 = y0 + cardH;
	}
}

int WYD_SelCharHitTest(uint32_t width, uint32_t height, int x, int y)
{
	for (int i = 0; i < 4; ++i)
	{
		int x0, y0, x1, y1;
		SlotRect(width, height, i, x0, y0, x1, y1);
		if (x >= x0 && x < x1 && y >= y0 && y < y1)
			return i;
	}
	return -1;
}

bool WYD_SelCharPanelBuild(WYDSelCharPanel& panel, const WYDLoginSession& login,
	uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return false;

	panel.image.width = width;
	panel.image.height = height;
	panel.image.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
	std::uint8_t* px = panel.image.rgba.data();

	FillRect(px, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height),
		18, 22, 32);

	DrawText(px, width, height, 24, 24, "SELECT CHARACTER  1-4 OR CLICK", 2, 220, 220, 230);

	char acct[48];
	std::snprintf(acct, sizeof(acct), "ACCOUNT:%s",
		login.account_name[0] ? login.account_name : "?");
	DrawText(px, width, height, 24, 52, acct, 1, 160, 170, 190);

	for (int i = 0; i < 4; ++i)
	{
		int x0, y0, x1, y1;
		SlotRect(width, height, i, x0, y0, x1, y1);

		const bool occ = login.slots[i].occupied;
		const bool sel = (panel.selected == i);
		const bool hov = (panel.hover == i);

		std::uint8_t r = occ ? 40 : 28;
		std::uint8_t g = occ ? 55 : 32;
		std::uint8_t b = occ ? 78 : 40;
		if (hov) { r = 55; g = 75; b = 105; }
		if (sel) { r = 70; g = 110; b = 70; }
		if (login.field_entered && sel) { r = 90; g = 140; b = 90; }

		FillRect(px, width, height, x0, y0, x1, y1, r, g, b);
		FillRect(px, width, height, x0, y0, x1, y0 + 2, 200, 200, 210);
		FillRect(px, width, height, x0, y1 - 2, x1, y1, 200, 200, 210);

		char label[8];
		std::snprintf(label, sizeof(label), "[%d]", i + 1);
		DrawText(px, width, height, x0 + 12, y0 + 16, label, 2, 230, 230, 240);

		if (occ)
		{
			DrawText(px, width, height, x0 + 12, y0 + 56, login.slots[i].name, 2, 245, 245, 250);
			char lvl[32];
			std::snprintf(lvl, sizeof(lvl), "LV %u", login.slots[i].level);
			DrawText(px, width, height, x0 + 12, y0 + 90, lvl, 2, 180, 210, 180);
		}
		else
		{
			DrawText(px, width, height, x0 + 12, y0 + 56, "EMPTY", 2, 120, 120, 130);
		}
	}

	if (login.field_entered)
	{
		char line[96];
		std::snprintf(line, sizeof(line), "FIELD OK ID=%u POS %d,%d %s",
			login.client_id, login.pos_x, login.pos_y,
			login.field_mob_name[0] ? login.field_mob_name : "");
		DrawText(px, width, height, 24, static_cast<int>(height) - 36, line, 2, 120, 230, 140);
	}
	else if (login.charlogin_sent)
	{
		DrawText(px, width, height, 24, static_cast<int>(height) - 36,
			"WAITING 0x114 ...", 2, 230, 200, 120);
	}

	panel.dirty = false;
	panel.image.error.clear();
	return true;
}

void WYD_SelCharPanelDestroy(WYDSelCharPanel& panel)
{
	panel.image = {};
	panel.hover = -1;
	panel.selected = -1;
	panel.dirty = true;
}
