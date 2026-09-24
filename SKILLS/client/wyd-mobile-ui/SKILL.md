---
name: wyd-mobile-ui
description: >-
  Reaplica a stack de UI mobile touch-first do cliente WYD TMProject (L-35…L-45):
  MobileUiLayout factories, métricas adaptativas, chat Wild Rift, Menu Principal,
  HUD HP/MP, barra de skills, AUTO N, tema clássico bronze/cobre e inventário
  touch HD. Use when porting WYD UI to another client version, recreating
  field/login/selchar panels, fixing opaque chat, applying classic copper theme,
  inventory touch HD, or the user mentions UI mobile, L-40/L-41/L-42/L-43/L-44/L-45,
  Menu Principal, ou chat semi-transparente.
---

# WYD Mobile UI (TMProject)

Receita para **recriar** interfaces touch-first no cliente WYD (C++ / `SControl`), sem depender da arte NUI do bin.

**Referência:** tree do port mobile já estável (não editar o source Windows original).  
**Playbooks:** docs de replicação L-35…L-45 + stack de UI mobile do próprio port.

## Quando aplicar

- Portar UI mobile para **outra versão** do mesmo engine TMProject
- Chat preto/opaco, menus pequenos, HP/skills/AUTO pouco tocáveis
- Recriar botões/painéis com texture `-2` (sólidos)
- Inventário touch HD (L-45)

## Ordem obrigatória

1. **L-36** — finger→LMB nos menus (senão UI nova não clica)
2. **L-39** — tema + texto centrado (`GetTextExtentPoint32`)
3. **L-40** — `MobileUiLayout.*` factories / Retire / Reparent
4. **L-41** — `WYD_UiMetrics` + server/login/PIN/selchar/Skills/Menu C
5. **L-42** — field: inventário/cargo/shop/ESC/chat+sysmsg/MENU touch
6. **L-43** — chat WR, Menu Principal `65788`, HUD HP/MP, skills+AUTO −/+
7. **L-44** — tema clássico (`WYD_GetUiTheme` + `WYD_ApplyClassicFrame`)
8. **L-45** — inventário touch HD (`RebuildInventoryUi` + CANCELAR)

## Arquivos a copiar / espelhar

| Arquivo | Papel |
|---|---|
| `TMProject/MobileUiLayout.h/.cpp` | Tema, métricas, `WYD_Create*`, Retire, Reparent, ResizeGridCells |
| `TMProject/ResourceControl.h` | IDs runtime (ex. `90200+` chat WR, `90210/11` AUTO, `90230+` inv) |
| Cenas | `TMSelectServerScene`, `TMSelectCharScene`, `TMFieldScene` |
| Touch | `TouchControlsUI` / `TouchSystem` (MENU touch ≠ Menu Principal) |

Entrada CMake: incluir `MobileUiLayout.cpp` em `tmproject_sources`.

## Padrão de rebuild (L-40)

```cpp
WYD_RetireSubtree(oldPanel);           // zera IDs dos filhos
WYD_RetireControl(FindControl(ID));    // libera ID no FindControl
auto* p = WYD_CreatePanel(ID, x, y, w, h, th.panelBg);
WYD_AddToContainer(container, p);
auto* b = WYD_CreateButton(BTN_ID, ..., th.btnOk, container); // listener = container
p->AddChild(b);                        // mesmos IDs de evento do bin
```

- Texture sempre **-2** (sólido). Listener = `m_pControlContainer` (encaminha para a cena).
- Preservar IDs de evento (`B_CHAR` 65790, `B_EQUIP` 65791, `B_SYSTEM` 65796, …).

## L-43 — Field HUD (checklist)

| Peça | Função | IDs |
|---|---|---|
| Chat WR | `ApplyMobileChatLayout` + `SetChatExpanded` + `ApplyChatSendTab` | toggle 90200, tabs 90201–904, filtros 90205; lista 65667 |
| Menu (círculo HP) | `RebuildMiniMenuUi` + orb `B_HUD_MENU_ORB` | face `69636`; painel `65788`; atalhos S/I/P/H/X/C/N/M |
| Status | `ApplyMobileStatusHud` | preserva arte nativa `69633`; overlay no círculo |
| Skills + AUTO | `ApplyMobileSkillBarLayout` | cintos 65644/45; AUTO 65648; −/+ 90210/11 |

