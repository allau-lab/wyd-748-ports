#pragma once
// Macros/APIs Win32 em volume — só o necessário para compilar TMProject no GCC.
#include <cctype>
#include <cstdlib>
#include <ctime>

#ifndef __int8
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long
#endif

#ifndef WPARAM
using WPARAM = UINT_PTR;
using LPARAM = LONG_PTR;
using LRESULT = LONG_PTR;
#endif
using HGLOBAL = HANDLE;

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef sscanf_s
#define sscanf_s sscanf
#ifndef fscanf_s
#define fscanf_s fscanf
#endif
#endif
#ifndef lstrcpy
#define lstrcpy strcpy
#define lstrcat strcat
#define lstrlen strlen
#endif
#ifndef _tcscpy
#define _tcscpy strcpy
#define _tcscat strcat
#define _tcslen strlen
#define _stprintf sprintf
#define _tcsicmp strcasecmp
#endif

inline char* _strupr(char* s)
{
	if (!s)
		return s;
	for (char* p = s; *p; ++p)
		*p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
	return s;
}

#ifndef MB_OK
#define MB_OK 0
#define MB_ICONERROR 0x10
#define MB_ICONWARNING 0x30
#define MB_SYSTEMMODAL 0x1000
#endif
#ifndef WM_CLOSE
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_SIZE 0x0005
#define WM_PAINT 0x000F
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MOUSEMOVE 0x0200
#define WM_MOUSEWHEEL 0x020A
#endif
#ifndef GWL_STYLE
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define SWP_SHOWWINDOW 0x0040
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define HWND_NOTOPMOST ((HWND)(intptr_t)(-2))
#define HWND_TOPMOST ((HWND)(intptr_t)(-1))
#define HWND_TOP ((HWND)0)
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2
#endif
#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) ((HRESULT)(x) <= 0 ? (HRESULT)(x) : (HRESULT)(((x)&0xFFFF)|0x80070000))
#endif
#ifndef _tcsncat
#define _tcsncat strncat
#endif
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#ifndef GPTR
#define GPTR 0x0040
#endif
#ifndef D3D9b_SDK_VERSION
#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 32
#endif
#define D3D9b_SDK_VERSION D3D_SDK_VERSION
#endif

struct BITMAPINFOHEADER {
	DWORD biSize {};
	LONG biWidth {};
	LONG biHeight {};
	WORD biPlanes {};
	WORD biBitCount {};
	DWORD biCompression {};
	DWORD biSizeImage {};
	LONG biXPelsPerMeter {};
	LONG biYPelsPerMeter {};
	DWORD biClrUsed {};
	DWORD biClrImportant {};
};
struct RGBQUAD {
	BYTE rgbBlue {}, rgbGreen {}, rgbRed {}, rgbReserved {};
};
struct BITMAPINFO {
	BITMAPINFOHEADER bmiHeader {};
	RGBQUAD bmiColors[1] {};
};

struct DEVMODEA {
	char dmDeviceName[32] {};
	WORD dmSize {};
	DWORD dmFields {};
	DWORD dmPelsWidth {};
	DWORD dmPelsHeight {};
	DWORD dmBitsPerPel {};
	DWORD dmDisplayFrequency {};
};
using DEVMODE = DEVMODEA;
using PDEVMODE = DEVMODE*;
using LPDEVMODE = DEVMODE*;

struct SYSTEMTIME {
	WORD wYear {}, wMonth {}, wDayOfWeek {}, wDay {};
	WORD wHour {}, wMinute {}, wSecond {}, wMilliseconds {};
};
using _SYSTEMTIME = SYSTEMTIME;
using LPSYSTEMTIME = SYSTEMTIME*;

struct OSVERSIONINFOEX {
	DWORD dwOSVersionInfoSize {};
	DWORD dwMajorVersion {};
	DWORD dwMinorVersion {};
	DWORD dwBuildNumber {};
	DWORD dwPlatformId {};
	char szCSDVersion[128] {};
	WORD wServicePackMajor {};
	WORD wServicePackMinor {};
	WORD wSuiteMask {};
	BYTE wProductType {};
	BYTE wReserved {};
};
using DWORDLONG = ULONGLONG;
#ifndef VER_MAJORVERSION
#define VER_MAJORVERSION 0x2
#define VER_MINORVERSION 0x1
#define VER_GREATER_EQUAL 3
#define VER_SET_CONDITION(mask, type, op) ((void)(mask))
#endif

