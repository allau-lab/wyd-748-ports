#pragma once
// GDI real (TTF+DIB) — RenderDevice / TMFont2 / SControl.
#include "win32_extras.h"
#include "sdl_backend.h"

#ifndef HORZRES
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define VREFRESH 116
#define DIB_RGB_COLORS 0
#define TRANSPARENT 1
#define OPAQUE 2
#endif

#ifndef VK_BACK
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2E
#define VK_SPACE 0x20
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#endif

#ifndef WM_LBUTTONDBLCLK
#define WM_LBUTTONDBLCLK 0x0203
#endif

#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN 0x00000004
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#endif

using HGDIOBJ = HANDLE;
using COLORREF = DWORD;
using LPSIZE = SIZE*;

#ifndef TEXTMETRICA_DEFINED
#define TEXTMETRICA_DEFINED
struct TEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	char tmFirstChar;
	char tmLastChar;
	char tmDefaultChar;
	char tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
};
struct TEXTMETRICW {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	WCHAR tmFirstChar;
	WCHAR tmLastChar;
	WCHAR tmDefaultChar;
	WCHAR tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
};
using TEXTMETRIC = TEXTMETRICA;
#endif

HDC GetDC(HWND hWnd);
int ReleaseDC(HWND hWnd, HDC hdc);
HDC CreateDCA(LPCSTR, LPCSTR, LPCSTR, const void*);
#define CreateDC CreateDCA
BOOL DeleteDC(HDC hdc);
int GetDeviceCaps(HDC hdc, int index);
BOOL SetDeviceGammaRamp(HDC hdc, LPVOID ramp);
BOOL EnumDisplaySettingsA(LPCSTR deviceName, DWORD modeNum, DEVMODEA* devMode);
#define EnumDisplaySettings EnumDisplaySettingsA
LONG ChangeDisplaySettingsA(DEVMODEA* devMode, DWORD flags);
#define ChangeDisplaySettings ChangeDisplaySettingsA

HDC CreateCompatibleDC(HDC hdc);
HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO* pbmi, UINT usage, void** ppvBits, HANDLE, DWORD);
HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
	DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
	DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
#define CreateFont CreateFontA
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ obj);
BOOL DeleteObject(HGDIOBJ obj);
COLORREF SetTextColor(HDC hdc, COLORREF color);
COLORREF SetBkColor(HDC hdc, COLORREF color);
int SetBkMode(HDC hdc, int mode);
BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c);
#define TextOut TextOutA
BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
#define GetTextExtentPoint32 GetTextExtentPoint32A
int FillRect(HDC hdc, const RECT* lprc, HBRUSH hbr);

inline BOOL PtInRect(const RECT* r, POINT p)
{
	return r && p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}
inline BOOL IntersectRect(LPRECT dst, const RECT* a, const RECT* b)
{
	if (!dst || !a || !b)
		return FALSE;
	dst->left = a->left > b->left ? a->left : b->left;
	dst->top = a->top > b->top ? a->top : b->top;
	dst->right = a->right < b->right ? a->right : b->right;
	dst->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
	return dst->left < dst->right && dst->top < dst->bottom;
}

struct BITMAPFILEHEADER {
	WORD bfType;
	DWORD bfSize;
	WORD bfReserved1;
	WORD bfReserved2;
	DWORD bfOffBits;
};
#ifndef SRCCOPY
#define SRCCOPY 0x00CC0020
#endif
