#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../VideoServices/stdafx.h"
#endif
#include "../VideoServices/SoundServer.h"

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "SoftMixer.h"

#define STB_VORBIS_HEADER_ONLY
#include "../ThirdParty/stb/stb_vorbis.c"   // the declarations; the decoder itself is compiled in StbVorbis.cpp

namespace
{
SDL_AudioDeviceID audioDevice = 0;
// Guarded only by the same SDL_Lock/UnlockAudioDevice calls that already protect the mixer's voices: the audio callback and the main thread both
// touch it.
double gMasterVolume = 1.0;
SoftMix::Mixer gMixer;

std::uint16_t ReadUint16(const char* data)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[0])) |
           static_cast<std::uint16_t>(static_cast<unsigned char>(data[1]) << 8);
}

std::uint32_t ReadUint32(const char* data)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[0])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[1]) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[2]) << 16) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(data[3]) << 24);
}

// The resource's sound (length, a WAVEFORMATEX, the samples) as a 44.1 kHz stereo sound (SoftMixer.h says why this is done here and not by SDL).
std::shared_ptr<SoftMix::Sound> DecodeSound(const char* data, bool loop)
{
    if (data == nullptr) {
        return nullptr;
    }
    const std::uint32_t dataLength = ReadUint32(data);
    const std::uint16_t formatTag = ReadUint16(data + 4);
    const std::uint16_t channels = ReadUint16(data + 6);
    const std::uint32_t sampleRate = ReadUint32(data + 8);
    const std::uint16_t bitsPerSample = ReadUint16(data + 18);
    if (dataLength == 0 || dataLength > 64 * 1024 * 1024 || formatTag != 1 || sampleRate == 0) {
        return nullptr;
    }
    return SoftMix::Convert(reinterpret_cast<const std::uint8_t*>(data + 22), dataLength, channels, bitsPerSample, static_cast<int>(sampleRate), loop);
}

double DecibelGain(int decibels)
{
    return std::min(1.0, std::max(0.0, std::pow(10.0, decibels / 20.0)));
}
}

class MR_ShortSound
{
public:
    explicit MR_ShortSound(std::shared_ptr<SoftMix::Sound> samples, int copies)
        : samples(std::move(samples)), copies(std::max(1, copies)) {}

    std::shared_ptr<SoftMix::Sound> samples;
    int copies;
};

class MR_ContinuousSound
{
public:
    MR_ContinuousSound(std::shared_ptr<SoftMix::Sound> samples, int copies)
        : samples(std::move(samples)), copies(std::max(1, copies)), requests(static_cast<size_t>(this->copies)) {}

    std::shared_ptr<SoftMix::Sound> samples;
    int copies;

    struct Request
    {
        BOOL on = FALSE;
        int maxDecibels = -10000;
        double maxSpeed = 0.0;
    };
    std::vector<Request> requests;
};

namespace
{
std::vector<MR_ContinuousSound*>*& ContinuousSoundRegistry()
{
    static std::vector<MR_ContinuousSound*>* sounds = nullptr;
    return sounds;
}

std::vector<MR_ContinuousSound*>& ContinuousSounds()
{
    std::vector<MR_ContinuousSound*>*& sounds = ContinuousSoundRegistry();
    if (sounds == nullptr) {
        sounds = new std::vector<MR_ContinuousSound*>;
    }
    return *sounds;
}

// ---- music: Ogg Vorbis streamed through the audio callback ---------------------------------------------------------------------------------------------
struct MusicVoice
{
    std::shared_ptr<std::vector<unsigned char>> data;   // the file, kept for as long as the decoder reads it
    stb_vorbis* vorbis = nullptr;
    float gain = 0.0f;           // 0..1 now
    float target = 0.0f;         // 0..1 wanted
    float perFrame = 0.0f;       // how much the gain moves each frame toward the target
    bool active = false;
    void Close() { if (vorbis != nullptr) { stb_vorbis_close(vorbis); vorbis = nullptr; } data.reset(); active = false; gain = target = 0.0f; }
};
MusicVoice gMusic[2];          // [0] the track that is wanted; [1] the one fading out
double gMusicVolume = 0.7;     // 0..1
constexpr float kMusicHeadroom = 0.60f;   // music sits under the sounds

void MixMusic(Sint16* out, size_t frames)
{
    static std::vector<short> decoded;
    for (MusicVoice& voice : gMusic) {
        if (!voice.active || voice.vorbis == nullptr) continue;
        decoded.resize(frames * 2);
        size_t done = 0;
        int restarts = 0;
        while (done < frames) {
            const int got = stb_vorbis_get_samples_short_interleaved(voice.vorbis, 2, decoded.data() + done * 2, static_cast<int>((frames - done) * 2));
            if (got <= 0) {
                if (++restarts > 2 || !stb_vorbis_seek_start(voice.vorbis)) break;   // the end of the track: round again
                continue;
            }
            done += static_cast<size_t>(got);
        }
        const float master = static_cast<float>(gMasterVolume) * kMusicHeadroom * static_cast<float>(gMusicVolume);
        for (size_t i = 0; i < frames; ++i) {
            if (voice.gain != voice.target) {
                voice.gain = voice.gain < voice.target ? std::min(voice.target, voice.gain + voice.perFrame) : std::max(voice.target, voice.gain - voice.perFrame);
            }
            if (i < done) {
                for (int c = 0; c < 2; ++c) {
                    const int mixed = out[i * 2 + static_cast<size_t>(c)] + static_cast<int>(decoded[i * 2 + static_cast<size_t>(c)] * voice.gain * master);
                    out[i * 2 + static_cast<size_t>(c)] = static_cast<Sint16>(std::max(-32768, std::min(32767, mixed)));
                }
            }
        }
        if (voice.target == 0.0f && voice.gain == 0.0f) voice.Close();
    }
}

void SDLCALL MixAudio(void*, Uint8* stream, int length)
{
    const size_t frameCount = static_cast<size_t>(length) / (2 * sizeof(Sint16));
    gMixer.Mix(reinterpret_cast<Sint16*>(stream), frameCount, gMasterVolume);
    MixMusic(reinterpret_cast<Sint16*>(stream), frameCount);
}
}

