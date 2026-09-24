#pragma once

// d3d9.h do alvo Nintendo Switch — wrapper para switch_pch.h / win32_extras.h.
//
// Diferença em relação ao platform/linux/compat/d3d9.h: aqui a ordem de busca é
// **aarch64 primeiro** (o alvo é aarch64-none-elf) e o x86_64 fica como fallback
// só para leitura/inspeção no host. Em Linux/desktop a ordem é a inversa porque
// o binário do cliente roda no host.
//
// IMPORTANTE (mesma regra do Linux): os caminhos são RELATIVOS a partir DESTE
// arquivo, e o windows_base.h entra UMA única vez por esta cadeia — misturar com
// -I de outra árvore dxvk gera caminhos físicos diferentes e redefinition de
// GUID/POINT/RECT (ver docs/ERRORS-FIXED.md L-27).
//
// Na fase C o bridge (platform/switch/d3d9_gl_bridge.cpp) fornece a
// IMPLEMENTAÇÃO destas interfaces; os headers aqui são só declaração (o
// cliente compila contra a mesma superfície D3D9 que usa no Windows/Linux).

#if defined(__SWITCH__)
// SWITCH PORT (mesmo contrato do platform/linux/compat/d3d9.h): parsear a
// libnx ANTES do windows_base.h — o DXVK faz `#define interface struct` (COM)
// e a libnx usa `interface` como NOME DE PARÂMETRO (hid.h:1008, usb_comms.h).
// Incluindo <switch.h> aqui (antes do macro), toda inclusão posterior de
// switch.h (windows.h, iphlpapi.h…) vira no-op pelo include guard — o macro
// sobrevive para os d3dx9*.h do próprio dxvk. NÃO desfazer o macro aqui:
// TUs com ordem invertida (win32_extras antes de d3d9, ex. D3DEnumeration)
// ficariam com o macro morto e os d3dx9 quebram.
#include <switch.h>
#endif

#if __has_include("../../../third_party/dxvk-native-aarch64/usr/include/dxvk/d3d9.h")
#include "../../../third_party/dxvk-native-aarch64/usr/include/dxvk/windows_base.h"
#include "../../../third_party/dxvk-native-aarch64/usr/include/dxvk/unknwn.h"
#include "../../../third_party/dxvk-native-aarch64/usr/include/dxvk/d3d9.h"
#elif __has_include("../../../third_party/dxvk-native/usr/include/dxvk/d3d9.h")
#include "../../../third_party/dxvk-native/usr/include/dxvk/windows_base.h"
#include "../../../third_party/dxvk-native/usr/include/dxvk/unknwn.h"
#include "../../../third_party/dxvk-native/usr/include/dxvk/d3d9.h"
#else
#error "DXVK Native d3d9.h not found under third_party/ (port Switch, fase C)"
#endif
