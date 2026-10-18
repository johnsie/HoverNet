// SoftMixer.h -- the software mixer behind the SDL sound server (HeadlessSoundServer.cpp), free of SDL so it is tested on its own (SoftMixerTest.cpp).
//
// Why it is written the way it is (the motor sound used to crackle like a vinyl record):
//  - A sound is converted to 44.1 kHz stereo once, by plain linear interpolation, with no padding at the end. SDL's audio stream pads the end with silence
//    when it is flushed, which put a dropout at the loop point of the motor loop every time round.
//  - A looping sound wraps with interpolation between its last and first frame, so the loop is seamless.
//  - Playback interpolates between samples (no nearest-sample stepping), so a changed pitch is not gritty.
//  - Gain and pitch glide to their new value over about 10 ms instead of jumping once a frame, which was a click 60 times a second; a voice fades in when it
//    starts and out when it stops instead of being cut.
//  - Voices are summed in floating point and a gentle limiter keeps the sum from clipping into harsh distortion.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace SoftMix
{
struct Sound
{
    std::vector<float> frames;   // interleaved left, right in -1..1 at kRate
    std::size_t FrameCount() const { return frames.size() / 2; }
};

constexpr int kRate = 44100;

// PCM as stored in the game's resources (8-bit unsigned or 16-bit signed little endian, mono or stereo) to a stereo sound at kRate. Returns null for
// an unsupported layout or an empty buffer.
// `loop`: the sound is played round and round, so its last frames are interpolated toward its first (a loop that is seamless in the source stays seamless).
inline std::shared_ptr<Sound> Convert(const std::uint8_t* pcm, std::size_t bytes, int channels, int bits, int rate, bool loop = false)
{
    if (pcm == nullptr || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate <= 0) return nullptr;
    const std::size_t frameBytes = static_cast<std::size_t>(channels) * (bits / 8);
    const std::size_t inFrames = bytes / frameBytes;
    if (inFrames < 2) return nullptr;
    auto sample = [&](std::size_t frame, int channel) -> float {
        const std::uint8_t* p = pcm + frame * frameBytes + static_cast<std::size_t>(channel % channels) * (bits / 8);
        if (bits == 8) return (static_cast<int>(p[0]) - 128) / 128.0f;
        return static_cast<std::int16_t>(p[0] | (p[1] << 8)) / 32768.0f;
    };
    const std::size_t outFrames = std::max<std::size_t>(2, static_cast<std::size_t>(std::llround(static_cast<double>(inFrames) * kRate / rate)));
    std::shared_ptr<Sound> out(new Sound);
    out->frames.resize(outFrames * 2);
    for (std::size_t i = 0; i < outFrames; ++i) {
        const double at = static_cast<double>(i) * rate / kRate;
        const std::size_t a = std::min<std::size_t>(static_cast<std::size_t>(at), inFrames - 1);
        const std::size_t b = loop ? (a + 1) % inFrames : std::min<std::size_t>(a + 1, inFrames - 1);
        const float f = static_cast<float>(at - static_cast<double>(a));
        for (int c = 0; c < 2; ++c) out->frames[i * 2 + static_cast<std::size_t>(c)] = sample(a, c) * (1.0f - f) + sample(b, c) * f;
    }
    return out;
}

class Mixer
{
public:
    // A looping voice (an engine, the wind): created silent, it fades in to `gain` and glides to new gains and speeds. `key` is any unique pointer for the
    // sound and `copy` its copy number.
    void Continuous(const void* key, int copy, std::shared_ptr<const Sound> sound, double gain, double speed)
    {
        CVoice* voice = Find(key, copy);
        if (voice == nullptr) {
            CVoice fresh;
            fresh.key = key; fresh.copy = copy; fresh.sound = std::move(sound);
            fresh.speed = speed;
            mC.push_back(fresh);
            voice = &mC.back();
        }
        voice->targetGain = static_cast<float>(gain);
        voice->targetSpeed = static_cast<float>(Clamp(speed, 0.25, 4.0));
    }
    // The voice stops asking to be heard: it fades out and is removed.
    void ContinuousOff(const void* key, int copy) { if (CVoice* voice = Find(key, copy)) voice->targetGain = 0.0f; }
    void Forget(const void* key) { mC.erase(std::remove_if(mC.begin(), mC.end(), [key](const CVoice& v) { return v.key == key; }), mC.end()); }

    // A sound played once (a jump, a shot), with a gain for each side.
    void Once(std::shared_ptr<const Sound> sound, double left, double right)
    {
        if (!sound || sound->FrameCount() == 0) return;
        OVoice voice;
        voice.sound = std::move(sound); voice.left = static_cast<float>(left); voice.right = static_cast<float>(right);
        mO.push_back(std::move(voice));
    }

    // Called by the owner once per frame after it has set the continuous voices. A mixer that is not updated for a quarter of a second (a menu is open,
    // the race was left, the window is in the background) fades its continuous voices out by itself, so an engine never keeps running after the race.
    void EndUpdate() { mIdleFrames = 0; }

    void Clear() { mC.clear(); mO.clear(); }
    std::size_t ContinuousVoices() const { return mC.size(); }
    std::size_t OneShotVoices() const { return mO.size(); }

    // Mixes `frames` stereo frames into `out` (interleaved signed 16-bit, replaced, not added to).
    void Mix(std::int16_t* out, std::size_t frames, double master)
    {
        mBuffer.assign(frames * 2, 0.0f);
        mIdleFrames += frames;
        if (mIdleFrames > static_cast<std::size_t>(kRate / 4)) for (CVoice& v : mC) v.targetGain = 0.0f;
        constexpr float kGlide = 0.0023f;   // about a 10 ms time constant at 44.1 kHz
        for (CVoice& v : mC) {
            const std::size_t n = v.sound->FrameCount();
            const float* src = v.sound->frames.data();
            for (std::size_t i = 0; i < frames; ++i) {
                v.gain += (v.targetGain - v.gain) * kGlide;
                v.speed += (v.targetSpeed - v.speed) * kGlide;
                const std::size_t a = static_cast<std::size_t>(v.position) % n;
                const std::size_t b = (a + 1) % n;   // the loop wraps: the last frame blends into the first
                const float f = static_cast<float>(v.position - std::floor(v.position));
                const float g = v.gain * static_cast<float>(master);
                mBuffer[i * 2] += (src[a * 2] * (1.0f - f) + src[b * 2] * f) * g;
                mBuffer[i * 2 + 1] += (src[a * 2 + 1] * (1.0f - f) + src[b * 2 + 1] * f) * g;
                v.position += v.speed;
                if (v.position >= static_cast<double>(n)) v.position = std::fmod(v.position, static_cast<double>(n));
            }
        }
        constexpr std::size_t kEdge = 128;   // a short fade at each end of a one-shot: no click if the recording is cut mid-wave
        for (OVoice& v : mO) {
            const std::size_t n = v.sound->FrameCount();
            const float* src = v.sound->frames.data();
            for (std::size_t i = 0; i < frames && v.position < n; ++i, ++v.position) {
                const std::size_t into = v.position, left = n - 1 - v.position;
                const float edge = std::min(1.0f, std::min<std::size_t>(into, left) / static_cast<float>(kEdge));
                mBuffer[i * 2] += src[v.position * 2] * v.left * static_cast<float>(master) * edge;
                mBuffer[i * 2 + 1] += src[v.position * 2 + 1] * v.right * static_cast<float>(master) * edge;
            }
        }
        for (std::size_t i = 0; i < frames * 2; ++i) out[i] = ToSample(mBuffer[i]);
        mC.erase(std::remove_if(mC.begin(), mC.end(), [](const CVoice& v) { return v.targetGain == 0.0f && v.gain < 0.0004f; }), mC.end());
        mO.erase(std::remove_if(mO.begin(), mO.end(), [](const OVoice& v) { return v.position >= v.sound->FrameCount(); }), mO.end());
    }

    // The gentle limiter: linear up to 0.75 of full scale, then a smooth curve that never passes 1.0.
    static std::int16_t ToSample(float x)
    {
        const float magnitude = std::fabs(x);
        const float shaped = magnitude <= 0.75f ? magnitude : 0.75f + 0.25f * std::tanh((magnitude - 0.75f) / 0.25f);
        const float v = (x < 0 ? -shaped : shaped) * 32767.0f;
        return static_cast<std::int16_t>(std::lround(v));
    }

private:
    struct CVoice
    {
        const void* key = nullptr;
        int copy = 0;
        std::shared_ptr<const Sound> sound;
        double position = 0.0;
        float gain = 0.0f, targetGain = 0.0f;
        float speed = 1.0f, targetSpeed = 1.0f;
    };
    struct OVoice
    {
        std::shared_ptr<const Sound> sound;
        std::size_t position = 0;
        float left = 1.0f, right = 1.0f;
    };
    static double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    CVoice* Find(const void* key, int copy)
    {
        for (CVoice& v : mC) if (v.key == key && v.copy == copy) return &v;
        return nullptr;
    }
    std::vector<CVoice> mC;
    std::vector<OVoice> mO;
    std::vector<float> mBuffer;
    std::size_t mIdleFrames = 0;
};
}
