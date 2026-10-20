#pragma once

// ---------------------------------------------------------------------------
// AlignmentLogic.hpp
//
// Pure, engine-independent decision logic for the Input Quantization Assist.
// It has NO dependency on Geometry Dash or Geode so it can be unit-tested with
// any plain C++ compiler (see tests/alignment_test.cpp).
//
// Vocabulary
//   step           one physics tick of the game's internal step clock
//   target         an interactive object (orb / ring) the click is meant for
//   window         the span of steps during which the player overlaps a target
//   margin         the allowed error in steps (1..16, "extra frames")
// ---------------------------------------------------------------------------

namespace assist {

enum class Action {
    // Leave the input completely untouched (standard engine behaviour).
    PassThrough,
    // Click arrived slightly BEFORE the target window opens: hold it for
    // `steps` physics ticks and fire it as the window opens.
    HoldForTarget,
    // Click arrived slightly AFTER the target window closed: process it while
    // the target that was just left is still considered touchable.
    ReplayIntoWindow,
};

struct Decision {
    Action action = Action::PassThrough;
    int steps = 0; // hold length for HoldForTarget, lateness for ReplayIntoWindow
};

struct Inputs {
    // The player currently overlaps an interactive target (click is already
    // "on time", nothing to fix).
    bool windowActive = false;

    // Predicted number of steps until the player starts overlapping a target.
    // Values < 1 mean "no target is approaching".
    int stepsUntilTarget = -1;

    // Steps elapsed since the player stopped overlapping a target.
    // 0 = the overlap ended on the step we are processing right now.
    // Values < 0 mean "never overlapped one / unknown".
    int stepsSinceWindowClosed = -1;

    // Allowed error in physics steps.
    int margin = 1;

    bool allowEarly = true;
    bool allowLate = true;
};

constexpr int kMinMargin = 1;
constexpr int kMaxMargin = 16;

constexpr int clampMargin(long long value) {
    return value < kMinMargin ? kMinMargin
         : value > kMaxMargin ? kMaxMargin
         : static_cast<int>(value);
}

// Decide what to do with ONE jump-push that is being processed this step.
// The function is deterministic and side-effect free.
constexpr Decision decide(Inputs in) {
    Decision d;
    const int margin = clampMargin(in.margin);

    // Already inside the window: the engine will handle it correctly.
    if (in.windowActive) {
        return d;
    }

    // Early: a target is 1..margin steps away -> hold the click until then.
    if (in.allowEarly && in.stepsUntilTarget >= 1 && in.stepsUntilTarget <= margin) {
        d.action = Action::HoldForTarget;
        d.steps = in.stepsUntilTarget;
        return d;
    }

    // Late: the window closed 0..margin-1 steps ago, i.e. the click is
    // (since + 1) steps late, which must not exceed the margin.
    if (in.allowLate && in.stepsSinceWindowClosed >= 0 &&
        in.stepsSinceWindowClosed + 1 <= margin) {
        d.action = Action::ReplayIntoWindow;
        d.steps = in.stepsSinceWindowClosed + 1;
        return d;
    }

    return d; // PassThrough
}

} // namespace assist
