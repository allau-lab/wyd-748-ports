// DS_SOUND_MANAGER — BGM/SFX de arquivo (WAV/MP3) via SDL + minimp3.
#include "pch.h"
#include "DirShow.h"
#include "TMGlobal.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

int DS_SOUND_MANAGER::m_nMusicIndex = -1;
int DS_SOUND_MANAGER::m_nCastleIndex = -1;
char DS_SOUND_MANAGER::m_szMusicPathOrigin[15][256] = {
    "music/login.mp3", "music/town01.mp3", "music/field01.mp3", "music/town02.mp3",
    "music/field02.mp3", "music/dungeon01.mp3", "music/kingdom.mp3", "music/dungeon02.mp3",
    "music/town03.mp3", "music/field03.mp3", "music/CastleWar.mp3", "music/kepra.mp3",
    "music/khepraBoss.mp3", "", ""
};
char DS_SOUND_MANAGER::m_szMusicPath[15][256] = {
    "music/login.mp3", "music/town01.mp3", "music/field01.mp3", "music/town02.mp3",
    "music/field02.mp3", "music/dungeon01.mp3", "music/kingdom.mp3", "music/dungeon02.mp3",
    "music/town03.mp3", "music/field03.mp3", "music/CastleWar.mp3", "music/kepra.mp3",
    "music/khepraBoss.mp3", "", ""
};

namespace
{

std::string FixPath(const char* p)
{
    return WYD_NormalizePath(p ? p : "");
}

float DsVolToGain(int v)
{
    if (v <= -10000)
        return 0.f;
    if (v >= 0)
        return 1.f;
    return std::pow(10.f, static_cast<float>(v) / 2000.f);
}

bool LoadWavStereo(const char* path, std::vector<int16_t>& out, const SDL_AudioSpec& target)
{
    SDL_AudioSpec spec {};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len))
        return false;

#ifdef WYD_USE_SDL3
    Uint8* dst = nullptr;
    int dst_len = 0;
    if (!SDL_ConvertAudioSamples(&spec, buf, static_cast<int>(len), &target, &dst, &dst_len)) {
        SDL_free(buf);
        return false;
    }
    out.resize(dst_len / sizeof(int16_t));
    std::memcpy(out.data(), dst, dst_len);
    SDL_free(dst);
    SDL_free(buf);
    return true;
#else
    SDL_AudioCVT cvt {};
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, target.format, target.channels, target.freq) < 0) {
        SDL_FreeWAV(buf);
        return false;
    }
    cvt.len = static_cast<int>(len);
    cvt.buf = static_cast<Uint8*>(SDL_malloc(len * cvt.len_mult));
    std::memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);
    if (SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        return false;
    }
    out.resize(cvt.len_cvt / sizeof(int16_t));
    std::memcpy(out.data(), cvt.buf, cvt.len_cvt);
    SDL_free(cvt.buf);
    return true;
#endif
}

bool LoadMp3Stereo(const char* path, std::vector<int16_t>& out, int targetFreq)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.empty())
        return false;

    mp3dec_t dec {};
    mp3dec_init(&dec);
    mp3dec_frame_info_t info {};
    std::vector<int16_t> monoOrStereo;
    size_t pos = 0;
    int sampleRate = 0;
    int channels = 0;
    while (pos < file.size()) {
        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        const int samples = mp3dec_decode_frame(&dec, file.data() + pos, static_cast<int>(file.size() - pos), pcm, &info);
        if (samples > 0) {
            sampleRate = info.hz;
            channels = info.channels;
            monoOrStereo.insert(monoOrStereo.end(), pcm, pcm + samples * channels);
        }
        if (info.frame_bytes <= 0) {
            ++pos;
            continue;
        }
        pos += static_cast<size_t>(info.frame_bytes);
    }
    if (monoOrStereo.empty() || sampleRate <= 0)
        return false;

    // Converter para stereo targetFreq
#ifdef WYD_USE_SDL3
    std::vector<uint8_t> pcm8;
    pcm8.resize(monoOrStereo.size() * sizeof(int16_t));
    std::memcpy(pcm8.data(), monoOrStereo.data(), pcm8.size());

    SDL_AudioSpec src {};
    src.format = SDL_AUDIO_S16;
    src.channels = static_cast<int>(channels);
    src.freq = sampleRate;
    SDL_AudioSpec dstSpec {};
    dstSpec.format = SDL_AUDIO_S16;
    dstSpec.channels = 2;
    dstSpec.freq = targetFreq;
    Uint8* dst = nullptr;
    int dst_len = 0;
    if (!SDL_ConvertAudioSamples(&src, pcm8.data(), static_cast<int>(pcm8.size()), &dstSpec, &dst, &dst_len))
        return false;
    out.resize(dst_len / sizeof(int16_t));
    std::memcpy(out.data(), dst, dst_len);
    SDL_free(dst);
    return true;
