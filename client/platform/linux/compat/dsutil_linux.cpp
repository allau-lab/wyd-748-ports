// CSoundManager / CSound — port do Linux (WAV via SDL_LoadWAV + mixer).
// SDL3: usa SDL_OpenAudioDeviceStream + SDL_ConvertAudioSamples.
#include "pch.h"
#include "dsutil.h"
#include "TMGlobal.h"
#include "TMLog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#ifdef WYD_USE_SDL3
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#else
#include <SDL.h>
#endif

namespace {

float DsVolumeToGain(LONG v)
{
	if (v <= -10000)
		return 0.f;
	if (v >= 0)
		return 1.f;
	return std::pow(10.f, static_cast<float>(v) / 2000.f);
}

std::string FixPath(const char* p)
{
	return WYD_NormalizePath(p ? p : "");
}

void AudioDiag(const char* fmt, const char* a, int b, int c)
{
#if defined(__SWITCH__)
	if (FILE* f = fopen("sdmc:/switch/client748/wyd748_diag.txt", "ab")) {
		fprintf(f, "[AUDIO] ");
		fprintf(f, fmt, a, b, c);
		fprintf(f, "\n");
		fclose(f);
	}
#else
	(void)fmt; (void)a; (void)b; (void)c;
#endif
}

#ifdef WYD_USE_SDL3

std::vector<int16_t> ToStereoS16(const Uint8* buf, Uint32 len, const SDL_AudioSpec& spec, const SDL_AudioSpec& target)
{
	if (spec.channels < 1 || target.channels < 1)
		return {};
	Uint8* converted = nullptr;
	int convertedLen = 0;
	if (!SDL_ConvertAudioSamples(&spec, buf, static_cast<int>(len), &target, &converted, &convertedLen))
		return {};
	if (!converted || convertedLen <= 0) {
		SDL_free(converted);
		return {};
	}
	const size_t sampleCount = static_cast<size_t>(convertedLen) / sizeof(int16_t);
	std::vector<int16_t> out(sampleCount);
	std::memcpy(out.data(), converted, convertedLen);
	SDL_free(converted);
	return out;
}

#else

std::vector<int16_t> ToStereoS16(const Uint8* buf, Uint32 len, const SDL_AudioSpec& spec, const SDL_AudioSpec& target)
{
	SDL_AudioCVT cvt {};
	if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, target.format, target.channels, target.freq) < 0)
		return {};
	cvt.len = static_cast<int>(len);
	cvt.buf = static_cast<Uint8*>(SDL_malloc(len * static_cast<Uint32>(cvt.len_mult)));
	if (!cvt.buf)
		return {};
	std::memcpy(cvt.buf, buf, len);
	if (SDL_ConvertAudio(&cvt) < 0) {
		SDL_free(cvt.buf);
		return {};
	}
	std::vector<int16_t> out(cvt.len_cvt / sizeof(int16_t));
	std::memcpy(out.data(), cvt.buf, cvt.len_cvt);
	SDL_free(cvt.buf);
	return out;
}

#endif

} // namespace

#ifdef WYD_USE_SDL3

