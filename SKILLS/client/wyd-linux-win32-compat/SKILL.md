---
name: wyd-linux-win32-compat
description: >-
  Camada Win32→Linux do client WYD (platform/linux/compat): paths case-insensitive,
  CRT MSVC, janela SDL/Wayland, GDI/CP949, rede async sem WSAAsyncSelect, ShellExecute
  seguro, touch fallthrough e LoL controls. Usar ao implementar ou depurar shims
  Win32 no port Linux, crashes em fopen/sscanf_s, “Now Connecting…”, mouse preso,
  digtação morta, ou sync que apagou patches da compat.
disable-model-invocation: false
---

# Compat Win32 → Linux (client WYD)

Companion de `wyd-linux-client-port`. Código novo fica em `platform/linux/compat/`
(ou equivalente). No jogo: só `#ifdef WYD_LINUX` pontuais.

## Paths

`NormalizePath` obrigatório:

1. `\` → `/`
2. Se não existe: resolver **componente a componente** com `readdir` +
   `strcasecmp` (`Mesh`↔`mesh`)

Wrappers que devem normalizar: `fopen` / `fopen_s` / `_open` / `_stat*` /
`CopyFileA` / `ShellExecuteA`.

**PCH:** incluir `<cstdio>` **antes** de qualquer `#define fopen`, senão
`std::fopen` vira wrapper quebrado.

`GetModuleFileNameA` ← `/proc/self/exe`; cwd do processo = pasta dos assets.

## CRT MSVC

- Call sites `sscanf_s` **com sizes** → `sscanf` (segfault ~0x20 se não).
- Mapear `sprintf_s` / `strcpy_s` / `memcpy_s` / `_open` / `_O_BINARY` com cuidado.
- Não redefinir `__try` / `min` / `max` de forma que quebre `std::min`.

## Tipos / ABI (armadilha LP64)

No Linux `long` = **8** bytes; no MSVC x64 Win32 `LONG`/`DWORD` = 32-bit.

- Packets / structs de arquivo: usar tipos de largura fixa (`uint32_t`, etc.).
- `static_assert(sizeof…)` nos structs de wire.
- `__int64` → `long long`.
- Nunca “porque compilou” = ABI certo.

## Janela / mensagens

- `HWND` = `SDL_Window*`
- `CreateWindowEx`: `SDL_WINDOW_VULKAN | SHOWN`
- `WS_POPUP` → `SDL_WINDOW_FULLSCREEN_DESKTOP` (desktop do compositor)
- Fila: `SDL_PollEvent` → `PeekMessage` / `DispatchMessage`
- TEXTINPUT + TranslateMessage (senão login/chat mortos)
- Mouse **livre**: sem relative/grab permanente; `ClipCursor` ≈ no-op
- SDL3: `SDL_InitSubSystem` retorna **bool** — usar `!SDL_InitSubSystem(...)`,
  nunca `!= 0` no estilo SDL2

**Não regenerar** o arquivo de janela a partir de stubs Win32 após estável.

## GDI / texto

- DIB 32bpp; fonte via stb_truetype (NanumGothic → Noto CJK → DejaVu)
- Strings legado em **CP949**: iconv antes de rasterizar
- `TMFont2` lê cobertura no canal B
- `TextOut` com `OPAQUE`: preencher fundo da célula (senão texto sobreposto)
- `GetTextExtent`: `cy = lineHeight`, não bbox do glifo

## Rede

- CPSock sobre sockets BSD
- Emular `WSAAsyncSelect` com poll + post `WM_USER+…` no loop Run
- **Nunca** `#define socket` / símbolo que interponha a libc (hang no SDL)
- Emular `closesocket` / `ioctlsocket` / `WSAGetLastError` (`errno`)

## ShellExecute / WinInet

- Só `http(s)` / `mailto` ou arquivo **existente** → `xdg-open`
- Missing `Change.exe` = no-op
- `InternetOpenUrlA`: nunca `fprintf("%s", url)` sem validar printable http(s)
  (o client às vezes passa `&g_pServerList` — não é URL)

## Touch / LoL

- `OnPointer*` → **1 = consumiu**, **0 = fallthrough** para UI/mundo
- Touch ligado → forçar LoL (cast/move separados)
- Movimento do jogo = **click-to-move** (grade + pathfinding). Stick emite
  destinos no chão (~10 Hz), **não** inventa pacote vetorial
- Com modal/loja: HUD de combate não engole o centro da tela

## Áudio

- SFX: WAV via SDL (não DirectSound util)
- BGM: mp3 via minimp3 (DirectShow stub não serve)

## Exclusões CMake

Não linkar os `.cpp` Win32 originais substituídos (`dsutil`, `DirShow`,
`EventTranslator`, `TMVideoWnd`, `pch` MSVC). Usar os `*_linux.cpp`.
