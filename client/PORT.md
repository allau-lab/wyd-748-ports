# Playbook: portar client WYD 7.60+ para Linux

Este documento é o roteiro operacional para levar **outra versão do client**
(≥ 7.60) a Linux/Wayland, reaproveitando o trabalho já feito no **7.48** em
`WYDLINUX/`.

Índice detalhado (arquitetura, compat, gráficos, runtime):
**[docs/README.md](docs/README.md)**. Checklist curto: **[docs/PORTING.md](docs/PORTING.md)**.

---

## 0. Estado atual do port 7.48 (referência viva)

| Peça | Realidade |
|---|---|
| Código | `TMProject748/` + `platform/linux/compat/` (source Win32 em `codigo-fonte/` **intocado**) |
| Gráficos | **DXVK Native** `libdxvk_d3d9.so` + WSI **SDL2** + **Wayland** |
| Binário x86_64 | `client748/project` ← target `wyd_client` |
| Binário ARM64 | `client748/WYD Arm64` (OUTPUT_NAME CMake; cross/docker também pode gerar `project-aarch64`) |
| Touch HUD | `TouchControlsUI` + `assets/touch_icons/*.tga` → `client748/touch_icons/` |
| Controles | `[LOL_CONTROLS]` / `[TOUCH_UI]` em `client748/config.txt` |
| Janela | `WS_POPUP` → `SDL_WINDOW_FULLSCREEN_DESKTOP`; sempre `SDL_WINDOW_VULKAN` |
| DXVK host | `third_party/dxvk-native/` |
| DXVK aarch64 | `third_party/dxvk-native-aarch64/` |

**Critério de pronto (7.48):** login → selserver (texturas **e** texto) → selchar → campo.

**Proibido:** stub/no-op em feature usada no caminho login→campo.

---

## 1. Pré-requisitos

### Host de desenvolvimento (x86_64 ou aarch64)

```bash
pkexec apt-get install -y build-essential cmake pkg-config \
  libwayland-dev libsdl2-dev libxkbcommon-dev libegl1-mesa-dev \
  wayland-protocols libcurl4-openssl-dev libvulkan-dev \
  fonts-nanum   # Hangul / CP949 na UI
```

### DXVK Native

- **x86_64:** `./scripts/fetch-dxvk-native.sh` → `third_party/dxvk-native/`
  (pacote Steam Runtime sniper; default **3.0.2**).
- **aarch64:** não há tarball pronto confiável no mesmo fluxo — o
  `scripts/build-aarch64-docker.sh` **compila** DXVK Native no container e
  instala em `third_party/dxvk-native-aarch64/` (headers copiados do prefix
  x86_64). Em máquina aarch64 nativa, use o mesmo layout de prefix
  (`usr/include/dxvk` + `usr/lib/libdxvk_d3d9.so`).

Override: `-DWYD_DXVK_NATIVE_ROOT=/caminho` ou `WYD_DXVK_NATIVE_ROOT`.

### Cross ARM64 a partir de x86_64 (“wyd-builder-arm64”)

```bash
# Recomendado: Docker + cross-toolchain (não toca client748/project x86_64)
./scripts/build-aarch64-docker.sh
# Image default: ubuntu:24.04 (WYD_AARCH64_IMAGE=… para cache/tag própria,
# ex. wyd-builder-arm64). Produz DXVK aarch64 + wyd_client.
```

Com toolchain local: `./scripts/build-aarch64.sh` (fallback para docker se
`aarch64-linux-gnu-g++` ausente).

### Runtime

```bash
export DXVK_WSI_DRIVER=SDL2
export SDL_VIDEODRIVER=wayland
export LD_LIBRARY_PATH="$PWD/../third_party/dxvk-native/usr/lib:$LD_LIBRARY_PATH"
# ARM64:
# export LD_LIBRARY_PATH="$PWD/../third_party/dxvk-native-aarch64/usr/lib:$LD_LIBRARY_PATH"
export VK_LOADER_LAYERS_DISABLE='*steam*'
```

---

## 2. O que transfere do 7.48 vs o que muda em 7.60+

### Reaproveitar quase intacto (`platform/linux/compat/`)

