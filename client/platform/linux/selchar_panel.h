#pragma once

// Painel de seleção de personagem (RGBA procedural, sem fonte TTF).
#include "login_session.h"
#include "wyt_decode.h"

#include <cstdint>

struct WYDSelCharPanel
{
	WYDWytImage image {};
	int hover = -1;
	int selected = -1;
	bool dirty = true;
};

// Gera/atualiza bitmap w×h com 4 slots a partir da sessão.
bool WYD_SelCharPanelBuild(WYDSelCharPanel& panel, const WYDLoginSession& login,
	uint32_t width, uint32_t height);

// Mapeia clique (x,y) → slot 0..3 ou -1.
int WYD_SelCharHitTest(uint32_t width, uint32_t height, int x, int y);

void WYD_SelCharPanelDestroy(WYDSelCharPanel& panel);
