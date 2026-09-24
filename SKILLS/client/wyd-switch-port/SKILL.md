---
name: wyd-switch-port
description: >-
  Porta clients With Your Destiny (WYD / TMProject, qualquer versão: 7.48, 7.56,
  7.62…) para Nintendo Switch homebrew (.nro, libnx, Atmosphere) com backend
  D3D9→OpenGL ES 3 sobre SDL3, build em Docker devkitA64 e deploy por FTP.
  Cobre texturas WYT/WYS/DXT, fontes, combiner FFP, iluminação, fog, vertex
  shaders vs_1_1 (skinning), rede libnx e serverlist.bin. Usar quando o usuário
  mencionar Switch, libnx, .nro, homebrew, Joy-Con, portar outra versão do WYD,
  ou render quebrado no console (texturas pretas, caixas brancas, tom errado,
  personagens explodidos, Connection Failed).
disable-model-invocation: false
---

# Port do client WYD → Nintendo Switch

Idioma do port, logs e docs: **português**.

## Arquitetura que funciona (não reinventar)

O client WYD é um jogo **Direct3D 9** (TMProject). No Switch não há DXVK nem
Vulkan loader de desktop, então o port implementa **um objeto `IDirect3DDevice9`
próprio sobre GLES 3.2** (Mesa nouveau / NV120 do Tegra X1). O código do jogo
continua chamando D3D9 normalmente.

```text
TMProject (jogo, intacto)  ──D3D9──►  platform/switch/d3d9_gles_switch.cpp  ──GLES3──►  Mesa/Tegra
      │ Win32/GDI/Winsock                    ├─ FFP emulado em GLSL (VS + FS únicos)
      ▼                                      ├─ vs11_glsl.cpp: vs_1_x → GLSL (skinning)
platform/linux/compat + platform/switch/compat   └─ gl_hud9.cpp: HUD versão/FPS/erros
SDL3 (switch-sdl-3.4): janela, EGL, input, áudio
```

Mapa completo de arquivos e do backend: [references/arquitetura.md](references/arquitetura.md).

**Não fazer:** SDL_Renderer, SDL_ttf, "trocar para OpenGL 2D", DXVK no Switch,
dois backends em paralelo, reescrever TMProject em massa, instalar devkitPro no host.

## Regras

- Build **só em Docker** (`devkitpro/devkita64`). Nada de `dkp-pacman` no host.
- Código novo em `platform/switch/` (ou `platform/linux/compat` quando serve aos dois);
  no jogo, só `#ifdef WYD_LINUX` / `__SWITCH__` pontuais.
- Não mexer em lógica de servidor nem gameplay.
- **Toda build publicada incrementa a versão** (`WYD_SWITCH_VERSION` no
  `switch/CMakeLists.txt`, `set` simples — nunca `CACHE`). Ela aparece no NACP, no HUD
  e na linha `[BUILD]` do diag. Sem isso não dá para saber qual binário o usuário testou.
- Nunca afirmar que algo "funciona" sem evidência do console (captura ou diag).

## Laço de trabalho (sempre este)

```text
editar → bump versão → build Docker → conferir binário → deploy FTP → usuário abre →
puxar diag + capturas → analisar → próximo passo
```

1. **Build:** `./scripts/build-switch-docker.sh` (arquivo `.cpp` novo no CMake →
   `./scripts/build-switch-docker.sh reconfigure`).
2. **Conferir o binário antes do deploy** — o script de deploy envia o `.nro` que
   existir, mesmo se o build falhou:
   ```bash
   rg ' error|undefined reference' /tmp/build.log
   strings OUT/switch/wyd748.nro | grep -cx '0\.2\.7'   # versão nova presente
   md5sum OUT/switch/wyd748.nro                          # mudou em relação ao anterior
   ```
3. **Deploy:** `./scripts/deploy-switch-ftp.sh` (FTP do console só existe quando o
   usuário liga; se cair, avisar e seguir com outra tarefa). Depois do envio, baixar o
   `.nro` remoto e comparar md5.
4. **Evidência:** [scripts/puxar-evidencia.sh](scripts/puxar-evidencia.sh) baixa
   `wyd748_diag.txt` e as capturas `shots/*.tga` e converte para PNG. **Olhar a
   imagem** antes de concluir qualquer coisa.

Diagnóstico embutido no backend (manter ao portar):

