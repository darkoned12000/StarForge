#pragma once

#include "math/math.hpp"
#include "spatial/world_position.hpp"

#include <flecs.h>

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

// Collision event emitted on each involved entity after a physics step
// (see starforge_physics ContactQueue). Payload in CollisionData.
struct CollisionEvent
{
};
struct CollisionData
{
    flecs::entity other{};
    spatial::WorldPosition point;
    math::Vec3f normal{0.0f, 0.0f, 1.0f};
    float impulse = 0.0f;
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
