#pragma once

// Tipos e macros Win32 mínimos para compilar o client WYD em Linux.
// Não implementa o SDK Windows; só o suficiente para wire, sockets e stubs.
// Nunca incluir este header no source Win32 original.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>

#ifndef WYD_LINUX
#define WYD_LINUX 1
#endif

using BYTE = unsigned char;
using UCHAR = unsigned char;
using CHAR = char;
using WORD = unsigned short;
using USHORT = unsigned short;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using INT = int;
using LONG = std::int32_t;
using ULONG = std::uint32_t;
using LONGLONG = std::int64_t;
using ULONGLONG = std::uint64_t;
using BOOL = int;
using HRESULT = std::int32_t;
using HWND = void*;
using HINSTANCE = void*;
using HMODULE = void*;
using HDC = void*;
using HICON = void*;
using HCURSOR = void*;
using HBRUSH = void*;
using HMENU = void*;
using HANDLE = void*;
using HPALETTE = void*;
using HBITMAP = void*;
using HFONT = void*;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using UINT_PTR = std::uintptr_t;
using DWORD_PTR = std::uintptr_t;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using LPTSTR = char*;
using LPCTSTR = const char*;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL nullptr
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif

#ifndef S_OK
#define S_OK static_cast<HRESULT>(0)
#endif
#ifndef E_FAIL
#define E_FAIL static_cast<HRESULT>(0x80004005L)
#endif
#ifndef FAILED
#define FAILED(hr) (static_cast<HRESULT>(hr) < 0)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b) \
	static_cast<WORD>(((static_cast<BYTE>(a)) & 0xff) | ((static_cast<WORD>((static_cast<BYTE>(b)) & 0xff)) << 8))
#endif
#ifndef LOWORD
#define LOWORD(l) static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xffff)
#endif
#ifndef HIWORD
#define HIWORD(l) static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xfffF)
#endif

#ifndef ZeroMemory
#define ZeroMemory(Destination, Length) std::memset((Destination), 0, (Length))
#endif
#ifndef CopyMemory
#define CopyMemory(Destination, Source, Length) std::memcpy((Destination), (Source), (Length))
#endif
#ifndef FillMemory
#define FillMemory(Destination, Length, Fill) std::memset((Destination), (Fill), (Length))
#endif

// MSVC sprintf_s(buf, fmt, ...) → snprintf com sizeof(buf) quando buf é array.
#ifndef sprintf_s
#define sprintf_s(buf, ...) std::snprintf((buf), sizeof(buf), __VA_ARGS__)
#endif

// MSVC %I64d is long long. On glibc `%I64d` is parsed as `%I` + width-64 `d`,
// which right-pads into a 64-char field and truncates in 64-byte buffers —
// UI score fields then look empty (Str/Int/HP/etc.).
#ifndef WYD_FMT_I64
#define WYD_FMT_I64 "%lld"
#endif
#ifndef WYD_FMT_U64
#define WYD_FMT_U64 "%llu"
#endif

// NULL legado usado em SOCKET/unsigned int no CPSock.
#ifdef NULL
#undef NULL
#endif
#define NULL 0

// Comparações de string case-insensitive estilo MSVC.
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strcmpi
#define strcmpi strcasecmp
#endif

inline int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, unsigned)
{
	std::fprintf(stderr, "[%s] %s\n", caption ? caption : "WYD", text ? text : "");
	return 1;
}

inline char* GetCommandLineA()
{
	static char empty[] = "";
	return empty;
}

#ifndef GetCommandLine
#define GetCommandLine GetCommandLineA
#endif

inline DWORD GetTickCount()
{
	// Relógio monotônico em ms; suficiente para timers de UI/rede do client.
	struct timespec ts {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<DWORD>(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}

inline void Sleep(DWORD ms)
{
	usleep(static_cast<useconds_t>(ms) * 1000u);
}

inline int WideCharToMultiByte(unsigned, unsigned long, LPCWSTR src, int cch,
	LPSTR dst, int cb, LPCSTR, int*)
{
	if (!src || !dst || cb <= 0)
		return 0;
	int n = 0;
	for (int i = 0; (cch < 0 || i < cch) && src[i] && n + 1 < cb; ++i)
		dst[n++] = static_cast<char>(src[i] & 0xff);
	dst[n] = 0;
	return n;
}
