#include "../VideoServices/SoundServer.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
SDL_AudioDeviceID audioDevice = 0;

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

bool DecodeSound(const char* data, std::vector<Uint8>& output)
{
    if (data == nullptr) {
        return false;
    }

    const std::uint32_t dataLength = ReadUint32(data);
    const std::uint16_t formatTag = ReadUint16(data + 4);
    const std::uint16_t channels = ReadUint16(data + 6);
    const std::uint32_t sampleRate = ReadUint32(data + 8);
    const std::uint16_t bitsPerSample = ReadUint16(data + 18);
    if (dataLength == 0 || dataLength > 64 * 1024 * 1024 || formatTag != 1 ||
        (channels != 1 && channels != 2) || (bitsPerSample != 8 && bitsPerSample != 16) ||
        sampleRate == 0) {
        return false;
    }

    const SDL_AudioFormat sourceFormat = bitsPerSample == 8 ? AUDIO_U8 : AUDIO_S16LSB;
    SDL_AudioStream* stream = SDL_NewAudioStream(sourceFormat, channels, sampleRate,
                                                  AUDIO_S16SYS, 2, 44100);
    if (stream == nullptr) {
        return false;
    }

    const Uint8* samples = reinterpret_cast<const Uint8*>(data + 22);
    bool decoded = SDL_AudioStreamPut(stream, samples, static_cast<int>(dataLength)) == 0 &&
                   SDL_AudioStreamFlush(stream) == 0;
    const int available = decoded ? SDL_AudioStreamAvailable(stream) : 0;
    if (available > 0) {
        output.resize(static_cast<size_t>(available));
        decoded = SDL_AudioStreamGet(stream, output.data(), available) == available;
    }
    else {
        decoded = false;
    }
    SDL_FreeAudioStream(stream);
    return decoded;
}
}

class MR_ShortSound
{
public:
    explicit MR_ShortSound(std::vector<Uint8>&& samples, int copies)
        : samples(std::move(samples)), copies(std::max(1, copies)) {}

    std::vector<Uint8> samples;
    int copies;
};

class MR_ContinuousSound
{
public:
    MR_ContinuousSound(std::vector<Uint8>&& samples, int copies)
        : samples(std::move(samples)), copies(std::max(1, copies)), requests(static_cast<size_t>(this->copies)) {}

    std::vector<Uint8> samples;
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
struct OneShotVoice
{
    std::vector<Uint8> samples;
    double position = 0.0;
    double gain = 1.0;
    double leftGain = 1.0;
    double rightGain = 1.0;
};

struct ContinuousVoice
{
    MR_ContinuousSound* sound = nullptr;
    int copy = 0;
    double position = 0.0;
    double gain = 1.0;
    double speed = 1.0;
};

std::vector<OneShotVoice> oneShotVoices;
std::vector<ContinuousVoice> continuousVoices;

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

double DecibelGain(int decibels)
{
    return std::min(1.0, std::max(0.0, std::pow(10.0, decibels / 20.0)));
}

Sint16 Saturate(int value)
{
    return static_cast<Sint16>(std::max<int>(std::numeric_limits<Sint16>::min(),
                                              std::min<int>(std::numeric_limits<Sint16>::max(), value)));
}

void MixVoice(const Uint8* samples, size_t byteCount, double& position, double speed,
              double leftGain, double rightGain, Sint16* output, size_t frameCount, BOOL loop)
{
    const size_t sourceFrames = byteCount / (2 * sizeof(Sint16));
    if (sourceFrames == 0 || speed <= 0.0) {
        return;
    }

    const Sint16* source = reinterpret_cast<const Sint16*>(samples);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        if (position >= sourceFrames) {
            if (!loop) {
                break;
            }
            position = std::fmod(position, static_cast<double>(sourceFrames));
        }
        const size_t sourceFrame = static_cast<size_t>(position);
        output[frame * 2] = Saturate(output[frame * 2] +
                                      static_cast<int>(std::lround(source[sourceFrame * 2] * leftGain)));
        output[frame * 2 + 1] = Saturate(output[frame * 2 + 1] +
                                          static_cast<int>(std::lround(source[sourceFrame * 2 + 1] * rightGain)));
        position += speed;
    }
}

