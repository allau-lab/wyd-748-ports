# Portar outra versão do WYD para Switch

Referência: um port Linux estável da mesma família (DXVK Native + `platform/linux/compat/`).
Chamar a versão nova de `<ver>` (ex.: 762) e a pasta de assets no SD de `client<ver>`.

## 1. Pré-requisitos

```text
- [ ] A versão compila e roda no Linux com o mesmo esquema do WYDLINUX (WYD_LINUX,
      platform/linux/compat, cmake/tmproject_sources.cmake, cmake/tmproject_includes.cmake)
- [ ] Client completo da versão (assets) disponível para copiar ao SD
- [ ] Docker com devkitpro/devkita64
- [ ] Servidor da versão rodando na LAN
```

Se ainda não houver port Linux, fazê-lo primeiro: o Switch reaproveita os shims Win32,
D3DX, GDI e Winsock dele.

## 2. Copiar a camada Switch

Do 7.48 para a árvore da versão nova:

| Origem (7.48) | Ajustes |
|---|---|
| `platform/switch/` inteiro | nenhum no backend; em `client_main_switch.cpp` trocar `sdmc:/switch/client748` e os nomes `wyd748_diag.txt`/`wyd748_ev.txt` |
| `switch/CMakeLists.txt` | `TM_ROOT`, nome do target/NRO, `NAME` do NACP, `WYD_SWITCH_VERSION "0.1.0"`, filtros de fontes excluídas |
| `scripts/build-switch-docker.sh` | nome do `.nro` de saída |
| `scripts/deploy-switch-ftp.sh` | `WYD_SWITCH_NRO` / `WYD_SWITCH_FTP` |
| `platform/linux/compat/wingdi_linux.cpp` | lista `FindFontFile` com a pasta nova do SD |
| `TMProject/.../CPSock.cpp` | logs `[NET]` (caminho do diag) |

Buscar e trocar todos os caminhos fixos:

```bash
rg -n 'client748|wyd748' platform/switch platform/linux/compat switch scripts
```

## 3. Inventário do que muda entre versões

Rodar na árvore do jogo e comparar com o 7.48:

```bash
# Caminhos de dados
rg -n 'constexpr const char\*.*_Path' --glob '*TMPaths.h'
# Vertex shaders e declarações usados pelo RenderDevice
rg -n 'ShaderSkinMesh_Path|CreateVertexShader|CreateVertexDeclaration|VertexDecl[0-9]' --glob '*RenderDevice.cpp'
# Quem usa vertex shader na hora do draw
rg -n 'SetVertexShader\(|SetVertexShaderConstantF' --glob '*.cpp'
# Chave e formato do serverlist
rg -n -A12 'BASE_InitializeServerList' --glob '*Basedef.cpp'
# Estados que o 3D pede (filtros, fog, luz)
rg -n 'D3DSAMP_MINFILTER|D3DRS_FOGVERTEXMODE|D3DRS_AMBIENT|SetLight\(' --glob '*.cpp'
# Pixel shaders (o backend NÃO os executa hoje)
rg -n 'CreatePixelShader|SetPixelShader\(' --glob '*.cpp'
```

Pontos de atenção:

- **Vertex shaders**: validar todos os `.bin` da versão com `scripts/vs11-check.sh`.
  Se algum usar vs_2_0+, estender `vs11_glsl.cpp` (o tamanho da instrução passa a vir nos
  bits 24–27 e o endereçamento relativo ganha um token extra).
- **Pixel shaders** (`Shader/pseffect*.bin`): o backend ignora e usa o combiner FFP. Se a
  versão depender deles para algo visível (efeitos, água), é o próximo tradutor a escrever.
- **Declarações de vértice**: tipos novos exigem caso em `FetchElement` (caminho VS) e em
  `RepackVertex`/`typeSize` (caminho FFP).
- **serverlist.bin**: tamanho `grupos × servidores × 64` e chave podem mudar; ajustar
  `scripts/serverlist.py`.
- **Fonte**: tamanho do atlas (`TMFont2`) e formato (A4R4G4B4 no 7.48).
- **Porta do servidor**: `TM_CONNECTION_PORT` em `Basedef.h`.

## 4. SD do console

```text
sdmc:/switch/client<ver>/
├── <nro>
├── (client inteiro da versão)
├── UI/NanumBarunGothic.ttf   ← TTF real, não stub LFS (conferir tamanho > 1 MB)
└── fonts/DejaVuSans.ttf
```

TTF real no host: `/usr/share/fonts/truetype/nanum/NanumGothic.ttf`,
`/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf`.

## 5. Subir por fases

Seguir as fases F0→F6 do `SKILL.md` com o laço build → conferir → deploy → evidência.
Para cada defeito, consultar `sintomas.md` primeiro. Registrar cada build publicada no
`CHANGELOG_PORT.md` da versão (evidência, causa, correção, versão).

## 6. Diferenças esperadas por família de versão

| Família | O que costuma mudar |
|---|---|
| 7.4x / 7.5x | quase nada além de assets e `serverlist.bin` |
| 7.6x+ | mais efeitos com pixel shader, novos formatos de malha, mais constantes de VS |
| Clients com proteção / packer | caminhos e bins cifrados diferentes; resolver no port Linux antes |
