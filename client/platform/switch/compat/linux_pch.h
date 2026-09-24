#pragma once

// PCH Switch — linux_pch.h neste diretório tem precedência via -I.
// Ordem crítica (NS-14): <switch.h> antes de qualquer header DXVK/COM.

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <map>

// Aspas: força d3d9.h DESTE diretório (inclui switch.h de novo = no-op pelo guard,
// depois windows_base com #define interface).
#include "d3d9.h"
#include "win32_extras.h"
#include "wingdi.h"
#include "winuser_sdl.h"
#include "iphlpapi.h"

#include <d3dx9math.h>
#include <d3dx9tex.h>
#include <d3dx9core.h>

#include "MessageHeader.h"
#include "SharedStructs.h"

using namespace std::chrono_literals;
