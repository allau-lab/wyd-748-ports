# Sintoma → causa (port Linux client)

Consultar **antes** de investigar do zero. Quase todo defeito já apareceu.

| Sintoma | Causa | Onde |
|---|---|---|
| Hang / CPU 100% em `SDL_Init`, sem janela | Wrapper redefinindo `socket()` interpondo libc | `socket_poll_*` — emular só `closesocket`/`ioctlsocket`/`WSA*`; `SOCKET=int`; usar `::socket` |
| Segfault ~addr 0x20 | `sscanf_s` com sizes MSVC | call sites → `sscanf` |
| Sai em 1s exit 0 sem MessageBox | SDL3: `SDL_InitSubSystem` retorna **bool**; `!= 0` trata sucesso como falha | `!SDL_InitSubSystem(...)` sob build SDL3 |
| `VK_ERROR_OUT_OF_HOST_MEMORY` / surface OOM | Sem `SDL_WINDOW_VULKAN` ou WSI ≠ SDL | janela + `DXVK_WSI_DRIVER` |
| DXVK “Invalid window” | CreateWindow falhou (mesmo bug SDL3 bool) | idem |
| Initialize Data / TextureManager fail | `\` ou case (`Mesh`≠`mesh`) | `NormalizePath` CI por componente + cwd = assets |
| UI branca / caixas pretas / UI oca | TGA/DXT incompleto; textura null | D3DX: DDS primeiro; DXT1/3/5; TGA |
| Mesh/terrain arco-íris | DXT lido como RGBA / flags off | `m_bDXT1=m_bDXT3=1` + pitch por bloco |
| SEGV em `create_tex_from_rgba` / TMFont2 | A4R4G4B4 escrito como BGRA8 | upload no **bpp do Format** |
| Texto esticado / scanline | font tex sem resize 512×64 | D3DX Width/Height |
| Texto sobreposto na UI | `TextOut` sem fill OPAQUE | fundo por célula com `bkColor` |
| Texto some / altura errada | `GetTextExtent` com cy do glyph box | `cy = lineHeight` |
| Mojibake selserver | CP949 sem iconv / sem Hangul | GDI + NanumGothic |
| Mouse preso | relative/grab | SoftCapture; ClipCursor≈no-op |
| Clique/cursor morto | EventTranslator stub | input Linux real |
| Digitação morta | sem TEXTINPUT / TranslateMessage | winuser SDL |
| “Now Connecting…” eterno | `WSAAsyncSelect` no-op | poll + post `WM_USER+…` no Run |
| OpenUrl com lixo / SEGV em fprintf | `%s` em ptr que não é URL (`&g_pServerList`) | validar http(s) antes de logar |
| Browser abre `change.exe` | ShellExecute→xdg-open cego | só URL ou arquivo existente |
| Lista Canal vazia / nomes lixo | `serverlist` plaintext/chave/cwd | binário no cwd certo + decrypt |
| HUD comeu todos os cliques | touch sempre `return 1` | fallthrough em zona None |
| Mundo clica sob o HUD | touch nunca consome | `return 1` no hit |
| Stick “travado” / spam path | stick como vetor contínuo | click-to-move: destinos no chão ~10 Hz |
| Fullscreen/mouse/ícone sumiram após sync | overwrite de `winuser_sdl` | restaurar patch do port |
| Binário x86_64 virou ARM / sumiu | POST_BUILD cross no path errado | guards CMake por arch |

## Env mínimo

| Variável | Função |
|---|---|
| `DXVK_WSI_DRIVER=SDL2` ou `SDL3` | HWND = SDL_Window* |
| `SDL_VIDEODRIVER=wayland` | alvo Wayland |
| `LD_LIBRARY_PATH` | `libdxvk_d3d9.so` da **mesma** arch |
| `VK_LOADER_LAYERS_DISABLE=*steam*` | evita layers Steam |
