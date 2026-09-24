#include "wsi_switch.h"

#include <SDL.h>
#include <cstdio>

#if defined(__SWITCH__) || defined(WYD_SWITCH_HOST_SMOKE)
#include <SDL_opengles2.h>
#endif

bool WYD_SwitchWsiCreate(WYD_SwitchWsi& out, const char* title, int width, int height)
{
	out = {};
	out.width = width > 0 ? width : 1280;
	out.height = height > 0 ? height : 720;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0)
	{
		std::fprintf(stderr, "[WYD_SWITCH][wsi] SDL_Init: %s\n", SDL_GetError());
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// No Switch, NWindow já é fullscreen — não forçar SDL_WINDOW_FULLSCREEN
	// (regra do port anterior / SHAR). Desktop host: janela redimensionável.
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#if !defined(__SWITCH__)
	flags |= SDL_WINDOW_RESIZABLE;
#endif

	out.window = SDL_CreateWindow(
		title ? title : "WYD 7.48 Switch",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		out.width, out.height, flags);
	if (!out.window)
	{
		std::fprintf(stderr, "[WYD_SWITCH][wsi] CreateWindow: %s\n", SDL_GetError());
		return false;
	}

	out.gl_context = SDL_GL_CreateContext(out.window);
	if (!out.gl_context)
	{
		std::fprintf(stderr, "[WYD_SWITCH][wsi] GL_CreateContext: %s\n", SDL_GetError());
		SDL_DestroyWindow(out.window);
		out.window = nullptr;
		return false;
	}

	SDL_GL_MakeCurrent(out.window, static_cast<SDL_GLContext>(out.gl_context));
	SDL_GL_SetSwapInterval(1);
	SDL_GetWindowSize(out.window, &out.width, &out.height);
	out.ready = true;
	std::fprintf(stderr, "[WYD_SWITCH][wsi] OK %dx%d GLES2\n", out.width, out.height);
	return true;
}

void WYD_SwitchWsiDestroy(WYD_SwitchWsi& wsi)
{
	if (wsi.gl_context)
	{
		SDL_GL_DeleteContext(static_cast<SDL_GLContext>(wsi.gl_context));
		wsi.gl_context = nullptr;
	}
	if (wsi.window)
	{
		SDL_DestroyWindow(wsi.window);
		wsi.window = nullptr;
	}
	wsi.ready = false;
	SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
}

bool WYD_SwitchWsiSwap(WYD_SwitchWsi& wsi)
{
	if (!wsi.ready || !wsi.window)
		return false;
	SDL_GL_SwapWindow(wsi.window);
	return true;
}

void WYD_SwitchWsiGetSize(const WYD_SwitchWsi& wsi, int& w, int& h)
{
	w = wsi.width;
	h = wsi.height;
}
