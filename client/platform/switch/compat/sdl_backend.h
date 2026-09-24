#pragma once
// Backend SDL2 vs SDL3 para o port Linux (upgrade-sdl3).
// Com WYD_SDL3: inclui SDL3 e aliases mínimos. Sem WYD_SDL3: SDL2 intacto.

#if defined(WYD_SDL3)

// Impede macros de “rename error” do SDL3; usamos aliases locais abaixo.
#ifndef SDL_DISABLE_OLD_NAMES
#define SDL_DISABLE_OLD_NAMES
#endif
#include <SDL3/SDL.h>
#include <cstdlib>

// --- bools ---
#ifndef SDL_TRUE
#define SDL_TRUE true
#define SDL_FALSE false
#endif

// --- eventos (aliases para código compartilhado) ---
#ifndef SDL_QUIT
#define SDL_QUIT SDL_EVENT_QUIT
#define SDL_KEYDOWN SDL_EVENT_KEY_DOWN
#define SDL_KEYUP SDL_EVENT_KEY_UP
#define SDL_TEXTINPUT SDL_EVENT_TEXT_INPUT
#define SDL_TEXTEDITING SDL_EVENT_TEXT_EDITING
#define SDL_MOUSEMOTION SDL_EVENT_MOUSE_MOTION
#define SDL_MOUSEBUTTONDOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define SDL_MOUSEBUTTONUP SDL_EVENT_MOUSE_BUTTON_UP
#define SDL_MOUSEWHEEL SDL_EVENT_MOUSE_WHEEL
#define SDL_FINGERDOWN SDL_EVENT_FINGER_DOWN
#define SDL_FINGERUP SDL_EVENT_FINGER_UP
#define SDL_FINGERMOTION SDL_EVENT_FINGER_MOTION
#endif

// --- teclas letras (SDL3 usa SDLK_A..Z) ---
#ifndef SDLK_a
#define SDLK_a SDLK_A
#define SDLK_v SDLK_V
#define SDLK_z SDLK_Z
#endif

// --- mods ---
#ifndef KMOD_ALT
#define KMOD_ALT SDL_KMOD_ALT
#define KMOD_CTRL SDL_KMOD_CTRL
#define KMOD_SHIFT SDL_KMOD_SHIFT
#endif

// --- áudio formatos ---
#ifndef AUDIO_S16LSB
#define AUDIO_S16LSB SDL_AUDIO_S16LE
#define AUDIO_S16SYS SDL_AUDIO_S16
#endif

inline bool Wyd_SdlInitVideoEvents()
{
	// Prefer Wayland before first VIDEO init (no silent X11/GLX fallback).
	if (!std::getenv("SDL_VIDEO_DRIVER") || !std::getenv("SDL_VIDEO_DRIVER")[0])
	{
		const char* leg = std::getenv("SDL_VIDEODRIVER");
		if (leg && leg[0])
			setenv("SDL_VIDEO_DRIVER", leg, 0);
		else if (std::getenv("WAYLAND_DISPLAY") && std::getenv("WAYLAND_DISPLAY")[0])
			setenv("SDL_VIDEO_DRIVER", "wayland", 0);
	}
	if (!std::getenv("SDL_VIDEODRIVER") || !std::getenv("SDL_VIDEODRIVER")[0])
	{
		const char* d = std::getenv("SDL_VIDEO_DRIVER");
		if (d && d[0])
			setenv("SDL_VIDEODRIVER", d, 0);
	}
	if (!std::getenv("DXVK_WSI_DRIVER") || !std::getenv("DXVK_WSI_DRIVER")[0])
		setenv("DXVK_WSI_DRIVER", "SDL3", 0);

	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	// Keep focus when compositor briefly steals it (alt-tab / OSK).
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	return SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

inline bool Wyd_SdlInitAudio()
{
	return SDL_InitSubSystem(SDL_INIT_AUDIO);
}

inline void Wyd_SdlShowCursor(bool show)
{
	if (show)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

inline void Wyd_SdlSetWindowGrab(SDL_Window* win, bool grab)
{
	if (win)
		SDL_SetWindowMouseGrab(win, grab);
}

inline void Wyd_SdlSetRelativeMouse(bool on)
{
	// SDL3: relative mode é por janela (API global SDL2 removida).
	SDL_Window* win = SDL_GetKeyboardFocus();
	if (win)
		SDL_SetWindowRelativeMouseMode(win, on);
}

inline SDL_Surface* Wyd_SdlCreateRgbaSurface(void* pixels, int w, int h, int pitch)
{
	return SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, pitch);
}

inline void Wyd_SdlDestroySurface(SDL_Surface* s)
{
	SDL_DestroySurface(s);
}

inline SDL_Keycode Wyd_SdlEventKey(const SDL_Event& ev)
{
	return ev.key.key;
}

inline void Wyd_SdlStartTextInput(SDL_Window* win)
{
	if (win)
		SDL_StartTextInput(win);
}

inline void Wyd_SdlStopTextInput(SDL_Window* win)
{
	if (win)
		SDL_StopTextInput(win);
}

inline bool Wyd_SdlIsTextInputActive(SDL_Window* win)
{
	return win ? SDL_TextInputActive(win) : false;
}

inline void Wyd_SdlSetTextInputArea(SDL_Window* win, const SDL_Rect* r)
{
	if (win)
		SDL_SetTextInputArea(win, r, 0);
}

inline const bool* Wyd_SdlGetKeyboardState(int* n)
{
	return SDL_GetKeyboardState(n);
}

#else // !WYD_SDL3 — SDL2

#include <SDL.h>

inline bool Wyd_SdlInitVideoEvents()
{
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	return SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0;
}

inline bool Wyd_SdlInitAudio()
{
	return SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
}

inline void Wyd_SdlShowCursor(bool show)
{
	SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

inline void Wyd_SdlSetWindowGrab(SDL_Window* win, bool grab)
{
	if (win)
		SDL_SetWindowGrab(win, grab ? SDL_TRUE : SDL_FALSE);
}

inline void Wyd_SdlSetRelativeMouse(bool on)
{
	SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

inline SDL_Surface* Wyd_SdlCreateRgbaSurface(void* pixels, int w, int h, int pitch)
{
	return SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, 32, pitch, SDL_PIXELFORMAT_RGBA32);
}

inline void Wyd_SdlDestroySurface(SDL_Surface* s)
{
	SDL_FreeSurface(s);
}

inline SDL_Keycode Wyd_SdlEventKey(const SDL_Event& ev)
{
	return ev.key.keysym.sym;
}

inline void Wyd_SdlStartTextInput(SDL_Window*)
{
	SDL_StartTextInput();
}

inline void Wyd_SdlStopTextInput(SDL_Window*)
{
	SDL_StopTextInput();
}

inline bool Wyd_SdlIsTextInputActive(SDL_Window*)
{
	return SDL_IsTextInputActive() != 0;
}

inline void Wyd_SdlSetTextInputArea(SDL_Window*, const SDL_Rect* r)
{
	SDL_SetTextInputRect(const_cast<SDL_Rect*>(r));
}

inline const Uint8* Wyd_SdlGetKeyboardState(int* n)
{
	return SDL_GetKeyboardState(n);
}

#endif // WYD_SDL3
