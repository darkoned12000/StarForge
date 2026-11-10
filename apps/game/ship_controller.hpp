#pragma once

// First Flight player controller (M15.0.2).
//
// Pure flight model with no SDL/Flecs/Vulkan dependency, so it can be
// unit-tested (T1) and reasoned about in the game loop. The game app
// in main.cpp feeds it input edges + mouse deltas, then applies the
// result to ECS Transform/Velocity.
//
// Control scheme (Freelancer-style, fixed by the roadmap):
//   W/S      change a persistent throttle set-point (tap, not hold)
//   A/D      barrel roll left/right (held rate)
//   Mouse    pitch/yaw steering (flight mode only)
//   Space    toggle flight mode <-> UI mode
//   P/Esc    toggle pause menu

#include <algorithm>
#include <cmath>

namespace starforge::game
{

enum class FlightMode
{
    Flight,
    Ui,
};

struct FlightState
{
    // Persistent throttle 0..1 (set by W/S taps, persists until changed).
    float throttle = 0.0f;
    // Rates (rad/s).
    float pitchRate = 0.0f;
    float yawRate = 0.0f;
    float rollRate = 0.0f;
    bool paused = false;
    FlightMode mode = FlightMode::Flight;
    bool firePrimary = false;
    bool fireSecondary = false;
};

// NOTE: pause/space meta-commands also live in applyInput so headless
// callers can drive them deterministically. The game loop in main.cpp
// consumes those two edges immediately (outside fixed steps) so the
// pause menu works at uncapped fps; it passes cleared edges into
// applyInput, so the blocks here run only in tests. Keep both paths
// in sync if the edge set ever changes.

struct InputEdge
{
    bool wPressed = false;     // edge: key just went down this frame
    bool sPressed = false;
    bool aDown = false;        // level: currently held
    bool dDown = false;
    bool arrowUpDown = false;   // level: Up held -> pitch up rate
    bool arrowDownDown = false; // level: Down held -> pitch down
    bool arrowLeftDown = false; // level: Left held -> yaw left
    bool arrowRightDown = false;// level: Right held -> yaw right
    bool spacePressed = false;
    bool pausePressed = false; // P or Esc edge
    float mouseDx = 0.0f;      // pixels since last frame
    float mouseDy = 0.0f;
    float mouseScrollY = 0.0f; // scroll-wheel steps since last frame
    bool mouseLeftDown = false;
    bool mouseRightDown = false;
};

inline constexpr float kThrottleStep = 0.25f;
inline constexpr float kMaxSpeed = 60.0f; // m/s at full throttle
inline constexpr float kMouseSensitivity = 0.0035f; // rad/px
inline constexpr float kRollRateMax = 3.0f;         // rad/s
inline constexpr float kArrowRateMax = 1.5f;        // rad/s held arrow steering

// Advances state by one frame of input. Pure: depends only on state +
// current input, so fixed-step replay is deterministic.
inline void applyInput(FlightState& state, const InputEdge& input, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    if (input.pausePressed)
    {
        state.paused = !state.paused;
    }
    if (input.spacePressed)
    {
        state.mode = state.mode == FlightMode::Flight ? FlightMode::Ui : FlightMode::Flight;
    }

    if (state.paused)
    {
        state.pitchRate = 0.0f;
        state.yawRate = 0.0f;
        state.rollRate = 0.0f;
        state.firePrimary = false;
        state.fireSecondary = false;
        return;
    }

    // Throttle is a persistent set-point; only edges change it.
    if (input.wPressed)
    {
        state.throttle = std::min(1.0f, state.throttle + kThrottleStep);
    }
    if (input.sPressed)
    {
        state.throttle = std::max(0.0f, state.throttle - kThrottleStep);
    }

    // Mouse steering only in flight mode (UI mode frees the cursor).
    // Arrow keys add a held-rate alternative (headless/no-mouse play).
    float pitch = 0.0f;
    float yaw = 0.0f;
    if (state.mode == FlightMode::Flight)
    {
        pitch = -input.mouseDy * kMouseSensitivity; // push forward (up) = nose up
        yaw = -input.mouseDx * kMouseSensitivity;   // right = turn right
        float pitchRate = pitch / dt;
        float yawRate = yaw / dt;
        if (input.arrowUpDown)
        {
            pitchRate += kArrowRateMax;
        }
        if (input.arrowDownDown)
        {
            pitchRate -= kArrowRateMax;
        }
        if (input.arrowLeftDown)
        {
            yawRate += kArrowRateMax;
        }
        if (input.arrowRightDown)
        {
            yawRate -= kArrowRateMax;
        }
        state.pitchRate = pitchRate;
        state.yawRate = yawRate;
    }
    else
    {
        state.pitchRate = 0.0f;
        state.yawRate = 0.0f;
    }

    float roll = 0.0f;
    if (input.aDown)
    {
        roll -= kRollRateMax;
    }
    if (input.dDown)
    {
        roll += kRollRateMax;
    }
    state.rollRate = roll;

    state.firePrimary = input.mouseLeftDown;
    state.fireSecondary = input.mouseRightDown;
}

// Target forward speed (m/s) from throttle set-point.
inline float targetSpeed(const FlightState& state) noexcept
{
    return state.throttle * kMaxSpeed;
}

// Fire-arm states for the same input edges (convenience so tests and
// the game loop share one rule).
inline bool primaryEdgeFired(const InputEdge& input) noexcept
{
    return input.mouseLeftDown;
}
inline bool secondaryEdgeFired(const InputEdge& input) noexcept
{
    return input.mouseRightDown;
}

} // namespace starforge::game
