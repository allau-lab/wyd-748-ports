// Bootstrap Wayland + ponte D3D9/Vulkan (Fase D parcial).
// Clear/Present na superfície Wayland. Shaders/mesh do jogo ainda não.

#include "wayland_window.h"
#include "asset_paths.h"
#include "d3d9_bridge.h"

#include <SDL3/SDL.h>
#include <cstdio>

namespace
{
	const char* kCriticalAssets[] = {
		"config.txt",
		"UI/UIString.txt",
		"UI/interface.txt",
		"Shader/skinmesh1.bin",
		"Shader/vseffect1.bin",
		"Shader/pseffect1.bin",
	};
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	std::fprintf(stderr, "[WYDLINUX] bootstrap Wayland+Vulkan — asset root: %s\n",
		WYD_AssetRoot().c_str());

	int missing = 0;
	for (const char* rel : kCriticalAssets)
	{
		if (!WYD_AssetExists(rel))
		{
			WYD_LogMissingAsset(rel);
			++missing;
		}
	}
	if (missing)
	{
		std::fprintf(stderr,
			"[WYDLINUX] %d asset(s) críticos ausentes (case-sensitive).\n",
			missing);
	}

	WYDWaylandWindow win;
	if (!WYD_CreateWaylandWindow(win, "WYD 7.48 (Linux/Wayland+Vulkan)", 800, 600, false, true))
		return 1;

	WYDD3D9Bridge d3d9;
	if (!WYD_D3D9BridgeCreate(d3d9, WYD_GetSDLWindow(win), win.width, win.height))
	{
		WYD_DestroyWaylandWindow(win);
		return 2;
	}

	// Tom escuro estável (smoke visual). Alterna levemente para provar Present.
	uint32_t frame = 0;
	std::fprintf(stderr, "[WYDLINUX] ESC/fechar para sair. driver=%s\n",
		WYD_CurrentVideoDriver().c_str());

	while (WYD_PollWaylandEvents(win))
	{
		const uint32_t pulse = (frame / 30) % 2;
		const uint32_t clear = pulse ? 0xFF1A2740u : 0xFF102030u;
		WYD_D3D9BridgeClear(d3d9, clear);
		if (!WYD_D3D9BridgePresent(d3d9))
		{
			std::fprintf(stderr, "[WYDLINUX] Present falhou\n");
			break;
		}
		++frame;
		SDL_Delay(16);
	}

	WYD_D3D9BridgeDestroy(d3d9);
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr, "[WYDLINUX] encerrado após %u frames.\n", frame);
	return 0;
}
