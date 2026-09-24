# Arquitetura do port Switch (referência: 7.48 / WYDLINUX)

## Pré-requisito

O port Switch **reaproveita o port Linux** (`WYD_LINUX`): shims Win32/GDI/Winsock,
`d3dx9_linux.cpp` (loaders DDS/TGA/WYT/WYS), `wingdi_linux.cpp` (stb_truetype + CP949),
`socket_poll_linux.cpp` (WSAAsyncSelect por polling). Sem esse port Linux a versão
não é portável para Switch em tempo razoável.

## Arquivos do client real (target `wyd748` no `switch/CMakeLists.txt`)

| Arquivo | Papel |
|---|---|
| `cmake/tmproject_sources.cmake` | fontes do jogo; o CMake Switch exclui `DirShow.cpp`, `dsutil.cpp`, `EventTranslator.cpp`, `TMVideoWnd.cpp`, `pch.cpp` (substituídos pelos `*_linux.cpp`) |
| `platform/switch/fullclient/client_main_switch.cpp` | `main()`: cria pastas no SD, trunca diag, `[BUILD]`, `SDL_Init`, acha assets e faz `chdir`, `socketInitializeDefault()` + IP via nifm, chama `wWinMain` |
| `platform/switch/d3d9_gles_switch.cpp` | `IDirect3D9`/`IDirect3DDevice9` sobre GLES3; FFP em GLSL; texturas, RTs, state blocks, capturas, diag |
| `platform/switch/vs11_glsl.cpp/.h` | tradutor de vertex shader SM 1.x → GLSL ES 3.00 |
| `platform/switch/gl_hud9.cpp` | HUD (versão, FPS, texturas, erros) desenhado por cima |
| `platform/switch/switch_input.cpp` | HID libnx → mensagens Win32 (stick = cursor, A/B = clique, direcional = setas, X/Y/L/R = F1..F4, − = Insert, + = Esc; X também alterna PROBE) |
| `platform/switch/compat/*` | headers que sombreiam os do Linux (`d3d9.h`, `iphlpapi.h`, `linux_pch.h`), `winuser_sdl.cpp` (janela/eventos SDL3), `switch_link_stubs.cpp` |
| `platform/linux/compat/*` | shims compartilhados com o Linux |

Defines do alvo: `WYD_SWITCH=1 WYD_SDL3=1 WYD_LINUX=1 WYD_GLES=1 __SWITCH__=1
_WYDCLIENT=1 WYD_TEX_PROBE=1 WYD_SWITCH_VERSION="x.y.z"`.
Links: `SDL3 GLESv2 EGL glapi drm_nouveau curl mbedtls mbedx509 mbedcrypto z nx`.
Flags: `-O1 -g -fpermissive -fsigned-char -fexceptions -frtti`, C++17 com extensões GNU.

Arquivos `wsi_switch.cpp`, `input_switch.cpp`, `d3d9_switch/d3d9_gles_bridge.cpp` são do
bring-up (`-DWYD_SWITCH_BRINGUP=ON`), não do jogo.

## Build (Docker)

`scripts/build-switch-docker.sh`:
1. usa `devkitpro/devkita64:latest` com o tree montado em `/work`;
2. compila **SDL3 `switch-sdl-3.4`** uma vez em `third_party/sdl3-switch` (persistido);
3. `cmake -S switch -B build-switch -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake`;
4. copia `wyd748.nro` para `OUT/switch/`.

Argumentos: (nenhum) = build, `reconfigure` = apaga cache e reconfigura, `clean`, `bringup`.

## Layout no SD

```text
sdmc:/switch/client748/
├── wyd748.nro
├── config.txt, serverlist.bin, *.bin, UI/, mesh/, env/, effect/, Shader/, sound/ …  (client inteiro)
├── UI/NanumBarunGothic.ttf, UI/FontNanum.ttf, fonts/DejaVuSans.ttf   (TTF reais!)
├── wyd748_diag.txt, wyd748_ev.txt   (gerados)
├── shots/shotN.tga                  (gerados)
└── mesa_cache/
```

O jogo abre arquivos por caminho relativo (`mesh\\x.wyt`), então o `main` faz `chdir` para
a pasta dos assets; os shims convertem `\\` → `/`.

## Backend D3D9 → GLES (`d3d9_gles_switch.cpp`)

### Estado e desenho

- `SetRenderState`/`SetTextureStageState`/`SetSamplerState` só guardam em cache;
  `DrawCore` reaplica tudo a cada draw (`ApplyGlState`), porque state blocks da UI trocam
  alpha/z/cull entre draws.
- `DrawCore` recebe a faixa de vértices do VB (`baseVtx + minIdx`, `numVtx`), reempacota na
  CPU e sobe com `glBufferData(GL_STREAM_DRAW)`; índices vão para um EBO reajustados por
  `idx - minIdx`.
- Dois caminhos de vértice:
  - **FFP**: `RepackVertex` → layout fixo de 44 bytes (pos, normal, cor RGBA8, uv0, uv1),
    VAO do FFP, programa FFP.
  - **Vertex shader**: se `curVS` está setado e a tradução deu certo, cada registrador `vN`
    declarado vira um `vec4` montado a partir da declaração (`FetchElement` converte
    FLOAT1-4, D3DCOLOR, UBYTE4(N), SHORT2/4(N), USHORT2/4N, FLOAT16), VAO próprio do shader,
    constantes `c0..c95` em `uniform vec4 uC[96]`.
