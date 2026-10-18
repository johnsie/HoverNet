#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../VideoServices/stdafx.h"
#endif
#include "../VideoServices/SoundServer.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "SoftMixer.h"

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

void SDLCALL MixAudio(void*, Uint8* stream, int length)
{
    const size_t frameCount = static_cast<size_t>(length) / (2 * sizeof(Sint16));
    gMixer.Mix(reinterpret_cast<Sint16*>(stream), frameCount, gMasterVolume);
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