#else
    SDL_AudioCVT cvt {};
    const SDL_AudioFormat fmt = AUDIO_S16SYS;
    if (SDL_BuildAudioCVT(&cvt, fmt, static_cast<Uint8>(channels), sampleRate, fmt, 2, targetFreq) < 0)
        return false;
    cvt.len = static_cast<int>(monoOrStereo.size() * sizeof(int16_t));
    cvt.buf = static_cast<Uint8*>(SDL_malloc(cvt.len * cvt.len_mult));
    std::memcpy(cvt.buf, monoOrStereo.data(), cvt.len);
    if (SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(cvt.buf);
        return false;
    }
    out.resize(cvt.len_cvt / sizeof(int16_t));
    std::memcpy(out.data(), cvt.buf, cvt.len_cvt);
    SDL_free(cvt.buf);
    return true;
#endif
}

} // namespace

#ifdef WYD_USE_SDL3
void WYD_BgmStreamCallback(void* userdata, SDL_AudioStream* stream, int, int total);
#endif

void WYD_BgmCallback(void* userdata, Uint8* stream, int len)
{
    auto* mgr = static_cast<DS_SOUND_MANAGER*>(userdata);
    std::memset(stream, 0, len);
    auto* out = reinterpret_cast<int16_t*>(stream);
    const int frames = len / 4;
    std::lock_guard<std::mutex> lock(mgr->mtx);
    for (auto& ch : mgr->channels)
        ch.MixInto(out, frames);
}

#ifdef WYD_USE_SDL3
void WYD_BgmStreamCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int)
{
    // additional_amount está em BYTES (stereo S16 = 4 bytes por frame).
    const int frames = additional_amount / 4;
    if (frames <= 0)
        return;
    auto* mgr = static_cast<DS_SOUND_MANAGER*>(userdata);
    std::vector<uint8_t> frame(static_cast<size_t>(frames) * 4, 0);
    auto* out = reinterpret_cast<int16_t*>(frame.data());
    std::lock_guard<std::mutex> lock(mgr->mtx);
    for (auto& ch : mgr->channels)
        ch.MixInto(out, frames);
    SDL_PutAudioStreamData(stream, frame.data(), static_cast<int>(frame.size()));
}
#endif

struct DS_SOUND_CHANNEL::Mp3Stream {
    std::vector<uint8_t> file;
    size_t pos = 0;
    mp3dec_t dec {};
    std::vector<int16_t> frame;
    size_t cur = 0;
};

