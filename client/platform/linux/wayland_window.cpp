#include "wayland_window.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#include <type_traits>

namespace {
[[maybe_unused]] constexpr bool sdl3_fullscreen_on = true;
}


#include <SDL3/SDL_events.h>
#include <SDL3/SDL_error.h>

#else
#include <SDL.h>
#endif
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

bool WYD_ForceWaylandVideoDriver()
{
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	const char* allow_x11 = std::getenv("WYD_ALLOW_X11");

	if (!wayland || wayland[0] == '\0')
	{
		if (allow_x11 && allow_x11[0] == '1')
		{
			std::fprintf(stderr,
				"[WYDLINUX] WAYLAND_DISPLAY ausente; WYD_ALLOW_X11=1 — NÃO é o alvo deste port.\n");
			return true;
		}
		std::fprintf(stderr,
			"[WYDLINUX] Sessão Wayland obrigatória (WAYLAND_DISPLAY). Abortando.\n");
		return false;
	}

	// Impede fallback silencioso para x11.    setenv("SDL_VIDEODRIVER", "wayland", 1);
    setenv("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR", "1", 1);
    // Layers do Steam quebram CreateInstance em alguns desktops.
    setenv("VK_LOADER_LAYERS_DISABLE", "*steam*", 1);
    return true;
}

bool WYD_CreateWaylandWindow(WYDWaylandWindow& out, const char* title,
	unsigned width, unsigned height, bool fullscreen, bool vulkan)
{
	if (!WYD_ForceWaylandVideoDriver())
		return false;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
	{
		std::fprintf(stderr, "[WYDLINUX] SDL_Init falhou: %s\n", SDL_GetError());
		return false;
	}

	const char* driver = SDL_GetCurrentVideoDriver();
	if (!driver || std::strcmp(driver, "wayland") != 0)
	{
		std::fprintf(stderr,
			"[WYDLINUX] Driver de vídeo '%s' — exigido 'wayland'.\n",
			driver ? driver : "(null)");
		SDL_Quit();
		return false;
	}

	SDL_Window* window = SDL_CreateWindow(
		title ? title : "WYD",
		static_cast<int>(width),
		static_cast<int>(height),
		SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);

	if (!window)
	{
		std::fprintf(stderr, "[WYDLINUX] SDL_CreateWindow falhou: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}

	if (fullscreen)                SDL_SetWindowFullscreen(window, sdl3_fullscreen_on);

	out.sdl_window = window;
	out.width = width;
	out.height = height;
	out.fullscreen = fullscreen;
	out.vulkan = vulkan;
	out.running = true;

	out.wl_display = nullptr;

	std::fprintf(stderr,
		"[WYDLINUX] Janela Wayland %ux%u (driver=%s)\n",
		width, height, driver);
	return true;
}

void WYD_DestroyWaylandWindow(WYDWaylandWindow& win)
{
	if (win.sdl_window)
	{
		SDL_DestroyWindow(static_cast<SDL_Window*>(win.sdl_window));
		win.sdl_window = nullptr;
	}
	win.wl_display = nullptr;
	win.running = false;
	SDL_Quit();
}

bool WYD_PollWaylandEvents(WYDWaylandWindow& win)
{
	win.input = {};
	SDL_Event ev;
	while (SDL_PollEvent(&ev))
	{
		if (ev.type == SDL_EVENT_QUIT)
		{
			win.input.quit = true;
			win.running = false;
			return false;
		}
		if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
		{
			win.input.quit = true;
			win.running = false;
			return false;
		}
		if (ev.type == SDL_EVENT_KEY_DOWN)
		{
			const SDL_Keycode k = ev.key.key;
			if (k == SDLK_ESCAPE)
			{
				win.input.escape = true;
				win.running = false;
				return false;
			}
			if (k >= SDLK_1 && k <= SDLK_4)
				win.input.key_slot = static_cast<int>(k - SDLK_1);
			else if (k >= SDLK_KP_1 && k <= SDLK_KP_4)
				win.input.key_slot = static_cast<int>(k - SDLK_KP_1);
			else if (k == SDLK_T)
				win.input.key_toggle_view = true;
		}
		if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT)
		{
			win.input.mouse_down = true;
			win.input.mouse_x = ev.button.x;
			win.input.mouse_y = ev.button.y;
		}
		if (ev.type == SDL_EVENT_MOUSE_MOTION)
		{
			win.input.mouse_move = true;
			win.input.mouse_x = ev.motion.x;
			win.input.mouse_y = ev.motion.y;
		}
	}

	const bool* keys = SDL_GetKeyboardState(nullptr);
	if (keys)
	{
		if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
			win.input.move_x -= 1.f;
		if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
			win.input.move_x += 1.f;
		if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
			win.input.move_y -= 1.f;
		if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
			win.input.move_y += 1.f;
	}
	return win.running;
}

std::string WYD_CurrentVideoDriver()
{
	const char* d = SDL_GetCurrentVideoDriver();
	return d ? d : "";
}
