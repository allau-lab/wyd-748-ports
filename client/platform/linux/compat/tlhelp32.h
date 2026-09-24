#pragma once
// Stub: toolhelp32 — sem snapshot de processos no Linux port.
#include "win32_extras.h"

#ifndef TH32CS_SNAPPROCESS
#define TH32CS_SNAPPROCESS 0x00000002
#endif

typedef struct tagPROCESSENTRY32 {
	DWORD dwSize;
	DWORD th32ProcessID;
	char szExeFile[MAX_PATH];
} PROCESSENTRY32, *LPPROCESSENTRY32;

inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
inline BOOL Process32First(HANDLE, LPPROCESSENTRY32) { return FALSE; }
inline BOOL Process32Next(HANDLE, LPPROCESSENTRY32) { return FALSE; }
