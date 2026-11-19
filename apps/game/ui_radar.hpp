#pragma once

// M16.6: pure-CPU radar projection. Contacts are given in ship-relative
// planar coordinates: x = right, forward = ahead. Screen y grows downward.
#include <cmath>
#include <string>
#include <utility>

namespace starforge::game
{

// Plain 3-vector used by the radar transform (no backend math types).
struct RadarVec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Converts a camera-relative WORLD offset into ship-local (right, ahead)
// coordinates using the ship's world-space basis vectors.
[[nodiscard]] inline std::pair<float, float> toShipLocal(
    const RadarVec3& worldOffset, const RadarVec3& shipRight,
    const RadarVec3& shipForward) noexcept
{
    return {worldOffset.x * shipRight.x + worldOffset.y * shipRight.y +
                worldOffset.z * shipRight.z,
            worldOffset.x * shipForward.x + worldOffset.y * shipForward.y +
                worldOffset.z * shipForward.z};
}

enum class RadarKind
{
    Rock,
    Hostile,
    Planet,
    Station,
};

struct RadarContact
{
    std::string id;
    float x = 0.0f;      // right
    float forward = 0.0f; // ahead
    RadarKind kind = RadarKind::Rock;
};

struct RadarPoint
{
    float x = 0.0f;        // offset from radar centre in pixels
    float y = 0.0f;
    float distance = 0.0f; // metres
    float bearing = 0.0f;  // degrees clockwise from ahead (0 = straight ahead)
    bool visible = false;
};

// Projects a contact into a radar disc of `radiusPx` covering `range` metres.
[[nodiscard]] inline RadarPoint projectRadar(const RadarContact& contact, float range,
                                             float radiusPx) noexcept
{
    RadarPoint out;
    if (range <= 0.0f || radiusPx <= 0.0f) return out;
    out.distance = std::sqrt(contact.x * contact.x + contact.forward * contact.forward);
    out.bearing = std::atan2(contact.x, contact.forward) * 180.0f / 3.141592653589793f;
    if (out.bearing < 0.0f) out.bearing += 360.0f;
    out.visible = out.distance <= range;
    if (!out.visible) return out;
    out.x = contact.x / range * radiusPx;
    out.y = -contact.forward / range * radiusPx; // ahead appears at the top
    return out;
}

} // namespace starforge::game
