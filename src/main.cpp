// ---------------------------------------------------------------------------
// Input Quantization Assist  -  Geode mod for Geometry Dash (2.2081)
//
// An accessibility-oriented "input buffer" that nudges jump clicks which land
// within a 1-16 physics-step margin ("extra frames") of an interactive timing window (jump
// orbs / rings) so the engine processes them cleanly.
//
//   1. Keybind handler   : hook on CCKeyboardDispatcher flips m_assistEnabled
//                          (toggle key) and cycles the extra-frame count 1..16
//                          (cycle key), each with a 1-second fading popup.
//   2. Input interception: hook on GJBaseGameLayer::processQueuedButtons, the
//                          function that drains the click queue every step.
//   3. Alignment logic   : early clicks are held until the target window opens
//                          (stepsUntilTarget), late clicks are replayed into
//                          the window that was just left (stepsSinceClosed).
//                          The pure decision rules live in AlignmentLogic.hpp.
//   4. State isolation   : when m_assistEnabled is false the hook releases any
//                          in-flight held input and calls the original engine
//                          function untouched.
// ---------------------------------------------------------------------------

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "AlignmentLogic.hpp"

using namespace geode::prelude;

// ===========================================================================
//  Shared state + settings
// ===========================================================================
namespace {

struct AssistState {
    // Master switch, flipped by the hotkey.
    bool m_assistEnabled = false;

    // Cached settings (refreshed through setting-change listeners).
    int m_windowFrames = 1;          // extra frames: allowed error, 1..16 physics steps
    bool m_assistEarly = true;       // hold early clicks until the target
    bool m_assistLate = true;        // replay late clicks into the window
    bool m_showNotifications = true; // on-screen toast when toggling
    bool m_debugLog = false;         // verbose per-adjustment logging
    cocos2d::enumKeyCodes m_toggleKey = cocos2d::KEY_F8;
    cocos2d::enumKeyCodes m_cycleKey = cocos2d::KEY_F9;

    // Statistics for the current session.
    int m_adjustedEarly = 0;
    int m_adjustedLate = 0;

    static AssistState& get() {
        static AssistState s_state;
        return s_state;
    }

    void setEnabled(bool enabled) {
        m_assistEnabled = enabled;

        char const* text = enabled ? "Input Assist: ON" : "Input Assist: OFF";
        log::info("{}", text);

        if (m_showNotifications) {
            Notification::create(
                text,
                enabled ? NotificationIcon::Success : NotificationIcon::Warning,
                1.0f
            )->show();
        }
    }

    void toggle() { setEnabled(!m_assistEnabled); }

    // Advances the extra-frame count 1 -> 2 -> ... -> 16 -> 1 and shows a
    // popup that fades out after one second. Only one popup is ever on screen:
    // a new press replaces the previous one instead of stacking.
    void cycleFrames() {
        int next = m_windowFrames + 1;
        if (next > assist::kMaxMargin) next = assist::kMinMargin;
        m_windowFrames = next;

        // Persist so the settings screen and the next launch agree.
        Mod::get()->setSettingValue<int64_t>("window-frames", next);

        std::string const text = fmt::format("Cycled: {} extra frame{}", next, next == 1 ? "" : "s");
        log::info("{}", text);

        if (m_popup) m_popup->hide();
        m_popup = Notification::create(text, NotificationIcon::None, 1.0f);
        m_popup->show();
    }

    Ref<Notification> m_popup;
};

cocos2d::enumKeyCodes keyFromName(std::string const& name) {
    using namespace cocos2d;
    static std::unordered_map<std::string, enumKeyCodes> const s_keys = {
        {"F1", KEY_F1},   {"F2", KEY_F2},   {"F3", KEY_F3},
        {"F4", KEY_F4},   {"F5", KEY_F5},   {"F6", KEY_F6},
        {"F7", KEY_F7},   {"F8", KEY_F8},   {"F9", KEY_F9},
        {"F10", KEY_F10}, {"F11", KEY_F11}, {"F12", KEY_F12},
        {"Insert", KEY_Insert}, {"Home", KEY_Home}, {"Tab", KEY_Tab},
    };
    auto it = s_keys.find(name);
    return it != s_keys.end() ? it->second : KEY_F8;
}

void loadAllSettings() {
    auto* mod = Mod::get();
    auto& st = AssistState::get();
    st.m_windowFrames = assist::clampMargin(mod->getSettingValue<int64_t>("window-frames"));
    st.m_assistEarly = mod->getSettingValue<bool>("assist-early");
    st.m_assistLate = mod->getSettingValue<bool>("assist-late");
    st.m_showNotifications = mod->getSettingValue<bool>("show-notifications");
    st.m_debugLog = mod->getSettingValue<bool>("debug-log");
    st.m_toggleKey = keyFromName(mod->getSettingValue<std::string>("toggle-key"));
    st.m_cycleKey = keyFromName(mod->getSettingValue<std::string>("cycle-key"));
}

} // namespace

