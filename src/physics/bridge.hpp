#pragma once

// ECS <-> Jolt bridge (M11.2): moves entities between ECS Transform in
// world space (WorldPosition) and Jolt bodies in camera-relative
// float32. Physics runs in a floating-origin island around the camera;
// conversions use the same single-cast rule as rendering.

#include "ecs/world.hpp"
#include "physics/physics_world.hpp"

#include <flecs.h>

namespace starforge::physics
{

// Attached to ECS entities that have a Jolt body.
struct PhysicsBody
{
    BodyHandle body;
};

// Collision shape for a physics body (sphere or axis-aligned box).
struct Collider
{
    enum class Shape
    {
        Sphere,
        Box,
    };

    Shape shape = Shape::Sphere;
    float radius = 0.5f;           // Sphere
    math::Vec3f halfExtents{0.5f, 0.5f, 0.5f}; // Box
};

// Creates a Jolt body for the entity from its Transform (camera-relative)
// and Collider, registers the PhysicsBody component, and maps the Flecs
// entity ID into the body user data for contact callbacks.
[[nodiscard]] BodyHandle
attachBody(ecs::Ecs& world, flecs::entity entity, PhysicsWorld& physics, const spatial::WorldPosition& camera);

// Pushes ECS transforms into Jolt for kinematic/teleported bodies
// (dynamic bodies are integrated by Jolt itself).
void syncEcsToPhysics(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera);

// Reads dynamic body positions, orientations, and velocities back into
// ECS Transforms (camera-relative -> WorldPosition).
void syncPhysicsToEcs(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera);

} // namespace starforge::physics
