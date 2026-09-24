#pragma once

// Compat Win32 mínimo para compilar TMProject748 nativo (WYD_LINUX).
// Não é Wine: só tipos/APIs que o client realmente toca.

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <dlfcn.h>

using namespace std::chrono_literals;
using errno_t = int;

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct SDL_Window;

using BYTE = unsigned char;
using BOOLEAN = BYTE;
using CHAR = char;
using WCHAR = char16_t;
using SHORT = int16_t;
using USHORT = uint16_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using LONG = int32_t;
using ULONG = uint32_t;
using LONGLONG = int64_t;
using ULONGLONG = uint64_t;
using INT = int;
using UINT = unsigned;
using FLOAT = float;
using BOOL = int;
using HRESULT = int32_t;
using HANDLE = void*;
using HINSTANCE = void*;
using HMODULE = void*;
using HICON = void*;
using HCURSOR = void*;
using HBRUSH = void*;
using HMENU = void*;
using HDC = void*;
using HFONT = void*;
using HPALETTE = void*;
using HWND = SDL_Window*;
using HWAVEOUT = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPSTR = char*;
using LPCSTR = const char*;
using LPWSTR = char16_t*;
using LPCWSTR = const char16_t*;
using LPTSTR = char*;
using LPCTSTR = const char*;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using UINT_PTR = uintptr_t;
using LONG_PTR = intptr_t;
using ULONG_PTR = uintptr_t;
using SIZE_T = size_t;
using DWORD_PTR = uintptr_t;
using COLORREF = DWORD;
using REFIID = const void*;
using REFCLSID = const void*;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef __stdcall
#define __stdcall
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef FORCEINLINE
#define FORCEINLINE inline __attribute__((always_inline))
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

#define DECLSPEC_SELECTANY __attribute__((weak))
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#define ZERO_MEMORY(p, n) std::memset((p), 0, (n))

using SSIZE_T = ssize_t;

struct GUID {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
};
using IID = GUID;
using CLSID = GUID;

struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct SIZE { LONG cx, cy; };
struct CRITICAL_SECTION {
	pthread_mutex_t mtx {};
	bool inited = false;
};

using LPCRITICAL_SECTION = CRITICAL_SECTION*;

inline void InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
	pthread_mutex_init(&cs->mtx, nullptr);
	cs->inited = true;
}
inline void DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
	if (cs->inited)
		pthread_mutex_destroy(&cs->mtx);
	cs->inited = false;
}
inline void EnterCriticalSection(LPCRITICAL_SECTION cs) { pthread_mutex_lock(&cs->mtx); }
inline void LeaveCriticalSection(LPCRITICAL_SECTION cs) { pthread_mutex_unlock(&cs->mtx); }

