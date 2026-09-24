# CHANGELOG — Port Switch (WYDLINUX 7.48)

## 2026-09-23 — v0.2.23 (personagens sumidos: ItemList + CreateMobCompat)

- **Causa**: ItemList do Cliente 748 KR zerou ~197 meshes custom (fantasias 4150+,
  NPCs Anubis/etc.). Personagem com costume → CoatMesh=0 → some. Meshes de face
  base eram iguais; o bug era costume/equip custom.
- **Fix**: restaurado `ItemList.bin` pré-sync (`57f218b5…`) que tinha os meshes
  do servidor privado. KR fica em `ItemList.bin.bak-cliente748-korean`.
- **CreateMobCompat**: deixa de rematerializar vivo em todo head diverge (Init
  agressivo); só revive (morto/DelayDel) ou head preso em 0 com equip≠0.

## 2026-09-23 — v0.2.22 (CreateMobCompat: refresh skin, trade 230, DelayDel)

- **CreateMobCompat**: limpa `m_dwDelayDel` antes do equip; duplicata viva com head
  diferente rematerializa (fim do TK default preso); CreateMobTrade aplica `Equip[0]=230`
  como o path full; `Server` lido corretamente do pacote trade.
- Segue recomendações do audit CreateMobCompat (após ItemList 0.2.21).

## 2026-09-23 — v0.2.21 (ItemList Cliente 748, ícone WYD.exe, teleporte A/B)

- **ItemList.bin**: sync do Cliente 748 oficial (md5 `6b3a3c61…`) — o da árvore
  Switch divergia em ~59 KB e mapeava mesh/face errados (NPCs como TK/ORC ou
  “sumidos”).
- **Ícone NRO**: `switch/icon/icon_256.jpg` extraído do `WYD.exe` nativo.
- **Teleporte**: com MessageBox aberto, **A=Sim / B=Não**; botões Sim/Não maiores no toque.

## 2026-09-23 — v0.2.20 (desequipar, HP bars, painel Y, CONTROLE completo)

- **Desequipar**: 2 toques no item equipado volta a funcionar (sem filtro `LastTouchWasTap`);
  1º UP com item anexado não cai no `SwapItem` da mesma célula (arraste→inventário ok).
- **HP/MP overhead**: cores saturadas no Switch (verde/azul legíveis no OLED).
- **Painel Y**: fonte escala por `min(W,H)` ratio (fim do texto sobreposto em 16:9);
  botões "+" maiores; handlers aceitam IDs nativos 1073… e 65716….
- **HELP/CONTROLE**: abrir H vai direto à aba CONTROLE; lista expandida com quase todas as
  funções de teclado do Field (poções, correr, skills 0–9, guild, dash, chat size, etc.).

## 2026-09-23 — v0.2.19 (fecha auditoria: VBO ring, CreateMobCompat, -O2, chat)

- **Perf**: ring de 3 VBO/EBO + orphan (`glBufferData(nullptr)` + `SubData`) — fim do
  STREAM no mesmo buffer a cada draw. Visual idêntico.
- **CreateMobCompat**: montaria Equip[14], guild mark, espelho arma tipo 41, chaos/kills
  no MobName, altura `GroundGetMask` (paridade com path full).
- **Build**: `-O2` no client real (antes `-O1`).
- **Chat ZL**: Enter automático após swkbd — um toque envia.
- **ABI**: `sprintf_s` em MobName/Nick no CreateMob full.
- Residual fora de código: ~295 meshes LFS sem fonte no SD (precisa dos arquivos).

## 2026-09-23 — v0.2.18 (Look zone, chat notice/alpha, abas H)

- **Look**: raio menor (borda direita); câmera só após arrastar (~18 px) — taps em mobs no
  centro-direita passam.
- **Chat notice**: corrige bug que escrevia Y em `m_nPosX` três vezes; agora `SetPos(X, Y)`.
- **Chat alpha**: no Switch o ciclo para em 0xAA e volta ao transparente (sem preto cheio).
- **Abas H**: CONTROLE/MACRO descem para 2ª linha se não cabem à direita da 4ª aba.

## 2026-09-23 — v0.2.17 (touch UI sem delay, macro síncrono, Album, GL cache)