// Escopo global (não anon namespace): precisa casar com o friend na classe.
void WYD_SfxStreamCallback(void* userdata, SDL_AudioStream* stream,
	int additional_amount, int total_amount)
{
	auto* mgr = static_cast<CSoundManager*>(userdata);
	std::lock_guard<std::mutex> lock(mgr->m_mtx);

	SDL_AudioSpec dst {};
	if (!SDL_GetAudioStreamFormat(stream, nullptr, &dst))
		return;
	if (dst.format != SDL_AUDIO_S16LE && dst.format != SDL_AUDIO_S16)
		return;
	if (dst.channels < 1)
		return;

	const int bytes = additional_amount > 0 ? additional_amount : total_amount;
	if (bytes <= 0)
		return;
	std::vector<Uint8> mix(bytes, 0);
	auto* out = reinterpret_cast<int16_t*>(mix.data());
	const int frames = bytes / (static_cast<int>(dst.channels) * static_cast<int>(SDL_AUDIO_BYTESIZE(dst.format)));
	if (frames <= 0)
		return;

	for (int i = 0; i < frames; ++i) {
		float l = 0.f;
		float r = 0.f;
		for (WYDVoice* v : mgr->m_active) {
			if (!v || !v->active || v->pcm.empty())
				continue;
			if (v->cursor + 1 >= v->pcm.size()) {
				if (v->loop)
					v->cursor = 0;
				else {
					v->active = false;
					continue;
				}
			}
			const float sl = v->pcm[v->cursor++] * v->volume * v->gainL;
			const float sr = v->pcm[v->cursor++] * v->volume * v->gainR;
			l += sl;
			r += sr;
		}
		if (dst.channels >= 2) {
			out[i * 2] = static_cast<int16_t>(std::clamp(l, -32768.f, 32767.f));
			out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768.f, 32767.f));
		} else {
			out[i] = static_cast<int16_t>(std::clamp((l + r) * 0.5f, -32768.f, 32767.f));
		}
	}
	SDL_PutAudioStreamData(stream, mix.data(), bytes);
}

#endif

void WYD_SfxCallback(void* userdata, Uint8* stream, int len)
{
#ifdef WYD_USE_SDL3
	// SDL3 uses SDL_OpenAudioDeviceStream; this legacy callback form is not used.
	(void)userdata;
	(void)stream;
	(void)len;
#else
	auto* mgr = static_cast<CSoundManager*>(userdata);
	std::memset(stream, 0, len);
	auto* out = reinterpret_cast<int16_t*>(stream);
	const int frames = len / 4; // stereo s16
	std::lock_guard<std::mutex> lock(mgr->m_mtx);
	for (WYDVoice* v : mgr->m_active) {
		if (!v || !v->active || v->pcm.empty())
			continue;
		for (int i = 0; i < frames; ++i) {
			if (v->cursor + 1 >= v->pcm.size()) {
				if (v->loop)
					v->cursor = 0;
				else {
					v->active = false;
					break;
				}
			}
			const float sl = v->pcm[v->cursor++] * v->volume * v->gainL;
			const float sr = v->pcm[v->cursor++] * v->volume * v->gainR;
			int l = out[i * 2] + static_cast<int>(sl);
			int r = out[i * 2 + 1] + static_cast<int>(sr);
			out[i * 2] = static_cast<int16_t>(std::clamp(l, -32768, 32767));
			out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768, 32767));
		}
	}
#endif
}

HRESULT DirectSoundCreate8(LPCGUID, LPDIRECTSOUND8* out, LPUNKNOWN)
{
	if (out)
		*out = reinterpret_cast<LPDIRECTSOUND8>(1);
	return DS_OK;
}

CSoundManager::CSoundManager()
	: m_pDS(nullptr)
	, m_nSoundVolume(0)
	, m_bMute(0)
	, m_pDSListener(nullptr)
