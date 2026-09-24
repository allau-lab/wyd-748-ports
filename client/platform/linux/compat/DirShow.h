#pragma once
// Port Linux do DirShow — mesma API DS_SOUND_MANAGER; BGM MP3 via minimp3 + SDL.
#include "dsound.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

struct IBaseFilter;
struct IGraphBuilder;
struct IMediaControl;
struct IMediaSeeking;
struct IBasicAudio;
struct IMediaEventEx;
enum FILTER_STATE { State_Stopped = 0, State_Paused = 1, State_Running = 2 };

class DS_SOUND_CHANNEL {
public:
	DS_SOUND_CHANNEL();
	~DS_SOUND_CHANNEL();
	void InitClass();
	char CleanGraph();
	bool HasFilter(IBaseFilter*) { return false; }
	FILTER_STATE GetState() const;
	void OnEvent() {}
	IGraphBuilder* GetGraphBuilder() { return nullptr; }
	HRESULT GetVolume(long* vol);
	HRESULT SetVolume(long vol);
	HRESULT SetBalance(long);
	HRESULT Run();
	HRESULT Stop();
	HRESULT Pause();
	HRESULT SetPosition(long long pos);

	bool LoadFile(const char* path);
	void MixInto(int16_t* out, int frames);
	bool HasData() const { return !pcm.empty() || stream != nullptr; }

private:
	friend class DS_SOUND_MANAGER;
	struct Mp3Stream;
	bool NextStreamFrame();
	// MP3 44.1 kHz é decodificado frame a frame no callback: decodificar a faixa
	// inteira custa segundos de CPU e dezenas de MB por troca de mapa no Switch.
	std::shared_ptr<Mp3Stream> stream;
	std::vector<int16_t> pcm;
	size_t cursor = 0;
	bool playing = false;
	bool paused = false;
	bool loop = true;
	float volume = 1.f;
	FILTER_STATE state = State_Stopped;
};

class DS_SOUND_MANAGER {
public:
	enum { DEF_CHANNELS = 0x5, MIN_CHANNELS = 0x1, ISERROR = 0xFFFFFFFF };

	DS_SOUND_MANAGER(int channel_num, int lBGMVolume);
	~DS_SOUND_MANAGER();
	void InitClass(int channel_num);
	int PlaySoundA(const char* path, const bool BGM_flag);
	int PlayBGM(const char* path);
	void PlayMusic(int nIndex);
	void PlayMusic2(int nIndex);
	void PlayASF(char* szURL);
	void StopASF();
	void OnEvent();
	HRESULT RunAll();
	HRESULT StopAll();
	HRESULT PauseAll();
	HRESULT RunSounds();
	HRESULT StopSounds();
	HRESULT PauseSounds();
	HRESULT StopBGM();
	HRESULT Run();
	HRESULT Stop();
	HRESULT Pause();
	HRESULT SetEntVolume();
	HRESULT SetEntBalance();
	HRESULT SetVolume(const int which, const int vol);
	int GetVolume(const int which);

	static int m_nMusicIndex;
	static int m_nCastleIndex;
	static char m_szMusicPathOrigin[15][256];
	static char m_szMusicPath[15][256];

	int m_lBGMVolume = 0;

private:
	friend void WYD_BgmCallback(void*, Uint8*, int);
	friend void WYD_BgmStreamCallback(void*, SDL_AudioStream*, int, int);
	int channel_num = 0;
	int cur_channel = 1;
	bool init_flag = false;
	std::vector<DS_SOUND_CHANNEL> channels;
#ifdef WYD_USE_SDL3
	SDL_AudioStream* bgm_stream = nullptr;
#else
	SDL_AudioDeviceID device = 0;
#endif
	SDL_AudioSpec have {};
	std::mutex mtx;
	std::vector<int> channelVolumes;
	HWND m_hwndASFPlayer = nullptr;
};
