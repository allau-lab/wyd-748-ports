#pragma once
// WinInet → libcurl (implementação em wininet_linux.cpp).
#include "win32_extras.h"

using HINTERNET = HANDLE;

#ifndef INTERNET_OPEN_TYPE_PRECONFIG
#define INTERNET_OPEN_TYPE_PRECONFIG 0
#define INTERNET_FLAG_RELOAD 0x80000000
#define INTERNET_FLAG_NO_CACHE_WRITE 0x04000000
#endif

HINTERNET InternetOpenA(LPCSTR agent, DWORD accessType, LPCSTR proxy, LPCSTR proxyBypass, DWORD flags);
#define InternetOpen InternetOpenA
HINTERNET InternetOpenUrlA(HINTERNET session, LPCSTR url, LPCSTR headers, DWORD headersLen, DWORD flags, DWORD_PTR context);
#define InternetOpenUrl InternetOpenUrlA
BOOL InternetReadFile(HINTERNET file, LPVOID buffer, DWORD numberOfBytesToRead, LPDWORD numberOfBytesRead);
BOOL InternetCloseHandle(HINTERNET handle);
BOOL InternetGetConnectedState(LPDWORD lpdwFlags, DWORD dwReserved);
