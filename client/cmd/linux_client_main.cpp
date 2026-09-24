// Entrada Linux do client real — encaminha para NewApp::wWinMain.
#ifdef WYD_LINUX

#include "pch.h"
#include "NewApp.h"

extern int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
	// Resolution is applied by NewApp after config.txt is loaded. Keep the
	// command-line parsing here so the client can be launched directly as
	// `./project --resolution=1280x720` without editing the game data.
	for (int i = 1; i < argc; ++i)
	{
		if (std::strncmp(argv[i], "--resolution=", 13) == 0)
		{
			setenv("WYD_RESOLUTION", argv[i] + 13, 1);
			break;
		}
	}
	(void)argc;
	setenv("DXVK_WSI_DRIVER",
#ifdef WYD_USE_SDL3
		"SDL3", 0); // default respeitando env do usuário
#else
		"SDL2", 1);
#endif
	return wWinMain(nullptr, nullptr, nullptr, 1);
}

#endif
