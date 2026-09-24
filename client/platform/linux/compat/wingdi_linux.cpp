// GDI Linux — rasterização de fonte real via stb_truetype + DIB em memória.
#include "wingdi.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <iconv.h>
#include <cerrno>
#include <cstdio>

namespace {

struct FontObj {
	std::vector<unsigned char> ttf;
	stbtt_fontinfo info {};
	float pixelHeight = 16.f;
	float scale = 1.f;
	int ascent = 0;
};

struct DibObj {
	int w = 0;
	int h = 0;
	std::vector<uint32_t> pixels; // A8R8G8B8
};

struct DcObj {
	DibObj* dib = nullptr;
	FontObj* font = nullptr;
	COLORREF textColor = 0x00FFFFFF;
	COLORREF bkColor = 0;
	int bkMode = OPAQUE;
	HWND hwnd = nullptr;
};

std::map<HDC, std::unique_ptr<DcObj>> g_dcs;
std::map<HFONT, std::unique_ptr<FontObj>> g_fonts;
std::map<HBITMAP, std::unique_ptr<DibObj>> g_dibs;

DcObj* DC(HDC h) { return h ? reinterpret_cast<DcObj*>(h) : nullptr; }

// Client 7.48: strings de jogo em CP949; labels mobile (PT-BR) em UTF-8.
static bool IsValidUtf8(const char* s, int nbytes)
{
	int i = 0;
	while (i < nbytes)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		int need = 0;
		if (c <= 0x7F)
			need = 1;
		else if ((c & 0xE0) == 0xC0)
			need = 2;
		else if ((c & 0xF0) == 0xE0)
			need = 3;
		else if ((c & 0xF8) == 0xF0)
			need = 4;
		else
			return false;
		if (i + need > nbytes)
			return false;
		for (int k = 1; k < need; ++k)
		{
			if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
				return false;
		}
		i += need;
	}
	return true;
}

static bool Utf8HasMultibyte(const char* s, int nbytes)
{
	for (int i = 0; i < nbytes; ++i)
	{
		if (static_cast<unsigned char>(s[i]) >= 0x80)
			return true;
	}
	return false;
}

