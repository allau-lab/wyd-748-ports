// Smoke Fase H: DXVK Native CreateDevice + Clear/Present + Create*Shader×18.
// Não mistura swapchain com vulkan_textured do project.

#include "wayland_window.h"
#include "d3d9_dxvk_native.h"
#include "shader_dx9_catalog.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>
#include <cstdlib>

int main()
{
	setenv("DXVK_WSI_DRIVER", "SDL2", 0);
	setenv("SDL_VIDEODRIVER", "wayland", 1);

	WYDDx9ShaderCatalog catalog;
	if (!WYD_LoadDx9ShaderCatalog(catalog))
	{
		std::fprintf(stderr, "[dxvk_smoke] catálogo shaders falhou\n");
		return 1;
	}

	WYDWaylandWindow win;
	if (!WYD_CreateWaylandWindow(win, "WYD DXVK Native", 800, 600, false, true))
		return 2;

	WYDD3D9NativeDevice native {};
	if (!WYD_D3D9NativeLoad(native))
	{
		std::fprintf(stderr, "[dxvk_smoke] %s\n", native.status.c_str());
		WYD_DestroyWaylandWindow(win);
		return 3;
	}

	if (!WYD_D3D9NativeCreateDevice(native, WYD_GetSDLWindow(win),
		win.width, win.height))
	{
		std::fprintf(stderr, "[dxvk_smoke] %s\n", native.status.c_str());
		WYD_D3D9NativeDestroy(native);
		WYD_DestroyWaylandWindow(win);
		return 4;
	}

	const int n = WYD_D3D9NativeCreateShadersFromCatalog(native, &catalog);
	std::fprintf(stderr, "[dxvk_smoke] shaders=%d — Clear/Present (ESC)\n", n);

	uint32_t frames = 0;
	while (WYD_PollWaylandEvents(win))
	{
		const uint32_t color = 0xFF1020A0u ^ ((frames & 0x3Fu) << 8);
		if (!WYD_D3D9NativeClear(native, color) || !WYD_D3D9NativePresent(native))
		{
			std::fprintf(stderr, "[dxvk_smoke] Clear/Present falhou\n");
			break;
		}
		++frames;
		SDL_Delay(16);
	}

	WYD_D3D9NativeDestroy(native);
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr, "[dxvk_smoke] fim frames=%u device=1 shaders=%d\n",
		frames, n);
	return (n >= 18) ? 0 : 5;
}
