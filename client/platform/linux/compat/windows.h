#pragma once
#ifndef _WINDOWS_
#define _WINDOWS_

#if defined(__SWITCH__)
// SWITCH PORT: parsear a libnx ANTES do windows_base.h — o DXVK define
// `interface` como `struct` e a libnx usa `interface` como nome de parâmetro.
#include <switch.h>
#endif

// Tipos Win32 base do DXVK — SEM win32_extras (evita ciclo: d3d9.h → windows.h → …).
#include <windows_base.h>

#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <limits.h>
#include <cstring>
#include <cstdio>
#include <cstdint>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef FARPROC
using FARPROC = void (*)();
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif
#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#endif
#ifndef CREATE_ALWAYS
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x80
#endif
#ifndef FILE_APPEND_DATA
#define FILE_APPEND_DATA 0x0004
#endif
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif
#ifndef KEY_READ
#define KEY_READ 0x20019
#endif
#ifndef HKEY_LOCAL_MACHINE
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002)
#endif
#ifndef REG_SZ
#define REG_SZ 1
#endif
#ifndef REG_BINARY
#define REG_BINARY 3
#endif
#ifndef REG_DWORD
#define REG_DWORD 4
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#endif

inline HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD, void*, DWORD disposition, DWORD, HANDLE)
{
	if (!path)
		return INVALID_HANDLE_VALUE;
	char npath[PATH_MAX] {};
	size_t i = 0;
	for (; path[i] && i + 1 < sizeof(npath); ++i)
		npath[i] = (path[i] == '\\') ? '/' : path[i];
	npath[i] = 0;
	int flags = O_CLOEXEC;
	mode_t mode = 0644;
	if ((access & GENERIC_WRITE) || (access & FILE_APPEND_DATA)) {
		flags |= O_WRONLY | O_CREAT;
		if (access & FILE_APPEND_DATA)
			flags |= O_APPEND;
		else if (disposition == CREATE_ALWAYS || disposition == CREATE_NEW)
			flags |= O_TRUNC;
	} else {
		flags |= O_RDONLY;
	}
	const int fd = ::open(npath, flags, mode);
	if (fd < 0)
		return INVALID_HANDLE_VALUE;
	return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd + 1));
}
#define CreateFile CreateFileA
inline BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, void*)
{
	if (h == INVALID_HANDLE_VALUE || !buf)
		return FALSE;
	const int fd = static_cast<int>(reinterpret_cast<intptr_t>(h)) - 1;
	// SWITCH PORT: um write() pode gravar menos que o pedido (SD/FAT no HOS).
	// O cliente confere apenas o BOOL → dado faltando em config/log/arquivos.
	const auto* p = static_cast<const unsigned char*>(buf);
	DWORD total = 0;
	while (total < n) {
		const ssize_t w = ::write(fd, p + total, n - total);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (w == 0)
			break;
		total += static_cast<DWORD>(w);
	}
	if (written)
		*written = total;
	return total == n || n == 0;
}
inline BOOL CloseHandle(HANDLE h)
{
	if (!h || h == INVALID_HANDLE_VALUE)
		return TRUE;
	const intptr_t v = reinterpret_cast<intptr_t>(h);
	if (v > 0 && v < 65536) {
		::close(static_cast<int>(v) - 1);
		return TRUE;
	}
	return TRUE;
}
inline DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
#if defined(__SWITCH__)
// SWITCH PORT: newlib/libnx define pthread_t como ponteiro (__pthread_t*).
inline DWORD GetCurrentThreadId() { return static_cast<DWORD>(reinterpret_cast<uintptr_t>(pthread_self())); }
#else
inline DWORD GetCurrentThreadId() { return static_cast<DWORD>(pthread_self()); }
#endif
inline HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(intptr_t(1)); }
inline HMODULE LoadLibraryA(LPCSTR) { return nullptr; }
#define LoadLibrary LoadLibraryA
inline BOOL FreeLibrary(HMODULE) { return TRUE; }
inline FARPROC GetProcAddress(HMODULE, LPCSTR) { return nullptr; }

inline LONG RegOpenKeyEx(HKEY, LPCSTR, DWORD, DWORD, HKEY*) { return 1; }
#define RegOpenKeyExA RegOpenKeyEx
inline LONG RegQueryValueEx(HKEY, LPCSTR, DWORD*, DWORD*, BYTE*, DWORD*) { return 1; }
#define RegQueryValueExA RegQueryValueEx
inline LONG RegCloseKey(HKEY) { return 0; }
inline LONG RegSetValueEx(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD) { return 1; }
#define RegSetValueExA RegSetValueEx
inline LONG RegCreateKeyEx(HKEY, LPCSTR, DWORD, LPSTR, DWORD, DWORD, void*, HKEY*, DWORD*) { return 1; }
#define RegCreateKeyExA RegCreateKeyEx

#endif // _WINDOWS_
