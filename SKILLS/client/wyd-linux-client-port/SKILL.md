---
name: wyd-linux-client-port
description: >-
  Porta clients With Your Destiny (WYD / TMProject, qualquer versão) para Linux
  nativo com Wayland + DXVK Native + SDL (WSI), sem Wine. Cobre layout do tree,
  ordem de implementação, merge Windows→Linux, ARM64 cross, smoke login→campo e
  isolamento de versão. Usar quando o usuário pedir port Linux do client WYD,
  DXVK Native, client Wayland, portar 7.xx para Linux, ou fork de outra versão
  a partir de um port já estável.
disable-model-invocation: false
---

# Port do client WYD → Linux (DXVK Native)

Idioma: **português**. Skills irmãs (detalhe cirúrgico):
`wyd-linux-win32-compat`, `wyd-linux-dxvk-d3dx`.

## Arquitetura que funciona (não reinventar)

O client é **Direct3D 9 + Win32**. No Linux o caminho validado é:

```text
TMProject (clone)  ──D3D9──►  libdxvk_d3d9.so (DXVK Native)
      │ Win32/GDI/Winsock              ▲
      ▼                                │ HWND = SDL_Window*
platform/linux/compat/          DXVK_WSI_DRIVER=SDL2|SDL3
SDL (janela/input/áudio) + Wayland
```

**Não fazer:** Wine como alvo do port nativo; stub/no-op no caminho
login→selserver→selchar→campo; misturar `.so` x86_64 com build aarch64;
reescrever o TMProject em massa; sobrescrever binário de uma arch com o da outra.

## Isolamento (obrigatório)

1. Trabalhar **só** na pasta da versão alvo. Não misturar source/assets/packets
   de outra release.
2. Baseline Windows = read-only. Trabalho = clone (`TMProject*/`) +
   `platform/linux/compat/` + dir de assets (`client*/` ou release de produção).
3. A versão alvo dita **ABI e loaders**. Compilar ≠ loader certo.
4. Documentar sintoma novo nas refs locais do port (RUNTIME / PITFALLS).

## Layout alvo (nomes genéricos)

```text
<PORT-TREE>/
  PORT.md  AGENTS.md  CMakeLists.txt  cmake/
  cmd/linux_client_main.cpp     # setenv WSI → wWinMain
  platform/linux/compat/        # backends Win32/D3DX/GDI/net/áudio/touch
  TMProject*/                   # clone adaptado (#ifdef WYD_LINUX pontuais)
  client*/ ou Release*/         # assets + binário instalado
  third_party/
    dxvk-native/                # x86_64
    dxvk-native-aarch64/        # ARM64 (prefix separado)
  scripts/                      # fetch DXVK, build, cross docker, audit-diff
```

Reaproveitar `platform/linux/compat/` de um port estável como ponto de partida;
**não** recriar do zero.

## Ordem de implementação

```text
1 CRT/paths/PCH/tipos ABI
2 Janela SDL + fila WM_* + TEXTINPUT
3 D3D9 via DXVK + D3DX real (smoke CreateDevice)
4 TextureManager / splash / WYT·WYS
5 Texto GDI (CP949 + fonte Hangul)
6 Rede (poll async no lugar de WSAAsyncSelect)
7 Campo 3D
8 Touch/LoL (se alvo mobile/ARM)
```

Detalhe de cada camada: skill `wyd-linux-win32-compat` e `wyd-linux-dxvk-d3dx`.

## O que muda entre versões (validar sempre)

- Packets / opcodes / criptografia / `serverlist`
- Tamanho de record em listas (`*TextureList*`, ItemList, SkillData)
- Magics WYT/WYS e pós-processamento
- Controles UI, grids, fluxos login/selchar
- APIs Win32 novas → shim **real** (nunca S_OK vazio)
- Porta e chave do servidor desta pasta

## Build e empacotamento

| Build | Destino típico | DXVK |
|---|---|---|
| Host x86_64 | `project` ou `WYD` no dir de assets | `dxvk-native` |
| Host/cross aarch64 | `WYD Arm64` (+ `touch_icons/`) | `dxvk-native-aarch64` |

- Cross **nunca** sobrescreve o binário x86_64.
- Smoke com **cwd = pasta de produção/assets**, não só o artefato em `build/`.
- Runtime:

```bash
export DXVK_WSI_DRIVER=SDL2   # ou SDL3 conforme o build
export SDL_VIDEODRIVER=wayland
export LD_LIBRARY_PATH="<prefix-dxvk>/usr/lib:$LD_LIBRARY_PATH"
export VK_LOADER_LAYERS_DISABLE='*steam*'
```

Host deps: `build-essential cmake pkg-config libwayland-dev libsdl2-dev`
(ou SDL3) `libxkbcommon-dev libegl1-mesa-dev wayland-protocols`
`libcurl4-openssl-dev libvulkan-dev fonts-nanum`.

## Arquivos sensíveis (não regenerar em sync cego)

| Peça | Por quê |
|---|---|
| `winuser_sdl.*` | Wayland, fullscreen desktop, mouse livre, TEXTINPUT, ícone |
| `EventTranslator_linux.*` | Input + touch fallthrough + LoL |
| `TouchControlsUI.*` | HUD touch |
| `d3dx9_linux.*` | DXT/TGA/resize/bpp |
| `wingdi_linux.*` | CP949 / fontes |
| `socket_poll_linux.*` | async net; **sem** redefinir `socket()` |
| `wininet_linux.*` | OpenUrl seguro |
| `win32_extras.*` | paths, ShellExecute, CRT |
| CMake POST_BUILD | separar arches |

## Critério de aceite

- [ ] Smoke DXVK: CreateDevice + shaders
- [ ] Splash sem MessageBox fatal / sem SEGV ~30s
- [ ] Selserver: texturas **e** texto legível + lista de canais
- [ ] Digitação login/chat; mouse livre; clique UI
- [ ] Login → selchar → campo com servidor da versão
- [ ] SFX/BGM básicos
- [ ] Se touch: HUD consome só suas zonas; mundo clicável fora
- [ ] `file` do binário bate com a arch; cross não apagou x86_64

## Laço de trabalho

```text
editar compat/jogo → build → smoke no dir de assets → anotar sintoma
→ consultar references/sintomas.md → próximo passo
```

Nunca afirmar “funciona” sem evidência (janela + log + caminho login→campo).

## Referências desta skill

- [references/sintomas.md](references/sintomas.md) — tabela sintoma→causa
- [references/fases.md](references/fases.md) — checklist curto por fase
