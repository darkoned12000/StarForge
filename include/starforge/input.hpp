#pragma once

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
    Count,
};

enum class MouseButton
{
    Left = 0,
    Right,
    Middle,
};

// Accumulated mouse movement since last take.
struct MouseMotion
{
    float dx = 0.0f; // pixels to the right
    float dy = 0.0f; // pixels down
    float scrollY = 0.0f; // scroll wheel steps (positive = up)
};

} // namespace starforge