- **Touch**: delay de 2 frames só no pick 3D; painéis (inventário/cargo/shop/skills) pressionam
  na hora. Double-tap / SmartMove só se o dedo moveu <12 px (arrasto não conta como tap).
- **Macro**: `WYD_Linux_SetMacroMode(0/1/2)` síncrono (`g_GameAuto` + DN) — sem fila WM_CHAR.
  Ciclo A e menu MACRO usam a API nova.
- **Album / RAM**: aviso no chat ao entrar no Field se applet ≠ Application. Em low-mem, UI +
  effect ficam residentes; só model/env fazem thrash.
- **GLES**: `ApplyGlState` diferencial (blend/depth/cull/scissor só quando mudam).
- **ATK HUD**: botão Attack do overlay chama `FireAttack()` (antes ligava macro por engano).
- **Crash .bon**: `MeshManager` não lê handle inválido se o arquivo de bone faltar no SD.

## 2026-09-23 — v0.2.16 (respawn mobs, touch inventário, HUD limpo)

- **Respawn**: `OnPacketCreateMobCompat` ignorava CreateMob se o MobID ainda existia
  (mob morto antes do DelayDelete ~10s). Agora revive o nó (`Init` + reequipa).
- **Touch inventário**: 2 toques = `SmartMoveItem` (equipa / desequipa / inventário↔cargo
  se guarda-carga aberto). No Switch, DOWN anexa o item para arrastar e soltar noutro slot.
- **HUD**: indicador MACRO permanente desligado (só aba H/L3). Hud9 de FPS/TX desligado por
  padrão (`WYD_SWITCH_HUD=1` reativa, canto inferior direito). `g_bDebugMsg=0` no Switch
  (FPS Objects não cobre o chat).
- Meshes 7.48 (`mesh748_add` 736 arquivos) enviados ao SD.

## 2026-09-23 — v0.2.15 (macro visível, meshes 7.48, Clear com scissor, assets residentes)

- **Macro**: aba MACRO ao lado de CONTROLE no painel H; HUD fixo `MACRO OFF/DN/MG | PK ON/OFF`
  (canto superior esquerdo). L3 abre o menu do macro. Explica regra PK original (zona PK, fora
  da cidade, grupo/guilda/aliada/comércio nunca; PK off = só guerra). A = ciclo MG/DN/OFF.
- **Monstros/meshes**: diag `[MISSING] kind path` (msa/msh/bon/texturas) em `wyd748_diag.txt`.
  736 meshes 7.48 recuperados → `OUT/switch/mesh748_add/` (upload em `client748/mesh/`).
- **Paredes / Clear**: `IDirect3DDevice9::Clear` no GLES agora aplica scissor no viewport (e em
  `pRects`). Antes `glClear` apagava o FBO inteiro — portrait mid-frame limpava o mundo.
  Translucência de casa a ≤6 células continua (comportamento original 7.48 em `TMHouse::Render`).
- **RAM**: `WYD_Switch_KeepAssetsResident` se processo ≥1.5 GB (lançar via title override / R no
  jogo) — texturas UI/effect/model/env e meshes não são liberados em `ReleaseNotUsing*`.
- **SelChar**: botões Criar/Delete/Esc deixam de ser reposicionados à força (filhos (0,0) dos
  painéis 5654/5655/5672) — textos voltaram.

## 2026-09-23 — v0.2.14 (som: driver Switch, BGM em streaming, assets 7.48)

- Sem som porque `CSoundManager::Initialize` (dsutil_linux) forçava `SDL_HINT_AUDIO_DRIVER`
  "pulse" e depois "alsa"; no Switch o driver é `switch` (SDL3 tem `SWITCHAUDIO_bootstrap`).
  Ambos falhavam → `g_pSoundManager = nullptr`; o hint "alsa" ficava setado e o BGM
  (`DS_SOUND_MANAGER`) também falhava no `SDL_InitSubSystem`. Hints agora só fora do `__SWITCH__`.
- `WYD_BgmStreamCallback` tratava bytes como frames → enfileirava 4× o pedido a cada callback
  (fila SDL crescendo sem limite, volume/troca de faixa atrasando). Usa `additional_amount / 4`.
