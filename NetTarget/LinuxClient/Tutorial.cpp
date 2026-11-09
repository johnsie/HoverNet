#include "Tutorial.h"

#include <algorithm>
#include <fstream>

namespace HoverNetTutorial
{
namespace
{
constexpr long kAccelerateNeed = 2000, kSteerNeed = 600, kBrakeNeed = 700, kReadMs = 9000, kWelcomeMs = 6000;
constexpr double kMoving = 0.12, kFast = 0.40;
}

void Tutorial::Start()
{
    *this = Tutorial();
    mActive = true;
}

const char* Tutorial::StepName(Step step)
{
    switch (step) {
    case Step::eWelcome: return "Welcome";
    case Step::eAccelerate: return "Accelerate";
    case Step::eSteer: return "Steer";
    case Step::eBrake: return "Brake";
    case Step::eJump: return "Jump";
    case Step::eFire: return "Fire";
    case Step::eHud: return "Read the HUD";
    case Step::eLap: return "Finish a lap";
    case Step::eDone: return "Done";
    }
    return "";
}

void Tutorial::Advance(long nowMs)
{
    mLastCompleted = nowMs;
    mStep = static_cast<Step>(std::min(static_cast<int>(mStep) + 1, static_cast<int>(Step::eDone)));
    mStepStart = nowMs;
    mReadMs = 0;
    mWasFast = false;
}

void Tutorial::SkipLesson()
{
    if (mActive && mStep != Step::eDone) Advance(mLast < 0 ? 0 : mLast);
}

void Tutorial::Update(const Frame& f)
{
    if (!mActive) return;
    const long dt = mLast < 0 ? 0 : std::max(0L, std::min(100L, f.timeMs - mLast));
    if (mLast < 0) { mStepStart = f.timeMs; mStartLap = f.lap; }
    mLast = f.timeMs;
    const bool moving = f.speed >= kMoving;
    const int pressed = f.controls & ~mPreviousControls;   // bits that went down this frame
    mPreviousControls = f.controls;
    if (mStep == Step::eBrake && f.speed >= kFast) mWasFast = true;   // up to speed during this lesson, so there is something to brake from

    switch (mStep) {
    case Step::eWelcome:
        mReadMs += dt;
        if (mReadMs >= kWelcomeMs || (f.controls & kMotor)) Advance(f.timeMs);
        break;
    case Step::eAccelerate:
        if ((f.controls & kMotor) && moving) mMotorMs += dt;
        if (mMotorMs >= kAccelerateNeed) Advance(f.timeMs);
        break;
    case Step::eSteer:
        if (moving && (f.controls & kLeft)) mLeftMs += dt;
        if (moving && (f.controls & kRight)) mRightMs += dt;
        if (mLeftMs >= kSteerNeed && mRightMs >= kSteerNeed) Advance(f.timeMs);
        break;
    case Step::eBrake:
        if (mWasFast && (f.controls & kBrake)) mBrakeMs += dt;
        if (mBrakeMs >= kBrakeNeed) Advance(f.timeMs);
        break;
    case Step::eJump:
        if ((pressed & kJump) && moving) mJumped = true;
        if (mJumped) Advance(f.timeMs);
        break;
    case Step::eFire:
        if (pressed & kFire) mFired = true;
        if (mFired) Advance(f.timeMs);
        break;
    case Step::eHud:
        mReadMs += dt;
        if (mReadMs >= kReadMs || (f.confirm && mReadMs > 800)) Advance(f.timeMs);
        break;
    case Step::eLap:
        if (f.finished || f.lap > mStartLap || f.lap >= f.totalLaps) Advance(f.timeMs);
        break;
    case Step::eDone:
        break;
    }
}

float Tutorial::Progress() const
{
    switch (mStep) {
    case Step::eWelcome: return std::min(1.0f, mReadMs / static_cast<float>(kWelcomeMs));
    case Step::eAccelerate: return std::min(1.0f, mMotorMs / static_cast<float>(kAccelerateNeed));
    case Step::eSteer: return (std::min(mLeftMs, kSteerNeed) + std::min(mRightMs, kSteerNeed)) / static_cast<float>(2 * kSteerNeed);
    case Step::eBrake: return std::min(1.0f, mBrakeMs / static_cast<float>(kBrakeNeed));
    case Step::eHud: return std::min(1.0f, mReadMs / static_cast<float>(kReadMs));
    default: return -1.0f;
    }
}

std::string Tutorial::Title(const Labels&) const
{
    if (mStep == Step::eDone) return "TUTORIAL COMPLETE";
    return "LESSON " + std::to_string(Number()) + " OF " + std::to_string(kLessons) + ": " + StepName(mStep);
}

std::string Tutorial::Text(const Labels& l) const
{
    switch (mStep) {
    case Step::eWelcome:
        return "Welcome to HoverNet. This short practice race teaches the basics: each lesson waits until you have done it. Press " + l.accelerate + " to begin.";
    case Step::eAccelerate:
        return "Hold " + l.accelerate + " to accelerate. Keep it down until the bar is full. Your craft glides on a cushion of air, so it keeps its speed when you let go.";
    case Step::eSteer:
        return "Steer with " + l.left + " and " + l.right + ". Turn both ways while you are moving. Gentle turns keep your speed up; hard turns scrub it off.";
    case Step::eBrake:
        return mWasFast ? "Now hold " + l.brake + " to brake until the bar is full. Brake before bends and walls, not in them."
                        : "Get up to speed first with " + l.accelerate + ", then you will brake.";
    case Step::eJump:
        return "Press " + l.jump + " to jump. Jump over gaps and obstacles on a track, and watch the landing.";
    case Step::eFire:
        return "Press " + l.fire + " to fire a missile at a rival. The HUD tells you when your missile is ready. Aim by pointing your craft at them.";
    case Step::eHud:
        return "The bars at the top are your speed and your fuel: running dry leaves you stranded, so grab the fuel pads. Under them: your position, weapon and race time. The lap time is at the bottom.";
    case Step::eLap:
        return "Drive a full lap and cross the finish line. Follow the arrows on the walls. Pick up fuel and power-ups as you go. When you finish, press " + l.results + " for your results.";
    case Step::eDone:
        return "Well done: you know the controls! Press " + l.results + " to see your results. You can run this tutorial again from Settings.";
    }
    return "";
}

bool HasCompleted(const std::string& configDir)
{
    std::ifstream in(configDir + "/tutorial_complete");
    int version = 0;
    return (in >> version) && version >= 1;
}

void MarkCompleted(const std::string& configDir)
{
    std::ofstream out(configDir + "/tutorial_complete");
    out << 1 << '\n';
}
}
