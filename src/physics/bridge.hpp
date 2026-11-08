#pragma once

// ECS <-> Jolt bridge (M11.2): moves entities between ECS Transform in
// world space (WorldPosition) and Jolt bodies in camera-relative
// float32. Physics runs in a floating-origin island around the camera;
// conversions use the same single-cast rule as rendering.

#include "ecs/spatial_index.hpp"
#include "ecs/world.hpp"
#include "physics/physics_world.hpp"

#include <flecs.h>

namespace starforge::physics
{

enum class MotionType
{
    Dynamic,   // Integrated by Jolt; ECS reads back.
    Kinematic, // Driven by ECS (teleported each sync); Jolt only collides.
};

// Attached to ECS entities that have a Jolt body.
struct PhysicsBody
{
    BodyHandle body;
    MotionType motionType = MotionType::Dynamic;
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
[[nodiscard]] BodyHandle attachBody(ecs::Ecs& world, flecs::entity entity, PhysicsWorld& physics,
                                    const spatial::WorldPosition& camera,
                                    MotionType motionType = MotionType::Dynamic);

// Pushes ECS transforms into Jolt for kinematic bodies (continuous
// MoveKinematic displacement with collision response). Dynamic bodies
// are integrated by Jolt itself and skipped.
void syncEcsToPhysics(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera, float dt);

// Reads dynamic body positions, orientations, and velocities back into
// ECS Transforms (camera-relative -> WorldPosition).
void syncPhysicsToEcs(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera);

// Removes the entity's PhysicsBody component. Body destruction happens
// via the cleanup observer when registerBodyCleanup was called; without
// it, destroy the body via PhysicsWorld::destroyBody first.
void detachBody(ecs::Ecs& world, flecs::entity entity, PhysicsWorld& physics);

// Attaches Jolt bodies to asteroid entities in the camera's spatial
// index neighborhood that do not have one yet (island activation).
// Returns the number of newly attached bodies.
[[nodiscard]] std::uint32_t syncIslandBodies(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera,
                                             const ecs::SpatialIndex& index);

// Shifts every attached body by -delta in island space after the camera
// moved, keeping ECS world positions invariant under camera motion.
void rebaseIsland(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& oldCamera,
                  const spatial::WorldPosition& newCamera);

// Destroys Jolt bodies farther than maxRadiusMeters from the camera
// (island eviction; requires registerBodyCleanup for body destruction).
void evictDistantBodies(ecs::Ecs& world, PhysicsWorld& physics, const spatial::WorldPosition& camera,
                        double maxRadiusMeters);

// Registers an on_remove observer that destroys Jolt bodies when their
// ECS entity is destroyed, preventing ghost bodies. Call once per world.
void registerBodyCleanup(ecs::Ecs& world, PhysicsWorld& physics);

} // namespace starforge::physics
