#pragma once
// Port Linux do dsutil — API idêntica ao client; backend SDL2 ou SDL3.
#include "dsound.h"
#include "TMPaths.h"

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <vector>
#include <mutex>
#include <string>

class CSoundManager;
class CSound;
class CStreamingSound;
class CWaveFile;

constexpr auto MAX_SOUNDLIST = 512;

#if !defined DDERR_NOTINITIALIZED
#define DDERR_NOTINITIALIZED 0x800401F0
#endif
#ifndef GUID_NULL
inline constexpr GUID GUID_NULL = {};
#endif

#define WAVEFILE_READ 1
#define WAVEFILE_WRITE 2
#define DSUtil_StopSound(s) \
	do { \
		if (s) \
			(s)->Stop(); \
	} while (0)
#define DSUtil_PlaySound(s) \
	do { \
		if (s) \
			(s)->Play(0, 0); \
	} while (0)
#define DSUtil_PlaySoundLooping(s) \
	do { \
		if (s) \
			(s)->Play(0, DSBPLAY_LOOPING); \
	} while (0)

struct stSoundData {
	CSound* pSoundData;
	char szFileName[256];
	int nChannel;
};

struct DS3DLISTENER {
	DWORD dwSize;
	FLOAT vPosition[3];
	FLOAT vVelocity[3];
	FLOAT vOrientFront[3];
	FLOAT vOrientTop[3];
	FLOAT flDistanceFactor;
	FLOAT flRolloffFactor;
	FLOAT flDopplerFactor;
};

struct WYDVoice {
	std::vector<int16_t> pcm; // stereo interleaved
	size_t cursor = 0;
	bool loop = false;
	bool active = false;
	float gainL = 1.f;
	float gainR = 1.f;
	float volume = 1.f;
};

class CSoundManager {
protected:
	LPDIRECTSOUND8 m_pDS;
	stSoundData m_stSoundDataList[MAX_SOUNDLIST];

public:
	int m_nSoundVolume;
	int m_bMute;
	LPDIRECTSOUND3DLISTENER m_pDSListener;
	DS3DLISTENER m_dsListenerParams;

	CSoundManager();
	~CSoundManager();

	int LoadSoundData();
	HRESULT Initialize(HWND hWnd, DWORD dwCoopLevel, DWORD dwPrimaryChannels, DWORD dwPrimaryFreq, DWORD dwPrimaryBitRate);
	LPDIRECTSOUND8 GetDirectSound() { return m_pDS; }
	HRESULT SetPrimaryBufferFormat(DWORD, DWORD, DWORD);
	HRESULT Get3DListenerInterface(LPDIRECTSOUND3DLISTENER* pp);

	HRESULT Create(CSound** ppSound, LPTSTR strWaveFileName, DWORD dwCreationFlags = 0, GUID guid3DAlgorithm = GUID_NULL, DWORD dwNumBuffers = 1);
	HRESULT CreateFromMemory(CSound** ppSound, BYTE* pbData, ULONG ulDataSize, LPWAVEFORMATEX pwfx, DWORD dwCreationFlags = 0, GUID guid3DAlgorithm = GUID_NULL, DWORD dwNumBuffers = 1);
	HRESULT CreateStreaming(CStreamingSound** pp, LPTSTR, DWORD, GUID, DWORD, DWORD, HANDLE);
	CSound* GetSoundData(int nIndex);
	void SetSoundVolumeByIndex(int nIndex, int nVolume);
	void SetSoundVolume(int nVolume);

	void RegisterVoice(WYDVoice* v);
	void UnregisterVoice(WYDVoice* v);
	SDL_AudioSpec HaveSpec() const { return m_have; }

private:
	friend void WYD_SfxCallback(void*, Uint8*, int);
#ifdef WYD_USE_SDL3
	friend void WYD_SfxStreamCallback(void*, SDL_AudioStream*, int, int);
	SDL_AudioStream* m_stream = nullptr;
#else
	SDL_AudioDeviceID m_device = 0;
#endif
	SDL_AudioSpec m_have{};
	std::mutex m_mtx;
	std::vector<WYDVoice*> m_active;
};

class CSound {
protected:
	struct Buf final : IDirectSoundBuffer {
		CSound* owner = nullptr;
		int index = 0;
		HRESULT SetVolume(LONG v) override;
		HRESULT GetStatus(DWORD* s) override;
		HRESULT Play(DWORD, DWORD, DWORD) override;
		HRESULT Stop() override;
	};

	std::vector<WYDVoice> m_channels;
	std::vector<Buf> m_bufs;
	DWORD m_dwNumBuffers;
	CWaveFile* m_pWaveFile;
	DWORD m_dwCreationFlags;
	CSoundManager* m_mgr;
	LONG m_volume = 0;

public:
	CSound(CSoundManager* mgr, std::vector<int16_t> pcm, DWORD dwNumBuffers);
	virtual ~CSound();

	unsigned int GetBufferCount();
	HRESULT Get3DBufferInterface(DWORD, LPDIRECTSOUND3DBUFFER*);
	HRESULT FillBufferWithSound(LPDIRECTSOUNDBUFFER, BOOL);
	LPDIRECTSOUNDBUFFER GetFreeBuffer();
	LPDIRECTSOUNDBUFFER GetBuffer(DWORD dwIndex);

	HRESULT Play(DWORD dwPriority = 0, DWORD dwFlags = 0, LONG lVolume = 0, LONG lFrequency = -1, LONG lPan = 0);
	HRESULT Play3D(LPDS3DBUFFER p3DBuffer, DWORD dwPriority = 0, DWORD dwFlags = 0, LONG lFrequency = 0);
	HRESULT Stop();
	HRESULT Reset();
	BOOL IsSoundPlaying();
};

class CStreamingSound : public CSound {
public:
	CStreamingSound(CSoundManager* mgr, std::vector<int16_t> pcm, DWORD);
	~CStreamingSound() override;
	HRESULT HandleWaveStreamNotification(BOOL);
	HRESULT Reset();
};

class CWaveFile {
public:
	WAVEFORMATEX* m_pwfx;
	DWORD m_dwSize;
	BOOL m_bIsReadingFromMemory;
	BYTE* m_pbData;
	BYTE* m_pbDataCur;
	ULONG m_ulDataSize;
	std::vector<int16_t> pcmStereo;

	CWaveFile();
	~CWaveFile();
	HRESULT Open(LPTSTR strFileName, WAVEFORMATEX* pwfx, DWORD dwFlags);
	HRESULT OpenFromMemory(BYTE* pbData, ULONG ulDataSize, WAVEFORMATEX* pwfx, DWORD dwFlags);
	HRESULT Read(BYTE* pBuffer, DWORD dwSizeToRead, DWORD* pdwSizeRead);
	DWORD GetSize() const { return m_dwSize; }
	HRESULT Close();
};
