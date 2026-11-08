#pragma once

#include "math/math.hpp"
#include "spatial/world_position.hpp"

namespace starforge::ecs
{

// Spatial position, orientation, and uniform scale in world space.
// Orientation is a unit quaternion (xyzw).
struct Transform
{
    spatial::WorldPosition position;
    math::Vec4f orientation{0.0f, 0.0f, 0.0f, 1.0f};
    float scale = 1.0f;
};

// Linear and angular kinematics, attached only to moving entities.
// Static entities (stations, gates) carry no Velocity component.
struct Velocity
{
    math::Vec3d linear{0.0, 0.0, 0.0};   // Metres per second
    math::Vec3f angular{0.0f, 0.0f, 0.0f}; // Radians per second (axis * rate)
};

// Entity kind tags (Flecs empty components).
struct ShipTag
{
};
struct AsteroidTag
{
};
struct StationTag
{
};
struct ProjectileTag
{
};

} // namespace starforge::ecs
