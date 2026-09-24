#pragma once

#include <cstdint>

// Ponte mínima Clear/Present estilo d3d9_bridge do Linux, sobre GLES.
// MVP: prova WSI + textura 2×2 antes de portar IDirect3DDevice9 completo.

struct WYD_D3D9GlesBridge
{
	bool ready = false;
	uint32_t clear_argb = 0xFF102030;
	unsigned program = 0;
	unsigned texture = 0;
	unsigned vbo = 0;
	int attr_pos = 0;
	int attr_uv = 1;
	int uni_tex = -1;
};

bool WYD_D3D9GlesCreate(WYD_D3D9GlesBridge& out);
void WYD_D3D9GlesDestroy(WYD_D3D9GlesBridge& b);
bool WYD_D3D9GlesClear(WYD_D3D9GlesBridge& b, uint32_t argb);
bool WYD_D3D9GlesDrawTestQuad(WYD_D3D9GlesBridge& b);
// Present = caller chama WYD_SwitchWsiSwap após Clear/Draw.