bool DS_SOUND_CHANNEL::NextStreamFrame()
{
    Mp3Stream& s = *stream;
    mp3d_sample_t samples[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (s.pos < s.file.size()) {
        mp3dec_frame_info_t info {};
        const int n = mp3dec_decode_frame(&s.dec, s.file.data() + s.pos,
            static_cast<int>(s.file.size() - s.pos), samples, &info);
        if (info.frame_bytes <= 0)
            break;
        s.pos += static_cast<size_t>(info.frame_bytes);
        if (n <= 0)
            continue;
        s.frame.resize(static_cast<size_t>(n) * 2);
        for (int i = 0; i < n; ++i) {
            const int16_t l = samples[i * info.channels];
            s.frame[i * 2] = l;
            s.frame[i * 2 + 1] = info.channels > 1 ? samples[i * info.channels + 1] : l;
        }
        s.cur = 0;
        return true;
    }
    return false;
}

DS_SOUND_CHANNEL::DS_SOUND_CHANNEL() = default;
DS_SOUND_CHANNEL::~DS_SOUND_CHANNEL() = default;
void DS_SOUND_CHANNEL::InitClass()
{
    stream.reset();
    pcm.clear();
    cursor = 0;
    playing = false;
    paused = false;
    state = State_Stopped;
}
char DS_SOUND_CHANNEL::CleanGraph()
{
    InitClass();
    return 1;
}
FILTER_STATE DS_SOUND_CHANNEL::GetState() const { return state; }
HRESULT DS_SOUND_CHANNEL::GetVolume(long* vol)
{
    if (!vol)
        return E_INVALIDARG;
    *vol = volume >= 0.999f ? 0 : static_cast<long>(2000.f * std::log10(std::max(volume, 1e-5f)));
    return S_OK;
}
HRESULT DS_SOUND_CHANNEL::SetVolume(long vol)
{
    volume = DsVolToGain(static_cast<int>(vol));
    return S_OK;
}
HRESULT DS_SOUND_CHANNEL::SetBalance(long) { return S_OK; }
HRESULT DS_SOUND_CHANNEL::Run()
{
    playing = true;
    paused = false;
    state = State_Running;
    return S_OK;
}
HRESULT DS_SOUND_CHANNEL::Stop()
{
    playing = false;
    state = State_Stopped;
    return S_OK;
}
HRESULT DS_SOUND_CHANNEL::Pause()
{
    paused = true;
    state = State_Paused;
    return S_OK;
}
HRESULT DS_SOUND_CHANNEL::SetPosition(long long pos)
{
    if (stream) {
        // Só o reinício é usado (PlaySoundA); seek arbitrário exigiria varrer frames.
        stream->pos = 0;
        stream->frame.clear();
        stream->cur = 0;
        mp3dec_init(&stream->dec);
        return S_OK;
    }
    if (pcm.empty())
        return S_OK;
    const size_t frame = static_cast<size_t>(pos / 4);
    cursor = std::min(frame * 2, pcm.size());
    return S_OK;
}

bool DS_SOUND_CHANNEL::LoadFile(const char* path)
{
    InitClass();
    SDL_AudioSpec target {};
    target.freq = 44100;
#ifdef WYD_USE_SDL3
    target.format = SDL_AUDIO_S16;
#else
    target.format = AUDIO_S16SYS;
#endif
    target.channels = 2;
    const std::string p = FixPath(path);
    const bool isWav = p.size() >= 4 && (strcasecmp(p.c_str() + p.size() - 4, ".wav") == 0);
    if (isWav)
        return LoadWavStereo(p.c_str(), pcm, target);

    auto st = std::make_shared<Mp3Stream>();
    {
        std::ifstream f(p, std::ios::binary);
        if (!f)
            return false;
        st->file.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (st->file.empty())
        return false;
    mp3dec_init(&st->dec);
    mp3dec_frame_info_t info {};
    size_t probe = 0;
    while (probe < st->file.size()) {
        const int n = mp3dec_decode_frame(&st->dec, st->file.data() + probe,
            static_cast<int>(st->file.size() - probe), nullptr, &info);
        if (info.frame_bytes <= 0)
            return false;
        if (n > 0)
            break;
        probe += static_cast<size_t>(info.frame_bytes);
    }
    if (info.hz != target.freq)
        return LoadMp3Stereo(p.c_str(), pcm, target.freq);
    mp3dec_init(&st->dec);
    stream = std::move(st);
    return true;
}

void DS_SOUND_CHANNEL::MixInto(int16_t* out, int frames)
{
    if (!playing || paused)
        return;
    if (stream) {
        for (int i = 0; i < frames; ++i) {
            if (stream->cur + 1 >= stream->frame.size() && !NextStreamFrame()) {
                SetPosition(0);
                if (!loop || !NextStreamFrame()) {
                    playing = false;
                    state = State_Stopped;
                    break;
                }
            }
            int l = out[i * 2] + static_cast<int>(stream->frame[stream->cur++] * volume);
            int r = out[i * 2 + 1] + static_cast<int>(stream->frame[stream->cur++] * volume);
            out[i * 2] = static_cast<int16_t>(std::clamp(l, -32768, 32767));
            out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768, 32767));
        }
        return;
    }
    if (pcm.empty())
        return;
    for (int i = 0; i < frames; ++i) {
        if (cursor + 1 >= pcm.size()) {
            if (loop)
                cursor = 0;
            else {
                playing = false;
                state = State_Stopped;
                break;
            }
        }
        int l = out[i * 2] + static_cast<int>(pcm[cursor++] * volume);
        int r = out[i * 2 + 1] + static_cast<int>(pcm[cursor++] * volume);
        out[i * 2] = static_cast<int16_t>(std::clamp(l, -32768, 32767));
        out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768, 32767));
    }
}

DS_SOUND_MANAGER::DS_SOUND_MANAGER(int channel_num_, int lBGMVolume)
    : m_lBGMVolume(lBGMVolume)
{
    InitClass(channel_num_);
}

DS_SOUND_MANAGER::~DS_SOUND_MANAGER()
{
#ifdef WYD_USE_SDL3
    if (bgm_stream) {
        SDL_DestroyAudioStream(bgm_stream);
        bgm_stream = nullptr;
    }
#else
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
#endif
}

void DS_SOUND_MANAGER::InitClass(int n)
{
    channel_num = std::max(n, 1);
    cur_channel = 1;
    channels.assign(static_cast<size_t>(channel_num), DS_SOUND_CHANNEL{});
    channelVolumes.assign(static_cast<size_t>(channel_num), 0);
    for (auto& c : channels)
        c.InitClass();

#ifdef WYD_USE_SDL3
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { // SDL3: bool
#else
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
#endif
        init_flag = false;
        return;
    }
#ifdef WYD_USE_SDL3
    SDL_AudioSpec want {};
    want.freq = 44100;
    want.format = SDL_AUDIO_S16;
    want.channels = 2;
    bgm_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &want,
        WYD_BgmStreamCallback,
        this);
    init_flag = bgm_stream != nullptr;
    if (init_flag)
        SDL_ResumeAudioStreamDevice(bgm_stream);
