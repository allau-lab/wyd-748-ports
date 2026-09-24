#pragma once

// switch_types.h — subconjunto mínimo de tipos Win32 usados pelo bridge Switch.
//
// Fase A/B: o bridge só precisa de DWORD/HRESULT/S_OK (o caminho Linux/DXVK
// traz o resto em platform/linux/wincompat.h + d3d9_min_api.h — que dependem de
// SDL/DXVK e não entram no build do .nro).
//
// Fase C (CreateDevice/vtable D3D9 completa) este header é substituído pela
// inclusão da camada real (ver SWITCH_PORT.md § Roadmap).

#include <cstdint>
#include <cstddef>

#ifndef DWORD
using DWORD = uint32_t;
#endif
#ifndef ULONG
using ULONG = uint32_t;
#endif
#ifndef HRESULT
using HRESULT = int32_t;
#endif
#ifndef BOOL
using BOOL = int;
#endif

#ifndef S_OK
#define S_OK 0
#endif
#ifndef E_FAIL
#define E_FAIL 0x80004005
#endif
#ifndef D3D_OK
#define D3D_OK S_OK
#endif