- BGM MP3 44.1 kHz agora em streaming (minimp3 frame a frame no callback); antes decodificava a
  faixa inteira (town01 = ~36 MB PCM, segundos de CPU no Switch) segurando o mutex do áudio.
  Outras taxas caem no decode completo antigo.
- `FixPath` de SFX/BGM usa `WYD_NormalizePath` (mesma resolução do `Wyd_fopen`).
- Diag: `[AUDIO] OK driver=… freq=… ch=…` / falhas de init / soundlist / LoadWAV.
- Assets: `client748/` não tinha `sound/`; o `codigo-fonte/CLIENTS/WYD` só tem ponteiros Git LFS.
  Montado `OUT/switch/audio748/` (116 MB) validado por SHA-256 contra os ponteiros LFS 7.48:
  `soundlist.txt` + 13 MP3 de `WYD/Game`, 349 WAV de WYDESTINY/WYD 2.0 (bit a bit iguais).
  `deploy-switch-ftp.sh --audio` envia o pacote.

## 2026-09-22 — v0.2.13 (ícones 3D da UI alinhados em 16:9)

- Causa do desalinhamento que crescia para as bordas: `TMMesh::RenderForUI` (ícones 3D de itens
  no inventário/loja/baú/barra) converte X de pixel para o espaço da câmera de UI com constante
  fixa 6.68 = largura visível em 4:3 (câmera do `SetMatrixForUI`: FOV 0.1 a distância 50 →
  altura ≈ 5.0; largura = 5.0 × aspect). Em 16:9 a largura real é ≈ 8.9, então o ícone ficava a
  75% da distância real do centro (certo no meio, errado nas bordas, simétrico).
- Fix: para aspect > 1.4 usa `100 * tanf(0.05f) * aspect`; 4:3 (6.68) e 5:4 (6.26) mantêm os
  valores originais do Ghidra.

## 2026-09-22 — v0.2.12 (movimento preciso no analógico + atraso do toque)

- L-stick (Joy-Con) e stick do HUD touch não "clicam" mais 6 células à frente. Novo
  `TMFieldScene::IssueStickStep(dirX, dirY, stop)`: passo em linha reta na direção do stick,
  cortado no 1º obstáculo por `BASE_GetHitPosition` (mesmo teste do `MoveAttack`), sem marcador
  de clique e sem desvio de rota. Alcance = distância andada no intervalo de rota do `GetRoute`
  (1000 ms / 500 ms vel. 4–5 / 100 ms vel. 6–7), limitado a 2–5 células, para não travar.
- Soltar o stick = parar: `WYD_Linux_PadStop` → `IssueStickStep(..., stop=true)` com `bStop=1`
  (o `GetRoute` aceita na hora) mirando a célula imediatamente à frente.
- Toque: o botão esquerdo é pressionado 2 frames após posicionar o cursor. O campo resolve o
  pick 3D (chão/NPC/mob) no `FrameMove` a partir da posição anterior; com move+down no mesmo
  frame o clique caía onde o cursor estava antes. Toque curto solto antes disso é reenviado.
- Diagnóstico em `wyd748_diag.txt` (até 300 linhas cada): `[TOUCH] down x,y hud=0/1` e
  `[UI] lbtn x,y -> id=… type=… rect=…` (todo controle que aceitou o clique) para mapear
  pontos de UI ainda desalinhados.

## 2026-09-22 — v0.2.11 (aba CONTROLE na ajuda + remapeamento)

- Painel de ajuda (H) ganhou a aba **CONTROLE** (botão criado ao lado da 4ª aba nativa, lista
  própria com botões /\ e \/ para rolar). Mostra o mapa atual de cada botão.
- Remapeamento: tocar numa linha (ou D-pad + A com a aba aberta) abre a lista de ações;
  escolher grava em `sdmc:/switch/client748/controls.cfg` (`BOTAO=acao`). "Restaurar padrão"
  volta ao mapa da 0.2.10. B volta da lista de ações. Fixos: L-stick andar, Plus = Esc.
- `switch_input.cpp`: mapa virou tabela (19 entradas: botões + 4 direções do R-stick como
  botões virtuais) × 25 ações (macro, chat, letras nativas I/C/S/M/P/H/K/Z/X/N/Tab/], skills 1–5,
  atacar, câmera, zoom). Chat funciona em qualquer botão (releitura do pad após o applet).