#else
    SDL_AudioSpec want {};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = WYD_BgmCallback;
    want.userdata = this;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    init_flag = device != 0;
    if (init_flag)
        SDL_PauseAudioDevice(device, 0);
#endif
}

int DS_SOUND_MANAGER::PlaySoundA(const char* path, const bool BGM_flag)
{
    if (!init_flag || !path)
        return -1;
    int channel = BGM_flag ? 0 : cur_channel;
    if (!BGM_flag && channel_num < 2)
        return -1;
    if (channel < 0 || channel >= channel_num)
        return -1;

    DS_SOUND_CHANNEL loaded;
    if (!loaded.LoadFile(path))
        return -1;

    std::lock_guard<std::mutex> lock(mtx);
    auto& ch = channels[static_cast<size_t>(channel)];
    ch.Stop();
    ch.stream = std::move(loaded.stream);
    ch.pcm = std::move(loaded.pcm);
    ch.cursor = 0;
    ch.loop = BGM_flag ? true : false;
    ch.SetVolume(channelVolumes[static_cast<size_t>(channel)]);
    ch.SetPosition(0);
    ch.Run();

    if (BGM_flag)
        return 0;
    const int play_channel = cur_channel;
    cur_channel = (cur_channel + 1 == channel_num) ? 1 : cur_channel + 1;
    return play_channel;
}

int DS_SOUND_MANAGER::PlayBGM(const char* path) { return PlaySoundA(path, true); }

void DS_SOUND_MANAGER::PlayMusic(int nIndex)
{
    m_nMusicIndex = nIndex;
    if (nIndex < 0 || nIndex >= 15)
        return;
    StopBGM();
    const std::string primary = FixPath(m_szMusicPath[nIndex]);
    const std::string fallback = FixPath(m_szMusicPathOrigin[nIndex]);
    if (access(primary.c_str(), F_OK) == 0)
        PlayBGM(primary.c_str());
    else
        PlayBGM(fallback.c_str());
    if (GetVolume(0) != -10000)
        SetVolume(0, m_lBGMVolume);
}

void DS_SOUND_MANAGER::PlayMusic2(int nIndex)
{
    m_nCastleIndex = nIndex;
    PlayMusic(nIndex);
}

void DS_SOUND_MANAGER::PlayASF(char* url)
{
    if (url)
        PlayBGM(url);
}
void DS_SOUND_MANAGER::StopASF() { StopBGM(); }
void DS_SOUND_MANAGER::OnEvent() {}

HRESULT DS_SOUND_MANAGER::RunAll() { return Run(); }
HRESULT DS_SOUND_MANAGER::StopAll() { return Stop(); }
HRESULT DS_SOUND_MANAGER::PauseAll() { return Pause(); }
HRESULT DS_SOUND_MANAGER::RunSounds() { return Run(); }
HRESULT DS_SOUND_MANAGER::StopSounds() { return Stop(); }
HRESULT DS_SOUND_MANAGER::PauseSounds() { return Pause(); }

HRESULT DS_SOUND_MANAGER::StopBGM()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (!channels.empty())
        channels[0].Stop();
    return S_OK;
}

HRESULT DS_SOUND_MANAGER::Run()
{
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& c : channels)
        if (c.HasData())
            c.Run();
    return S_OK;
}
HRESULT DS_SOUND_MANAGER::Stop()
{
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& c : channels)
        c.Stop();
    return S_OK;
}
HRESULT DS_SOUND_MANAGER::Pause()
{
    std::lock_guard<std::mutex> lock(mtx);
    for (auto& c : channels)
        c.Pause();
    return S_OK;
}
HRESULT DS_SOUND_MANAGER::SetEntVolume() { return S_OK; }
HRESULT DS_SOUND_MANAGER::SetEntBalance() { return S_OK; }

HRESULT DS_SOUND_MANAGER::SetVolume(const int which, const int vol)
{
    if (which < 0 || which >= channel_num)
        return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(mtx);
    channelVolumes[static_cast<size_t>(which)] = vol;
    channels[static_cast<size_t>(which)].SetVolume(vol);
    return S_OK;
}

int DS_SOUND_MANAGER::GetVolume(const int which)
{
    if (which < 0 || which >= channel_num)
        return -10000;
    return channelVolumes[static_cast<size_t>(which)];
}
