// client_main_switch.cpp — entrada do cliente WYD 7.48 real no Switch (SDL3).
// Espelho do cmd/linux_client_main.cpp: resolve assets → wWinMain.

#ifdef WYD_SWITCH

#include <switch.h>

#ifdef interface
#undef interface
#endif

#include "../../../third_party/dxvk-native-aarch64/usr/include/dxvk/windows_base.h"
#ifndef APIENTRY
#define APIENTRY
#endif

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

int wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

namespace
{

void SLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void SLog(const char* fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	std::fputs(line, stderr);
	std::fputc('\n', stderr);
	std::fflush(stderr);

	const char* paths[] = {
		"sdmc:/switch/client748/wyd748_diag.txt",
		"sdmc:/switch/client748/wyd748_ev.txt",
	};
	for (const char* path : paths)
	{
		if (std::FILE* f = std::fopen(path, "ab"))
		{
			std::fputs(line, f);
			std::fputc('\n', f);
			std::fclose(f);
		}
	}
}

constexpr const char* kAssetMarkers[] = { "Config.bin", "config.txt", "ItemList.bin" };

bool HasGameAssets(const char* dir)
{
	if (!dir || dir[0] == '\0')
		return false;
	for (const char* marker : kAssetMarkers)
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf), "%s/%s", dir, marker);
		struct stat st {};
		if (stat(buf, &st) == 0 && S_ISREG(st.st_mode))
			return true;
	}
	return false;
}

const char* ResolveAssetsDir()
{
	static char chosen[512];
	if (const char* env = getenv("WYD_ASSET_ROOT"); env && env[0] && HasGameAssets(env))
	{
		std::snprintf(chosen, sizeof(chosen), "%s", env);
		return chosen;
	}
	if (HasGameAssets("."))
		return ".";
	if (HasGameAssets("romfs:/"))
		return "romfs:/";
	constexpr const char* kDirs[] = {
		"sdmc:/switch/client748",
		"sdmc:/switch/wyd748",
		"sdmc:/switch/wyd",
		"sdmc:/switch",
		"sdmc:/",
	};
	for (const char* d : kDirs)
	{
		if (HasGameAssets(d))
		{
			std::snprintf(chosen, sizeof(chosen), "%s", d);
			return chosen;
		}
	}
	return nullptr;
}

void DumpDir(const char* dir, int maxEntries)
{
	DIR* d = opendir(dir);
	if (!d)
	{
		SLog("[BOOT] opendir(%s) falhou (errno=%d)", dir, errno);
		return;
	}
	SLog("[BOOT] ls %s:", dir);
	int n = 0;
	while (dirent* e = readdir(d))
	{
		if (++n > maxEntries)
			break;
		SLog("[BOOT]   %s", e->d_name);
	}
	closedir(d);
}

} // namespace

int main(int argc, char** argv)
{
	mkdir("sdmc:/switch", 0777);
	mkdir("sdmc:/switch/client748", 0777);
	mkdir("sdmc:/switch/client748/mesa_cache", 0777);

	// Truncar diag ANTES de qualquer SLog — BUILD/BOOT ficam no arquivo limpo.
	if (std::FILE* f = std::fopen("sdmc:/switch/client748/wyd748_diag.txt", "wb"))
		std::fclose(f);

	// Cache Mesa OFF neste ciclo de evidência (evita GLSL stale após troca de FFP).
	setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
	setenv("MESA_GLSL_CACHE_DIR", "sdmc:/switch/client748/mesa_cache", 0);
	setenv("MESA_SHADER_CACHE_DIR", "sdmc:/switch/client748/mesa_cache", 0);

	SLog("[BUILD] WYD748 Switch v%s %s %s",
#if defined(WYD_SWITCH_VERSION)
		WYD_SWITCH_VERSION,
#else
		"0.0.0",
#endif
		__DATE__, __TIME__);
	SLog("[BOOT] argc=%d argv0=%s", argc, argv && argv[0] ? argv[0] : "(null)");

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD) < 0)
	{
		SLog("[BOOT] SDL_Init falhou: %s", SDL_GetError());
		return 1;
	}
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

	const char* assets = ResolveAssetsDir();
	if (!assets)
	{
		SLog("[BOOT] assets NAO encontrados — coloque o client (config.txt, ItemList.bin, UI/, …) em sdmc:/switch/client748/");
		DumpDir("sdmc:/switch/client748", 30);
		SDL_Quit();
		return 2;
	}
	SLog("[BOOT] assets=%s", assets);
	if (chdir(assets) != 0)
	{
		SLog("[BOOT] chdir(%s) falhou errno=%d", assets, errno);
		SDL_Quit();
		return 3;
	}
	setenv("WYD_ASSET_ROOT", assets, 1);

	// libnx: sem socketInitialize, socket()/connect() falham sempre.
	const Result sockRc = socketInitializeDefault();
	SLog("[NET] socketInitializeDefault rc=0x%x", (unsigned)sockRc);
	if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User)))
	{
		u32 ip = 0;
		const Result ipRc = nifmGetCurrentIpAddress(&ip);
		SLog("[NET] nifm ip=%u.%u.%u.%u rc=0x%x", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24,
			(unsigned)ipRc);
		nifmExit();
	}

	const int rc = wWinMain(nullptr, nullptr, nullptr, 1);
	SLog("[EXIT] wWinMain rc=%d", rc);
	if (R_SUCCEEDED(sockRc))
		socketExit();
	SDL_Quit();
	return rc;
}

#endif // WYD_SWITCH
