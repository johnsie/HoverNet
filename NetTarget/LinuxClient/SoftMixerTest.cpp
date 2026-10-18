// Tests of the software mixer (SoftMixer.h): the things that made the motor sound crackle must not come back.
#include "SoftMixer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int gFailures = 0;
static void Check(bool ok, const char* what) { if (!ok) { ++gFailures; std::printf("FAIL: %s\n", what); } }

// The largest jump between two successive samples of one channel: a click is a jump far bigger than the signal's own slope.
static int MaxStep(const std::vector<std::int16_t>& stereo, std::size_t from = 0)
{
    int worst = 0;
    for (std::size_t i = from + 1; i < stereo.size() / 2; ++i) worst = std::max(worst, std::abs(stereo[i * 2] - stereo[(i - 1) * 2]));
    return worst;
}

static std::vector<std::int16_t> Render(SoftMix::Mixer& mixer, double seconds, double master = 1.0, bool update = true)
{
    std::vector<std::int16_t> all;
    const std::size_t block = 1024;   // as the audio device asks for it
    const std::size_t blocks = static_cast<std::size_t>(seconds * SoftMix::kRate / block);
    std::vector<std::int16_t> out(block * 2);
    for (std::size_t b = 0; b < blocks; ++b) {
        if (update) mixer.EndUpdate();
        mixer.Mix(out.data(), block, master);
        all.insert(all.end(), out.begin(), out.end());
    }
    return all;
}

