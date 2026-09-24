// Bootstrap GLES + input — prova WSI/textura no hardware antes do client real.
// Não chama consoleInit() (conflita com GPU / SDL). Ver skill Switch §2.

#include <cstdio>

#include <SDL.h>

#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif

#include "wsi_switch.h"
#include "input_switch.h"
#include "d3d9_switch/d3d9_gles_bridge.h"

#ifdef __SWITCH__
static void ResolveAssets()
{
	fsdevMountSdmc();
	if (chdir("sdmc:/switch/wyd748") != 0)
		std::fprintf(stderr, "[WYD_SWITCH] cwd sdmc:/switch/wyd748 falhou (ok no bring-up)\n");
}
#endif

int main(int, char**)
{
#ifdef __SWITCH__
	ResolveAssets();
	s32 width = 1280, height = 720;
	appletGetDefaultDisplayResolution(&width, &height);
	std::fprintf(stderr, "[WYD_SWITCH] display=%dx%d mode=%s\n",
		(int)width, (int)height,
		appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");
#else
	const int width = 1280;
	const int height = 720;
#endif

	SDL_SetMainReady();

	WYD_SwitchWsi wsi{};
	if (!WYD_SwitchWsiCreate(wsi, "WYD748 BRING-UP (nao e o jogo)", width, height))
		return 1;

	WYD_D3D9GlesBridge gles{};
	if (!WYD_D3D9GlesCreate(gles))
	{
		WYD_SwitchWsiDestroy(wsi);
		return 2;
	}

	WYD_SwitchInput input{};
	WYD_SwitchInputInit(input);

	std::fprintf(stderr,
		"[WYD_SWITCH] BRING-UP ONLY — tela azul + quad 4 cores = GLES OK.\n"
		"[WYD_SWITCH] O client real (login/selserver) ainda nao esta linkado neste NRO.\n"
		"[WYD_SWITCH] loop — + sai; A loga input\n");

#ifdef __SWITCH__
	while (appletMainLoop())
#else
	for (;;)
#endif
	{
		int ww = 0, hh = 0;
		WYD_SwitchWsiGetSize(wsi, ww, hh);
		if (!WYD_SwitchInputPoll(input, ww, hh))
			break;

		if (input.state.buttons & WYD_SWITCH_BTN_A)
			std::fprintf(stderr, "[INPUT] A L=(%.2f,%.2f) touch=%d,%d\n",
				input.state.lx, input.state.ly, input.state.touch_x, input.state.touch_y);

		WYD_D3D9GlesClear(gles, 0xFF102030);
		WYD_D3D9GlesDrawTestQuad(gles);
		WYD_SwitchWsiSwap(wsi);
	}

	WYD_SwitchInputShutdown(input);
	WYD_D3D9GlesDestroy(gles);
	WYD_SwitchWsiDestroy(wsi);
#ifdef __SWITCH__
	fsdevUnmountAll();
#endif
	return 0;
}
