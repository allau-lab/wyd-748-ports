#pragma once

// Ponte mínima estilo D3D9 → Vulkan/Wayland.
// Expõe Clear/Present compatíveis com o fluxo do RenderDevice legado,
// sem implementar a API COM completa ainda. Fase D parcial.

#include "vulkan_present.h"

#include <cstdint>

struct SDL_Window;

struct WYDD3D9Bridge
{
	WYDVulkanPresent present {};
	bool ready = false;
	uint32_t clear_argb = 0xFF102030;
};

// Equivalente pragmático a Direct3DCreate9 + CreateDevice(HWND=SDL_Window*).
bool WYD_D3D9BridgeCreate(WYDD3D9Bridge& out, SDL_Window* window,
	uint32_t width, uint32_t height);

void WYD_D3D9BridgeDestroy(WYDD3D9Bridge& bridge);

// D3DCOLOR 0xAARRGGBB → clear do backbuffer.
bool WYD_D3D9BridgeClear(WYDD3D9Bridge& bridge, uint32_t argb);

// Present() — flip para a superfície Wayland.
bool WYD_D3D9BridgePresent(WYDD3D9Bridge& bridge);
