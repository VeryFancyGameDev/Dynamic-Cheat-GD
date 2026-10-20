# Input Quantization Assist (Geode mod for Geometry Dash)

An accessibility-oriented **input buffer**. When a jump click lands 1-16 physics
steps away from an interactive timing window (a jump orb / ring), the mod
corrects it so the engine processes the click cleanly. It is toggled with a
hotkey (default **F8**); **F9** cycles the number of extra frames 1→16 with a popup that fades after 1 second and is fully inert while off.

Targets Geometry Dash **2.2081**, Geode **v5.x** (Windows, macOS, Android).

## How it works

| Requirement | Implementation (`src/main.cpp`) |
|---|---|
| Keybind handler | `$modify(AssistKeyboardDispatcher, CCKeyboardDispatcher)` hooks `dispatchKeyboardMSG`. On a fresh press of the toggle key it flips `m_assistEnabled`, writes a log line and shows an on-screen notification. |
| Input interception | `$modify(AssistGameLayer, GJBaseGameLayer)` hooks `processQueuedButtons`, the function that drains the click queue each physics step. |
| Frame-window alignment | Every step the hook samples the player (speed, whether an orb/ring is overlapped, when the overlap last ended). A jump-push is then compared against that timing window (`AlignmentLogic.hpp`): |
| | **Early** (target 1-N steps ahead): the click is held and fired through the engine's own `handleButton` exactly when the window opens. |
| | **Late** (window closed < N steps ago): the ring just left is re-exposed to the player for that one call so the engine's own button code activates it, then removed again. |
| State isolation | If `m_assistEnabled` is false (or the level is not a `PlayLayer`) the hook releases anything still held and calls the original function untouched. |

`N` is the **Window (physics steps)** setting, clamped to 1..16.

The decision rules are a pure header (`src/AlignmentLogic.hpp`) with unit tests
that need no game or SDK:

```
cd tests
g++ -std=c++20 -I../src alignment_test.cpp -o alignment_test && ./alignment_test
```

## Settings

| Key | Default | Meaning |
|---|---|---|
| `start-enabled` | off | Start with the assist on (handy where there is no keyboard). |
| `toggle-key` | F8 | F1-F12, Insert, Home or Tab. |
| `cycle-key` | F9 | Each press adds an extra frame (1-16, wraps to 1) and shows a 1s popup. |
| `window-frames` | 1 | Extra frames: allowed error in physics steps (1-16). |
| `assist-early` | on | Hold early clicks until the target opens. |
| `assist-late` | on | Re-apply clicks that are just late. |
| `show-notifications` | on | Toast when toggled. |
| `debug-log` | off | Log each adjustment. |

## Build

Prerequisites: [Geode SDK + CLI](https://docs.geode-sdk.org/getting-started/), CMake 3.21+, a C++23 compiler (see the Geode docs for per-platform toolchains).

```
geode sdk version        # note your SDK version
# edit "geode" in mod.json to match it, and set "id" / "developer"
geode build
```

The result is `yourname.input-quantization-assist.geode` in `build/`; drop it
into your Geode `mods` folder. No local toolchain? Push this folder to GitHub
and the included workflow (`.github/workflows/build.yml`) builds every
platform and uploads the `.geode` files as an artifact.

## Before you publish / use

* `id` and `developer` in `mod.json` are placeholders - change them.
* **This code was written against the 2.2081 bindings but has not been compiled or run in-game.** Expect to do a build-and-test pass. Things worth checking first: that `processQueuedButtons` is called once per physics step, that `PlayerButtonCommand::m_step` / `buttonIsRelevant` behave as assumed, and the hold/replay timing in a practice-mode orb section (turn on `debug-log`).
* The mod changes when clicks take effect, so it can affect leaderboard-eligible runs. Index reviewers may ask for the `cheat` tag; don't use it for submitted records unless the community rules where you play allow input assists.
* The late-click path physically re-exposes a ring for one engine call; the early path delays a click by up to N steps if its predicted target never materialises. Both are bounded by the 1-16 step window.

## Layout

```
mod.json                  metadata + settings
CMakeLists.txt            build script
src/main.cpp              hooks, settings, hotkey
src/AlignmentLogic.hpp    pure decision logic
tests/alignment_test.cpp  unit tests for the logic
.github/workflows/        CI build
about.md  changelog.md  logo.png  LICENSE
```