#ifdef WYD_USE_SDL3
{
#else
	, m_device(0)
{
#endif
	g_pSoundManager = this;
	m_dsListenerParams = {};
	for (int i = 0; i < MAX_SOUNDLIST; ++i) {
		std::memset(m_stSoundDataList[i].szFileName, 0, sizeof m_stSoundDataList[i].szFileName);
		m_stSoundDataList[i].pSoundData = nullptr;
		m_stSoundDataList[i].nChannel = 1;
	}
}

CSoundManager::~CSoundManager()
{
#ifdef WYD_USE_SDL3
	if (m_stream) {
		SDL_DestroyAudioStream(m_stream);
		m_stream = nullptr;
	}
#else
	if (m_device) {
		SDL_CloseAudioDevice(m_device);
		m_device = 0;
	}
#endif
	for (int i = 0; i < MAX_SOUNDLIST; ++i) {
		delete m_stSoundDataList[i].pSoundData;
		m_stSoundDataList[i].pSoundData = nullptr;
	}
	if (g_pSoundManager == this)
		g_pSoundManager = nullptr;
}

int CSoundManager::LoadSoundData() { return 1; }

HRESULT CSoundManager::SetPrimaryBufferFormat(DWORD, DWORD, DWORD) { return S_OK; }

HRESULT CSoundManager::Get3DListenerInterface(LPDIRECTSOUND3DLISTENER* pp)
{
	if (pp)
		*pp = reinterpret_cast<LPDIRECTSOUND3DLISTENER>(1);
	m_pDSListener = reinterpret_cast<LPDIRECTSOUND3DLISTENER>(1);
	return S_OK;
}

HRESULT CSoundManager::Initialize(HWND, DWORD, DWORD, DWORD, DWORD)
{
#if !defined(__SWITCH__)
	// Preferir PulseAudio do host (container monta XDG_RUNTIME_DIR).
	if (!std::getenv("SDL_AUDIODRIVER") || !std::getenv("SDL_AUDIODRIVER")[0])
#ifdef WYD_USE_SDL3
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pulse");
#else
		SDL_SetHint(SDL_HINT_AUDIODRIVER, "pulse");
#endif
	if (!std::getenv("PULSE_SERVER") || !std::getenv("PULSE_SERVER")[0]) {
		const char* xdg = std::getenv("XDG_RUNTIME_DIR");
		if (xdg && xdg[0]) {
			char buf[512];
			std::snprintf(buf, sizeof(buf), "unix:%s/pulse/native", xdg);
			setenv("PULSE_SERVER", buf, 0);
		}
	}

#endif

#ifdef WYD_USE_SDL3
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { // SDL3: bool
#else
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
#endif
		LOG_WRITELOG("SDL_INIT_AUDIO: %s\n", SDL_GetError());
		AudioDiag("SDL_INIT_AUDIO falhou: %s (%d %d)", SDL_GetError(), 0, 0);
#if defined(__SWITCH__)
		g_pSoundManager = nullptr;
		return E_FAIL;
#endif
		// Fallback ALSA
#ifdef WYD_USE_SDL3
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "alsa");
#else
		SDL_SetHint(SDL_HINT_AUDIODRIVER, "alsa");
#endif
#ifdef WYD_USE_SDL3
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { // SDL3: bool
#else
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
#endif
			LOG_WRITELOG("SDL_INIT_AUDIO(alsa): %s\n", SDL_GetError());
			g_pSoundManager = nullptr;
			return E_FAIL;
		}
	}

#ifdef WYD_USE_SDL3
	SDL_AudioSpec want {};
	want.freq = 44100;
	want.format = SDL_AUDIO_S16LE;
	want.channels = 2;

	m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, WYD_SfxStreamCallback, this);
	if (!m_stream) {
		LOG_WRITELOG("OpenAudioDeviceStream: %s\n", SDL_GetError());
		AudioDiag("OpenAudioDeviceStream falhou: %s (%d %d)", SDL_GetError(), 0, 0);
		g_pSoundManager = nullptr;
		return E_FAIL;
	}
	if (!SDL_GetAudioStreamFormat(m_stream, nullptr, &m_have)) {
		LOG_WRITELOG("GetAudioStreamFormat: %s\n", SDL_GetError());
		SDL_DestroyAudioStream(m_stream);
		m_stream = nullptr;
		g_pSoundManager = nullptr;
		return E_FAIL;
	}
	LOG_WRITELOG("Audio OK driver=%s freq=%d ch=%d\n",
		SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
		m_have.freq, m_have.channels);
	AudioDiag("OK driver=%s freq=%d ch=%d",
		SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?", m_have.freq, m_have.channels);
	m_pDS = reinterpret_cast<LPDIRECTSOUND8>(1);
	if (!SDL_ResumeAudioStreamDevice(m_stream)) {
		LOG_WRITELOG("ResumeAudioStreamDevice: %s\n", SDL_GetError());
	}
#else
	SDL_AudioSpec want {};
	want.freq = 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = WYD_SfxCallback;
	want.userdata = this;

	m_device = SDL_OpenAudioDevice(nullptr, 0, &want, &m_have, 0);
	if (!m_device) {
		LOG_WRITELOG("OpenAudioDevice: %s\n", SDL_GetError());
		g_pSoundManager = nullptr;
		return E_FAIL;
	}
	LOG_WRITELOG("Audio OK driver=%s freq=%d ch=%d\n",
		SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
		m_have.freq, m_have.channels);
	m_pDS = reinterpret_cast<LPDIRECTSOUND8>(1);
	SDL_PauseAudioDevice(m_device, 0);
#endif

	FILE* fp = nullptr;
	fopen_s(&fp, FixPath(SoundList_Path).c_str(), "rt");
	if (fp) {
		int nIndex = -1;
		while (fscanf(fp, "%d", &nIndex) == 1) {
			if (nIndex > 0 && nIndex < MAX_SOUNDLIST) {
				if (fscanf(fp, "%255s %d", m_stSoundDataList[nIndex].szFileName, &m_stSoundDataList[nIndex].nChannel) != 2)
					break;
			}
		}
		fclose(fp);
	} else {
		LOG_WRITELOG("soundlist missing: %s\n", FixPath(SoundList_Path).c_str());
		AudioDiag("soundlist ausente: %s (%d %d)", FixPath(SoundList_Path).c_str(), 0, 0);
	}

	Get3DListenerInterface(&m_pDSListener);
	m_dsListenerParams.dwSize = sizeof(m_dsListenerParams);
	return S_OK;
}

