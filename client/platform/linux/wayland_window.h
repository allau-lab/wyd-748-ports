#pragma once

// Janela e input Wayland via SDL2 (backend wayland).
// X11 fica desabilitado por política deste port: SDL_VIDEODRIVER=wayland.

#include "wincompat.h"

#include <string>

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

struct SDL_Window;

struct WYDInputFrame
{
	bool quit = false;
	bool escape = false;
	int key_slot = -1;     // 0..3 se pressionou 1-4 / KP1-4
	bool mouse_down = false;
	int mouse_x = 0;
	int mouse_y = 0;
	bool mouse_move = false;
	// Movimento contínuo (WASD / setas) — amostrado por frame via teclado.
	float move_x = 0.f;
	float move_y = 0.f;
	bool key_toggle_view = false; // T: 2D mapa ↔ 3D mesh
};

struct WYDWaylandWindow
{
	void* sdl_window = nullptr;   // SDL_Window*
	void* wl_display = nullptr;   // wl_display* (opcional, via SDL_GetWindowWMInfo)
	unsigned width = 800;
	unsigned height = 600;
	bool running = false;
	bool fullscreen = false;
	bool vulkan = false;
	WYDInputFrame input {};
};

// Força Wayland antes de SDL_Init. Retorna false se WAYLAND_DISPLAY ausente
// e o usuário não forçou WYD_ALLOW_X11=1 (escape hatch só para CI).
bool WYD_ForceWaylandVideoDriver();

// vulkan=true adiciona SDL_WINDOW_VULKAN (necessário para a ponte D3D9/Vulkan).
bool WYD_CreateWaylandWindow(WYDWaylandWindow& out, const char* title,
	unsigned width, unsigned height, bool fullscreen, bool vulkan = false);

void WYD_DestroyWaylandWindow(WYDWaylandWindow& win);

// Processa eventos SDL/Wayland. Retorna false quando o usuário fecha a janela.
bool WYD_PollWaylandEvents(WYDWaylandWindow& win);

// Nome do driver de vídeo ativo ("wayland" esperado).
std::string WYD_CurrentVideoDriver();

inline SDL_Window* WYD_GetSDLWindow(WYDWaylandWindow& win)
{
	return static_cast<SDL_Window*>(win.sdl_window);
}