void SDLCALL MixAudio(void*, Uint8* stream, int length)
{
    SDL_memset(stream, 0, static_cast<size_t>(length));
    Sint16* output = reinterpret_cast<Sint16*>(stream);
    const size_t frameCount = static_cast<size_t>(length) / (2 * sizeof(Sint16));

    for (ContinuousVoice& voice : continuousVoices) {
        MixVoice(voice.sound->samples.data(), voice.sound->samples.size(), voice.position, voice.speed,
                 voice.gain, voice.gain, output, frameCount, TRUE);
    }
    for (OneShotVoice& voice : oneShotVoices) {
        MixVoice(voice.samples.data(), voice.samples.size(), voice.position, 1.0,
                 voice.leftGain, voice.rightGain, output, frameCount, FALSE);
    }
    oneShotVoices.erase(std::remove_if(oneShotVoices.begin(), oneShotVoices.end(),
                                       [](const OneShotVoice& voice) {
                                           return voice.position >= voice.samples.size() / (2 * sizeof(Sint16));
                                       }),
                        oneShotVoices.end());
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
        desired.samples = 1024;
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
        oneShotVoices.clear();
        continuousVoices.clear();
        delete ContinuousSoundRegistry();
        ContinuousSoundRegistry() = nullptr;
    }

    MR_ShortSound* CreateShortSound(const char* data, int copies)
    {
        std::vector<Uint8> samples;
        return DecodeSound(data, samples) ? new MR_ShortSound(std::move(samples), copies) : nullptr;
    }

    void DeleteShortSound(MR_ShortSound* sound) { delete sound; }

    void Play(MR_ShortSound* sound, int decibels, double, int pan)
    {
        if (audioDevice == 0 || sound == nullptr || sound->samples.empty()) {
            return;
        }
        const double gain = DecibelGain(decibels);
        const double panFraction = std::min(1.0, std::max(-1.0, pan / 100.0));
        SDL_LockAudioDevice(audioDevice);
        OneShotVoice voice;
        voice.samples = sound->samples;
        voice.gain = gain;
        voice.leftGain = gain * (panFraction > 0.0 ? 1.0 - panFraction : 1.0);
        voice.rightGain = gain * (panFraction < 0.0 ? 1.0 + panFraction : 1.0);
        oneShotVoices.push_back(std::move(voice));
        SDL_UnlockAudioDevice(audioDevice);
    }

    int GetNbCopy(MR_ShortSound* sound) { return sound ? sound->copies : 1; }

    MR_ContinuousSound* CreateContinuousSound(const char* data, int copies)
    {
        std::vector<Uint8> samples;
        if (!DecodeSound(data, samples)) {
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
            continuousVoices.erase(std::remove_if(continuousVoices.begin(), continuousVoices.end(),
                                                   [sound](const ContinuousVoice& voice) {
                                                       return voice.sound == sound;
                                                   }),
                                 continuousVoices.end());
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

    void ApplyContinuousPlay()
    {
        if (audioDevice == 0) {
            return;
        }
        SDL_LockAudioDevice(audioDevice);
        for (ContinuousVoice& voice : continuousVoices) {
            const MR_ContinuousSound::Request& request = voice.sound->requests[static_cast<size_t>(voice.copy)];
            if (!request.on) {
                voice.sound = nullptr;
                continue;
            }
            voice.gain = DecibelGain(request.maxDecibels);
            voice.speed = request.maxSpeed;
        }
        continuousVoices.erase(std::remove_if(continuousVoices.begin(), continuousVoices.end(),
                                              [](const ContinuousVoice& voice) { return voice.sound == nullptr; }),
                               continuousVoices.end());
        for (MR_ContinuousSound* sound : ContinuousSounds()) {
            for (int copy = 0; copy < sound->copies; ++copy) {
                MR_ContinuousSound::Request& request = sound->requests[static_cast<size_t>(copy)];
                if (request.on && std::none_of(continuousVoices.begin(), continuousVoices.end(),
                                                [sound, copy](const ContinuousVoice& voice) {
                                                    return voice.sound == sound && voice.copy == copy;
                                                })) {
                    ContinuousVoice voice;
                    voice.sound = sound;
                    voice.copy = copy;
                    voice.gain = DecibelGain(request.maxDecibels);
                    voice.speed = request.maxSpeed;
                    continuousVoices.push_back(voice);
                }
                request = MR_ContinuousSound::Request();
            }
        }
        SDL_UnlockAudioDevice(audioDevice);
    }
}