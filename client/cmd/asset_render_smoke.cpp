// Smoke D++: valida 18 shaders DX9 + desenha UI/logo1.wyt no Wayland/Vulkan.

#include "wayland_window.h"
#include "shader_dx9_catalog.h"
#include "wyt_decode.h"
#include "vulkan_textured.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>

int main()
{
	WYDDx9ShaderCatalog shaders;
	if (!WYD_LoadDx9ShaderCatalog(shaders))
	{
		std::fprintf(stderr, "[asset_render] falha no catálogo DX9 (18 bins)\n");
		return 1;
	}

	WYDWytImage logo;
	if (!WYD_LoadWytRgba("UI/logo1.wyt", logo))
	{
		std::fprintf(stderr, "[asset_render] WYT: %s\n", logo.error.c_str());
		return 2;
	}

	WYDWaylandWindow win;
	if (!WYD_CreateWaylandWindow(win, "WYD assets (Wayland/Vulkan)", 800, 600, false, true))
		return 3;

	WYDVulkanTextured tex;
	if (!WYD_VulkanTexturedCreate(tex, WYD_GetSDLWindow(win), win.width, win.height))
	{
		WYD_DestroyWaylandWindow(win);
		return 4;
	}
	if (!WYD_VulkanTexturedUploadRgba(tex, logo))
	{
		WYD_VulkanTexturedDestroy(tex);
		WYD_DestroyWaylandWindow(win);
		return 5;
	}

	std::fprintf(stderr, "[asset_render] desenhando logo1.wyt — ESC para sair\n");
	uint32_t frames = 0;
	while (WYD_PollWaylandEvents(win))
	{
		if (!WYD_VulkanTexturedDraw(tex))
			break;
		++frames;
		SDL_Delay(16);
	}

	WYD_VulkanTexturedDestroy(tex);
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr, "[asset_render] %u frames | shaders DX9=%d\n",
		frames, shaders.loaded);
	return 0;
}