$on_mod(Loaded) {
    loadAllSettings();
    AssistState::get().m_assistEnabled = Mod::get()->getSettingValue<bool>("start-enabled");

    listenForSettingChanges<int64_t>("window-frames", [](int64_t value) {
        AssistState::get().m_windowFrames = assist::clampMargin(value);
    });
    listenForSettingChanges<bool>("assist-early", [](bool value) {
        AssistState::get().m_assistEarly = value;
    });
    listenForSettingChanges<bool>("assist-late", [](bool value) {
        AssistState::get().m_assistLate = value;
    });
    listenForSettingChanges<bool>("show-notifications", [](bool value) {
        AssistState::get().m_showNotifications = value;
    });
    listenForSettingChanges<bool>("debug-log", [](bool value) {
        AssistState::get().m_debugLog = value;
    });
    listenForSettingChanges<std::string>("toggle-key", [](std::string value) {
        AssistState::get().m_toggleKey = keyFromName(value);
    });
    listenForSettingChanges<std::string>("cycle-key", [](std::string value) {
        AssistState::get().m_cycleKey = keyFromName(value);
    });
}

// ===========================================================================
//  1. Keybind handler
// ===========================================================================
class $modify(AssistKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool isKeyRepeat, double timestamp) {
        auto& st = AssistState::get();

        // Fire once per physical key press (ignore OS key-repeat and key-up).
        if (down && !isKeyRepeat) {
            if (key == st.m_toggleKey) {
                st.toggle();
            }
            else if (key == st.m_cycleKey) {
                st.cycleFrames();
            }
        }

        // Always forward so the rest of the game (and other mods) still see it.
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, isKeyRepeat, timestamp);
    }
};

// ===========================================================================
//  2 + 3 + 4.  Input interception, alignment logic, state isolation
// ===========================================================================
namespace {

// A click that is being held back until its target window opens.
struct HeldInput {
    PlayerButtonCommand cmd;
    int dueStep; // physics step at which it is released
};

// A ring we temporarily re-added to the player's touching list for one call.
struct ReinjectedRing {
    int playerIdx;
    Ref<GameObject> ring;
};

// Object types that behave as a timed "click target" (orbs / rings).
bool isInteractiveTarget(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

} // namespace

class $modify(AssistGameLayer, GJBaseGameLayer) {
    struct Fields {
        int m_lastStep = -1;

        // Per-player tracking, index 0 = player 1, index 1 = player 2.
        bool m_haveX[2] = {false, false};
        float m_lastX[2] = {0.f, 0.f};
        float m_stepDx[2] = {0.f, 0.f};      // horizontal travel per step
        bool m_touching[2] = {false, false}; // overlapping a target right now
        int m_closeStep[2] = {-1, -1};       // step the overlap last ended
        Ref<GameObject> m_lastRing[2];       // last target that was touched

        std::vector<HeldInput> m_held;
    };

    // ----- helpers ---------------------------------------------------------

    PlayerObject* assistPlayer(int idx) {
        return idx == 0 ? m_player1 : m_player2;
    }

    void assistResetTracking() {
        m_fields->m_lastStep = -1;
        for (int i = 0; i < 2; ++i) {
            m_fields->m_haveX[i] = false;
            m_fields->m_lastX[i] = 0.f;
            m_fields->m_stepDx[i] = 0.f;
            m_fields->m_touching[i] = false;
            m_fields->m_closeStep[i] = -1;
            m_fields->m_lastRing[i] = Ref<GameObject>();
        }
        m_fields->m_held.clear();
    }

