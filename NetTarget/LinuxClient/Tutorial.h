// Tutorial.h -- the in-game tutorial: a short practice race with a coach. No screens to read: you drive, and each lesson waits until you have done the thing.
//
// The lessons (accelerate, steer, brake, jump, fire, read the HUD, finish a lap) are completed by what the player really does in the race (the controls they
// press and the speed and lap of their craft), never by pressing a "next" button, except for the HUD lesson which is reading. This file is the logic only,
// with no screen or game code in it, so it can be tested; the race loop feeds it every frame and shows its card with MR_Observer::SetTutorialCard.
#ifndef HOVERNET_TUTORIAL_H
#define HOVERNET_TUTORIAL_H

#include <string>

namespace HoverNetTutorial
{
enum class Step { eWelcome, eAccelerate, eSteer, eBrake, eJump, eFire, eHud, eLap, eDone };
constexpr int kLessons = 8;   // Welcome .. Lap; Done is the closing card

// The control bits the race uses (MR_MainCharacter): the tutorial reads them by these numbers, so they are repeated here and checked by the test.
constexpr int kMotor = 1, kRight = 2, kLeft = 4, kBrake = 32, kJump = 64, kFire = 128;

struct Frame
{
    long timeMs = 0;            // any steady clock in milliseconds (race time or real time)
    int controls = 0;           // the control bits held now
    double speed = 0;           // the craft's speed as a fraction of its steady speed (0..1+)
    int lap = 0;                // laps completed
    int totalLaps = 1;
    bool finished = false;      // the craft has crossed the finish
    bool confirm = false;       // Enter or Space is held (only the reading lesson uses it)
};

// What each lesson calls the controls, for the player's own keys or controller or touch screen.
struct Labels
{
    std::string accelerate = "Up", brake = "Down", left = "Left", right = "Right", jump = "Space", fire = "Ctrl", skip = "N", pause = "Esc";
};

class Tutorial
{
public:
    void Start();
    void Stop() { mActive = false; }
    bool Active() const { return mActive; }
    bool Finished() const { return mActive && mStep == Step::eDone; }
    void Update(const Frame& frame);
    void SkipLesson();                       // move on without doing it
    Step Current() const { return mStep; }
    int Number() const { return static_cast<int>(mStep) + 1; }   // 1-based, for "lesson 2 of 8"
    float Progress() const;                  // 0..1 through the current lesson, or -1 when it has no bar
    bool JustCompleted(long nowMs) const { return mActive && mLastCompleted >= 0 && nowMs - mLastCompleted < 1100 && mStep != Step::eDone; }
    std::string Title(const Labels& labels) const;   // "LESSON 3 OF 8: Steer"
    std::string Heading() const { return mStep == Step::eDone ? "Tutorial complete" : StepName(mStep); }
    std::string Text(const Labels& labels) const;
    static const char* StepName(Step step);

private:
    void Advance(long nowMs);
    bool mActive = false;
    Step mStep = Step::eWelcome;
    long mLast = -1, mStepStart = 0, mLastCompleted = -1;
    long mMotorMs = 0, mLeftMs = 0, mRightMs = 0, mBrakeMs = 0, mReadMs = 0;
    bool mWasFast = false;
    int mPreviousControls = 0;
    int mStartLap = 0;
    bool mJumped = false, mFired = false;
};

// The tutorial is "done" once the player has finished it (or skipped to the end); kept in a small file in the settings folder.
bool HasCompleted(const std::string& configDir);
void MarkCompleted(const std::string& configDir);
}

#endif
