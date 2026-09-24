# WYD Mobile UI — referência rápida de IDs e funções

## Prefixo de envio do chat (`m_cChatType`)

| Aba | Prefixo |
|---|---|
| Normal | `""` |
| Guild | `"-"` |
| Party | `"="` |
| Whisper | `"/%s "` (nome em `m_cWhisperName`) |

Filtros de **recebimento** (ON/OFF): `65673`–`65680` (só com Filtros ligado).

## Menu Principal (círculo HP / atalhos)

| ID | Const | Ação |
|---|---|---|
| 90220 | `B_HUD_MENU_ORB` | Toque no círculo laranja (overlay em `69636`) |
| 65788 | `P_MINIBTNPANEL` | Painel do menu |
| 90221 | `B_HUD_MENU_SKILL` | S Skills |
| 65791 | `B_EQUIP` | I Inventário |
| 90222 | `B_HUD_MENU_PARTY` | P Grupo |
| 65795 | `B_HELP` | H Ajuda |
| 65793 | `B_QUESTLOG` | X Quest |
| 65790 | `B_CHAR` | C Personagem |
| 90223 | `B_HUD_MENU_NAME` | N Ocultar nome |
| 90224 | `B_HUD_MENU_MAP` | M Mapa |

Botão textual `65787` fica oculto (substituído pelo orb).

## AUTO N

- Painel `65648`, segmentos `65775`–`65784`
- Default `m_nAutoSkillNum = 4`; só visível com `m_cAutoAttack == 1`
- Tecla `-` / botões `90210`/`90211` → `SetAutoSkillNum`

## Opcode skills

`MSG_SetShortSkill` `0x378` — preservar ao equipar short skill.

## Helpers MobileUiLayout

`WYD_GetUiTheme`, `WYD_GetUiMetrics`, `WYD_ApplyUiFont`, `WYD_SyncGdiFont`,  
`WYD_CreatePanel/Button/Label/Edit/ListBox`, `WYD_RetireControl/Subtree`,  
`WYD_ReparentAllChildren`, `WYD_AddToContainer`, `WYD_RebuildSolidPanel`,  
`WYD_ReplaceCloseButton`, `WYD_StyleSolid*`, `WYD_EnsureMinTouchSize`.