| Camada | Arquivos-chave | Notas |
|---|---|---|
| Janela / WM_* | `winuser_sdl.*` | HWND=`SDL_Window*`; **não sobrescrever** ao mergear source Windows |
| Input | `EventTranslator_linux.cpp` | Touch fallthrough + LoL RMB |
| Touch HUD | `TouchControlsUI.*` | Independe do protocolo; depende de RenderDevice/GeomControl |
| Paths/CRT | `win32_extras.h`, `linux_pch.h` | `\`→`/`, case CI, `sscanf` |
| GDI/fonte | `wingdi_linux.cpp` | CP949 + NanumGothic |
| D3DX | `d3dx9_linux.cpp` | TGA/DDS/resize Width×Height |
| Áudio | `dsutil_linux.cpp`, `DirShow_linux.cpp` | WAV + minimp3 |
| HTTP | `wininet_linux.cpp` | libcurl |
| Net async | `socket_poll_linux.*` | substitui `WSAAsyncSelect` |
| Headers D3D9 | `d3d9.h` → DXVK Native | forçar includes do third_party |

### Vai diferir (validar byte a byte)

- **Packets / ABI / wire** — opcodes, structs, criptografia, `serverlist`.
- **Assets** — tamanho de record em listas (`*TextureList*`), magics WYT/WYS,
  `SkillData`, `strdef`, `sn.bin`, shaders `.bin`, telas RC/bin de UI.
- **Cenas / UI** — novos controles, grids, fluxos de login/selchar.
- **Lista de fontes CMake** — `cmake/tmproject_sources.cmake` / includes.
- **Dependências Win32 extras** — APIs novas no client 7.60+ precisam de shim
  real (não stub) na compat.

Regra herdada do port Windows 7.48: a versão nova ensina *comportamento*; o
ABI/assets da versão alvo ditam o *formato*. Não copiar offsets “porque
compilou”.

---

## 3. Layout sugerido para um fork 7.60+

```text
WYD760-LINUX/                    # ou WYDLINUX/ com pastas versionadas
  PORT.md                        # este playbook (adaptado)
  CMakeLists.txt                 # clone do 7.48; ajuste TM_ROOT / client dir
  cmake/
    tmproject_sources.cmake
    tmproject_includes.cmake
    aarch64-linux-gnu.cmake
  cmd/linux_client_main.cpp      # setenv DXVK_WSI_DRIVER=SDL2 → wWinMain
  platform/linux/compat/         # COPIAR do WYDLINUX 7.48 como ponto de partida
  TMProject760/                  # cópia do source Windows 7.60+ adaptada
  client760/                     # assets da versão + binários instalados
  assets/touch_icons/            # TGA do HUD (opcional se TOUCH_UI)
  third_party/
    dxvk-native/
    dxvk-native-aarch64/
    minimp3/ stb_*.h
  scripts/
    fetch-dxvk-native.sh
    build.sh
    build-aarch64.sh
    build-aarch64-docker.sh
    audit-port-diff.sh           # baseline Windows vs árvore portada
