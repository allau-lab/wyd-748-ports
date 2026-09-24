// Smoke da API D3D9 mínima (CreateDevice/Clear/Present) sobre Wayland+Vulkan.

#include "wayland_window.h"
#include "d3d9_min_api.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include <cstdio>

int main()
{
	WYDWaylandWindow win{};
	if (!WYD_CreateWaylandWindow(win, "WYD D3D9-min (Wayland)", 800, 600, false, true))
		return 1;

	LPDIRECT3D9 d3d = Direct3DCreate9(32);
	if (!d3d)
	{
		WYD_DestroyWaylandWindow(win);
		return 2;
	}

	LPDIRECT3DDEVICE9 device = nullptr;
	HWND focus = WYD_GetSDLWindow(win);
	if (FAILED(d3d->CreateDevice(0, 0, focus, 0, nullptr, &device)) || !device)
	{
		std::fprintf(stderr, "[d3d9_min] CreateDevice falhou\n");
		d3d->Release();
		WYD_DestroyWaylandWindow(win);
		return 3;
	}

	std::fprintf(stderr, "[d3d9_min] loop Clear/Present — ESC para sair\n");
	uint32_t frames = 0;
	while (WYD_PollWaylandEvents(win))
	{
		device->BeginScene();
		device->Clear(0, nullptr, D3DCLEAR_TARGET,
			D3DCOLOR_ARGB(255, 16, 32, 48), 1.0f, 0);
		device->EndScene();
		if (FAILED(device->Present(nullptr, nullptr, nullptr, nullptr)))
			break;
		++frames;
		SDL_Delay(16);
	}

	WYD_D3D9BridgeDestroy(device->bridge);
	delete device;
	d3d->Release();
	WYD_DestroyWaylandWindow(win);
	std::fprintf(stderr, "[d3d9_min] %u frames OK\n", frames);
	return 0;
}