void CSoundManager::RegisterVoice(WYDVoice* v)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	m_active.push_back(v);
}

void CSoundManager::UnregisterVoice(WYDVoice* v)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	m_active.erase(std::remove(m_active.begin(), m_active.end(), v), m_active.end());
}

HRESULT CSoundManager::Create(CSound** ppSound, LPTSTR strWaveFileName, DWORD, GUID, DWORD dwNumBuffers)
{
	if (!ppSound || !strWaveFileName)
		return E_INVALIDARG;
	*ppSound = nullptr;

	SDL_AudioSpec wavSpec {};
	Uint8* buf = nullptr;
	Uint32 len = 0;
	const std::string path = FixPath(strWaveFileName);
	if (!SDL_LoadWAV(path.c_str(), &wavSpec, &buf, &len)) {
		LOG_WRITELOG("SDL_LoadWAV(%s): %s\n", path.c_str(), SDL_GetError());
		AudioDiag("LoadWAV falhou: %s (%d %d)", path.c_str(), 0, 0);
		return E_FAIL;
	}
	auto pcm = ToStereoS16(buf, len, wavSpec, m_have);
#ifdef WYD_USE_SDL3
	SDL_free(buf);
#else
	SDL_FreeWAV(buf);
#endif
	if (pcm.empty())
		return E_FAIL;

	if (dwNumBuffers < 1)
		dwNumBuffers = 1;
	*ppSound = new CSound(this, std::move(pcm), dwNumBuffers);
	return S_OK;
}

HRESULT CSoundManager::CreateFromMemory(CSound**, BYTE*, ULONG, LPWAVEFORMATEX, DWORD, GUID, DWORD)
{
	return E_NOTIMPL;
}

HRESULT CSoundManager::CreateStreaming(CStreamingSound**, LPTSTR, DWORD, GUID, DWORD, DWORD, HANDLE)
{
	return E_NOTIMPL;
}

CSound* CSoundManager::GetSoundData(int nIndex)
{
	if (nIndex <= 0 || nIndex >= MAX_SOUNDLIST)
		return nullptr;
	if (m_nSoundVolume == -10000)
		return nullptr;
	if (!m_stSoundDataList[nIndex].pSoundData) {
		if (FAILED(Create(&m_stSoundDataList[nIndex].pSoundData, m_stSoundDataList[nIndex].szFileName, DSBCAPS_CTRLVOLUME, GUID_NULL, m_stSoundDataList[nIndex].nChannel))) {
			LOG_WRITELOG("Load Sound Error %d : %s\n", nIndex, m_stSoundDataList[nIndex].szFileName);
			return nullptr;
		}
		const int nBufferCount = static_cast<int>(m_stSoundDataList[nIndex].pSoundData->GetBufferCount());
		for (int j = 0; j < nBufferCount; ++j) {
			IDirectSoundBuffer* buff = m_stSoundDataList[nIndex].pSoundData->GetBuffer(j);
			if (buff)
				buff->SetVolume(m_nSoundVolume);
		}
	}
	return m_stSoundDataList[nIndex].pSoundData;
}

