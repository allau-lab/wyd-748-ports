# Checklist: portar outra versão do client WYD

Roteiro completo (layout, merge, ARM64, smokes): **[../PORT.md](../PORT.md)**.

Cada item abaixo é um bloqueio real já enfrentado no **7.48**.

## 0. Isolamento

- [ ] Copiar/adaptar source **para dentro** do tree do port (ex.: `TMProject760/`).
- [ ] Manter o original em `codigo-fonte/` (ou baseline Windows) intacto.
- [ ] Assets da versão em `client*/` — casing Linux exato.
- [ ] Copiar `platform/linux/compat/` do 7.48 como ponto de partida (não reescrever).

## 1. Build / link

- [ ] CMake target real (`wyd_client`) com PCH Linux (`linux_pch.h`).
- [ ] Headers D3D9 **forçados** para DXVK Native (`compat/d3d9.h` → third_party).
- [ ] Link `libdxvk_d3d9.so` + RPATH; `DXVK_WSI_DRIVER=SDL2` **antes** de `Direct3DCreate9`.
- [ ] Separar POST_BUILD: x86_64 → `project`; aarch64 → `WYD Arm64` (+ `touch_icons/`).
- [ ] Cross **não** sobrescreve o binário da outra arch.
- [ ] Não redefinir `__try`/`min`/`max` de forma que quebre libstdc++ / `std::min`.

## 2. CRT / paths (Linux case-sensitive)

| Sintoma | Causa | Onde |
|---|---|---|
| Segfault cedo | `sscanf_s` ainda com args de size MSVC | call sites → `sscanf` |
| “Can't read ItemList.bin” | path `.\` / `UI\\` | `WYD_NormalizePath` + `#define fopen` |
| Init Texture Manager fail | `Mesh\` vs `mesh/` | normalize **case-insensitive** por componente |
| Log não grava | `dir\\file` + `_open` flags | normalize + flags POSIX |

Obrigatório na compat:

- [ ] `\` → `/`
- [ ] Resolução CI de diretórios (`Mesh`↔`mesh`)
- [ ] `fopen` / `fopen_s` / `_open` / `_stat` / `CopyFile` normalizados
- [ ] `GetModuleFileNameA` via `/proc/self/exe`
- [ ] PCH: `<cstdio>` **antes** de redefinir `fopen`

## 3. Janela + WSI

- [ ] `HWND` = `SDL_Window*`
- [ ] `CreateWindowEx` com `SDL_WINDOW_VULKAN | SHOWN`
- [ ] `WS_POPUP` → `SDL_WINDOW_FULLSCREEN_DESKTOP` (não exclusive cru sem testar)
- [ ] `ShellExecute`: só URL http(s)/mailto ou exe existente — **não** `xdg-open` cego em `Change.exe`
- [ ] **Não** regenerar `winuser_sdl.cpp` a partir de stubs Win32 remotos

## 4. Render (DXVK)

- [ ] `Direct3DCreate9` → `CreateDevice(hFocus=SDL_Window*)`
- [ ] `m_bDXT1 = m_bDXT3 = 1`
- [ ] D3DX `FromFileInMemoryEx`: DDS+pitch, DXT→RGBA se preciso, **TGA**, **Width/Height resize**, ColorKey
- [ ] Prefix DXVK correto por arch (`dxvk-native` vs `dxvk-native-aarch64`)

## 5. Formatos de asset (podem mudar na versão nova!)

| Extensão | Magic | Pipeline do client 7.48 |
|---|---|---|
| `.wyt` | `WT10` | strip 4 bytes → TGA (+ footer) → D3DX |
| `.wys` | `WS10` | strip 1 byte → prefixo `DDS` + FourCC@84 → DXT1/3 |
| Listas UI/mesh/effect/env | bin legado ~264 B/row | `WYD748_LoadTextureList` |
| `UI/UITextureSetList.txt` | texto | `fopen` path `UI\\…` |

Validar na versão nova: tamanho de record, magic, paths nas listas, shaders `.bin`.

## 6. Fonte / UI texto

- [ ] GDI real + CP949→Unicode + fonte Hangul (NanumGothic / Noto CJK)
- [ ] `TMFont2` textura A4R4G4B4 512×64 via D3DX+resize

## 6b. Mouse / touch / LoL

- [ ] Sem relative/grab permanente; `ClipCursor` no-op
- [ ] Touch: `OnPointer*` **1=consome**, **0=fallthrough** para UI/mundo
- [ ] `[TOUCH_UI]` / `[LOL_CONTROLS]` em `config.txt`; touch implica LoL
- [ ] Ícones em `assets/touch_icons/` instalados no client dir

## 7. Áudio / net / input

- [ ] SFX: SDL_LoadWAV (`dsutil_linux`)
- [ ] BGM: minimp3 (`DirShow_linux`)
- [ ] HTTP: libcurl (`wininet_linux`)
- [ ] Input: SDL → fila Win32 (`EventTranslator` / `winuser_sdl`)
- [ ] Net: `socket_poll_linux` (sem WSAAsyncSelect)

## 8. Critério de pronto (por versão)

1. Sobe sem segfault / MessageBox de init
2. Login / selserver com **texturas + texto legível**
3. Selchar
4. Campo com terreno/personagem do TMProject
5. (ARM/touch) HUD jogável sem bloquear o mundo

## 9. O que NÃO fazer

- Stub que “compila e linka” mas retorna S_OK vazio / textura null
- Assumir filesystem case-insensitive
- Assumir que D3DX ignora Width/Height
- Misturar Wine DXVK com alvo Wayland-nativo sem WSI SDL2
- Sobrescrever `client*/project` x86_64 com binário aarch64
- Documentar só o happy path — registrar sintomas→causa (ver RUNTIME.md)
