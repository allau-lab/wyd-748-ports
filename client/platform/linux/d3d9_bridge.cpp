#include "d3d9_bridge.h"

#include <cstdio>

bool WYD_D3D9BridgeCreate(WYDD3D9Bridge& out, SDL_Window* window,
	uint32_t width, uint32_t height)
{
	out = {};
	if (!WYD_VulkanPresentCreate(out.present, window, width, height))
	{
		std::fprintf(stderr, "[WYDLINUX][d3d9] CreateDevice/VulkanPresent falhou\n");
		return false;
	}
	out.ready = true;
	out.clear_argb = 0xFF102030;
	std::fprintf(stderr, "[WYDLINUX][d3d9] bridge OK (Clear/Present via Vulkan)\n");
	return true;
}

void WYD_D3D9BridgeDestroy(WYDD3D9Bridge& bridge)
{
	if (bridge.ready)
		WYD_VulkanPresentDestroy(bridge.present);
	bridge = {};
}

bool WYD_D3D9BridgeClear(WYDD3D9Bridge& bridge, uint32_t argb)
{
	if (!bridge.ready)
		return false;
	bridge.clear_argb = argb;
	return true;
}

bool WYD_D3D9BridgePresent(WYDD3D9Bridge& bridge)
{
	if (!bridge.ready)
		return false;
	return WYD_VulkanPresentFrame(bridge.present, bridge.clear_argb);
}