    // Runs once per physics step: samples the player so we know the speed,
    // whether a target window is open, and when it last closed.
    void assistObserve(int step) {
        int const elapsed = std::max(1, step - m_fields->m_lastStep);

        for (int i = 0; i < 2; ++i) {
            auto* player = assistPlayer(i);
            if (!player) continue;

            float const x = player->getPositionX();
            if (m_fields->m_haveX[i]) {
                m_fields->m_stepDx[i] = std::fabs(x - m_fields->m_lastX[i]) / static_cast<float>(elapsed);
            }
            m_fields->m_lastX[i] = x;
            m_fields->m_haveX[i] = true;

            bool const touching = player->m_touchingRings && player->m_touchingRings->count() > 0;
            if (touching) {
                m_fields->m_lastRing[i] = static_cast<GameObject*>(player->m_touchingRings->objectAtIndex(0));
            }
            else if (m_fields->m_touching[i]) {
                m_fields->m_closeStep[i] = step; // overlap ended this step
            }
            m_fields->m_touching[i] = touching;
        }
    }

    // Predicts how many steps (1..margin) until the player overlaps a target,
    // or -1 when no target is about to be reached.
    int assistPredictStepsUntilTarget(int idx, int margin) {
        auto* player = assistPlayer(idx);
        float const dx = m_fields->m_stepDx[idx];
        if (!player || !m_objects || dx < 0.01f) return -1;

        cocos2d::CCRect const base = player->getObjectRect();
        float const dir = player->m_isGoingLeft ? -1.f : 1.f;
        float const px = base.getMidX();

        int best = -1;
        for (auto* obj : CCArrayExt<GameObject*>(m_objects)) {
            if (!obj || !isInteractiveTarget(obj->m_objectType)) continue;

            cocos2d::CCRect const target = obj->getObjectRect();

            // Cheap reject: too far away horizontally to be reached in time.
            float const reach = dx * static_cast<float>(margin) + (base.size.width + target.size.width) * 0.5f + 1.f;
            if (std::fabs(target.getMidX() - px) > reach) continue;

            // Already overlapping: the window is open, nothing to predict.
            if (base.intersectsRect(target)) continue;

            for (int k = 1; k <= margin; ++k) {
                cocos2d::CCRect moved = base;
                moved.origin.x += dir * dx * static_cast<float>(k);
                if (moved.intersectsRect(target)) {
                    if (best < 0 || k < best) best = k;
                    break;
                }
            }
        }
        return best;
    }

    // Due step of the oldest held input for this player, INT_MAX if none.
    int assistHeldDue(int idx) {
        int due = INT_MAX;
        for (auto const& held : m_fields->m_held) {
            if ((held.cmd.m_isPlayer2 ? 1 : 0) == idx) due = std::min(due, held.dueStep);
        }
        return due;
    }

    // Replays a held input through the engine's own button handler.
    void assistFire(HeldInput const& held) {
        this->handleButton(
            held.cmd.m_isPush,
            static_cast<int>(held.cmd.m_button),
            !held.cmd.m_isPlayer2 // handleButton takes "isPlayer1"
        );
    }

    // Releases every held input whose player has reached its due step.
    void assistReleaseDue(int step) {
        if (m_fields->m_held.empty()) return;

        int due[2] = {INT_MAX, INT_MAX};
        for (auto const& held : m_fields->m_held) {
            int const i = held.cmd.m_isPlayer2 ? 1 : 0;
            due[i] = std::min(due[i], held.dueStep);
        }

        std::vector<HeldInput> remaining;
        for (auto const& held : m_fields->m_held) {
            int const i = held.cmd.m_isPlayer2 ? 1 : 0;
            if (step >= due[i]) {
                this->assistFire(held);
            }
            else {
                remaining.push_back(held);
            }
        }
        m_fields->m_held = std::move(remaining);
    }

    // Immediately fires everything still held (used when the assist is
    // switched off or the player dies) so no input is ever swallowed.
    void assistFlushHeld(bool fire) {
        auto held = std::move(m_fields->m_held);
        m_fields->m_held.clear();
        if (!fire) return;
        for (auto const& h : held) this->assistFire(h);
    }