static std::vector<int> DecodeUtf8ToCodepoints(const char* s, int nbytes)
{
	std::vector<int> out;
	int i = 0;
	while (i < nbytes)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		int cp = 0;
		int need = 1;
		if (c <= 0x7F)
		{
			cp = c;
			need = 1;
		}
		else if ((c & 0xE0) == 0xC0 && i + 1 < nbytes)
		{
			cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
			need = 2;
		}
		else if ((c & 0xF0) == 0xE0 && i + 2 < nbytes)
		{
			cp = ((c & 0x0F) << 12) |
				((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
				(static_cast<unsigned char>(s[i + 2]) & 0x3F);
			need = 3;
		}
		else if ((c & 0xF8) == 0xF0 && i + 3 < nbytes)
		{
			cp = ((c & 0x07) << 18) |
				((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
				((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
				(static_cast<unsigned char>(s[i + 3]) & 0x3F);
			need = 4;
		}
		else
		{
			cp = c;
			need = 1;
		}
		if (cp != 0)
			out.push_back(cp);
		i += need;
	}
	return out;
}

std::vector<int> DecodeUiStringToCodepoints(const char* s, int nbytes)
{
	std::vector<int> out;
	if (!s || nbytes <= 0)
		return out;

	// Labels PT-BR (UTF-8 com acentos) vs strings de item/NPC (CP949).
	// Só preferir UTF-8 se houver codepoints Latin/Hangul reais após decode —
	// evita CP949 Hangul mal interpretado como UTF-8.
	if (IsValidUtf8(s, nbytes) && Utf8HasMultibyte(s, nbytes))
	{
		auto utf = DecodeUtf8ToCodepoints(s, nbytes);
		for (int cp : utf)
		{
			if ((cp >= 0x00C0 && cp <= 0x024F) ||
				(cp >= 0x1E00 && cp <= 0x1EFF) ||
				(cp >= 0xAC00 && cp <= 0xD7AF) ||
				(cp >= 0x3040 && cp <= 0x30FF))
			{
				return utf;
			}
		}
	}

#if defined(__SWITCH__)
	// SWITCH PORT: devkitPro não fornece libiconv p/ Switch (só 3ds-libiconv).
	// O fast-path UTF-8 acima cobre os labels PT-BR; sem CP949 os bytes altos
	// caem em Latin-1 — exatamente o fallback do Linux quando iconv_open falha.
	if (IsValidUtf8(s, nbytes))
		return DecodeUtf8ToCodepoints(s, nbytes);
	for (int i = 0; i < nbytes; ++i)
		out.push_back(static_cast<unsigned char>(s[i]));
	return out;
#else
	iconv_t cd = iconv_open("UTF-32LE", "CP949");
	if (cd == reinterpret_cast<iconv_t>(-1))
		cd = iconv_open("UTF-32LE", "EUC-KR");
	if (cd == reinterpret_cast<iconv_t>(-1)) {
		for (int i = 0; i < nbytes; ++i)
			out.push_back(static_cast<unsigned char>(s[i]));
		return out;
	}

	std::vector<char> inbuf(s, s + nbytes);
	size_t inleft = static_cast<size_t>(nbytes);
	char* inptr = inbuf.data();
	std::vector<char> outbuf(static_cast<size_t>(nbytes) * 4 + 16);
	size_t outleft = outbuf.size();
	char* outptr = outbuf.data();
	if (iconv(cd, &inptr, &inleft, &outptr, &outleft) == static_cast<size_t>(-1)) {
		iconv_close(cd);
		if (IsValidUtf8(s, nbytes))
			return DecodeUtf8ToCodepoints(s, nbytes);
		for (int i = 0; i < nbytes; ++i)
			out.push_back(static_cast<unsigned char>(s[i]));
		return out;
	}
	iconv_close(cd);

	const size_t produced = outbuf.size() - outleft;
	for (size_t i = 0; i + 3 < produced; i += 4) {
		const int cp = static_cast<unsigned char>(outbuf[i]) |
			(static_cast<unsigned char>(outbuf[i + 1]) << 8) |
			(static_cast<unsigned char>(outbuf[i + 2]) << 16) |
			(static_cast<unsigned char>(outbuf[i + 3]) << 24);
		if (cp != 0)
			out.push_back(cp);
	}
	return out;
#endif // __SWITCH__
}

std::string FindFontFile(const char* face, int weight)
{
	std::vector<std::string> candidates;
	const int wantBold = (weight >= 600) ? 1 : 0;

	// Switch / assets do client: TTF do jogo (sem /usr/share no homebrew).
	candidates.push_back("UI/NanumBarunGothic.ttf");
	candidates.push_back("UI/FontNanum.ttf");
	candidates.push_back("fonts/NanumBarunGothic.ttf");
	candidates.push_back("fonts/FontNanum.ttf");
	candidates.push_back("fonts/DejaVuSans.ttf");
	candidates.push_back("./UI/NanumBarunGothic.ttf");
	candidates.push_back("./fonts/NanumBarunGothic.ttf");
	candidates.push_back("./fonts/DejaVuSans.ttf");
	candidates.push_back("sdmc:/switch/client748/UI/NanumBarunGothic.ttf");
	candidates.push_back("sdmc:/switch/client748/UI/FontNanum.ttf");
	candidates.push_back("sdmc:/switch/client748/fonts/NanumBarunGothic.ttf");
	candidates.push_back("sdmc:/switch/client748/fonts/DejaVuSans.ttf");
	candidates.push_back("sdmc:/switch/client748/fonts/FontNanum.ttf");

	if (face && face[0]) {
		candidates.push_back(face);
		const std::string f = face;
		if (f.find("Noto") != std::string::npos || f.find("noto") != std::string::npos) {
			if (wantBold) {
				candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf");
				candidates.push_back("/usr/share/fonts/noto/NotoSans-Bold.ttf");
				candidates.push_back("fonts/NotoSans-Bold.ttf");
				candidates.push_back("/game/fonts/NotoSans-Bold.ttf");
			}
			candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
			candidates.push_back("/usr/share/fonts/noto/NotoSans-Regular.ttf");
		}
		if (f.find("Nanum") != std::string::npos || f.find("nanum") != std::string::npos) {
			candidates.push_back("UI/NanumBarunGothic.ttf");
			candidates.push_back("UI/FontNanum.ttf");
			candidates.push_back("/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf");
			candidates.push_back("/usr/share/fonts/truetype/nanum/NanumGothic.ttf");
			candidates.push_back("/usr/share/fonts/truetype/nanum/NanumBarunGothic.ttf");
		}
		if (f.find("DejaVu") != std::string::npos) {
			if (wantBold)
				candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
			candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
		}
	}

	// Preferir Noto/DejaVu (Latina + acentos PT) antes de Nanum (Hangul fino no Latin).
	if (wantBold) {
		candidates.push_back("fonts/NotoSans-Bold.ttf");
		candidates.push_back("/game/fonts/NotoSans-Bold.ttf");
		candidates.push_back("./fonts/NotoSans-Bold.ttf");
		candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf");
		candidates.push_back("/usr/share/fonts/noto/NotoSans-Bold.ttf");
		candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
		candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf");
		candidates.push_back("/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf");
	}
	candidates.push_back("fonts/NotoSans-Regular.ttf");
	candidates.push_back("fonts/DroidSans.ttf");
	candidates.push_back("/game/fonts/NotoSans-Regular.ttf");
	candidates.push_back("/game/fonts/DroidSans.ttf");
	candidates.push_back("./fonts/NotoSans-Regular.ttf");
	candidates.push_back("./fonts/DroidSans.ttf");
	candidates.push_back("/usr/share/fonts/noto/NotoSans-Regular.ttf");
	candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
	candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
	candidates.push_back("/usr/share/fonts/droid/DroidSans.ttf");
	candidates.push_back("/usr/share/fonts/truetype/nanum/NanumGothic.ttf");
	candidates.push_back("/usr/share/fonts/truetype/nanum/NanumBarunGothic.ttf");
	candidates.push_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
	candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");

	for (const auto& c : candidates) {
		if (c.empty())
			continue;
		std::ifstream f(c, std::ios::binary);
		if (f)
			return c;
	}
	return {};
}

bool InitStbFont(FontObj& font, const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	font.ttf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	if (font.ttf.empty())
		return false;
	const int offset = stbtt_GetFontOffsetForIndex(font.ttf.data(), 0);
	if (offset < 0)
		return false;
	return stbtt_InitFont(&font.info, font.ttf.data(), offset) != 0;
}

} // namespace

HDC GetDC(HWND hWnd)
{
	auto dc = std::make_unique<DcObj>();
	dc->hwnd = hWnd;
	HDC h = reinterpret_cast<HDC>(dc.get());
	g_dcs[h] = std::move(dc);
	return h;
}

int ReleaseDC(HWND, HDC hdc)
{
	g_dcs.erase(hdc);
	return 1;
}

HDC CreateDCA(LPCSTR, LPCSTR, LPCSTR, const void*)
{
	return GetDC(nullptr);
}

BOOL DeleteDC(HDC hdc)
{
	g_dcs.erase(hdc);
	return TRUE;
}

int GetDeviceCaps(HDC, int index)
{
	int w = 1920, h = 1080, refresh = 60;
#if defined(WYD_SDL3)
	int count = 0;
	SDL_DisplayID* ids = SDL_GetDisplays(&count);
	if (ids && count > 0)
	{
		const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[0]);
		if (mode)
		{
			w = mode->w;
			h = mode->h;
			refresh = (int)mode->refresh_rate;
			if (refresh <= 0)
				refresh = 60;
		}
		SDL_free(ids);
	}
#else
	SDL_DisplayMode mode {};
	if (SDL_GetDesktopDisplayMode(0, &mode) != 0) {
		mode.w = 1920;
		mode.h = 1080;
		mode.refresh_rate = 60;
	}
	w = mode.w;
	h = mode.h;
	refresh = mode.refresh_rate > 0 ? mode.refresh_rate : 60;
#endif
	switch (index) {
	case HORZRES: return w;
	case VERTRES: return h;
	case BITSPIXEL: return 32;
	case VREFRESH: return refresh;
	default: return 0;
	}
}

BOOL SetDeviceGammaRamp(HDC hdc, LPVOID ramp)
{
#if defined(WYD_SDL3)
	(void)hdc;
	(void)ramp;
	return TRUE; // API removida no SDL3
#else
	auto* dc = DC(hdc);
	if (!dc || !dc->hwnd || !ramp)
		return FALSE;
	auto* win = reinterpret_cast<SDL_Window*>(dc->hwnd);
	auto* r = static_cast<uint16_t(*)[256]>(ramp);
	return SDL_SetWindowGammaRamp(win, r[0], r[1], r[2]) == 0 ? TRUE : FALSE;
#endif
}

BOOL EnumDisplaySettingsA(LPCSTR, DWORD modeNum, DEVMODEA* devMode)
{
	if (!devMode)
		return FALSE;
#if defined(WYD_SDL3)
	if (!SDL_WasInit(SDL_INIT_VIDEO))
		SDL_InitSubSystem(SDL_INIT_VIDEO);
	int count = 0;
	SDL_DisplayID* ids = SDL_GetDisplays(&count);
	if (!ids || count <= 0)
		return FALSE;
	const SDL_DisplayID disp = ids[0];

	auto fillFromMode = [&](const SDL_DisplayMode* mode) -> BOOL {
		if (!mode)
			return FALSE;
		std::memset(devMode, 0, sizeof(*devMode));
		devMode->dmSize = sizeof(DEVMODEA);
		devMode->dmPelsWidth = mode->w;
		devMode->dmPelsHeight = mode->h;
		devMode->dmBitsPerPel = 32;
		devMode->dmDisplayFrequency = mode->refresh_rate > 0 ? (DWORD)mode->refresh_rate : 60;
		devMode->dmFields = 0xDC0000;
		return TRUE;
	};

	// Win32: ENUM_CURRENT_SETTINGS (-1) / ENUM_REGISTRY_SETTINGS (-2).
	if (modeNum == static_cast<DWORD>(-1) || modeNum == static_cast<DWORD>(-2))
	{
		const SDL_DisplayMode* cur = SDL_GetCurrentDisplayMode(disp);
		if (!cur)
			cur = SDL_GetDesktopDisplayMode(disp);
		const BOOL ok = fillFromMode(cur);
		SDL_free(ids);
		return ok;
	}

	int nmodes = 0;
	const SDL_DisplayMode* const* modes = SDL_GetFullscreenDisplayModes(disp, &nmodes);
	SDL_free(ids);
	if (!modes || modeNum >= static_cast<DWORD>(nmodes))
		return FALSE;
	return fillFromMode(modes[modeNum]);
#else
	const int n = SDL_GetNumDisplayModes(0);
	if (static_cast<int>(modeNum) >= n)
		return FALSE;
	SDL_DisplayMode mode {};
	if (SDL_GetDisplayMode(0, static_cast<int>(modeNum), &mode) != 0)
		return FALSE;
	std::memset(devMode, 0, sizeof(*devMode));
	devMode->dmSize = sizeof(DEVMODEA);
	devMode->dmPelsWidth = mode.w;
	devMode->dmPelsHeight = mode.h;
	devMode->dmBitsPerPel = 32;
	devMode->dmDisplayFrequency = mode.refresh_rate > 0 ? mode.refresh_rate : 60;
	devMode->dmFields = 0xDC0000;
	return TRUE;
#endif
}

LONG ChangeDisplaySettingsA(DEVMODEA* dm, DWORD)
{
	if (!dm)
		return 0;
	// Wayland: resolução via SDL_Window — aplicada quando NewApp cria a janela.
	return 0; // DISP_CHANGE_SUCCESSFUL
}

HDC CreateCompatibleDC(HDC)
{
	return GetDC(nullptr);
}

HBITMAP CreateDIBSection(HDC, const BITMAPINFO* pbmi, UINT, void** ppvBits, HANDLE, DWORD)
{
	if (!pbmi || !ppvBits)
		return nullptr;
	auto dib = std::make_unique<DibObj>();
	dib->w = pbmi->bmiHeader.biWidth;
	dib->h = std::abs(pbmi->bmiHeader.biHeight);
	if (dib->w <= 0 || dib->h <= 0)
		return nullptr;
	dib->pixels.assign(static_cast<size_t>(dib->w * dib->h), 0);
	*ppvBits = dib->pixels.data();
	HBITMAP h = reinterpret_cast<HBITMAP>(dib.get());
	g_dibs[h] = std::move(dib);
	return h;
}

HFONT CreateFontA(int cHeight, int, int, int, int fnWeight, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR pszFaceName)
{
	auto font = std::make_unique<FontObj>();
	std::string path = FindFontFile(pszFaceName, fnWeight);
	if (path.empty())
		path = FindFontFile(nullptr, fnWeight);
	if (path.empty() || !InitStbFont(*font, path)) {
		std::fprintf(stderr,
			"[WYDLINUX] CreateFontA falhou (face=%s weight=%d). Sem TTF o texto fica invisível.\n",
			pszFaceName ? pszFaceName : "(null)", fnWeight);
		if (FILE* df = ::fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab")) {
			std::fprintf(df, "[FONT] CreateFontA FALHOU face=%s — coloque UI/NanumBarunGothic.ttf no client748\n",
				pszFaceName ? pszFaceName : "(null)");
			std::fclose(df);
		}
		return nullptr;
	}
	std::fprintf(stderr, "[WYDLINUX] fonte UI: %s @ %dpx weight=%d\n", path.c_str(),
		std::abs(cHeight) > 0 ? std::abs(cHeight) : 16, fnWeight);
	if (FILE* df = ::fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab")) {
		std::fprintf(df, "[FONT] ok %s @ %dpx\n", path.c_str(),
			std::abs(cHeight) > 0 ? std::abs(cHeight) : 16);
		std::fclose(df);
	}
	font->pixelHeight = static_cast<float>(std::abs(cHeight) > 0 ? std::abs(cHeight) : 16);
	font->scale = stbtt_ScaleForPixelHeight(&font->info, font->pixelHeight);
	int ascent = 0, descent = 0, lineGap = 0;
	stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
	font->ascent = static_cast<int>(ascent * font->scale);
	HFONT h = reinterpret_cast<HFONT>(font.get());
	g_fonts[h] = std::move(font);
	return h;
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj)
{
	auto* dc = DC(hdc);
	if (!dc)
		return nullptr;
	if (g_dibs.count(reinterpret_cast<HBITMAP>(obj))) {
		HGDIOBJ prev = reinterpret_cast<HGDIOBJ>(dc->dib);
		dc->dib = reinterpret_cast<DibObj*>(obj);
		return prev;
	}
	if (g_fonts.count(reinterpret_cast<HFONT>(obj))) {
		HGDIOBJ prev = reinterpret_cast<HGDIOBJ>(dc->font);
		dc->font = reinterpret_cast<FontObj*>(obj);
		return prev;
	}
	return nullptr;
}

BOOL DeleteObject(HGDIOBJ obj)
{
	if (g_dibs.erase(reinterpret_cast<HBITMAP>(obj)))
		return TRUE;
	if (g_fonts.erase(reinterpret_cast<HFONT>(obj)))
		return TRUE;
	return FALSE;
}

COLORREF SetTextColor(HDC hdc, COLORREF color)
{
	auto* dc = DC(hdc);
	if (!dc)
		return 0;
	const COLORREF prev = dc->textColor;
	dc->textColor = color;
	return prev;
}

COLORREF SetBkColor(HDC hdc, COLORREF color)
{
	auto* dc = DC(hdc);
	if (!dc)
		return 0;
	const COLORREF prev = dc->bkColor;
	dc->bkColor = color;
	return prev;
}

int SetBkMode(HDC hdc, int mode)
{
	auto* dc = DC(hdc);
	if (!dc)
		return 0;
	const int prev = dc->bkMode;
	dc->bkMode = mode;
	return prev;
}

BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
{
	auto* dc = DC(hdc);
	if (!dc || !dc->font || !psizl)
		return FALSE;
	if (c < 0)
		c = static_cast<int>(std::strlen(lpString ? lpString : ""));
	const auto cps = DecodeUiStringToCodepoints(lpString, c);
	float x = 0;
	for (int code : cps) {
		int adv = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&dc->font->info, code, &adv, &lsb);
		// Leve tracking extra evita colisão visual em UI touch.
		x += adv * dc->font->scale + 0.6f;
	}
	psizl->cx = static_cast<LONG>(std::ceil(x));
	psizl->cy = static_cast<LONG>(dc->font->pixelHeight);
	return TRUE;
}

BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
	auto* dc = DC(hdc);
	if (!dc || !dc->dib || !dc->font || !lpString)
		return FALSE;
	if (c < 0)
		c = static_cast<int>(std::strlen(lpString));

	const uint8_t tr = (dc->textColor >> 16) & 0xFF;
	const uint8_t tg = (dc->textColor >> 8) & 0xFF;
	const uint8_t tb = dc->textColor & 0xFF;

	float penX = static_cast<float>(x);
	const float baseline = static_cast<float>(y + dc->font->ascent);
	const auto cps = DecodeUiStringToCodepoints(lpString, c);

	for (int code : cps) {
		int ax = 0, lsb = 0;
		stbtt_GetCodepointHMetrics(&dc->font->info, code, &ax, &lsb);
		int x0, y0, x1, y1;
		stbtt_GetCodepointBitmapBox(&dc->font->info, code, dc->font->scale, dc->font->scale, &x0, &y0, &x1, &y1);
		const int gw = x1 - x0;
		const int gh = y1 - y0;
		if (gw > 0 && gh > 0) {
			std::vector<unsigned char> bitmap(static_cast<size_t>(gw * gh));
			stbtt_MakeCodepointBitmap(&dc->font->info, bitmap.data(), gw, gh, gw, dc->font->scale, dc->font->scale, code);
			const int dstX = static_cast<int>(penX) + x0;
			const int dstY = static_cast<int>(baseline) + y0;
			for (int py = 0; py < gh; ++py) {
				for (int px = 0; px < gw; ++px) {
					const int dx = dstX + px;
					const int dy = dstY + py;
					if (dx < 0 || dy < 0 || dx >= dc->dib->w || dy >= dc->dib->h)
						continue;
					const uint8_t a = bitmap[static_cast<size_t>(py * gw + px)];
					if (!a)
						continue;
					uint32_t& pix = dc->dib->pixels[static_cast<size_t>(dy * dc->dib->w + dx)];
					const uint8_t cov = a;
					const uint8_t r = static_cast<uint8_t>((tr * cov) / 255);
					const uint8_t g = static_cast<uint8_t>((tg * cov) / 255);
					const uint8_t b = static_cast<uint8_t>((tb * cov) / 255);
					pix = (cov << 24) | (r << 16) | (g << 8) | b;
				}
			}
		}
		penX += ax * dc->font->scale + 0.6f;
	}
	return TRUE;
}

int FillRect(HDC hdc, const RECT* lprc, HBRUSH)
{
	auto* dc = DC(hdc);
	if (!dc || !dc->dib || !lprc)
		return 0;
	const int x0 = std::max<LONG>(0, lprc->left);
	const int y0 = std::max<LONG>(0, lprc->top);
	const int x1 = std::min<LONG>(dc->dib->w, lprc->right);
	const int y1 = std::min<LONG>(dc->dib->h, lprc->bottom);
	const uint32_t color = dc->bkColor | 0xFF000000u;
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
			dc->dib->pixels[static_cast<size_t>(y * dc->dib->w + x)] = color;
	return 1;
}
