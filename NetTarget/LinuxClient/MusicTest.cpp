// MusicTest -- the music files decode, are the right format and are not silent, and the sound server can play, fade and stop them. Run from the repository root.
#include "../VideoServices/SoundServer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include "../ThirdParty/stb/stb_vorbis.c"

namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    const char* files[] = {"NetTarget/Music/HardLeft.ogg", "NetTarget/Music/Asphalt.ogg"};
    const double minSeconds[] = {100.0, 250.0};
    for (int i = 0; i < 2; ++i) {
        std::ifstream in(files[i], std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        char what[160];
        std::snprintf(what, sizeof what, "%s is there (%zu bytes)", files[i], bytes.size());
        Check(bytes.size() > 100000, what);
        int error = 0;
        stb_vorbis* v = stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &error, nullptr);
        std::snprintf(what, sizeof what, "%s opens as Ogg Vorbis", files[i]);
        Check(v != nullptr, what);
        if (v == nullptr) continue;
        const stb_vorbis_info info = stb_vorbis_get_info(v);
        std::snprintf(what, sizeof what, "%s is 44.1 kHz stereo (%u Hz, %d channels)", files[i], info.sample_rate, info.channels);
        Check(info.sample_rate == 44100 && info.channels == 2, what);
        const float seconds = stb_vorbis_stream_length_in_seconds(v);
        std::snprintf(what, sizeof what, "%s lasts %.0f seconds (at least %.0f)", files[i], seconds, minSeconds[i]);
        Check(seconds >= minSeconds[i], what);
        // not silent, not clipping all the time: sample the middle of the track
        stb_vorbis_seek(v, static_cast<unsigned>(seconds * 0.5 * 44100));
        std::vector<short> pcm(44100 * 2 * 3);
        const int got = stb_vorbis_get_samples_short_interleaved(v, 2, pcm.data(), static_cast<int>(pcm.size()));
        double sum = 0; int clipped = 0;
        for (int k = 0; k < got * 2; ++k) { sum += static_cast<double>(pcm[k]) * pcm[k]; if (pcm[k] >= 32767 || pcm[k] <= -32768) ++clipped; }
        const double rms = got > 0 ? std::sqrt(sum / (got * 2)) : 0;
        std::snprintf(what, sizeof what, "%s has sound in the middle (rms %.0f of 32768, %d clipped samples)", files[i], rms, clipped);
        Check(rms > 800 && clipped < got / 50, what);
        stb_vorbis_close(v);
    }
    // The sound server plays and stops it (with a silent audio device).
    if (MR_SoundServer::Init(nullptr)) {
        std::ifstream in(files[1], std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        MR_SoundServer::MusicPlay(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<unsigned long>(bytes.size()), 0.5, 0.2);
        Check(MR_SoundServer::MusicPlaying(), "the sound server plays a track");
        MR_SoundServer::MusicSetVolume(0.3);
        MR_SoundServer::MusicStop(0.1);
        Check(!MR_SoundServer::MusicPlaying(), "and stops it");
        MR_SoundServer::Close();
    } else std::printf("no audio device: playing not checked\n");
    std::printf(gFail == 0 ? "music: all checks passed\n" : "music: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