    // ----- the hook --------------------------------------------------------

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto& st = AssistState::get();
        bool const inPlayLayer = typeinfo_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this)) != nullptr;

        // ---- State isolation: flag off -> stock engine behaviour ----------
        if (!st.m_assistEnabled || !inPlayLayer) {
            if (!m_fields->m_held.empty()) this->assistFlushHeld(true);
            GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
            return;
        }

        int const step = m_currentStep;

        // A new attempt rewinds the step counter: drop all stale tracking.
        if (step < m_fields->m_lastStep) this->assistResetTracking();
        if (step != m_fields->m_lastStep) {
            this->assistObserve(step);
            m_fields->m_lastStep = step;
        }

        // Dead player: never carry inputs over into the next attempt.
        if (m_player1 && m_player1->m_isDead) {
            m_fields->m_held.clear();
            GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
            return;
        }

        // Fire held clicks whose target window should be open by now.
        this->assistReleaseDue(step);

        int const margin = assist::clampMargin(st.m_windowFrames);

        std::vector<PlayerButtonCommand> kept;
        std::vector<ReinjectedRing> reinjected;
        bool queueChanged = false;

        for (size_t i = 0; i < m_queuedButtons.size(); ++i) {
            PlayerButtonCommand const cmd = m_queuedButtons[i];

            // Only jump commands that belong to this step are of interest.
            if (cmd.m_button != PlayerButton::Jump || !this->buttonIsRelevant(cmd)) {
                kept.push_back(cmd);
                continue;
            }

            int const idx = cmd.m_isPlayer2 ? 1 : 0;

            // Preserve ordering: anything queued behind a held click waits too
            // (e.g. the matching button-up).
            int const heldDue = this->assistHeldDue(idx);
            if (heldDue != INT_MAX) {
                m_fields->m_held.push_back({cmd, heldDue});
                queueChanged = true;
                continue;
            }

            // Releases are never re-timed on their own.
            if (!cmd.m_isPush) {
                kept.push_back(cmd);
                continue;
            }

            // ---- Frame-window alignment ----------------------------------
            assist::Inputs in;
            in.windowActive = m_fields->m_touching[idx];
            in.margin = margin;
            in.allowEarly = st.m_assistEarly;
            in.allowLate = st.m_assistLate;
            in.stepsSinceWindowClosed =
                m_fields->m_closeStep[idx] >= 0 ? step - m_fields->m_closeStep[idx] : -1;
            in.stepsUntilTarget =
                (!in.windowActive && in.allowEarly) ? this->assistPredictStepsUntilTarget(idx, margin) : -1;

            assist::Decision const decision = assist::decide(in);

            switch (decision.action) {
                case assist::Action::HoldForTarget: {
                    // Early click: defer it until the target window opens.
                    m_fields->m_held.push_back({cmd, step + decision.steps});
                    queueChanged = true;
                    st.m_adjustedEarly++;
                    if (st.m_debugLog) {
                        log::debug("[assist] player {} click held {} step(s) (step {})", idx + 1, decision.steps, step);
                    }
                    break;
                }
                case assist::Action::ReplayIntoWindow: {
                    // Late click: re-expose the ring we just left so the
                    // engine's own button code activates it for this click.
                    auto* player = this->assistPlayer(idx);
                    GameObject* ring = m_fields->m_lastRing[idx];
                    if (player && ring && player->m_touchingRings &&
                        !player->m_touchingRings->containsObject(ring)) {
                        player->m_touchingRings->addObject(ring);
                        reinjected.push_back({idx, Ref<GameObject>(ring)});
                        st.m_adjustedLate++;
                        if (st.m_debugLog) {
                            log::debug("[assist] player {} click replayed, {} step(s) late (step {})", idx + 1, decision.steps, step);
                        }
                    }
                    kept.push_back(cmd);
                    break;
                }
                case assist::Action::PassThrough:
                default:
                    kept.push_back(cmd);
                    break;
            }
        }

        // Write the (possibly reduced) queue back for the engine.
        if (queueChanged) {
            m_queuedButtons.clear();
            for (auto const& c : kept) m_queuedButtons.push_back(c);
        }

        // Hand over to the original engine routine.
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);

        // Undo the temporary ring re-exposure so physics stays untouched.
        for (auto const& r : reinjected) {
            auto* player = this->assistPlayer(r.playerIdx);
            if (player && player->m_touchingRings && player->m_touchingRings->containsObject(r.ring)) {
                player->m_touchingRings->removeObject(r.ring);
            }
        }
    }
};
