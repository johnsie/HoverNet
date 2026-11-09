// TutorialTest -- the tutorial's lessons are finished by what the player does, in order, and not by anything else.
#include "Tutorial.h"
#include "../MainCharacter/MainCharacter.h"

#include <cstdio>
#include <cstdlib>

using namespace HoverNetTutorial;
namespace { int gFail = 0; void Check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; } }

int main()
{
    // The control bits must be the race's own.
    Check(kMotor == MR_MainCharacter::eMotorOn && kRight == MR_MainCharacter::eRight && kLeft == MR_MainCharacter::eLeft && kBrake == MR_MainCharacter::eBreakDirection &&
          kJump == MR_MainCharacter::eJump && kFire == MR_MainCharacter::eFire, "the tutorial reads the race's own control bits");

    Tutorial t; Frame f; long now = 0;
    auto run = [&](int controls, double speed, int ms) { for (int i = 0; i < ms / 16; ++i) { f.timeMs = now += 16; f.controls = controls; f.speed = speed; t.Update(f); } };
    Check(!t.Active(), "not active until started");
    t.Start();
    Check(t.Active() && t.Current() == Step::eWelcome, "starts at the welcome");
    run(0, 0, 500);
    Check(t.Current() == Step::eWelcome, "the welcome waits (the player has done nothing yet)");
    run(kMotor, 0.2, 32);
    Check(t.Current() == Step::eAccelerate, "pressing accelerate moves on from the welcome");
    run(0, 0.5, 3000);
    Check(t.Current() == Step::eAccelerate, "doing nothing does not finish the accelerate lesson");
    run(kMotor, 0.05, 3000);
    Check(t.Current() == Step::eAccelerate, "accelerating without actually moving does not finish it");
    run(kMotor, 0.5, 1000);
    Check(t.Current() == Step::eAccelerate && t.Progress() > 0.4f && t.Progress() < 0.6f, "the bar fills as you accelerate (half way after a second)");
    run(kMotor, 0.6, 1200);
    Check(t.Current() == Step::eSteer, "holding accelerate for two seconds finishes it");
    Check(t.JustCompleted(now), "the finished lesson is flashed");
    run(kMotor | kLeft, 0.6, 1000);
    Check(t.Current() == Step::eSteer, "steering one way only does not finish the steering lesson");
    run(kMotor | kRight, 0.6, 608);
    Check(t.Current() == Step::eBrake, "steering both ways does");
    run(kBrake, 0.1, 2000);
    Check(t.Current() == Step::eBrake, "braking when slow does not count");
    run(kMotor, 0.7, 200);
    run(kBrake, 0.6, 1000);
    Check(t.Current() == Step::eJump, "braking after being up to speed does");
    run(kJump, 0.5, 16);
    Check(t.Current() == Step::eFire, "a jump while moving finishes the jump lesson");
    run(kFire | kMotor, 0.5, 16);
    Check(t.Current() == Step::eHud, "firing finishes the fire lesson");
    run(0, 0.5, 2000);
    Check(t.Current() == Step::eHud, "the reading lesson waits");
    f.confirm = true; run(0, 0.5, 100); f.confirm = false;
    Check(t.Current() == Step::eLap, "Enter finishes the reading lesson");
    f.lap = 0; f.totalLaps = 1; run(kMotor, 0.8, 5000);
    Check(t.Current() == Step::eLap, "driving does not finish the lap lesson");
    f.lap = 1; run(kMotor, 0.8, 32);
    Check(t.Current() == Step::eDone && t.Finished(), "completing the lap finishes the tutorial");
    Check(t.Progress() < 0 && !t.Title(Labels()).empty() && !t.Text(Labels()).empty(), "every lesson has a title and text");

    // Skipping, and the held-key case: a key held from before a lesson began is not a new press.
    Tutorial s; s.Start(); now = 0;
    for (int i = 0; i < kLessons; ++i) s.SkipLesson();
    Check(s.Finished(), "skipping every lesson ends the tutorial");
    Tutorial j; j.Start(); Frame g; g.timeMs = 16; g.controls = kJump; g.speed = 0.5; j.Update(g);
    for (int i = 0; i < 4; ++i) j.SkipLesson();   // to Jump
    g.timeMs = 32; j.Update(g);
    Check(j.Current() == Step::eJump, "a jump key already held when the lesson starts does not count");
    g.timeMs = 48; g.controls = 0; j.Update(g); g.timeMs = 64; g.controls = kJump; j.Update(g);
    Check(j.Current() == Step::eFire, "a fresh press does");

    // Text names the player's own keys.
    Labels l; l.accelerate = "W"; l.jump = "J";
    Tutorial w; w.Start();
    Check(w.Text(l).find("W") != std::string::npos, "lessons use the player's own key names");
    // Persistence.
    const std::string dir = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
    std::remove((dir + "/tutorial_complete").c_str());
    Check(!HasCompleted(dir), "not completed at first");
    MarkCompleted(dir);
    Check(HasCompleted(dir), "completion is remembered");
    std::remove((dir + "/tutorial_complete").c_str());
    std::printf(gFail == 0 ? "tutorial: all checks passed\n" : "tutorial: FAILED\n");
    return gFail == 0 ? 0 : 1;
}
