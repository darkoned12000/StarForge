#pragma once

#include <cmath>
#include <limits>

namespace starforge::gameplay::ui_targeting
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

[[nodiscard]] inline float dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline Vec3 add(Vec3 a, Vec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline Vec3 scale(Vec3 v, float s) noexcept
{
    return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] inline float length(Vec3 v) noexcept
{
    return std::sqrt(dot(v, v));
}

// Smallest positive intercept time for a constant-velocity target and a
// straight bolt of speed boltSpeed: solve |p + v*t| = boltSpeed*t.
//
//    (v.v - s*s) t^2 + 2 (p.v) t + p.p = 0
//
// A positive root exists whenever the geometry allows an intercept, even
// for a receding target (p.v > 0) if the bolt is faster. No solution and
// degenerate cases (target at exactly bolt speed, zero relative velocity)
// are reported as invalid rather than producing NaN.
struct LeadSolution
{
    bool valid = false;
    float time = 0.0f;
    Vec3 aim{}; // relative position of the lead point
};

[[nodiscard]] inline LeadSolution solveLead(Vec3 p, Vec3 v, float boltSpeed) noexcept
{
    LeadSolution result;
    if (!(boltSpeed > 0.0f) || !std::isfinite(boltSpeed))
    {
        return result;
    }

    const float a = dot(v, v) - boltSpeed * boltSpeed;
    const float b = 2.0f * dot(p, v);
    const float c = dot(p, p);
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
    {
        return result;
    }

    float t = std::numeric_limits<float>::infinity();
    if (std::abs(a) < 1e-4f)
    {
        // Target recedes at exactly bolt speed: only a closing target can
        // be caught, and then at the linear intercept time.
        if (b < -1e-6f)
        {
            t = -c / b;
        }
    }
    else
    {
        const float discriminant = b * b - 4.0f * a * c;
        if (discriminant >= 0.0f)
        {
            const float root = std::sqrt(discriminant);
            const float t0 = (-b - root) / (2.0f * a);
            const float t1 = (-b + root) / (2.0f * a);
            if (t0 > 0.0f && std::isfinite(t0)) t = t0;
            if (t1 > 0.0f && std::isfinite(t1) && t1 < t) t = t1;
        }
    }

    if (!std::isfinite(t) || t <= 0.0f)
    {
        return result;
    }
    result.valid = true;
    result.time = t;
    result.aim = add(p, scale(v, t));
    return result;
}

// Screen-space edge arrow. Inputs are the target direction in view space
// (+X right, +Y up, -Z in front) and its projected NDC position. When the
// target is on screen the caller draws brackets instead and `onScreen` is
// true. Otherwise the arrow is clamped to an inset rectangle at the screen
// edge; targets behind the ship flip direction and report `behind`.
struct EdgeArrow
{
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f; // radians, screen space (Y down)
    bool onScreen = false;
    bool behind = false;
};

[[nodiscard]] inline EdgeArrow edgeArrow(Vec3 viewDirection,
                                         float ndcX, float ndcY,
                                         float screenWidth, float screenHeight,
                                         float inset) noexcept
{
    EdgeArrow result;
    if (!(screenWidth > 0.0f) || !(screenHeight > 0.0f))
    {
        return result;
    }

    result.behind = viewDirection.z >= 0.0f;
    result.onScreen = !result.behind &&
                      ndcX >= -1.0f && ndcX <= 1.0f &&
                      ndcY >= -1.0f && ndcY <= 1.0f;
    if (result.onScreen)
    {
        return result;
    }

    // Use the view-space XY directly. Mirroring for behind targets used to
    // put the arrow on the opposite side from the turn cue: view-space XY
    // already points along the shortest turn, including behind the ship.
    float dx = viewDirection.x;
    float dy = viewDirection.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-6f)
    {
        dx /= len;
        dy /= len;
    }
    else
    {
        // Directly ahead/behind with no sideways offset: point up.
        dx = 0.0f;
        dy = 1.0f;
    }

    const float halfW = screenWidth * 0.5f;
    const float halfH = screenHeight * 0.5f;
    const float limitX = (std::max)(1.0f, halfW - inset);
    const float limitY = (std::max)(1.0f, halfH - inset);
    const float tx = std::abs(dx) > 1e-6f ? limitX / std::abs(dx)
                                          : std::numeric_limits<float>::infinity();
    const float ty = std::abs(dy) > 1e-6f ? limitY / std::abs(dy)
                                          : std::numeric_limits<float>::infinity();
    const float t = (std::min)(tx, ty);

    result.x = halfW + dx * t;
    result.y = halfH - dy * t; // screen Y grows downward
    result.angle = std::atan2(-dy, dx);
    return result;
}

// Chevron around the crosshair that points the shortest way to turn. The
// strength fades to zero as the target lines up with the reticle.
struct TurnCue
{
    float angle = 0.0f;
    float strength = 0.0f;
    bool visible = false;
};

[[nodiscard]] inline TurnCue turnCue(Vec3 viewDirection, float deadZone = 0.08f) noexcept
{
    TurnCue result;
    const float horizontal = std::sqrt(viewDirection.x * viewDirection.x +
                                       viewDirection.y * viewDirection.y);
    if (horizontal <= 1e-6f)
    {
        // Dead ahead or dead behind: match edgeArrow's fallback, which
        // points the arrow at the top of the screen in this case.
        result.visible = viewDirection.z >= 0.0f;
        result.angle = 1.5707963f;
        result.strength = result.visible ? 1.0f : 0.0f;
        return result;
    }
    if (viewDirection.z >= 0.0f)
    {
        result.visible = true;
        result.angle = std::atan2(viewDirection.y, viewDirection.x);
        result.strength = 1.0f;
        return result;
    }

    result.angle = std::atan2(viewDirection.y, viewDirection.x);
    const float offAxis = horizontal / (-viewDirection.z);
    const float clamped = (std::min)(1.0f, (std::max)(0.0f, offAxis / (std::max)(deadZone, 1e-3f)));
    result.strength = clamped;
    result.visible = clamped > 0.02f;
    return result;
}

} // namespace starforge::gameplay::ui_targeting
