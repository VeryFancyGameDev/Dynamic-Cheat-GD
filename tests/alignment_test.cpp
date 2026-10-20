// Standalone unit tests for the pure decision logic (no Geode / GD needed).
//
//   g++ -std=c++20 -I../src alignment_test.cpp -o alignment_test && ./alignment_test

#include <cstdio>
#include <cstdlib>

#include "AlignmentLogic.hpp"

using namespace assist;

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);              \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

int main() {
    // clampMargin
    static_assert(clampMargin(-5) == 1);
    static_assert(clampMargin(0) == 1);
    static_assert(clampMargin(1) == 1);
    static_assert(clampMargin(2) == 2);
    static_assert(clampMargin(16) == 16);
    static_assert(clampMargin(17) == 16);
    static_assert(clampMargin(99) == 16);

    // Inside the window: never touched.
    {
        Inputs in; in.windowActive = true; in.stepsUntilTarget = 1; in.stepsSinceWindowClosed = 0;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Early click, target 1 step away, margin 1 -> hold 1.
    {
        Inputs in; in.stepsUntilTarget = 1; in.margin = 1;
        auto d = decide(in);
        CHECK(d.action == Action::HoldForTarget && d.steps == 1);
    }

    // Early click, target 2 steps away, margin 1 -> too far, pass through.
    {
        Inputs in; in.stepsUntilTarget = 2; in.margin = 1;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Early click, target 2 steps away, margin 2 -> hold 2.
    {
        Inputs in; in.stepsUntilTarget = 2; in.margin = 2;
        auto d = decide(in);
        CHECK(d.action == Action::HoldForTarget && d.steps == 2);
    }

    // Early click, target 3 steps away, margin 2 -> pass through.
    {
        Inputs in; in.stepsUntilTarget = 3; in.margin = 2;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Early assist disabled.
    {
        Inputs in; in.stepsUntilTarget = 1; in.margin = 2; in.allowEarly = false;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Late click, window closed this very step, margin 1 -> 1 step late, replay.
    {
        Inputs in; in.stepsSinceWindowClosed = 0; in.margin = 1;
        auto d = decide(in);
        CHECK(d.action == Action::ReplayIntoWindow && d.steps == 1);
    }

    // Late click, 1 step after close, margin 1 -> 2 steps late, too late.
    {
        Inputs in; in.stepsSinceWindowClosed = 1; in.margin = 1;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Late click, 1 step after close, margin 2 -> 2 steps late, replay.
    {
        Inputs in; in.stepsSinceWindowClosed = 1; in.margin = 2;
        auto d = decide(in);
        CHECK(d.action == Action::ReplayIntoWindow && d.steps == 2);
    }

    // Late click, 2 steps after close, margin 2 -> 3 steps late, too late.
    {
        Inputs in; in.stepsSinceWindowClosed = 2; in.margin = 2;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Never overlapped a target.
    {
        Inputs in; in.stepsSinceWindowClosed = -1; in.margin = 2;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Late assist disabled.
    {
        Inputs in; in.stepsSinceWindowClosed = 0; in.margin = 2; in.allowLate = false;
        CHECK(decide(in).action == Action::PassThrough);
    }

    // Out-of-range margins are clamped (0 -> 1, 50 -> 16).
    {
        Inputs in; in.stepsUntilTarget = 2; in.margin = 0;
        CHECK(decide(in).action == Action::PassThrough);
        in.margin = 50;
        CHECK(decide(in).action == Action::HoldForTarget);
    }

    // Large margin: 16 extra frames.
    {
        Inputs in; in.stepsUntilTarget = 16; in.margin = 16;
        auto d = decide(in);
        CHECK(d.action == Action::HoldForTarget && d.steps == 16);
        in.stepsUntilTarget = 17;
        CHECK(decide(in).action == Action::PassThrough);
        Inputs late; late.stepsSinceWindowClosed = 15; late.margin = 16;
        CHECK(decide(late).action == Action::ReplayIntoWindow && decide(late).steps == 16);
        late.stepsSinceWindowClosed = 16;
        CHECK(decide(late).action == Action::PassThrough);
    }

    // Default-constructed inputs are always a pass-through.
    {
        Inputs in;
        CHECK(decide(in).action == Action::PassThrough);
    }

    if (g_failures == 0) {
        std::printf("All alignment tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", g_failures);
    return 1;
}