- `SetShaderState`/`BindTexturesAndSetStageUniforms` usam `FfpProgram& ffp = *curProg`, então
  o mesmo código de uniforms serve ao FFP e aos programas traduzidos (uniform ausente = -1,
  ignorado pelo GL).

### Profundidade, cull, clear

- FB padrão do Switch tem depth 24 / stencil 8 (consultar com
  `glGetFramebufferAttachmentParameteriv` no FB 0; logar).
- `zEnable`/`zWrite` só com depth disponível no alvo corrente.
- Cull: backbuffer **não** é espelhado; RT (FBO) é desenhado com Y invertido, o que
  inverte o winding → `glFrontFace((cull==CW) != (curRT!=nullptr) ? GL_CCW : GL_CW)`.
- `Clear`: liberar `glColorMask`/`glDepthMask`/`glStencilMask` antes de `glClear`.

### Texturas

- DXT1/3/5 sobem **comprimidas** (`glCompressedTexImage2D`, S3TC disponível no Mesa do
  Switch); fallback de decodificação na CPU se der erro.
- A4R4G4B4, A1R5G5B5, X8R8G8B8 etc. convertidos para RGBA8 na CPU.
- Só o nível 0 é enviado: `GL_TEXTURE_MAX_LEVEL=0` e filtros sempre sem mipmap.
- `UpdateSurface`/`UpdateTexture` copiam de verdade na CPU e marcam a textura suja.
- Upload preguiçoso no bind (textura suja + `cpu` não vazio); `glActiveTexture` antes do upload.

### Fragment shader (combiner de texture stages)

Estado de cada estágio empacotado em um `int`: `op | arg1 << 8 | arg2 << 16`, uniforms
escalares `uColorSt0..2` / `uAlphaSt0..2` (**Mesa 20.1 ignora arrays de int**).
- `arg()` trata `D3DTA_DIFFUSE/CURRENT/TEXTURE/TFACTOR/SPECULAR` + `COMPLEMENT` (0x10) e
  `ALPHAREPLICATE` (0x20).
- `opv()` implementa D3DTOP 2–16, 18–21, 24.
- COLOROP DISABLE (ou 0) encerra a cadeia; ALPHAOP ≤ 1 mantém o alpha corrente.
- Estágio sem textura amostra `(0,0,0,1)`.
- Alpha test (8 funções) e fog: linear/exp/exp2 sobre `vFogDepth`, modo 100 = fator direto
  (`oFog` de vertex shader).

### Vertex shader do FFP

- RHW (`D3DFVF_XYZRHW`): pixels → NDC com `uRhwVP`, flip Y por alvo (`uRhwYFlip`).
- 3D: `uWVP` (matrizes D3D row-major enviadas com `GL_FALSE`, o GLSL faz `M * v`).
- Iluminação D3D9 com 8 luzes: direcional `L = -Direction`; pontual com `Range` e
  `1/(a0 + a1·d + a2·d²)`; ambiente = global + Σ ambiente das luzes × atenuação; resultado
  `emissive + amb·matAmbient + Σdif·base` saturado. Sem iluminação: cor do vértice direta
  (branco se o vértice não tem cor).
- Fog: `vFogDepth = (uWV * pos).z`.
- Transformação de textura (`D3DTS_TEXTUREn` + `TEXTURETRANSFORMFLAGS`).

### Tradutor vs_1_x → GLSL (`vs11_glsl.cpp`)

- Aceita só versão 1.x (`0xFFFE01xx`); em SM1 o tamanho da instrução não é codificado,
  então há tabela de operandos por opcode.
- Registradores: tipo = `((tok >> 28) & 7) | ((tok >> 8) & 0x18)`; número = `tok & 0x7FF`;
  máscara de escrita `(tok >> 16) & 0xF`; swizzle `(tok >> 16) & 0xFF`; modificador
  `(tok >> 24) & 0xF` (1 = negação); bit `0x2000` = endereçamento relativo `c[a0.x + n]`.
- Opcodes: mov add sub mad mul rcp rsq dp3 dp4 min max slt sge exp log lit dst lrp frc
  m4x4 m4x3 m3x4 m3x3 m3x2 expp logp, `dcl`, `def`, comentários.
- Saídas: `oPos` → `gl_Position` com `z = 2z - w` e `y *= uYFlip` (-1 em RT); `oD0` → `vColor`;
  `oT0/oT1` → `vUV0/vUV1`; `oFog` → `vFogDepth` (FS em modo 100 quando `FOGTABLEMODE=NONE`).
- Tradução no primeiro draw com o shader; falha = log `[VS] traducao FALHOU` e draw pelo FFP.
- Entradas usadas sem `dcl` (shaders estilo DX8) são declaradas automaticamente.

Skinning do WYD (`CMesh.cpp`): `c0 = (1, power, progresso, escala_indice)`, `c1` = direção de
luz, `c2..c5` = projeção transposta, `c9 + 3·i` = osso i (3 linhas, já × view), `c92..c95`
= inversa da view. Índices de osso chegam em `BLENDINDICES` (UBYTE4 no Linux com o patch
`v2.zyxw → v2.xyzw` e `c0.w = 3`; D3DCOLOR × 765 no Windows original).

## Rede

- `socketInitializeDefault()` no `main` (sem ela, todo `socket()`/`connect()` falha).
- `CPSock::ConnectServer` → `bind(INADDR_ANY)` → `connect` bloqueante → `WSAAsyncSelect`
  (polling em `socket_poll_linux.cpp`). Logs `[NET] connect OK/FALHOU host:porta errno`.
- Servidor precisa escutar no IP da LAN (não `127.0.0.1`) e o firewall do host precisa
  liberar a porta (7.48: 8281).