void CSoundManager::SetSoundVolumeByIndex(int nIndex, int nVolume)
{
	if (nIndex > 0 && nIndex < MAX_SOUNDLIST && m_nSoundVolume != -10000 && m_stSoundDataList[nIndex].pSoundData) {
		const int n = static_cast<int>(m_stSoundDataList[nIndex].pSoundData->GetBufferCount());
		for (int j = 0; j < n; ++j) {
			auto* b = m_stSoundDataList[nIndex].pSoundData->GetBuffer(j);
			if (b)
				b->SetVolume(nVolume);
		}
	}
}

void CSoundManager::SetSoundVolume(int nVolume)
{
	m_nSoundVolume = nVolume;
	for (int i = 1; i < MAX_SOUNDLIST; ++i)
		SetSoundVolumeByIndex(i, nVolume);
}

CSound::CSound(CSoundManager* mgr, std::vector<int16_t> pcm, DWORD dwNumBuffers)
	: m_dwNumBuffers(dwNumBuffers)
	, m_pWaveFile(nullptr)
	, m_dwCreationFlags(0)
	, m_mgr(mgr)
{
	m_channels.resize(dwNumBuffers);
	m_bufs.resize(dwNumBuffers);
	for (DWORD i = 0; i < dwNumBuffers; ++i) {
		m_channels[i].pcm = pcm;
		m_bufs[i].owner = this;
		m_bufs[i].index = static_cast<int>(i);
		if (m_mgr)
			m_mgr->RegisterVoice(&m_channels[i]);
	}
}

CSound::~CSound()
{
	Stop();
	if (m_mgr) {
		for (auto& v : m_channels)
			m_mgr->UnregisterVoice(&v);
	}
	delete m_pWaveFile;
}

unsigned int CSound::GetBufferCount() { return m_dwNumBuffers; }

HRESULT CSound::Get3DBufferInterface(DWORD, LPDIRECTSOUND3DBUFFER* pp)
{
	if (pp)
		*pp = nullptr;
	return E_NOTIMPL;
}

HRESULT CSound::FillBufferWithSound(LPDIRECTSOUNDBUFFER, BOOL) { return S_OK; }

LPDIRECTSOUNDBUFFER CSound::GetFreeBuffer()
{
	for (DWORD i = 0; i < m_dwNumBuffers; ++i) {
		if (!m_channels[i].active)
			return &m_bufs[i];
	}
	return m_dwNumBuffers ? &m_bufs[rand() % m_dwNumBuffers] : nullptr;
}

LPDIRECTSOUNDBUFFER CSound::GetBuffer(DWORD dwIndex)
{
	if (dwIndex < m_dwNumBuffers)
		return &m_bufs[dwIndex];
	return nullptr;
}

HRESULT CSound::Buf::SetVolume(LONG v)
{
	if (owner)
		owner->m_volume = v;
	return S_OK;
}

HRESULT CSound::Buf::GetStatus(DWORD* s)
{
	if (!s || !owner)
		return E_FAIL;
	*s = owner->m_channels[index].active ? 1u : 0u;
	return S_OK;
}

HRESULT CSound::Buf::Play(DWORD, DWORD flags, DWORD)
{
	if (!owner)
		return E_FAIL;
	auto& v = owner->m_channels[index];
	v.cursor = 0;
	v.loop = (flags & DSBPLAY_LOOPING) != 0;
	v.volume = DsVolumeToGain(owner->m_volume);
	v.active = true;
	return S_OK;
}

HRESULT CSound::Buf::Stop()
{
	if (owner)
		owner->m_channels[index].active = false;
	return S_OK;
}