```

Isolamento: **não editar** o tree Windows original; trabalhar só no clone do
port.

---

## 4. Passos (ordem que funcionou no 7.48)

### A. Win32 stubs → backends reais

1. Copiar `platform/linux/compat/` do 7.48.
2. Compilar um `wyd_client` mínimo com PCH `linux_pch.h` (`<cstdio>` **antes**
   de redefinir `fopen`).
3. Substituir call sites `sscanf_s` (com sizes MSVC) por `sscanf`.
4. Garantir `GetModuleFileNameA` via `/proc/self/exe` e cwd = pasta dos assets.
5. `ShellExecute`: só URL http(s)/mailto ou arquivo existente — nunca
   `xdg-open` cego em `Change.exe`.

### B. D3D9 / DXVK

1. Headers DXVK Native; link `libdxvk_d3d9.so` + RPATH.
2. `CreateWindowEx`: `SDL_WINDOW_VULKAN | SHOWN`; fullscreen via
   `SDL_WINDOW_FULLSCREEN_DESKTOP` quando `WS_POPUP`.
3. `DXVK_WSI_DRIVER=SDL2` **antes** de `Direct3DCreate9`.
4. `m_bDXT1 = m_bDXT3 = 1`; D3DX com TGA + DXT pitch + **resize** Width/Height
   (fonte 512×64).
5. Smoke: `dxvk_native_smoke` (CreateDevice + shaders).

### C. Rede

1. CPSock sobre sockets BSD (já no port 7.48).
2. `socket_poll_linux` + `WM_USER+100` no `Run()` (sem isso: “Now Connecting…”).
3. Validar `serverlist` / chave de cifra / IP real do servidor alvo (8281 no
   pack 7.48; confirmar na 7.60+).

### D. Assets

1. Popular `client760/` (ou symlink RO) com casing Linux correto.
2. Conferir magics/loaders: `.wyt`/`WT10`, `.wys`/`WS10`, listas UI/mesh.
3. Fonte CJK instalada; testar texto selserver sem mojibake.

### E. UI / cenas

1. Login → selserver → selchar → field sem MessageBox fatal.
2. Ajustar só o que a versão nova mudou (RC/bin, controles, fluxos).

### F. Input / touch

1. Mouse livre: sem relative/grab permanente (`SoftCaptureMouse`).
2. `TouchControlsUI`: `OnPointer*` retorna **1 = consumiu**, **0 = fallthrough**
   para o jogo/UI Win32. Errar para um lado só = HUD morto **ou** mundo
   inacessível sob os botões.
3. ARM64: default `g_bTouchUi=1`; `config.txt` `[TOUCH_UI]` / `[LOL_CONTROLS]`.
   Com touch ligado, forçar LoL controls (cast/move separados).
4. POST_BUILD ARM64 deve copiar `assets/touch_icons/` → `client*/touch_icons/`.

### G. Empacotamento

| Build | Árvore CMake | Binário em `client*/` | DXVK |
|---|---|---|---|
| Host x86_64 | `build/` | `project` | `dxvk-native` |
| Host aarch64 nativo | `build/` | `WYD Arm64` | `dxvk-native-aarch64` |
| Cross (docker/toolchain) | `build-aarch64/` | `WYD Arm64` (+ alias `project-aarch64` no script docker) | `dxvk-native-aarch64` |

**Pitfall POST_BUILD:** nunca deixar o cross-build sobrescrever
`client748/project` (x86_64). O CMake 7.48 já separa por
`CMAKE_CROSSCOMPILING` / `aarch64`.

Launcher/settings: `scripts/wyd-settings.sh` edita `config.txt` (RES, WINDOW,
áudio…). Ícone de janela: `wyd_exe_icon.inc` em `winuser_sdl`.

---

## 5. Como reusar `platform/linux/compat`

1. Copiar a pasta inteira para o novo tree.
2. Manter a lista de exclusões no CMake (não linkar `DirShow.cpp`,
   `dsutil.cpp`, `EventTranslator.cpp`, `TMVideoWnd.cpp`, `pch.cpp` Win32 —
   usar `*_linux.cpp`).
3. Ao mergear source Windows novo **em** `TMProjectXXX/`:
   - **Diff só no game code**, não em `platform/linux/`.
   - Tratar `winuser_sdl.cpp` / `EventTranslator_linux.cpp` /
     `TouchControlsUI.cpp` / `d3dx9_linux.cpp` como **patches remotos do port**:
     se um sync/agent regenerar stubs Win32, você perde fullscreen desktop,
     mouse livre, ícone, text input, touch fallthrough.
4. Usar `./scripts/audit-port-diff.sh` (adaptar paths A/B) para ranquear
   arquivos divergentes vs baseline Windows.

---

## 6. Estratégia de merge Windows → Linux

```text
baseline Windows 7.60+ (read-only)
        │
        ▼
   TMProject760/  ← cópia de trabalho
        │  aplicar #ifdef WYD_LINUX / includes Linux só onde inevitável
        │
        ▼
   diff vs TMProject748 portado (opcional) para ver churn de cenas/ABI
        │
        ▼
   platform/linux/compat/  ← NÃO recriar do zero; portar deltas de API
```

Ordem prática por PR/commit:

1. Build linka (stubs mínimos só para símbolos mortos).
2. Janela + Clear/Present DXVK.
3. TextureManager + UI splash.
4. Texto (GDI).
5. Login/net.
6. Campo 3D.
7. Touch/LoL (se alvo mobile/ARM).

Evitar um “big bang” que mistura ABI 7.60 com loaders 7.48.

---

## 7. Build — receitas

### x86_64 (client jogável)

```bash
cd WYDLINUX   # ou o tree 7.60+
./scripts/fetch-dxvk-native.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DWYD_BUILD_REAL_CLIENT=ON -DWYD_BUILD_SMOKE_PROJECT=OFF
cmake --build build --target wyd_client -j"$(nproc)"
# POST_BUILD → client748/project
cd client748 && export DXVK_WSI_DRIVER=SDL2 SDL_VIDEODRIVER=wayland \
  LD_LIBRARY_PATH="$PWD/../third_party/dxvk-native/usr/lib:$LD_LIBRARY_PATH" \
  VK_LOADER_LAYERS_DISABLE='*steam*'
