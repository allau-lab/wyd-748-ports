# Arquitetura WYDLINUX

```text
WYDLINUX/
  docs/                    ← esta documentação
  PORT.md                  playbook 7.60+ / regras 7.48
  AGENTS.md                guardas para agents
  DXVK.md                  fases/smoke DXVK
  CMakeLists.txt
  cmake/
    tmproject_sources.cmake
    tmproject_includes.cmake
    aarch64-linux-gnu.cmake
  cmd/
    linux_client_main.cpp  main → setenv DXVK_WSI_DRIVER=SDL2 → wWinMain
  platform/linux/
    compat/                Win32/D3DX/GDI/user32 + touch
      win32_extras.h       CRT, paths, ShellExecute, fopen wrapper
      winuser_sdl.cpp      HWND=SDL_Window*, fullscreen desktop, fila WM_*
      EventTranslator_linux.cpp
      TouchControlsUI.*    HUD stick/ATK/skills/câmera
      wingdi_linux.cpp     fonte TTF→DIB (CP949)
      d3dx9_linux.cpp      math + DDS/TGA/BMP + sprite
      linux_pch.h          PCH (stdio antes do #define fopen)
      socket_poll_linux.*  async net sem WSAAsyncSelect
    wyt_decode.* / wys_decode.*
    d3d9_dxvk_native.*     helper smoke DXVK
  TMProject748/            cópia adaptada do client (não o original)
  client748/               assets + binários
  assets/touch_icons/      TGA → POST_BUILD em client748/touch_icons/
  third_party/
    dxvk-native/           x86_64
    dxvk-native-aarch64/   ARM64
    minimp3/ stb_*.h
  scripts/                 build, fetch DXVK, settings, audit-diff
```

## Targets CMake relevantes

| Target | Função |
|---|---|
| `wyd_client` | Client real → `client748/project` ou `WYD Arm64` |
| `dxvk_native_smoke` | Gate CreateDevice + shaders |
| `wyd_project` / smokes | Demo/legado; **não** substituem o client |

Flags típicas:

```bash
cmake -S . -B build -DWYD_BUILD_REAL_CLIENT=ON -DWYD_BUILD_SMOKE_PROJECT=OFF
cmake --build build --target wyd_client -j"$(nproc)"
```

### POST_BUILD / nomes de binário

| Condição | OUTPUT / destino |
|---|---|
| Host x86_64, não-cross | `client748/project` |
| Host aarch64 nativo | OUTPUT_NAME `WYD Arm64` → `client748/WYD Arm64` + `touch_icons/` |
| Cross-compile aarch64 | idem `WYD Arm64` + `touch_icons/`; script docker também copia `project-aarch64` |

`WYD_DXVK_NATIVE_ROOT` (ou detecção por `CMAKE_SYSTEM_PROCESSOR`) escolhe
`third_party/dxvk-native` vs `dxvk-native-aarch64`.

## Fluxo runtime

```text
main (linux_client_main)
  → setenv DXVK_WSI_DRIVER=SDL2
  → wWinMain / NewApp
      → lê config.txt (LOL_CONTROLS, TOUCH_UI, WINDOW, …)
      → CreateWindowEx (SDL_WINDOW_VULKAN; FULLSCREEN_DESKTOP se WS_POPUP)
      → InitDevice → RenderDevice → D3DDevice
          → Direct3DCreate9 / CreateDevice(SDL_Window*)
          → TextureManager / TMFont2
      → cenas (SelServer / Login / SelChar / Field)
      → SControlContainer: TouchControlsUI::FrameMove se g_bTouchUi
```

## Camadas proibidas de stub

Tudo que o TMProject chama no caminho login→campo precisa de backend real em
`platform/linux/compat/` ou equivalente. Ver PORT.md.