HRESULT CSound::Play(DWORD, DWORD dwFlags, LONG, LONG, LONG lPan)
{
	if (m_mgr && m_mgr->m_bMute == 1)
		return S_OK;
	auto* buf = GetFreeBuffer();
	if (!buf)
		return E_FAIL;
	const int idx = static_cast<Buf*>(buf)->index;
	auto& v = m_channels[idx];
	// pan -10000..10000
	const float t = std::clamp(lPan / 10000.f, -1.f, 1.f);
	v.gainL = t <= 0 ? 1.f : 1.f - t;
	v.gainR = t >= 0 ? 1.f : 1.f + t;
	return buf->Play(0, dwFlags, 0);
}

HRESULT CSound::Play3D(LPDS3DBUFFER p3D, DWORD, DWORD dwFlags, LONG)
{
	LONG pan = 0;
	if (p3D) {
		const float x = p3D->vPosition.x;
		pan = static_cast<LONG>(std::clamp(x * 200.f, -10000.f, 10000.f));
	}
	return Play(0, dwFlags, 0, -1, pan);
}

HRESULT CSound::Stop()
{
	for (auto& v : m_channels)
		v.active = false;
	return S_OK;
}

HRESULT CSound::Reset()
{
	for (auto& v : m_channels)
		v.cursor = 0;
	return S_OK;
}

BOOL CSound::IsSoundPlaying()
{
	for (auto& v : m_channels)
		if (v.active)
			return TRUE;
	return FALSE;
}

CStreamingSound::CStreamingSound(CSoundManager* mgr, std::vector<int16_t> pcm, DWORD)
	: CSound(mgr, std::move(pcm), 1)
{
}

CStreamingSound::~CStreamingSound() = default;
HRESULT CStreamingSound::HandleWaveStreamNotification(BOOL) { return S_OK; }
HRESULT CStreamingSound::Reset() { return CSound::Reset(); }

CWaveFile::CWaveFile()
	: m_pwfx(nullptr)
	, m_dwSize(0)
	, m_bIsReadingFromMemory(FALSE)
	, m_pbData(nullptr)
	, m_pbDataCur(nullptr)
	, m_ulDataSize(0)
{
}

CWaveFile::~CWaveFile() { Close(); }

HRESULT CWaveFile::Close()
{
	delete m_pwfx;
	m_pwfx = nullptr;
	pcmStereo.clear();
	m_dwSize = 0;
	return S_OK;
}

HRESULT CWaveFile::Open(LPTSTR strFileName, WAVEFORMATEX*, DWORD)
{
	Close();
	SDL_AudioSpec spec {};
	Uint8* buf = nullptr;
	Uint32 len = 0;
	if (!SDL_LoadWAV(FixPath(strFileName).c_str(), &spec, &buf, &len))
		return E_FAIL;
	m_pwfx = new WAVEFORMATEX{};
	m_pwfx->wFormatTag = WAVE_FORMAT_PCM;
	m_pwfx->nChannels = spec.channels;
	m_pwfx->nSamplesPerSec = spec.freq;
	m_pwfx->wBitsPerSample = 16;
	m_pwfx->nBlockAlign = static_cast<WORD>(spec.channels * 2);
	m_pwfx->nAvgBytesPerSec = m_pwfx->nSamplesPerSec * m_pwfx->nBlockAlign;
	m_dwSize = len;
	SDL_AudioSpec target = spec;
#ifdef WYD_USE_SDL3
	target.format = SDL_AUDIO_S16LE;
#else
	target.format = AUDIO_S16SYS;
#endif
	target.channels = 2;
	pcmStereo = ToStereoS16(buf, len, spec, target);
#ifdef WYD_USE_SDL3
	SDL_free(buf);
#else
	SDL_FreeWAV(buf);
#endif
	return S_OK;
}

HRESULT CWaveFile::OpenFromMemory(BYTE*, ULONG, WAVEFORMATEX*, DWORD) { return E_NOTIMPL; }

HRESULT CWaveFile::Read(BYTE* pBuffer, DWORD dwSizeToRead, DWORD* pdwSizeRead)
{
	if (!pdwSizeRead)
		return E_INVALIDARG;
	*pdwSizeRead = 0;
	(void)pBuffer;
	(void)dwSizeToRead;
	return E_NOTIMPL;
}
