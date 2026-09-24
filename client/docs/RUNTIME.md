# Runtime: build, env, sintomas

## Build e run (x86_64 — 7.48)

```bash
cd WYDLINUX
./scripts/fetch-dxvk-native.sh   # se necessário
cmake -S . -B build -DWYD_BUILD_REAL_CLIENT=ON -DWYD_BUILD_SMOKE_PROJECT=OFF
cmake --build build --target wyd_client -j"$(nproc)"
# POST_BUILD → client748/project

cd client748
export DXVK_WSI_DRIVER=SDL3
export SDL_VIDEODRIVER=wayland
export LD_LIBRARY_PATH="$PWD/../third_party/dxvk-native/usr/lib:${LD_LIBRARY_PATH:-}"
export VK_LOADER_LAYERS_DISABLE='*steam*'
./project
```

**ATENÇÃO — qual binário executar:**

- ✅ `client748/project` — binário SDL3 correto (install do build).
- ❌ `client748/wyd_client` — **era uma cópia antiga** que carregava
  `libdxvk_d3d9.so.0` 32-bit (`wrong ELF class: ELFCLASS32`) e não iniciava.
  Agora é symlink → `project`. Se copiar o client para outro lugar, leve o
  `project` **e** mantenha o RUNPATH (`readelf -d project | grep PATH`) ou
  exporte `LD_LIBRARY_PATH` apontando para `third_party/dxvk-native/usr/lib`.

Atalho: `./scripts/build.sh` (compila x86_64 e em seguida chama o build aarch64).

## Build ARM64

```bash
# Cross (Docker / “wyd-builder-arm64”) — não toca client748/project
./scripts/build-aarch64-docker.sh
# → third_party/dxvk-native-aarch64/
# → client748/WYD Arm64 e/ou project-aarch64

# Toolchain local (ou fallback para docker):
./scripts/build-aarch64.sh
```

Em host **aarch64 nativo**, o mesmo `wyd_client` usa OUTPUT_NAME `WYD Arm64` e
copia `assets/touch_icons/` → `client748/touch_icons/`.

Run ARM64 (no device):

```bash
cd client748
export DXVK_WSI_DRIVER=SDL2 SDL_VIDEODRIVER=wayland
export LD_LIBRARY_PATH="$PWD/../third_party/dxvk-native-aarch64/usr/lib:${LD_LIBRARY_PATH:-}"
export VK_LOADER_LAYERS_DISABLE='*steam*'
./"WYD Arm64"    # ou ./project-aarch64
```

## Env vars

| Variável | Função |
|---|---|
| `DXVK_WSI_DRIVER=SDL2` | HWND = SDL_Window* |
| `SDL_VIDEODRIVER=wayland` | alvo Wayland |
| `LD_LIBRARY_PATH` | encontra `libdxvk_d3d9.so` (prefix da arch certa) |
| `WYD_DXVK_NATIVE_ROOT` | override do prefix no CMake |
| `WYD_DXVK_LIBDIR` | smokes / override runtime |
| `WYD_D3D9_SO` | path explícito da .so |
| `VK_LOADER_LAYERS_DISABLE=*steam*` | evita layers Steam quebrando |
| `WYD_AARCH64_IMAGE` | imagem Docker do cross (default `ubuntu:24.04`) |
| `WYD_ALLOW_X11=1` | escape hatch CI (não é o alvo do port) |

## config.txt (relevantes ao Linux)

| Chave | Efeito |
|---|---|
| `[WINDOW] 0` | fullscreen (WS_POPUP → FULLSCREEN_DESKTOP) |
| `[WINDOW] 1` | janela |
| `[LOL_CONTROLS] 1` | move/cast estilo LoL |
| `[TOUCH_UI] 1` | liga `TouchControlsUI` (ARM default 1 se chave ausente) |

Com touch ligado o client força LoL controls. Settings GUI:
`./scripts/wyd-settings.sh`.

