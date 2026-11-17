// The real track, in 3D, for a menu to show behind its panels: the race view of a craft on the track, drawn once by the game's own renderer (MR_Observer, with the camera exactly as the
// race has it: this file never moves, sets or reads the camera) into a picture. One at the start line and one a few seconds into a scripted drive, so a screen can fade between them.
// Presentation only: it builds its own throwaway session and never touches a running race.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HoverNetBackdrop
{
struct Picture { int width = 0, height = 0; std::vector<std::uint8_t> rgba; };

// Draws `frames` pictures of the track at `path` (a .trk), the first at the start and the others every `ticksBetween` simulation ticks of scripted driving. Returns the number drawn (0 = failed).
int Render(const std::string& path, const std::string& name, int width, int height, int frames, int ticksBetween, std::vector<Picture>& out);

// An orbiting live view over the whole track (MR_Observer::RenderFlyover): a camera that circles the track, tilted down on it, redrawn as often as the caller asks. It has its own throwaway session.
// This is a menu camera only; the race camera is a different piece of code and is not touched.
class Flyover
{
public:
    Flyover();
    ~Flyover();
    Flyover(const Flyover&) = delete;
    Flyover& operator=(const Flyover&) = delete;
    bool Open(const std::string& path, const std::string& name, int width, int height);   // false: the track would not load
    bool Ready() const;
    // Draws the view with the eye `degrees` round the orbit into `rgba` (width * height * 4). Returns false when not open.
    bool Draw(float degrees, std::vector<std::uint8_t>& rgba);
    int Width() const;
    int Height() const;
private:
    struct Impl;
    Impl* mImpl;
};

// The orbit, drawn once and played back: a thread opens the track and draws a ring of pictures all the way round it, at a gentle pace (it sleeps at least as long as each picture took), and
// then the menu only fades between them, which costs nothing. Until the ring is complete the newest picture is shown. A slow machine pays a few seconds of background work once per track and never
// slows the menu.
class FlyoverWorker
{
public:
    static constexpr int kRing = 36;       // pictures round the track, one every ten degrees
    struct Frame { int index = 0, width = 0, height = 0; std::vector<std::uint8_t> rgba; };
    FlyoverWorker();
    ~FlyoverWorker();
    FlyoverWorker(const FlyoverWorker&) = delete;
    FlyoverWorker& operator=(const FlyoverWorker&) = delete;
    void SetTrack(const std::string& path, const std::string& name);   // a different track: the ring is drawn again (pictures of the old one stop arriving)
    void SetStill(bool still);                                          // draw only the first picture (Reduced motion)
    int Generation() const;                                             // counts up each time SetTrack starts a new ring, so the caller can drop the pictures it holds
    // Moves the pictures finished since the last call into `out` (in order). Returns how many.
    int TakeFrames(std::vector<Frame>& out);
    bool Failed() const;                                                // the track could not be opened
private:
    struct Impl;
    Impl* mImpl;
};
}