./project
```

### ARM64 via Docker (cross)

```bash
./scripts/build-aarch64-docker.sh
# → third_party/dxvk-native-aarch64/
# → client748/WYD Arm64 e/ou project-aarch64
# client748/project (x86_64) permanece
```

### ARM64 nativo

```bash
cmake -S . -B build -DWYD_BUILD_REAL_CLIENT=ON -DWYD_BUILD_SMOKE_PROJECT=OFF \
  -DWYD_DXVK_NATIVE_ROOT=$PWD/third_party/dxvk-native-aarch64
cmake --build build --target wyd_client -j"$(nproc)"
# → client748/WYD Arm64 + touch_icons/
```

### Opções CMake úteis

| Option | Default | Função |
|---|---|---|
| `WYD_BUILD_REAL_CLIENT` | ON | `wyd_client` (TMProject real) |
| `WYD_BUILD_SMOKE_PROJECT` | OFF | demo `wyd_project` (não substitui o client) |
| `WYD_BUILD_WAYLAND` | ON | smokes Wayland/Vulkan |
| `WYD_BUILD_NET_SMOKE` | ON | `net_smoke` |
| `WYD_BUILD_ARCH_TESTS` | ON | testes de wire (desligar no cross) |
| `WYD_DXVK_NATIVE_ROOT` | auto por arch | prefix DXVK |

---

## 8. Checklist de smoke tests

- [ ] `dxvk_native_smoke` — CreateDevice + Create*Shader
- [ ] `./project` (ou `WYD Arm64`) abre splash sem MessageBox fatal
- [ ] Selserver: texturas UI + texto legível (não mojibake CP949)
- [ ] Digitação login/chat (TEXTINPUT → WM_CHAR)
- [ ] Mouse livre; clique UI funciona
- [ ] Login → selchar → campo com servidor online
- [ ] Som SFX/BGM básicos
- [ ] Se `TOUCH_UI=1`: stick move, ATK, skills, menu; toque fora do HUD
      ainda move/clica o mundo (fallthrough)
- [ ] Cross ARM64 não apagou o `project` x86_64
- [ ] `file client*/project` vs `file 'client*/WYD Arm64'` batem com a arch
      esperada

---

## 9. Falhas comuns (7.48) — não reabrir como “mistério”

| Sintoma | Causa | Onde |
|---|---|---|
| Segfault ~addr 0x20 | `sscanf_s` com sizes MSVC | call sites → `sscanf` |
| Surface OOM / Init Render fail | sem `SDL_WINDOW_VULKAN` / WSI ≠ SDL2 | `winuser_sdl` + env |
| UI branca | TGA ausente no D3DX | `d3dx9_linux` |
| Mesh arco-íris | DXT como RGBA / flags | `m_bDXT*=1` + pitch |
| Texto esticado | font tex sem resize 512×64 | D3DX Width/Height |
| Mojibake selserver | CP949 sem iconv/fonte Hangul | `wingdi_linux` |
| Mouse preso | relative/grab | `SoftCaptureMouse` |
| Clique/cursor morto | `OnMouseEvent` stub | `EventTranslator_linux` |
| Digitação morta | sem TEXTINPUT / TranslateMessage | `winuser_sdl` |
| Trava “Now Connecting…” | `WSAAsyncSelect` no-op | `socket_poll_linux` |
| Browser abre `change.exe` | ShellExecute cego | `win32_extras` |
| Path / TextureManager fail | `\` ou `Mesh`≠`mesh` | `WYD_NormalizePath` |
| SDL3: sai em 1s sem erro / DXVK "Invalid window" | `SDL_InitSubSystem` retorna **bool** no SDL3 (`!= 0` trata sucesso como falha) | `!SDL_InitSubSystem(...)` sob `WYD_USE_SDL3` |
| SDL3: texto da UI sobreposto | `TextOutA` sem fill OPAQUE — 7.48 apaga linha desenhando espaços | fundo por célula com `bkColor` antes do glifo |
| Cross matou client x86_64 | POST_BUILD no path errado | CMake arch guards |
| HUD comeu todos os cliques | touch sem fallthrough (`return 1` sempre) | `TouchControlsUI` / EventTranslator |
| Mundo clica sob o HUD | touch nunca consome (`return 0` sempre) | idem |
| Perda de fullscreen/mouse/ícone após “sync” | `winuser_sdl.cpp` regenerado/overwrite | **proteger arquivo do port** |

Detalhe sintoma→causa: [docs/RUNTIME.md](docs/RUNTIME.md).

---

## 10. Regras permanentes (qualquer versão)

1. Source original intocado; trabalho só no tree do port.
2. Backend real para feature usada — sem stub “S_OK vazio”.
3. Preservar contrato de packets/assets da **versão alvo**.
4. Wayland + DXVK Native; WSI `SDL2` ou `SDL3` conforme o build (`DXVK_WSI_DRIVER` é defaultado pelo `main`, respeitando env do usuário).
5. Documentar sintomas novos em RUNTIME.md quando aparecerem.

---

## 11. Auditoria do upstream Linux (felipeletsgo/wyd748)

Revisado em 2026-09-20. O upstream continua sendo uma fonte de evidência e
correções pontuais, não um tree para copiar cegamente: a arquitetura local usa
shim Win32/SDL3 e o upstream mistura validação nativa, client C++ e servidor Go.

### Correções compatíveis aplicadas nesta árvore

- `TMMesh::RenderForUI`: a faixa de legendas `116..125` agora inclui o índice
  116; isso corrige o primeiro nível de textura/refino.
- `SGrid::SellItem/SellItem2/MouseOver`: a decisão do destino usa o estado
  efetivo `!sDestType`, evitando tratar um grid de inventário como equipamento.
  A alteração é de lógica do client e vale igualmente para x86_64 e ARM64.
- O limite de câmera voltou a `20.0f`: aumentar para 28 expunha áreas sem
  terreno/objetos no streamer atual e produzia a tela azul observada. Para
  ampliar o horizonte com qualidade será necessário primeiro um streamer de
  múltiplos chunks, não apenas aumentar o zoom.

### Itens upstream adicionados ao backlog

- Macro: comparar a implementação upstream de seleção de skill, cooldown,
  consumíveis, buffs/passivas, alvo travado e persistência de estado; só portar
  regras confirmadas no contrato 7.48.
- Combate: revisar party/guild/PK e impedir que o auto-target reabra painéis ou
  selecione aliados durante atualizações de roster.
- Skills/buffs: auditar textura de item/refino e ciclo de hover da barra de
  skills, incluindo `nTextureSetIndex`/`nTextureIndex`.
- Lifecycle: comparar bootstrap, `WM_CHAR`/IME, shutdown e ownership de blur;
  não importar COM/AVI ou offsets nativos sem consumidor Linux equivalente.
- Banco/painel: separar mudanças do servidor Go das mudanças do client para
  evitar misturar protocolo, persistência e apresentação.

Cada item deve ser validado em x86_64 e ARM64; o build ARM existente foi gerado
em `/src/build-aarch64` dentro de Docker e não pode ser reutilizado diretamente
no host por causa dos caminhos absolutos do CMake.

### Configuração gráfica SDL3/DXVK-native

O cliente agora aceita opções adicionais no `client748/config.txt`, sem alterar
o formato legado das primeiras linhas:

```ini
[VSYNC] 1          # 1 = estável, 0 = menor latência/mais FPS
[ANISOTROPIC] 8   # 0/1 = linear; 2, 4, 8 ou 16 para terreno em ângulo
[MIPMAP] 50       # 0 = sem mipmap; 50 = qualidade máxima do carregador legado
[ANTIALIAS] 4     # 0, 2, 4 ou 8 conforme a GPU
[REFLECTION] 1    # reflexos opcionais
[DEBUG_STATS] 0   # 1 = mostra FPS/objetos no canto inferior
```

Essas opções são aplicadas ao código comum do cliente e não usam instruções
específicas de CPU, portanto permanecem compatíveis com x86_64 e ARM64. O
anisotrópico é limitado a 16x e o antialiasing é limitado a 8x para evitar que
um valor inválido cause falha na criação do dispositivo.