## Sintoma → causa (essencial)

| Sintoma | Causa provável | Fix |
|---|---|---|
| Segfault imediato / addr ~0x20 | `sscanf_s` com sizes MSVC | `sscanf` nos call sites Linux |
| Browser abre `change.exe` | ShellExecute→xdg-open cego | só URL ou file existente |
| Initialize Data Failed | path `\` / cwd | normalize + asset root |
| Initialize Render Failed + surface OOM | sem `SDL_WINDOW_VULKAN` / WSI | flags janela + env SDL2 |
| Initialize Render Failed pós-device | TextureManager path/case | CI path + listas |
| UI caixas brancas | TGA não no D3DX | decoder TGA |
| Mesh/terrain arco-íris | DXT como RGBA / flags DXT | decode ou `m_bDXT*=1` + pitch |
| Texto esticado / scanline | font tex 128² em vez de 512×64 | D3DX respeita Width/Height |
| Nome servidor mojibake (`Āß…`) | CP949 + fonte sem Hangul | iconv CP949 + NanumGothic |
| Mouse preso na janela | `SDL_SetRelativeMouseMode(TRUE)` | SoftCaptureMouse (sem grab) |
| Cursor do jogo congelado / clique morto | `OnMouseEvent` stub | `EventTranslator_linux` |
| Digitação morta (login/chat) | sem CHAR de Enter/Tab/Back | TEXTINPUT + TranslateMessage |
| Connection failed / IP lixo | chave `serverlist` / slots | chave latin-1; cifrar vazios |
| Trava em "Now Connecting…" | `WSAAsyncSelect` no-op | `socket_poll_linux` + WM_USER+100 |
| Selchar chão preto / escuro | visual/fog/luz | forçar visual; ↑ emissive; fog |
| HUD touch come todos os cliques | `OnPointer*` sempre consome | return 0 em `Zone::None` / fallthrough |
| Toque no HUD também clica o mundo | touch nunca consome | return 1 quando hit zona |
| `project` x86_64 virou ARM / sumiu | POST_BUILD cross no path errado | guards CMake + scripts separados |
| Fullscreen/mouse/ícone sumiram após sync | `winuser_sdl.cpp` overwrite | restaurar arquivo do port |
| Canais vazios / net | servidor / lista / mock | fora do escopo gráfico |
| Sai em 1s sem erro (exit 0, sem MessageBox) | SDL2-ismos: `SDL_InitSubSystem(...) != 0` — em SDL3 retorna **bool** (true=sucesso), então o sucesso era tratado como falha (`EventTranslator::Initialize` → FALSE → `InitDevice` return 0) | `!SDL_InitSubSystem(...)` sob `WYD_USE_SDL3` (8 sites: EventTranslator, winuser, dsutil ×2, DirShow, audio_sdl, wingdi) |
| Texto sobreposto/embaralhado na UI | `TextOutA` ignorava `bkMode OPAQUE` — o 7.48 apaga a linha desenhando espaços com fundo; sem o fill, strings se acumulam na textura de fonte | fundo por célula (`[pen,pen+adv) × [y, y+lineHeight)`) com `bkColor` antes do glifo |
| Texto some parcialmente / altura de linha errada | `GetTextExtentPoint32A` retornava `cy` do bitmap-box do glifo (relativo à baseline) em vez da célula | `cy = lineHeight (ascent-descent)`, fallback `pixelHeight` |
| `err: SDL3 WSI: SDL_GetWindowSizeinPixels: Invalid window` (DXVK) | consequência do exit-1s: `CreateWindowExA` falhava pelo mesmo `SDL_InitSubSystem` bool | mesmo fix acima |

## Logs

- DXVK: stderr / `project_d3d9.log`
- Client: `clientlogs.log` (path também passa por normalize)
- MessageBox Linux: impresso como `[MessageBox] ...`