- Cena de compatibilidade não carregava `interface.txt`/`command.txt`/`etc.txt` nas abas
  nativas da ajuda; agora carrega (só se a lista estiver vazia).

## 2026-09-22 — v0.2.10 (novo mapa de botões + chat com teclado virtual)

- Mapa definido pelo usuário. Letras injetadas como `WM_CHAR` (mesmo caminho do teclado):
  X = I (inventário), Y = C (personagem), B = S (skills), ZR = M (mapa), R3 = X,
  R-stick cima/baixo/esq/dir = P (grupo) / H / K (PK nativo `OnKeyPK`) / Z.
- A alterna macro: OFF → MG (C.C mágico nativo, tecla D → `ToggleNativeCCMode(2)`, que usa
  `AutoSkillUse` e já respeita PK) → DN (auto-ataque com PK da 0.2.9) → OFF.
- ZL: abre o teclado virtual (swkbd libnx); com texto, injeta Enter (abre o chat nativo) + texto;
  ZL de novo envia (Enter). Fora do Field, ZL digita no campo focado (login/senha).
  Com o chat focado, as letras dos botões não disparam atalhos.
- D-pad = skills 1–4, L = skill 5, R = girar câmera, Minus = resetar câmera, Plus = Esc.
  Cursor no Field só por toque (R-stick virou direcional).

## 2026-09-22 — v0.2.9 (macro de auto-ataque refeito, PK original)

- Revisão do macro do `TouchControlsUI` (botão AUTO / B no Joy-Con). Bugs do antigo:
  com PK ligado **pulava** jogadores (lógica invertida); descartava a guilda em guerra
  (que é a inimiga); guardava ponteiro cru do alvo (podia apontar para mob já removido);
  dividia o timer com o stick; brigava com o laço nativo chamando `MoveAttack`/`SkillUse` por conta própria.
- Novo `TMFieldScene::CanAttackHuman`: espelha os portões de `MobAttack` (clique) e de
  `MoveAttack` no modo auto (party, guilda/aliança, zona PK dos dois lados, cidade, trade,
  invocações, torres, guerra de castelo/mantua, guerra de guilda) sem hover nem cooldown.
- Macro agora só escolhe o alvo: liga o auto-ataque nativo (`SetAutoTarget` → `m_cAutoAttack`),
  e o `FrameMove` nativo ataca/persegue via `MoveAttack` com a rotação de skills do painel auto.
  Alvo por ID (`GetHumanByID`), raio de 12 células (mesmo do C.C nativo), jogador atacável
  tem prioridade sobre monstro; alvo manual válido é mantido; troca de alvo quando o atual
  deixa de ser atacável (ex.: PK desligado).
- Stick (Joy-Con ou HUD) pausa o macro por 1,2 s para o jogador poder sair andando.
- Mensagem no chat ao ligar/desligar indicando se o PK está incluído.

## 2026-09-22 — v0.2.8 (resolução travada 720p + input Joy-Con/touch)

- Evidência 0.2.7: personagens renderizam certo (skinning ok); seleção de personagem sem o
  painel esquerdo; Field sem barra inferior; toque "longe" do alvo; HUD de toque não reage.
- Causa comum: `config.txt` com `[RES] 10` (1600×1024). O backend força o backbuffer à NWindow
  (1280×720), mas layout, picking e cursor do jogo continuavam em 1600×1024 → painéis de borda
  fora da tela e clique deslocado. `NewApp` agora força `nResIndex = 4` (1280×720) no `__SWITCH__`.
- Touch: faltava `hidInitializeTouchScreen()`. Multi-touch agora vai primeiro ao
  `TouchControlsUI` (`WYD_Linux_TouchHudPointer`); toque não consumido vira clique esquerdo.
- Joy-Con no Field: L-stick anda (relativo à câmera, `TouchControlsUI::PadMove`), R-stick = cursor,
  A atacar (segurar repete), B auto-ataque, X inventário, Y skills, Minus personagem,
  D-pad skills 1–4, L skill 5, R girar câmera, R3 reset câmera, ZR/ZL clique esq/dir, Plus = Esc.
  Fora do Field: sticks = cursor, A/ZR clique esq, B/ZL clique dir, D-pad setas, Minus = Tab.
