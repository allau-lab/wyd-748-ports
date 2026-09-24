#include "audio_sdl.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#else
#include <SDL.h>
#endif
#include <cstdio>

bool WYD_AudioInit(WYDAudio& out)
{
	out = {};
#ifdef WYD_USE_SDL3
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) // SDL3: bool
#else
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
#endif
	{
		out.status = std::string("SDL_INIT_AUDIO: ") + SDL_GetError();
		std::fprintf(stderr, "[WYDLINUX][audio] %s (seguindo sem áudio)\n", out.status.c_str());
		return false;
	}

#ifdef WYD_USE_SDL3
	SDL_AudioSpec want {};
	want.freq = 22050;
	want.format = SDL_AUDIO_S16LSB;
	want.channels = 1;

	SDL_AudioSpec have {};
	const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
	if (dev == 0)
	{
		out.status = std::string("OpenAudioDevice: ") + SDL_GetError();
		std::fprintf(stderr, "[WYDLINUX][audio] %s (seguindo sem áudio)\n", out.status.c_str());
		return false;
	}

	// Em SDL3 o device abre pausado; usar o formato obtido para informação.
	SDL_GetAudioDeviceFormat(dev, &have, 0);
	SDL_PauseAudioDevice(dev, 1);
	out.ready = true;
	out.status = "SDL3 audio OK (pausado)";
	std::fprintf(stderr, "[WYDLINUX][audio] %s freq=%d ch=%d\n",
		out.status.c_str(), have.freq, have.channels);
	static SDL_AudioDeviceID s_dev = 0;
	if (s_dev)
		SDL_CloseAudioDevice(s_dev);
	s_dev = dev;
#else
	SDL_AudioSpec want {};
	want.freq = 22050;
	want.format = AUDIO_S16LSB;
	want.channels = 1;
	want.samples = 512;
	want.callback = nullptr;

	SDL_AudioSpec have {};
	const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (dev == 0)
	{
		out.status = std::string("OpenAudioDevice: ") + SDL_GetError();
		std::fprintf(stderr, "[WYDLINUX][audio] %s (seguindo sem áudio)\n", out.status.c_str());
		return false;
	}

	SDL_PauseAudioDevice(dev, 1);
	out.ready = true;
	out.status = "SDL2 audio OK (pausado)";
	std::fprintf(stderr, "[WYDLINUX][audio] %s freq=%d ch=%d\n",
		out.status.c_str(), have.freq, have.channels);
	static SDL_AudioDeviceID s_dev = 0;
	if (s_dev)
		SDL_CloseAudioDevice(s_dev);
	s_dev = dev;
#endif
	return true;
}

void WYD_AudioShutdown(WYDAudio& out)
{
	if (out.ready)
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	out = {};
}
