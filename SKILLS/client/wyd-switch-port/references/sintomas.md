# Sintoma → causa → correção

Todos observados no console durante o port 7.48 (versões entre parênteses; detalhes no
`CHANGELOG_PORT.md`). Antes de investigar, **confirmar no diag que a linha `[BUILD]` é a
versão que acabou de ser enviada** — o diag antigo fica no SD até o app ser reaberto.

## Boot e build

| Sintoma | Causa | Correção |
|---|---|---|
| App abre e fecha na hora | FS/VS não compila (ex.: uniform usado sem declarar) → `CreateDevice` falha | Ler `[GLES9] shader compile FALHOU` no diag; declarar o uniform |
| Mudou o código e "nada mudou" no console | build falhou em silêncio e o deploy mandou o `.nro` antigo | Checar erros no log, `strings` com a versão e md5 **antes** do deploy |
| Versão não muda no binário | `WYD_SWITCH_VERSION` como `CACHE` no CMake ignora o valor novo | Usar `set()` simples; apagar a linha do `CMakeCache.txt` |
| Erro `std::fopen is not a member` | macro do jogo redefine `fopen` | usar `::fopen` |
| Diag sumiu / sobrescrito | escrever marcador por cima do diag | nunca sobrescrever; usar o espelho `wyd748_ev.txt` |
| Shader velho depois de trocar GLSL | cache do Mesa no SD | `MESA_SHADER_CACHE_DISABLE=true` durante o desenvolvimento |

## UI 2D e texto

| Sintoma | Causa | Correção |
|---|---|---|
| Tela toda em falso-cor "infravermelho" | PROBE em modo UV (`fract(uv)`) por padrão | PROBE padrão = NORMAL (2); X alterna |
| Sem texto nenhum | TTF do repositório é **stub do Git LFS** (≈130 bytes); ou caminho Linux (`/usr/share/fonts`) | Copiar TTF real (NanumGothic para CP949) para `UI/` e `fonts/` no SD; `FindFontFile` procura caminhos relativos e `sdmc:/…`; log `[FONT] ok` |
| Painéis da UI como retângulos brancos | estágio sem textura retornava branco; ALPHAOP ignorado | Estágio sem textura = `(0,0,0,1)`; combiner com COLOROP/ALPHAOP separados (0.2.4) |
| Atlas de fonte vazio no 1º upload | upload antes do `LockRect` preencher | só subir textura com `dirty` e `cpu` não vazio |
| UV da UI muito fora de 0..1 | falta `D3DTS_TEXTUREn` + `TEXTURETRANSFORMFLAGS` | aplicar matriz de textura no VS |
| Milhares de uploads de textura, FPS baixo | `UnlockRect` marcava sujo mesmo com `D3DLOCK_READONLY` | só marcar sujo sem READONLY |

## 3D estático (login / castelo)

| Sintoma | Causa | Correção |
|---|---|---|
| Geometria plana, só cor do vértice | `uniform int uTexOn[3]` + `glUniform1iv` ignorado pelo **Mesa 20.1 NV120** | uniforms `int` **escalares** (`uTexOn0/1/2`, `uColorSt0…`) |
| Silhueta chapada com 2 estágios | `glBindTexture` no upload sem `glActiveTexture` → estágio 1 sobrescreve unidade 0 | `glActiveTexture(i)` antes de todo upload/bind; rebind final das unidades |
| Faces de trás visíveis / objetos atravessados | depth test nunca ligado | consultar depth bits do FB 0 e ligar Z |
| Metade das faces somem | cull invertido no backbuffer | `frontFace` depende de o alvo ser RT (Y invertido) ou não |
| Castelo com cor única (névoa) | fog calculado com z de mundo | `vFogDepth = (WV * pos).z` |
| **Texturas do cenário pretas**, UI ok | jogo pede `MINFILTER=ANISOTROPIC` (config `[ANISOTROPIC]`, padrão 8); backend usava `GL_LINEAR_MIPMAP_LINEAR` com só o nível 0 → textura incompleta | filtros sem mipmap + `GL_TEXTURE_MAX_LEVEL=0` (0.2.5) |
| Cena inteira amarelada | luzes pontuais (tochas `TMLight`, Range 4) sem alcance/atenuação; direcional usando `Position`; ambiente de luz ignorado | iluminação D3D9 completa, 8 luzes (0.2.6) |
| Objetos sem luz com tom errado | caminho sem iluminação somava emissiva + ambiente | sem iluminação = cor do vértice |

## Personagens