inline LONG GetWindowLongA(HWND, int) { return 0; }
inline LONG SetWindowLongA(HWND, int, LONG) { return 0; }
#define GetWindowLong GetWindowLongA
#define SetWindowLong SetWindowLongA
BOOL GetWindowRect(HWND, LPRECT r); // winuser_sdl.cpp — tamanho real da janela
BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
inline LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
#define SendMessage SendMessageA

inline DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD n)
{
	const char* v = name ? std::getenv(name) : nullptr;
	if (!v)
		return 0;
	if (!buf || !n)
		return static_cast<DWORD>(std::strlen(v) + 1);
	std::strncpy(buf, v, n);
	buf[n - 1] = 0;
	return static_cast<DWORD>(std::strlen(buf));
}
inline BOOL VerifyVersionInfoA(OSVERSIONINFOEX*, DWORD, DWORDLONG) { return TRUE; }
#define VerifyVersionInfo VerifyVersionInfoA
inline void GetLocalTime(LPSYSTEMTIME st)
{
	if (!st)
		return;
	*st = {};
	const std::time_t now = std::time(nullptr);
	struct tm tm {};
	localtime_r(&now, &tm);
	st->wYear = static_cast<WORD>(tm.tm_year + 1900);
	st->wMonth = static_cast<WORD>(tm.tm_mon + 1);
	st->wDayOfWeek = static_cast<WORD>(tm.tm_wday);
	st->wDay = static_cast<WORD>(tm.tm_mday);
	st->wHour = static_cast<WORD>(tm.tm_hour);
	st->wMinute = static_cast<WORD>(tm.tm_min);
	st->wSecond = static_cast<WORD>(tm.tm_sec);
	st->wMilliseconds = 0;
}
inline HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes)
{
	void* p = std::calloc(1, bytes ? bytes : 1);
	(void)flags;
	return reinterpret_cast<HGLOBAL>(p);
}
inline void* GlobalLock(HGLOBAL h) { return reinterpret_cast<void*>(h); }
inline BOOL GlobalUnlock(HGLOBAL) { return TRUE; }
inline HGLOBAL GlobalFree(HGLOBAL h)
{
	std::free(reinterpret_cast<void*>(h));
	return nullptr;
}
#ifndef GMEM_FIXED
#define GMEM_FIXED 0
#define GMEM_ZEROINIT 0x40
#endif

using HIMC = HANDLE;
struct CANDIDATELIST {
	DWORD dwSize {};
	DWORD dwStyle {};
	DWORD dwCount {};
	DWORD dwSelection {};
	DWORD dwPageStart {};
	DWORD dwPageSize {};
	DWORD dwOffset[1] {};
};
using LPCANDIDATELIST = CANDIDATELIST*;
inline HIMC ImmGetContext(HWND) { return nullptr; }
inline BOOL ImmReleaseContext(HWND, HIMC) { return TRUE; }
inline BOOL ImmGetConversionStatus(HIMC, LPDWORD, LPDWORD) { return FALSE; }
inline BOOL ImmSetConversionStatus(HIMC, DWORD, DWORD) { return FALSE; }
inline BOOL ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
inline BOOL ImmGetOpenStatus(HIMC) { return FALSE; }
inline LONG ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD) { return 0; }
#define ImmGetCompositionString ImmGetCompositionStringA
inline HIMC ImmAssociateContext(HWND, HIMC) { return nullptr; }
inline HIMC ImmCreateContext() { return nullptr; }
inline BOOL ImmDestroyContext(HIMC) { return TRUE; }
inline HWND ImmGetDefaultIMEWnd(HWND) { return nullptr; }
inline DWORD ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
#define ImmGetCandidateList ImmGetCandidateListA
inline int GetWindowTextA(HWND, LPSTR buf, int n)
{
	if (buf && n > 0)
		buf[0] = 0;
	return 0;
}
#define GetWindowText GetWindowTextA
BOOL GetClientRect(HWND, LPRECT r); // winuser_sdl.cpp — tamanho real da janela
inline BOOL SetMenu(HWND, HMENU) { return TRUE; }
inline HMENU GetMenu(HWND) { return nullptr; }
// PostMessage real está em winuser_sdl.cpp
inline int _snprintf_s(char* d, size_t n, size_t, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int r = vsnprintf(d, n, fmt, ap);
	va_end(ap);
	return r;
}
#ifndef GCS_COMPSTR
#define GCS_COMPSTR 0x0008
#define GCS_RESULTSTR 0x0800
#define GCS_COMPATTR 0x0010
#endif