- Cursor do jogo continua visível no Switch mesmo com `[TOUCH_UI] 1`.
- PROBE de textura passou de X para L3.

## 2026-09-22 — v0.2.7 (vertex shaders vs_1_1 → GLSL: personagens)

- Evidência 0.2.6: **login funcionou**; castelo com cor correta; personagens na seleção
  aparecem como raios saindo do centro.
- Causa: malhas skinned (`CMesh`) usam vertex shader vs_1_1 (`Shader/skinmesh*.bin`,
  `m4x3 r, v0, c[a0.x+9]`) e o backend desenhava tudo pelo FFP, ignorando pesos/índices de osso.
- Novo `platform/switch/vs11_glsl.cpp`: tradutor SM 1.x → GLSL ES 3.00 (dcl/def, aritmética,
  lit, m4x4/m4x3/m3x3…, endereçamento relativo por a0, saídas oPos/oD0/oT0/oT1/oFog).
  Validado no host: os 20 shaders do client geram GLSL aceito pelo `glslangValidator`.
- Backend: caminho de desenho com vertex shader (entradas `vN` montadas pela declaração,
  `uC[96]` com as constantes, flip Y em RT, fog pelo `oFog`); falha de tradução cai no FFP
  com log `[VS]`.
- `DrawIndexedPrimitive`: faixa de vértices agora parte de `baseVtx + minIdx` (antes lia a
  faixa errada quando `baseVtx != 0`).
- Skill reescrita como guia genérico de port de qualquer versão do WYD.

## 2026-09-22 — v0.2.6 (rede + iluminação D3D9)

- Evidência 0.2.5: texturas 3D ok, tom amarelado; login → "Connection Failed"; nenhum log de rede.
- Rede: o port nunca chamava `socketInitializeDefault()` (libnx) → todo `socket/connect` falhava.
  Agora inicializa no `main`, loga `[NET] socketInitializeDefault rc` e o IP via nifm.
  `CPSock::ConnectServer` loga `[NET] connect OK/FALHOU host:porta errno` no diag.
- Iluminação (causa do amarelado): tochas são luzes pontuais quentes com Range=4, mas o shader
  ignorava alcance/atenuação e iluminava o castelo inteiro. Reescrito conforme D3D9:
  8 luzes, direcional usa `-Direction` (antes usava `Position`), pontual com Range e
  1/(a0+a1·d+a2·d²), ambiente por luz somado ao global, resultado saturado.
- Sem iluminação: saída = cor do vértice (antes somava emissiva + ambiente). Decl sem cor → branco.
- Servidor conferido: `wydserver` escuta em 192.168.1.229:8281, ufw desativado.

## 2026-09-22 — v0.2.5 (texturas 3D: filtro sem mipmap)

- Evidência 0.2.4: menu e texto corretos; castelo/cenário sem textura.
- Causa: `[ANISOTROPIC] 16` no config.txt faz o jogo usar `D3DSAMP_MINFILTER=ANISOTROPIC` no 3D;
  o backend mapeava para `GL_LINEAR_MIPMAP_LINEAR`, mas só o nível 0 é enviado → textura
  incompleta no GLES → amostra preta. UI usa LINEAR, por isso funcionava.
- Correção: ANISOTROPIC → `GL_LINEAR`; `GL_TEXTURE_MAX_LEVEL=0` na criação da textura.
- `serverlist.bin` (local e no Switch) já aponta para 192.168.1.229.

## 2026-09-22 — v0.2.4 (combiner FFP + fog view-space)

- Evidência 0.2.3 (shots): UI aparece (lista de servidores, login), mas 3D chapado e retângulos brancos na UI.
- FS: combiner de texture stages reescrito — COLOROP/ALPHAOP separados, args com modificadores
  COMPLEMENT/ALPHAREPLICATE, ops 2–16/18–21/24, COLOROP DISABLE encerra a cadeia, textura não vinculada = (0,0,0,1).
- Removido hack que forçava MODULATE no stage 0 e máscara `& 0x0F` nos args.
- Fog de vértice: `vFogDepth` agora é z em espaço de câmera (`uWV`), não z de mundo.
- Log `bind` mostra estado empacotado `c0/a0/c1/a1` (op | arg1<<8 | arg2<<16).