namespace MR_SoundServer
{
    BOOL Init(HWND)
    {
        if (audioDevice != 0) {
            return TRUE;
        }
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            return FALSE;
        }

        SDL_AudioSpec desired = {};
        desired.freq = 44100;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
#ifdef __EMSCRIPTEN__
        desired.samples = 2048;   // in a browser the audio callback shares the page's thread: a bigger buffer rides out a slow frame without a gap
#else
        desired.samples = 1024;
#endif
        desired.callback = MixAudio;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (audioDevice == 0) {
            return FALSE;
        }
        SDL_PauseAudioDevice(audioDevice, 0);
#ifdef __EMSCRIPTEN__
        // A page that is not in the foreground has its timers throttled by the browser, and here the audio callback runs on the page's thread, so the music starved
        // and stuttered. Audio is paused while the page is hidden and carries on from the same place when it is shown again.
        emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, [](int, const EmscriptenVisibilityChangeEvent* event, void*) -> EM_BOOL {
            if (audioDevice != 0 && event != nullptr) SDL_PauseAudioDevice(audioDevice, event->hidden ? 1 : 0);
            return EM_TRUE;
        });
#endif
        return TRUE;
    }

    void Close()
    {
        if (audioDevice != 0) {
            SDL_CloseAudioDevice(audioDevice);
            audioDevice = 0;
        }
        gMixer.Clear();
        delete ContinuousSoundRegistry();
        ContinuousSoundRegistry() = nullptr;
    }

    MR_ShortSound* CreateShortSound(const char* data, int copies)
    {
        std::shared_ptr<SoftMix::Sound> samples = DecodeSound(data, false);
        return samples ? new MR_ShortSound(std::move(samples), copies) : nullptr;
    }

    void DeleteShortSound(MR_ShortSound* sound) { delete sound; }

    void Play(MR_ShortSound* sound, int decibels, double, int pan)
    {
        if (audioDevice == 0 || sound == nullptr || !sound->samples) {
            return;
        }
        const double gain = DecibelGain(decibels);
        const double panFraction = std::min(1.0, std::max(-1.0, pan / 100.0));
        SDL_LockAudioDevice(audioDevice);
        gMixer.Once(sound->samples, gain * (panFraction > 0.0 ? 1.0 - panFraction : 1.0), gain * (panFraction < 0.0 ? 1.0 + panFraction : 1.0));
        SDL_UnlockAudioDevice(audioDevice);
    }

    int GetNbCopy(MR_ShortSound* sound) { return sound ? sound->copies : 1; }

    MR_ContinuousSound* CreateContinuousSound(const char* data, int copies)
    {
        std::shared_ptr<SoftMix::Sound> samples = DecodeSound(data, true);
        if (!samples) {
            return nullptr;
        }
        MR_ContinuousSound* sound = new MR_ContinuousSound(std::move(samples), copies);
        ContinuousSounds().push_back(sound);
        return sound;
    }

    void DeleteContinuousSound(MR_ContinuousSound* sound)
    {
        if (audioDevice != 0) {
            SDL_LockAudioDevice(audioDevice);
            gMixer.Forget(sound);
            SDL_UnlockAudioDevice(audioDevice);
        }
        std::vector<MR_ContinuousSound*>* sounds = ContinuousSoundRegistry();
        if (sounds != nullptr) {
            sounds->erase(std::remove(sounds->begin(), sounds->end(), sound), sounds->end());
        }
        delete sound;
    }

    void Play(MR_ContinuousSound* sound, int copy, int decibels, double speed, int)
    {
        if (sound == nullptr) {
            return;
        }
        copy = std::max(0, std::min(copy, sound->copies - 1));
        MR_ContinuousSound::Request& request = sound->requests[static_cast<size_t>(copy)];
        request.on = TRUE;
        request.maxDecibels = std::max(request.maxDecibels, decibels);
        request.maxSpeed = std::max(request.maxSpeed, speed);
    }

    int GetNbCopy(MR_ContinuousSound* sound) { return sound ? sound->copies : 1; }

    // Once a frame, after the game has asked for the sounds of that frame: what was asked for plays (gliding to its volume and pitch), what was not
    // fades out. A frame that never comes (a menu, the end of a race) lets everything fade out by itself (SoftMixer.h: EndUpdate).
    void ApplyContinuousPlay()
    {
        if (audioDevice == 0) {
            return;
        }
        SDL_LockAudioDevice(audioDevice);
        for (MR_ContinuousSound* sound : ContinuousSounds()) {
            for (int copy = 0; copy < sound->copies; ++copy) {
                MR_ContinuousSound::Request& request = sound->requests[static_cast<size_t>(copy)];
                if (request.on) gMixer.Continuous(sound, copy, sound->samples, DecibelGain(request.maxDecibels), request.maxSpeed);
                else gMixer.ContinuousOff(sound, copy);
                request = MR_ContinuousSound::Request();
            }
        }
        gMixer.EndUpdate();
        SDL_UnlockAudioDevice(audioDevice);
    }

    void MusicPlay( const unsigned char* pOgg, unsigned long pSize, double pVolume, double pFadeSeconds )
    {
        if (audioDevice == 0 || pOgg == nullptr || pSize < 64) return;
        std::shared_ptr<std::vector<unsigned char>> copy(new std::vector<unsigned char>(pOgg, pOgg + pSize));
        int error = 0;
        stb_vorbis* vorbis = stb_vorbis_open_memory(copy->data(), static_cast<int>(copy->size()), &error, nullptr);
        if (vorbis == nullptr) return;
        const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        if (info.sample_rate != 44100 || info.channels < 1 || info.channels > 2) { stb_vorbis_close(vorbis); return; }
        SDL_LockAudioDevice(audioDevice);
        // the track that was wanted fades out (a second one still fading is cut)
        gMusic[1].Close();
        if (gMusic[0].active) {
            gMusic[1] = gMusic[0];
            gMusic[1].target = 0.0f;
            gMusic[1].perFrame = pFadeSeconds > 0.01 ? 1.0f / static_cast<float>(pFadeSeconds * 44100.0) : 1.0f;
            gMusic[0] = MusicVoice();
        }
        gMusic[0].data = copy;
        gMusic[0].vorbis = vorbis;
        gMusic[0].active = true;
        gMusic[0].gain = pFadeSeconds > 0.01 ? 0.0f : 1.0f;
        gMusic[0].target = 1.0f;
        gMusic[0].perFrame = pFadeSeconds > 0.01 ? 1.0f / static_cast<float>(pFadeSeconds * 44100.0) : 1.0f;
        gMusicVolume = std::min(1.0, std::max(0.0, pVolume));
        SDL_UnlockAudioDevice(audioDevice);
    }

    void MusicStop( double pFadeSeconds )
    {
        if (audioDevice == 0) return;
        SDL_LockAudioDevice(audioDevice);
        for (MusicVoice& voice : gMusic) {
            if (!voice.active) continue;
            voice.target = 0.0f;
            voice.perFrame = pFadeSeconds > 0.01 ? 1.0f / static_cast<float>(pFadeSeconds * 44100.0) : 1.0f;
        }
        SDL_UnlockAudioDevice(audioDevice);
    }

    void MusicSetVolume( double pVolume )
    {
        if (audioDevice != 0) SDL_LockAudioDevice(audioDevice);
        gMusicVolume = std::min(1.0, std::max(0.0, pVolume));
        if (audioDevice != 0) SDL_UnlockAudioDevice(audioDevice);
    }

    bool MusicPlaying()
    {
        return gMusic[0].active && gMusic[0].target > 0.0f;
    }

    void SetMasterVolume( double pVolume )
    {
        pVolume = std::min(1.0, std::max(0.0, pVolume));
        if (audioDevice != 0) {
            SDL_LockAudioDevice(audioDevice);
            gMasterVolume = pVolume;
            SDL_UnlockAudioDevice(audioDevice);
        } else {
            // No device open yet (Init() hasn't run, or it failed) -- still
            // record the value so it takes effect once Init() does succeed,
            // matching a caller that sets this once at startup before Init().
            gMasterVolume = pVolume;
        }
    }

    double GetMasterVolume()
    {
        return gMasterVolume;
    }
}