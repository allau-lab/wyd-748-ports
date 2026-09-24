#pragma once
#include "win32_extras.h"

// Tipos suficientes para ClientDiagnostics compilar; MiniDump no Linux
// não é carregado (LoadLibraryA falha) — o log de crash continua real.
#ifndef MINIDUMP_TYPE_DEFINED
#define MINIDUMP_TYPE_DEFINED
enum MINIDUMP_TYPE {
	MiniDumpNormal = 0x00000000,
	MiniDumpWithDataSegs = 0x00000001,
};
struct MINIDUMP_EXCEPTION_INFORMATION {
	DWORD ThreadId {};
	void* ExceptionPointers {};
	BOOL ClientPointers {};
};
struct MINIDUMP_USER_STREAM_INFORMATION;
struct MINIDUMP_CALLBACK_INFORMATION;
#endif
