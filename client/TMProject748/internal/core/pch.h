#ifndef PCH_H
#define PCH_H

#ifdef WYD_LINUX
#include "linux_pch.h"
#else

// Cabeçalho pré-compilado compartilhado pela source Win32 do client.
#include "framework.h"

#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <time.h>

#define DIRECTINPUT_VERSION 0x0800

#pragma warning(push, 0)
#include <d3d9.h>
#include <d3dx9.h>
#include <Dshow.h>
#pragma warning(pop)

#include <iostream>
#include <fileapi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iphlpapi.h>
#include <chrono>
using namespace std::chrono_literals;

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "Strmiids.lib")

#include "SharedStructs.h"

#endif // WYD_LINUX

#endif // PCH_H
