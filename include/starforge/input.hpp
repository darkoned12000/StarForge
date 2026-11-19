#pragma once

#include <cstddef>

namespace starforge
{

// Backend-neutral input values. No SDL types here.
enum class Key
{
    Unknown = 0,
    W,
    A,
    S,
    D,
    Space,
    P,
    Escape,
    Up,
    Down,
    Left,
    Right,
    LeftShift,
    Tab,
    Return,
    Backspace,
    Equal,
    Minus,
    M,
    T,
    Count,
};

enum class MouseButton
{
    Left = 0,
    Right,
    Middle,
};

// Absolute cursor position in window coordinates (top-left origin).
// Meaningful in UI mode; in relative-steering mode the OS may not track it.
struct MousePosition
{
    float x = 0.0f;
    float y = 0.0f;
};

// Tracks press/release edges from per-frame mouse-button level samples.
// Pure CPU with no backend dependency: feed the held state once per frame
// through update(), then query pressed()/released(). Useful for UI clicks.
class MouseButtonEdges
{
public:
    void update(bool left, bool right, bool middle) noexcept
    {
        previous_[0] = current_[0];
        previous_[1] = current_[1];
        previous_[2] = current_[2];
        current_[0] = left;
        current_[1] = right;
        current_[2] = middle;
    }

    [[nodiscard]] bool pressed(MouseButton button) const noexcept
    {
        const std::size_t i = index(button);
        return current_[i] && !previous_[i];
    }

    [[nodiscard]] bool released(MouseButton button) const noexcept
    {
        const std::size_t i = index(button);
        return !current_[i] && previous_[i];
    }

private:
    static std::size_t index(MouseButton button) noexcept
    {
        return button == MouseButton::Right ? 1u
             : button == MouseButton::Middle ? 2u
                                             : 0u;
    }

    bool current_[3] = {};
    bool previous_[3] = {};
};

// Accumulated mouse movement since last take.
struct MouseMotion
{
    float dx = 0.0f; // pixels to the right
    float dy = 0.0f; // pixels down
    float scrollY = 0.0f; // scroll wheel steps (positive = up)
};

} // namespace starforge
