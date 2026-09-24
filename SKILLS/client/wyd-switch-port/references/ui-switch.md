# UI Switch — touch, Joy-Con, safe zone

Princípios (console): clareza sob pressão, controller-first, safe zones, touch
targets grandes, imersão sem poluir o viewport.

## Reaproveitar o que já existe

`TouchControlsUI` (Wild Rift-like) já resolve o pior do MMORPG no portátil:

- Stick + look + ATK + 5 skills + menu/câmera/inv/skills/char
- Layout por fração de tela (`RebuildLayout`, `s = min(W,H)`)
- Ícones TGA em `assets/touch_icons/`
- Integração: `EventTranslator_linux.cpp` + `TMFieldScene` + `SControlContainer`
- Contrato OnPointer: **1 = consumiu**, **0 = fallthrough**

No Switch: manter o overlay; alimentar também via Joy-Con (não só touch).

Config:

```text
[TOUCH_UI] 1
[LOL_CONTROLS] 1
```

ARM Linux já defaulta touch=1 — Switch deve fazer o mesmo (`WYD_SWITCH`).

## Safe zones

| Zona | Margem | Conteúdo |
|---|---|---|
| Title safe ~90% | 5% cada lado | Texto, HUD estático, botões touch |
| Action safe ~93% | 3.5% | Elementos em movimento |

- Default conservador em TV/docked; 0–2% em handheld se calibrado.
- Nunca ancorar HUD crítico em (0,0) absoluto sem margem.
- Opção futura: slider 0–10% em settings.

## Docked vs handheld

| Modo | Resolução típica | UI |
|---|---|---|
| Handheld | 1280×720 | Touch + Joy-Con; tipografia ≥ 16–18 px equivalentes |
| Docked | 1920×1080 (ou 720p TV) | Preferir gamepad; touch opcional; safe zone maior |

Ao mudar modo: atualizar `m_dwScreenWidth/Height` e chamar rebuild do HUD.

## Touch targets

- Mínimo **48×48** px lógicos para ações primárias (ATK, skills, confirmar).
- Hitboxes PC (grids inventário) muitas vezes menores — expandir só a área de toque
  sem necessariamente redesenhar o art.
- Single-touch no MVP.

## Remapeamento (0.2.11)

- Tabelas `kButtons` × `kActions` em `switch_input.cpp`; persistência em `controls.cfg`.
- UI: abas CONTROLE e MACRO no painel de ajuda (`TMFieldScene::SwitchControlsTab*`, só `__SWITCH__`).
  L3 = `macromenu` (abre aba MACRO). HUD MACRO permanente desligado (0.2.16). Touch: 2 toques = SmartMoveItem (equip/desequip/cargo). Página do menu em
  `WYD_Switch_ControlsSetPage` (0 controle / 1 macro).
  A cena só desenha as linhas (`WYD_Switch_ControlsLine`) e repassa cliques; navegação por
  D-pad/A/B fica no input enquanto a aba está visível (`WYD_Switch_ControlsSetActive`).
- Textos da fonte do cliente: sem acento (8 bits).

## Mapeamento Joy-Con padrão (0.2.10, `switch_input.cpp`)

| Joy-Con | Field | Fora do Field |
|---|---|---|
| L-Stick | andar (`TouchControlsUI::PadMove`) | cursor |
| R-Stick ↑ ↓ ← → | P (grupo) / H / K (PK) / Z | cursor |
| R3 | X | — |
| A | macro OFF → MG (tecla D) → DN (PK) → OFF | clique esquerdo |
| B / X / Y | S skills / I inventário / C personagem | B = clique direito |
| ZR | M (mapa) | clique esquerdo |
| ZL | teclado virtual → chat; ZL de novo envia | teclado virtual no campo focado |
| D-pad / L | skills 1–4 / skill 5 | setas |
| R / Minus | girar / resetar câmera | Minus = Tab |
| Plus | Esc | Esc |

Atalhos de letra vão como `WM_CHAR` (`WYD_Switch_InjectChar`) para cair no `OnCharEvent`
nativo; não chamar os `SetVisible*` direto. Suprimir quando `m_pEditChat` estiver focado.
Teclado: `swkbdCreate/swkbdShow` (applet bloqueante); converter UTF-8 → Latin-1.

Ações do Field passam por `WYD_Linux_PadAction` (enum `TouchControlsUI::PadActionId`),
reaproveitando os mesmos caminhos do HUD de toque.

Macro (B / AUTO): só seleciona alvo com `TMFieldScene::CanAttackHuman` (regras de PK de
`MobAttack` + `MoveAttack`) e entrega ao auto-ataque nativo (`m_cAutoAttack`). Não enviar
pacotes de ataque pelo HUD; ao portar outra versão, espelhar os portões de PK dela.

Touch: libnx exige `hidInitializeTouchScreen()`. Cada dedo vai primeiro ao HUD
(`WYD_Linux_TouchHudPointer`, ids 200+); o que não for consumido vira clique esquerdo.

Menus (login, selserver, selchar): **navegação por foco** com D-pad além do
analógico-como-mouse. Sem foco = deadend (injogável só com controle).

## Prompts de botão

Proibido hardcodar `"Pressione A"`. Usar action id → glifo Joy-Con / touch.

## Checklist de validação UI

- [ ] Login → selserver → selchar → campo só com Joy-Con (sem touch)
- [ ] Mesmo fluxo só com touch (sem botões)
- [ ] Inventário/loja: HUD de combate em passthrough (`IsGameUiBlocking`)
- [ ] Nenhum texto/HUD cortado em TV com overscan 5%
- [ ] Alternar docked/handheld sem layout quebrado
- [ ] Fallthrough touch: mundo clicável onde não há HUD; HUD não “fura” cliques

## Movimento e toque (0.2.12)

- Andar por stick usa `TMFieldScene::IssueStickStep`: linha reta até o 1º obstáculo
  (`BASE_GetHitPosition`), alcance 2–5 células conforme o intervalo de rota do `GetRoute`;
  soltar o stick envia parada (`bStop=1`). Não usar `IssueMoveToPick` para stick: ele desenha
  marcador e faz pathfinding longo (desvia em pontes/teleportes).
- Toque: press atrasado 2 frames após o `WM_MOUSEMOVE` (pick 3D usa o cursor do frame anterior).
- Log `[TOUCH]`/`[UI]` no `wyd748_diag.txt` para achar controles com hitbox deslocada.
- Ícones 3D de UI (`TMMesh::RenderForUI`) em 16:9: largura da câmera de UI = `100*tanf(0.05)*aspect`
  (o original só tem 4:3/5:4). Sem isso os ícones "encolhem" para o centro. (0.2.13)
