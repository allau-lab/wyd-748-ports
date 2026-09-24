#pragma once

#include <cstdint>

struct SDL_Window;

struct WYD_SwitchWsi
{
	SDL_Window* window = nullptr;
	void* gl_context = nullptr; // SDL_GLContext
	int width = 1280;
	int height = 720;
	bool ready = false;
};

// Cria janela fullscreen + contexto OpenGL ES 3.0 (backend do MVP Switch).
bool WYD_SwitchWsiCreate(WYD_SwitchWsi& out, const char* title, int width, int height);
void WYD_SwitchWsiDestroy(WYD_SwitchWsi& wsi);
bool WYD_SwitchWsiSwap(WYD_SwitchWsi& wsi);
void WYD_SwitchWsiGetSize(const WYD_SwitchWsi& wsi, int& w, int& h);
