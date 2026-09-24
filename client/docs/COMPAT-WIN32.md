# Compat Win32 → Linux

Arquivos centrais: `platform/linux/compat/win32_extras.h`, `winuser_sdl.cpp`,
`wingdi_linux.cpp`, `EventTranslator_linux.cpp`, `TouchControlsUI.*`.

**Atenção:** `winuser_sdl.cpp` e o EventTranslator Linux concentram patches do
port (fullscreen desktop, mouse livre, TEXTINPUT, ícone, touch). Não
sobrescrever com stubs Win32 ao mergear source remoto — ver [PORT.md](../PORT.md).

## Paths

`WYD_NormalizePath`:

1. Troca `\` por `/`
2. Se o path não existe, resolve **componente a componente** com `readdir` + `strcasecmp` (ex.: `Mesh/` → `mesh/`)

Macros/wrappers que devem usar normalize:

- `fopen` → `Wyd_fopen` (libc capturado **antes** do `#define`)
- `fopen_s`, `_open`, `_access`, `_stat64i32`, `CopyFileA`, `ShellExecuteA`

**PCH:** incluir `<cstdio>` antes de `win32_extras.h`, senão `std::fopen` vira `std::Wyd_fopen`.

## Janela / mensagens

- `RegisterClass` / `CreateWindowEx` / `PeekMessage` / `DispatchMessage`
- `HWND` = `SDL_Window*`
- Flags: `SDL_WINDOW_VULKAN` obrigatório para surface DXVK
- `WS_POPUP` → `SDL_WINDOW_FULLSCREEN_DESKTOP` (modo desktop do compositor)
- Ícone da janela: `wyd_exe_icon.inc` via `ApplyWydExeWindowIcon`

## GDI (fonte)

- `CreateCompatibleDC`, `CreateDIBSection` (32bpp top-down se `biHeight < 0`)
- `CreateFontA` → stb_truetype; prioriza **NanumGothic** (Hangul) depois Noto CJK / DejaVu
- Strings do client 7.48 estão em **CP949**: `TextOut` / `GetTextExtent` convertem via `iconv(CP949→UTF-32)` antes de rasterizar
- `TMFont2` lê canal B (`& 0xFF`) como cobertura

## Mouse (livre como browser)

- **Nunca** relative mode / window grab permanentes
- `SoftCaptureMouse`: mantém ponteiro livre; `ClipCursor` ≈ no-op útil
- UI usa coordenadas absolutas (`motion.x/y`); `xrel/yrel` só alimentam delta de câmera

## Touch HUD + fallthrough

- `g_bTouchUi` / `TouchControlsUI` (default ON em aarch64; override `[TOUCH_UI]`)
- Touch ligado força `[LOL_CONTROLS]` (cast/move separados)
- Contrato de hit-test:
  - `OnPointerDown/Move/Up` → **1** = evento consumido pelo HUD (não despachar WM_*)
  - **0** = fallthrough para o pipeline normal (UI Win32 / movimento no campo)
- Dedos SDL (`SDL_FINGER*`) e mouse compartilham o mesmo HUD
- Ícones: `assets/touch_icons/*.tga` → `client748/touch_icons/` (POST_BUILD ARM)

## CRT MSVC

- `sscanf_s` / `sprintf_s` / `strcpy_s` / `memcpy_s`
- Em Linux, call sites de `sscanf_s` com size args **devem** virar `sscanf` (senão segfault em endereço pequeno)
- `_open` / `_read` / `_filelength` / `_O_BINARY`

## ShellExecute

- URL `http(s):` / `mailto:` → `xdg-open`
- Arquivo: só se existir e (opcionalmente) executável
- Missing `Change.exe` = no-op (evitar browser abrindo lixo)

## Rede async

- `socket_poll_linux` emula o papel de `WSAAsyncSelect` (senão trava em “Now Connecting…”).