| Recurso | Onde |
|---|---|
| `sdmc:/switch/<pasta>/wyd748_diag.txt` | truncado no boot; `SLog` também espelha em `wyd748_ev.txt` |
| Capturas TGA automáticas (`[SHOT]`) | `MaybeCaptureFrame()` no `Present`, `glReadPixels` → `shots/shotN.tga` |
| HUD | versão, FPS, texturas, último erro; **X** alterna PROBE (UV / SCREEN / NORMAL) |
| Logs-chave | `[BUILD]`, `[FONT]`, `[NET]`, `[VS]`, `[GLES9] FFP ok`, `bind c0=… a0=…`, `upload HW DXT` |

## Fases e critério de aceite

Copiar e acompanhar:

```text
- [ ] F0 Boot: [BUILD] com versão nova, assets achados, FFP compila (FFP ok)
- [ ] F1 UI 2D: tela de servidores com painéis texturizados e texto legível
- [ ] F2 3D estático: castelo/login com texturas, cores corretas, profundidade ok
- [ ] F3 Rede: login no servidor (Connection Failed resolvido)
- [ ] F4 Personagens: seleção de personagem com malhas skinned corretas
- [ ] F5 Campo: andar, NPCs, efeitos, alpha/blend
- [ ] F6 Controles: Joy-Con + touch, safe zone, docked/handheld
```

Em cada fase, os defeitos conhecidos e suas causas estão em
[references/sintomas.md](references/sintomas.md). **Consultar essa tabela antes de
investigar do zero** — quase todo defeito visual já apareceu no 7.48.

Resumo das armadilhas mais caras (detalhes na tabela):

1. Diag antigo persiste até o usuário reabrir o app — confirmar `[BUILD]` com a versão nova.
2. Textura criada com mip >1 mas só o nível 0 enviado + filtro `*_MIPMAP_*` (o jogo pede
   ANISOTROPIC) → textura incompleta → **preta**. Mapear para `GL_LINEAR` e
   `GL_TEXTURE_MAX_LEVEL=0`.
3. Estágio sem textura em D3D9 amostra `(0,0,0,1)`, não branco; COLOROP e ALPHAOP são
   independentes; `D3DTA_COMPLEMENT`/`ALPHAREPLICATE` não podem ser mascarados.
4. Luz direcional usa `-Direction` (não `Position`); luz pontual tem `Range` e
   atenuação — sem isso tochas pintam a cena inteira de amarelo.
5. Fog de vértice usa z da **view**, não do mundo.
6. Personagens usam **vertex shader vs_1_1** (skinning com `m4x3 c[a0.x+9]`). Sem
   tradução para GLSL, a malha "explode" em raios.
7. libnx exige `socketInitializeDefault()` antes de qualquer socket.
8. Fontes do repositório podem ser **stubs do Git LFS** — copiar TTF real.

## Portar outra versão do WYD

Seguir [references/nova-versao.md](references/nova-versao.md). Em resumo:

1. Garantir que a versão já compila no Linux (`WYDLINUX` equivalente, `WYD_LINUX`).
2. Copiar `platform/switch/`, `switch/CMakeLists.txt` e os dois scripts; trocar
   `TM_ROOT`, nome do `.nro`, pasta do SD e a lista de fontes do jogo.
3. Inventariar o que muda entre versões: caminhos (`TMPaths.h`), shaders
   (`Shader/skinmesh*.bin`, `shader*.bin`), declarações de vértice do `RenderDevice`,
   chave do `serverlist.bin`, formatos WYT/WYS, tamanho do atlas de fonte.
4. Validar os shaders da versão no host com
   [scripts/vs11-check.sh](scripts/vs11-check.sh) antes do primeiro build.
5. Subir fase por fase (F0→F6) com o laço acima. Versão do binário recomeça em 0.1.0.

## Utilitários

| Script | Uso |
|---|---|
| [scripts/puxar-evidencia.sh](scripts/puxar-evidencia.sh) | baixa diag + capturas do console e converte TGA→PNG |
| [scripts/serverlist.py](scripts/serverlist.py) | decodifica / reescreve IP do `serverlist.bin` |
| [scripts/vs11-check.sh](scripts/vs11-check.sh) | traduz todos os vertex shaders da versão e valida com `glslangValidator` |

## Referências

- [references/arquitetura.md](references/arquitetura.md) — arquivos, backend D3D9→GLES, FFP, tradutor vs_1_1
- [references/sintomas.md](references/sintomas.md) — sintoma → causa → correção (com evidência)
- [references/nova-versao.md](references/nova-versao.md) — checklist para outra versão
- [references/ui-switch.md](references/ui-switch.md) — UI de console, safe zone, Joy-Con/touch
- [CHANGELOG_PORT.md](CHANGELOG_PORT.md) — histórico do port 7.48, versão a versão