## 2026-09-22 — v0.2.3 (depth/cull/clear)

- Depth test estava **sempre desligado** (`zEnable && !bbDirect` com
  `bbDirect=true`): agora consulta os bits de depth do default FB e liga Z
- Cull no backbuffer invertido (culling das faces da frente); RT mantém o inverso
- `Clear` reabilita color/depth/stencil mask antes do `glClear`
- Captura de tela em `sdmc:/switch/client748/shots/shot0..4.tga` (presents
  300/900/1800/3600/7200) — o álbum não grava homebrew em applet
- 0.2.1 confirmou `[FONT] ok UI/NanumBarunGothic.ttf` e atlas com conteúdo
  (`chk=481185`), mas a tela não mudou → problema está no desenho, não na fonte

## 2026-09-22 — v0.2.1 (texto + PROBE default + versão)

- PROBE default = **NORMAL (2)** — antes UV (0) pintava a tela em falso-cor
  “infravermelho” (`fract(uv)` RG)
- Fonte: paths `UI/*.ttf` / `fonts/*.ttf` / `sdmc:/switch/client748/...`
  (homebrew não tem `/usr/share/fonts`)
- `WYD_SWITCH_VERSION=0.2.1` no NACP, `[BUILD]`, HUD (`v0.2.1 FPS…`)

## 2026-09-22 — Replan texturas (PROBE + S3TC HW + UV)

Estratégia: evidência on-screen → S3TC nativo Tegra → UV. Sem chute-and-patch.

### Fase 0 — PROBE visual (WYD_TEX_PROBE=1)

- HUD: `PROBE n`, `S3TC/HW/CPU/CHK`, `UV min..max` (sem FTP)
- **X** cicla: `0=UV` | `1=SCREEN` (sample por fragCoord) | `2=NORMAL`
- Aceite: foto com PROBE 0 e 1 legíveis

### Fase 1 — S3TC hardware

- `glCompressedTexImage2D` quando `s3tc=1` (Tegra DXT nativo / gta3-nx)
- Fallback CPU só se `glGetError` após compressed upload
- HUD conta `HW` vs `CPU`

### Fase 2 — UV

- Decl: `FLOAT16_2`, `SHORT2`, `SHORT2N` além de `FLOAT2/3`
- HUD atualiza faixa UV a cada draw

### Fase 3 — FFP

- Estágio 0: `texture * diffuse` quando tex ligada

NRO: `b4e917cc…` em `sdmc:/switch/client748/` — **X** troca probe

### Hotfix crash open/close

FS do PROBE usava `uRhwVP` sem declarar → `shader compile FALHOU` → CreateDevice
falha → app fecha. Declarado `uniform vec2 uRhwVP` no FS.

`probe-ffp-v4` (`bce3d84f…`): marca BUILD no diag, espelho `wyd748_ev.txt` (não
truncado), Mesa cache OFF, `typeSize` com `FLOAT16_2`/`SHORT2N`/`USHORT2N`,
repack `USHORT2N`. Deploy: `sdmc:/switch/client748/wyd748.nro`.

### Evidência HW (boot `probe-ffp-v3`, diag vivo)

- `FFP ok` + `s3tc=1` + **`upload HW DXT`** (8×, sem fallback) → Fase 1 OK
- UV de mundo tipicamente `u/v=[0..1]` → **UV não está morto** (Fase 2)
- Outliers UI: `u` até 38 / `v` negativos / tile `5×12` → falta FFP
  `D3DTS_TEXTUREn` + `TEXTURETRANSFORMFLAGS`
- Fonte `512×64` A4R4G4B4 com `decode chk=0` (atlas vazio no 1º upload)

### `probe-ffp-v5`

- VS aplica `uTexM0/1` quando `TEXTURETRANSFORMFLAGS != 0`
- `UpdateSurface` / `UpdateTexture` reais (antes stub `S_OK`)
- BUILD tag `probe-ffp-v5`

## 2026-09-22 — Fix ActiveTexture clobber (silhueta flat)