inline DWORD GetTickCount()
{
	using namespace std::chrono;
	return static_cast<DWORD>(
		duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline void Sleep(DWORD ms) { usleep(ms * 1000u); }
inline DWORD timeGetTime() { return GetTickCount(); }

inline int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
{
	std::fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "", text ? text : "");
	return 1;
}
inline int MessageBoxW(HWND h, LPCWSTR, LPCWSTR, UINT) { return MessageBoxA(h, "(wchar)", "", 0); }
inline HWND FindWindowA(LPCSTR, LPCSTR) { return nullptr; }
inline HWND FindWindowW(LPCWSTR, LPCWSTR) { return nullptr; }
#define FindWindow FindWindowA

inline char* GetCommandLineA()
{
	static char buf[16] = "";
	return buf;
}
#define GetCommandLine GetCommandLineA

inline void GetKeyboardLayoutNameA(char* out)
{
	if (out)
		std::strcpy(out, "00000409");
}
#define GetKeyboardLayoutName GetKeyboardLayoutNameA

inline int wcscmp(const char16_t* a, const char16_t* b)
{
	while (*a && *a == *b) { ++a; ++b; }
	return static_cast<int>(*a - *b);
}

inline HMODULE LoadLibraryA(LPCSTR) { return nullptr; }
inline void* GetProcAddress(HMODULE, LPCSTR) { return nullptr; }
inline BOOL FreeLibrary(HMODULE) { return TRUE; }
inline HINSTANCE GetModuleHandleA(LPCSTR) { return nullptr; }
#define GetModuleHandle GetModuleHandleA

inline void OutputDebugStringA(LPCSTR s) { if (s) std::fputs(s, stderr); }
inline DWORD GetCurrentDirectoryA(DWORD n, LPSTR buf)
{
	if (!getcwd(buf, n))
		return 0;
	return static_cast<DWORD>(std::strlen(buf));
}
inline BOOL SetCurrentDirectoryA(LPCSTR p) { return chdir(p) == 0; }
inline DWORD GetFileAttributesA(LPCSTR p)
{
	struct stat st {};
	if (stat(p, &st) != 0)
		return static_cast<DWORD>(-1);
	return S_ISDIR(st.st_mode) ? 0x10u : 0x80u;
}
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10

inline int _open(const char* path, int flags, int mode = 0644)
{
	int fl = 0;
	if (flags & 0x8000) fl |= O_BINARY; // ignored
	(void)mode;
	if ((flags & 0x1) == 0)
		fl |= O_RDONLY;
	else if (flags & 0x1)
		fl |= O_RDWR;
	return ::open(path, fl, 0644);
}
#ifndef _O_BINARY
#define _O_BINARY 0
#define _O_RDONLY 0
#define _O_RDWR 2
#define _O_CREAT 0x100
#define _O_TRUNC 0x200
#endif
inline int _close(int fd) { return ::close(fd); }
inline long _filelength(int fd)
{
	struct stat st {};
	if (fstat(fd, &st) != 0)
		return -1;
	return static_cast<long>(st.st_size);
}
inline int _read(int fd, void* buf, unsigned n) { return static_cast<int>(::read(fd, buf, n)); }
inline int _write(int fd, const void* buf, unsigned n) { return static_cast<int>(::write(fd, buf, n)); }
inline long _lseek(int fd, long off, int whence) { return static_cast<long>(::lseek(fd, off, whence)); }
inline int _access(const char* p, int) { return access(p, F_OK); }

inline errno_t memcpy_s(void* d, size_t ds, const void* s, size_t n)
{
	if (!d || !s || n > ds)
		return 1;
	std::memcpy(d, s, n);
	return 0;
}
inline errno_t strcpy_s(char* d, size_t n, const char* s)
{
	if (!d || !s || std::strlen(s) >= n)
		return 1;
	std::strcpy(d, s);
	return 0;
}
inline errno_t strcat_s(char* d, size_t n, const char* s)
{
	if (!d || !s)
		return 1;
	if (std::strlen(d) + std::strlen(s) >= n)
		return 1;
	std::strcat(d, s);
	return 0;
}
inline errno_t sprintf_s(char* d, size_t n, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const int r = vsnprintf(d, n, fmt, ap);
	va_end(ap);
	return r < 0 ? 1 : 0;
}
inline int fopen_s(FILE** fp, const char* path, const char* mode)
{
	*fp = std::fopen(path, mode);
	return *fp ? 0 : 1;
}
inline int freopen_s(FILE** fp, const char* path, const char* mode, FILE*)
{
	*fp = std::freopen(path, mode, stdout);
	return *fp ? 0 : 1;
}

inline void ZeroMemory(void* p, size_t n) { std::memset(p, 0, n); }
inline void CopyMemory(void* d, const void* s, size_t n) { std::memcpy(d, s, n); }
inline void FillMemory(void* p, size_t n, int v) { std::memset(p, v, n); }

inline BOOL QueryPerformanceCounter(LONGLONG* c)
{
	using namespace std::chrono;
	*c = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
	return TRUE;
}
inline BOOL QueryPerformanceFrequency(LONGLONG* f)
{
	*f = 1000000000LL;
	return TRUE;
}

inline void* ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT) { return nullptr; }
#define ShellExecute ShellExecuteA

inline void DisableSysKey() {}
inline void AllocConsole() {}
inline BOOL CheckOS_Unused_Removed() { return FALSE; }

#ifndef SAFE_DELETE
#define SAFE_DELETE(p) do { delete (p); (p)=nullptr; } while(0)
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if(p){ (p)->Release(); (p)=nullptr; } } while(0)
#endif

// HRESULT helpers
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif
#ifndef S_OK
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define D3D_OK S_OK
#endif

struct WAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; };

// Stub DirectShow / DirectSound symbols as opaque.
struct IDirectSound;
struct IGraphBuilder;
#ifndef _T
#define _T(x) x
#define TEXT(x) x
#endif
#ifndef TCHAR
using TCHAR = char;
#endif
