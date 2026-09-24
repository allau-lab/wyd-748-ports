#pragma once

// PCH Linux: DXVK d3d9 + D3DX + Win32 + wire ABI.
// stdio ANTES de win32_extras (que redefine fopen para normalizar '\\').
// SDL é incluído por winuser_sdl.h (SDL2) ou winuser_sdl.h (SDL3) — não o
// inclua aqui diretamente para evitar contratos mistos SDL2/SDL3 no mesmo TU.
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

#include <d3d9.h>
#include "windows.h"
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
