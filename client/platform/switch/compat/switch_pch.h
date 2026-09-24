#pragma once

// PCH Nintendo Switch — devkitA64 (aarch64-none-elf / newlib) + libnx.
//
// Espelha o linux_pch.h: o cliente (TMProject) sempre inclui "pch.h" e este é o
// ramo do alvo Switch (ver TMProject/pch.h → WYD_SWITCH).
//
// O que muda em relação ao Linux:
//   Linux  → backend D3D9 = DXVK Native (third_party/dxvk-native*) sobre Vulkan
//   Switch → backend D3D9 = d3d9_gl_bridge (GLES3/EGL Mesa switch-portlibs) sobre
//            libnx, em platform/switch/
//
// Fase C (SWITCH_PORT.md): aqui entram SÓ as declarações que o cliente de
// lógica/rede/cena precisa (Win32 shim + COM/D3D9 + D3DX). O ramo de janela/GDI
// (wingdi.h/winuser_*) e o de rede específica (iphlpapi/ifaddrs) ficam para os
// TUs que os usam, com variantes Switch em platform/switch/compat/.
//
// REGRA do PORT.md vale aqui: nada de stub silencioso. O que ainda não existe no
// Switch falha em COMPILE-TIME nesta cadeia (a sonda scripts/switch-probe.sh
// aponta o TU e o header exato), nunca em runtime no console.

// ATENÇÃO (NS-03): este ramo exige -std=gnu++17. Em modo estrito (-std=c++17) o
// g++ define __STRICT_ANSI__ e o newlib esconde a API POSIX/BSD que o cliente e
// o shim Win32 usam (usleep, realpath, strcasecmp, O_CLOEXEC...). O Makefile do
// .nro e a sonda já usam gnu++17; não trocar para c++17.

// stdio/cstdlib/string ANTES do shim Win32: o win32_extras redefine fopen e
// espera os headers de libc já resolvidos.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <utility>

// Win32 mínimo + D3D9/D3DX. O include path da sonda coloca
// platform/switch/compat ANTES de platform/linux/compat, então um shim
// específico de Switch (mesmo nome de arquivo) tem precedência — é assim que
// winuser_switch.h / wingdi_switch.h / iphlpapi_switch.h substituem os do Linux
// sem tocar no código compartilhado.
#include "win32_extras.h"

// wingdi/winuser_* fornecem a superfície GDI+janela (DIB, TTF, VK_*, PostMessage,
// CreateWindowEx) que o cliente usa. No Switch o shim compartilhado continua
// valendo porque o switch-sdl2 (portlib) implementa SDL_Window/eventos/mixer —
// é exatamente o caminho reservado na fase C do SWITCH_PORT.md.
#include "wingdi.h"
#include "winuser_sdl.h"
#include "iphlpapi.h"

#include <d3d9.h>

#include <d3dx9math.h>
#include <d3dx9tex.h>
#include <d3dx9core.h>

// D3DXMATRIXA16: o d3dx9math do DXVK (Wine) não define; o do MSVC define.
// Mesmo typedef do linux_pch.h — TMProject/CMesh.cpp:628 usa em locals.
// Guard _D3DXMATRIXA16_DEFINED p/ não redefinir se um dia o header MSVC voltar.
#ifndef _D3DXMATRIXA16_DEFINED
typedef D3DXMATRIX D3DXMATRIXA16;
#define _D3DXMATRIXA16_DEFINED
#endif

// TMProject-Free: SharedStructs traz o envelope de pacotes (o mesmo do Win32).
#include "SharedStructs.h"

using namespace std::chrono_literals;