Diag do boot anterior: `texOn=1`, `op=4`, GL ids OK — uniforms não eram o problema.
Causa real: `UploadTexture` fazia `glBindTexture` **sem** `glActiveTexture`; no
estágio 1 isso sobrescrevia a unidade 0 (lightmap/branco) → `tex*diffuse` = flat.

### Correção

- `glActiveTexture(i)` **antes** de cada upload/bind
- Rebind final explícito das 3 units
- Log `decode chk=` + `UV u=[..]` no diag
- `SetStreamSource` offset respeitado

NRO: `17c05a6a…` via FTP

## 2026-09-22 — Fix texturas (uTexOn Mesa NV120)

Evidência: uploads DXT/A4R4G4B4 OK no diag, geometria flat (só diffuse).
Causa: `uniform int uTexOn[3]` + `glUniform1iv` no **Mesa 20.1/NV120** não
atualiza o FS → `uTexOn` fica 0 → `tex=vec4(1)` → silhueta.

### Correção

- Uniforms escalares `uTexOn0/1/2`, `uStageOp0…` + `glUniform1i` por estágio
- Rebind de samplers a cada draw
- Upload só com `dirty` (não subir CPU zerado pré-Lock)
- Diag truncado a cada boot

Deploy: `wyd748.nro` ~8.7 MB via FTP

## 2026-09-22 — Fase B (texturas FFP + state blocks + TX)

Evidência HW (binário anterior): geometria do castelo visível, silhuetas planas,
`FPS 21`, `TX 3218/168`, `BOFMT 0` — draw/EBO OK; textura não entrava no FFP e
UnlockRect marcava dirty sempre (re-upload em massa).

### Correções neste NRO

- **Stage0 forçado** `MODULATE TEXTURE×CURRENT` quando há textura vinculada
- **UnlockRect**: `dirty` só se não for `D3DLOCK_READONLY` (tex + surface)
- **StateBlock** Capture/Apply real (RS/TSS/SMP/tex/matrizes/luzes)
- **opv** FFP: SELECT/MODULATE/ADD/BLENDTEXTUREALPHA
- **MESA_SHADER_CACHE_DIR** → `sdmc:/switch/client748/mesa_cache`
- DXT continua decode CPU (S3TC Mesa fantasma no Tegra)

Deploy: `ftp://192.168.1.6:5000/switch/client748/wyd748.nro` (~8.7 MB)

### Esperado no próximo teste

- `TX` próximo de `created` após o load (não milhares)
- Castelo/UI com textura (não só verde/lavanda flat)
- FPS acima de ~21 se o spam de upload era o gargalo

## 2026-09-22 — Client real linkado

- `wyd748.nro` passa a ser o **TMProject748 + d3d9_gles_switch (SDL3)** (~9.1 MB)
- Build: `./scripts/build-switch-docker.sh` (Docker `devkitpro/devkita64` + SDL3 switch-sdl-3.4)
- Assets: `sdmc:/switch/client748/`
- Bring-up antigo: `./scripts/build-switch-docker.sh bringup`

## 2026-09-22 — Bring-up bootstrap

### Adicionado

- `WYDLINUX/platform/switch/` — WSI SDL2+GLES2, input Joy-Con/touch (libnx), ponte GLES Clear/Present
- `WYDLINUX/switch/` — CMake do alvo Switch
- `WYDLINUX/scripts/build-switch-docker.sh` — build no container `devkitpro/devkita64:latest`
- Artefato: `WYDLINUX/OUT/switch/wyd748.nro`
- Deploy opcional: `scripts/deploy-switch-ftp.sh` → `ftp://192.168.1.6:5000/switch/client748/` (só com FTP ligado)

### Decisões

- **Sem instalar toolchain no host** — reutiliza imagem Docker já presente (`devkitpro/devkita64`)
- **SDL2 dos portlibs** do container (não SDL3 fork) para o bring-up; client real pode migrar depois
- `pkexec` só se o usuário não tiver acesso ao daemon Docker (grupo `docker`)
- Não usa `consoleInit()` (conflita com GPU/SDL)

### Próximo

1. Backend D3D9 mínimo sobre GLES (subset CreateDevice/texturas) para login/selserver
2. Bundlar TTF CP949 + assets em romfs/`sdmc:/switch/wyd748`
3. Reusar `TouchControlsUI` do Linux no path Switch