// An 8-bit 11025 Hz mono loop of exactly 40 cycles of a sine: it repeats seamlessly, as a recorded engine loop does.
static std::vector<std::uint8_t> EightBitLoop(int frames, int cycles, double amplitude)
{
    std::vector<std::uint8_t> pcm(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) pcm[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(128 + std::lround(amplitude * 127.0 * std::sin(2.0 * 3.14159265358979 * cycles * i / frames)));
    return pcm;
}

int main()
{
    const std::vector<std::uint8_t> pcm = EightBitLoop(5512, 40, 0.8);   // half a second at 11025 Hz, 40 cycles = 80 Hz
    std::shared_ptr<SoftMix::Sound> sound = SoftMix::Convert(pcm.data(), pcm.size(), 1, 8, 11025, true);   // a loop
    std::shared_ptr<SoftMix::Sound> shotSound = SoftMix::Convert(pcm.data(), pcm.size(), 1, 8, 11025, false);
    Check(sound != nullptr, "an 8-bit mono sound converts");
    if (!sound) return 1;
    Check(sound->FrameCount() == 22048, "converted to 44.1 kHz it is exactly four times as long, with no padding at the end");
    const float tail = std::fabs(sound->frames[(sound->FrameCount() - 2) * 2]);
    Check(tail > 0.02f || std::fabs(sound->frames[(sound->FrameCount() - 40) * 2]) > 0.02f, "its last frames are signal, not silence (SDL's flush padded them with zeros)");
    Check(SoftMix::Convert(nullptr, 0, 1, 8, 11025) == nullptr && SoftMix::Convert(pcm.data(), pcm.size(), 3, 8, 11025) == nullptr && SoftMix::Convert(pcm.data(), pcm.size(), 1, 12, 11025) == nullptr && SoftMix::Convert(pcm.data(), 1, 1, 8, 11025) == nullptr, "unsupported or empty sounds are refused");

    // 1. a looped sound is seamless: over many passes of the loop the largest step is the sine's own slope (about 2*pi*80/44100*peak), not a click.
    SoftMix::Mixer mixer;
    mixer.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    std::vector<std::int16_t> out = Render(mixer, 4.0);
    const double peak = 0.8 * 32767 * 0.75;   // the limiter is linear below 0.75 of full scale
    const int slope = static_cast<int>(2.0 * 3.14159265 * 80.0 / 44100.0 * 0.8 * 32767.0) + 40;
    Check(MaxStep(out, 44100) <= slope, "a looping sound has no click at the loop point (over 3 s of passes)");
    (void)peak;

    // 2. it fades in: no jump at the start.
    SoftMix::Mixer fresh;
    fresh.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    std::vector<std::int16_t> start = Render(fresh, 0.3);
    Check(std::abs(start[0]) < 200 && std::abs(start[2]) < 400, "a voice starts silent and fades in (no click at the start)");
    Check(MaxStep(start) <= slope, "and the fade-in has no steps either");

    // 3. a change of volume glides: halving the gain at once is not a jump.
    SoftMix::Mixer glide;
    glide.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    Render(glide, 0.5);
    glide.Continuous(sound.get(), 0, sound, 0.2, 1.0);
    std::vector<std::int16_t> after = Render(glide, 0.5);
    Check(MaxStep(after) <= slope, "a sudden change of volume does not click (it glides over about 10 ms)");
    int level = 0;
    for (std::size_t i = after.size() / 2 - 4000; i < after.size() / 2; ++i) level = std::max(level, std::abs(after[i * 2]));
    Check(level > 0.1 * 0.8 * 32767 * 0.9 && level < 0.3 * 0.8 * 32767, "and settles at the new volume");

    // 4. a change of pitch glides too (and is smooth: no gritty stepping).
    SoftMix::Mixer pitch;
    pitch.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    Render(pitch, 0.3);
    pitch.Continuous(sound.get(), 0, sound, 1.0, 1.5);
    std::vector<std::int16_t> raised = Render(pitch, 0.6);
    Check(MaxStep(raised) <= static_cast<int>(slope * 1.6), "a change of pitch has no clicks");

    // 5. stopping fades out and the voice is removed.
    SoftMix::Mixer stop;
    stop.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    Render(stop, 0.4);
    stop.ContinuousOff(sound.get(), 0);
    std::vector<std::int16_t> faded = Render(stop, 0.5);
    Check(MaxStep(faded) <= slope, "a stopped voice fades out without a click");
    Check(stop.ContinuousVoices() == 0, "and is removed when it is silent");
    int lastLevel = 0;
    for (std::size_t i = faded.size() / 2 - 1000; i < faded.size() / 2; ++i) lastLevel = std::max(lastLevel, std::abs(faded[i * 2]));
    Check(lastLevel == 0, "and the output is silent afterwards");

    // 6. a mixer nobody updates (a menu, the end of a replay) fades its engines out by itself; one that is updated keeps them.
    SoftMix::Mixer idle;
    idle.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    Render(idle, 0.5);
    std::vector<std::int16_t> still = Render(idle, 1.0, 1.0, false);   // no EndUpdate: the game is not asking any more
    int tailLevel = 0;
    for (std::size_t i = still.size() / 2 - 1000; i < still.size() / 2; ++i) tailLevel = std::max(tailLevel, std::abs(still[i * 2]));
    Check(tailLevel == 0 && idle.ContinuousVoices() == 0, "an engine does not keep running when the game stops updating the sounds (leaving a replay or a race)");
    Check(MaxStep(still) <= slope, "and it fades rather than being cut");
    SoftMix::Mixer kept;
    kept.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    std::vector<std::int16_t> running = Render(kept, 1.0);
    int runLevel = 0;
    for (std::size_t i = running.size() / 2 - 1000; i < running.size() / 2; ++i) runLevel = std::max(runLevel, std::abs(running[i * 2]));
    Check(runLevel > 10000 && kept.ContinuousVoices() == 1, "an engine that is still being asked for keeps playing");

    // 7. many loud voices do not clip into distortion: the limiter keeps every sample inside the range, smoothly.
    SoftMix::Mixer loud;
    std::vector<std::shared_ptr<SoftMix::Sound>> sounds;
    for (int i = 0; i < 8; ++i) { sounds.push_back(SoftMix::Convert(pcm.data(), pcm.size(), 1, 8, 11025, true)); loud.Continuous(sounds.back().get(), 0, sounds.back(), 1.0, 1.0); }
    std::vector<std::int16_t> crowded = Render(loud, 1.0);
    int top = 0;
    for (std::int16_t v : crowded) top = std::max(top, std::abs(static_cast<int>(v)));
    Check(top <= 32767 && top > 20000, "eight engines at full volume stay inside the range (soft limited, not hard clipped)");
    Check(MaxStep(crowded, 22050) <= 8 * slope, "and the limited sum steps no more than the sum of the signals' own slopes");
    SoftMix::Mixer busy;   // a realistic crowd: three engines at 60%, together about 1.4 times full scale
    std::vector<std::shared_ptr<SoftMix::Sound>> three;
    for (int i = 0; i < 3; ++i) { three.push_back(SoftMix::Convert(pcm.data(), pcm.size(), 1, 8, 11025, true)); busy.Continuous(three.back().get(), 0, three.back(), 0.6, 1.0); }
    std::vector<std::int16_t> crowd = Render(busy, 1.0);
    std::size_t pinned = 0;
    for (std::int16_t v : crowd) if (std::abs(static_cast<int>(v)) >= 32700) ++pinned;
    Check(pinned == 0, "a sum that is over full scale is rounded off, not flat-topped (a hard clip would pin the peaks at the limit)");

    // 8. one-shots play once, with a short fade at each end, and are removed.
    SoftMix::Mixer once;
    once.Once(shotSound, 1.0, 0.5);
    std::vector<std::int16_t> shot = Render(once, 0.6);
    Check(once.OneShotVoices() == 0, "a one-shot sound is removed when it has played");
    Check(std::abs(shot[0]) < 300 && MaxStep(shot) <= 2 * slope, "a one-shot starts and ends without a click (a 3 ms fade at each end)");
    int left = 0, right = 0;
    for (std::size_t i = 0; i < 20000; ++i) { left = std::max(left, std::abs(shot[i * 2])); right = std::max(right, std::abs(shot[i * 2 + 1])); }
    Check(right > 0 && left > right * 18 / 10 && left < right * 22 / 10, "the pan gives each side its own volume");

    // 9. the master volume.
    SoftMix::Mixer quiet;
    quiet.Continuous(sound.get(), 0, sound, 1.0, 1.0);
    std::vector<std::int16_t> half = Render(quiet, 1.0, 0.5);
    int halfLevel = 0;
    for (std::size_t i = half.size() / 2 - 2000; i < half.size() / 2; ++i) halfLevel = std::max(halfLevel, std::abs(half[i * 2]));
    Check(halfLevel > 0.4 * 0.8 * 32767 * 0.9 && halfLevel < 0.5 * 0.8 * 32767 * 1.05, "the master volume scales the mix");

    if (gFailures == 0) std::printf("soft mixer tests passed\n");
    return gFailures == 0 ? 0 : 1;
}