**MENU textual escondido** — toque no **círculo laranja** (face/HP) abre o menu:

| Tecla | Item | Ação |
|---|---|---|
| S | Skills | `SetVisibleSkill` |
| I | Inventario | `B_EQUIP` / inventário |
| P | Grupo | `SetVisibleParty` |
| H | Ajuda | `B_HELP` |
| X | Quest | `B_QUESTLOG` |
| C | Personagem | `B_CHAR` |
| N | Ocultar nome | `SetVisibleNameLabel` |
| M | Mapa | `SetVisibleMiniMap` |

Cores chat/sysmsg: alpha **~0x66/0x88** (não `0xFF` — evita “tela preta”).

## L-45 — Inventário touch HD

| Peça | Função | IDs |
|---|---|---|
| Shell | `RebuildInventoryUi` rev2 — equip \| bolsas | `P_INV_PANEL` |
| Abas | BOLSA 1–4 sólidas | `B_INV_PAGE1…4` |
| Cancel | `DetachItem` sem ESC | `B_INV_CANCEL` 90230 |
| Hint | banner attached | `T_INV_TOUCH_HINT` 90231 |
| Grids | reparent + `WYD_ResizeGridCells` | 67072–75, equips, `G_INV_DELETE` |

- `SyncInvTouchChrome()` no `FrameMove` + ao abrir/fechar inventário
- Tap chrome (coords **locais** ao painel, fora de grid/botões) = Detach
- Não recriar protocolo de item — só chrome

## Anti-crash (obrigatório)

Handlers de botões recriados **devem** null-checar painéis/ponteiros legados:

- `B_CHAR`: `m_pShopPanel` / `m_pTradePanel` / botão
- `B_EQUIP` / `B_SYSTEM` / `B_QUESTLOG` / `B_AUTOTRADEBTN` / `P_MINIBTNPANEL_BTN`
- `B_SHORTSKILL_TGL1/2`: grids + `m_pShortSkill_Txt`
- `SetAutoSkillNum`: cada `m_pAutoSkillPanelChild[i]`
- `SButton::SetText`: botões criados com `" "` só ganham `pFont` no 1º `SetText` (já tratado em `SControl.cpp`)
- Whisper: `snprintf` em `m_cChatType[32]` (nunca `sprintf` com nome longo)
- Ordem chat: `EnsureSystemMsgPanel()` **antes** de `SetChatExpanded`
- **L-43c:** `SPanel::SetVisible(0)` limpa só `m_pPickedControl` — **nunca** `m_pControlContainer = nullptr`
- Listas: `WYD_StyleListBoxItem` (sem textura 566 branca); CONTAS = AlwaysOnTop + esconde login
- **L-44:** tema clássico — `panelBg` semi-transparente, `btnOk` cobre, moldura via `WYD_ApplyClassicFrame`
- **L-45:** páginas de bolsa e grids com null-guards; não chamar `SetVisible` em ponteiro nulo

## MENU touch vs Menu Principal

| | MENU touch | Menu Principal |
|---|---|---|
| Onde | overlay `TouchControlsUI` | nativo `65787`/`65788` |
| Conteúdo | INV/SKL/CHR/ESC/AUTO/PK | Personagem/Inventario/Quest/… |

Não confundir ao depurar “não vi mudança no Menu”.

## Aceite rápido

- [ ] Login/selchar/PIN tocáveis; modal CONTAS
- [ ] Chat semi-transparente; CHAT + expande; abas mudam prefixo
- [ ] MENU (direita) abre Menu Principal sem crash
- [ ] Personagem / Inventario / Sistema / Quest com null-guards
- [ ] AUTO on → faixa vermelha + −/+ altera N (1–10)
- [ ] Inventário HD: 4 bolsas, CANCELAR solta item sem ESC

## Detalhes

- Playbook L-43: `docs/replication/L-43-hud-chat-menu-mobile.md`
- Playbook L-44: `docs/replication/L-44-tema-classico-touch.md`
- Playbook L-45: `docs/replication/L-45-inventario-touch-hd.md`
- Stack: `docs/replication/UI-MOBILE-STACK.md`
- Erros: `docs/ERRORS-FIXED.md` (L-35…L-45)
- Referência estendida: [reference.md](reference.md)