| Sintoma | Causa | Correção |
|---|---|---|
| **Mobs não respawnam** | CreateMobCompat early-return no mesmo MobID enquanto DelayDelete | revive no Compat (0.2.16) |
| **Touch não equipa** | sem DBLCLK / Ctrl no touch | SmartMoveItem 2 toques + drag DOWN (0.2.16) |
| **FPS/MACRO cobrindo HP/chat** | Hud9 topo-esq + MACRO HUD + g_bDebugMsg | desligados no Switch (0.2.16) |
| **Monstros/NPCs invisíveis** | mesh/msa/bon ausente no SD (lista LFS 7.48) | `[MISSING]` no diag; copiar `mesh748_add` |
| **Mundo some / paredes “transparentes” mid-frame** | `Clear` GLES sem scissor (portrait RT) | Clear com scissor no viewport (0.2.15) |
| **Macro sem feedback** | só botão A ciclava sem UI | aba MACRO + HUD (0.2.15) |
| **Personagens como raios/lanças saindo do centro** | malhas skinned usam vertex shader vs_1_1 (`Shader/skinmesh*.bin`); backend desenhava pelo FFP ignorando pesos/índices de osso | traduzir vs_1_x → GLSL (`vs11_glsl.cpp`) e montar `vN` pela declaração (0.2.7) |
| Membros soltos / índices trocados | `BLENDINDICES` como D3DCOLOR com swizzle `.zyxw` vs UBYTE4 | manter consistente com o `RenderDevice` (patch Linux `v2.zyxw → v2.xyzw`, `c0.w = 3`) |
| Monstros gigantes | `c0.w` (escala de índice) não setado em caminhos alternativos | `CMesh` seta `c0` antes de cada draw (já no port Linux) |

## Rede

| Sintoma | Causa | Correção |
|---|---|---|
| "Connection Failed" e nenhuma linha de rede no diag | libnx sem `socketInitializeDefault()` | inicializar no `main`; logar IP via `nifmGetCurrentIpAddress` |
| Timeout conectando | servidor escuta em `127.0.0.1` ou firewall do host | `ss -ltnp` mostra o IP da LAN; liberar a porta (7.48: 8281) |
| Lista de servidores errada | `serverlist.bin` aponta para outro IP | `scripts/serverlist.py` (chave de 64 bytes do `BASE_InitializeServerList`) |

## UI e input

| Sintoma | Causa | Correção |
|---|---|---|
| Painéis de borda somem (selchar sem menu esquerdo, Field sem barra inferior); toque/clique deslocado | `[RES]` do config ≠ backbuffer real (ex.: 1600×1024 vs 1280×720) | forçar `nResIndex` da resolução da NWindow no `NewApp` (0.2.8) |
| Toque não faz nada | `hidInitializeTouchScreen()` ausente, ou toque injetado só como `WM_*` (HUD nunca recebe) | inicializar touch; alimentar `WYD_Linux_TouchHudPointer` antes do mouse |
| Cursor invisível com `[TOUCH_UI] 1` | `SControlContainer` esconde o cursor com HUD de toque | não esconder no `__SWITCH__` |

## Método quando o sintoma é novo

1. Pedir/puxar captura (`shots/`) e diag; nunca adivinhar pela descrição.
2. Localizar no jogo o código que desenha aquilo (cena, `RenderDevice`, `CMesh`, `TMMesh`)
   e listar o estado D3D9 que ele seta (RS, TSS, sampler, shader, decl).
3. Comparar com o que o backend faz para cada item; a divergência é a causa.
4. Adicionar um log de uma linha que prove a hipótese, se ela não for óbvia no código.

- **Stick anda impreciso / desvia em teleporte ou ponte** → stick estava usando clique distante
  (`IssueMoveToPick` 6 células). Usar `IssueStickStep` (linha reta + parada ao soltar). (0.2.12)
- **Toque em NPC/mob/chão acerta o lugar errado** → move+press no mesmo frame; pick usa cursor do
  frame anterior. Atrasar o press 2 frames. Para UI, ver `[UI] lbtn` no diag. (0.2.12)
- **Ícones de item deslocados, pior quanto mais à esquerda/direita (widescreen)** → constante 4:3
  (6.68) em `TMMesh::RenderForUI`. Usar `100*tanf(0.05f)*aspect` para aspect > 1.4. (0.2.13)
- **Sem som nenhum (SFX e música)** → hint de driver forçado "pulse"/"alsa" em `dsutil_linux`
  quebra o driver `switch`; e/ou `sound/`+`music/` ausentes no SD (checar `[AUDIO]` no diag e
  `SOUND`/`MUSIC` > 0 no config.txt). Assets 7.48 reais: validar contra ponteiros LFS. (0.2.14)
- **Travada de segundos ao trocar de mapa / RAM subindo com música** → BGM decodificando MP3
  inteiro ou callback SDL3 tratando bytes como frames. Streaming + `additional_amount/4`. (0.2.14)
