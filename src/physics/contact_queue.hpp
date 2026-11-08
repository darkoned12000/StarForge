#pragma once

// Deferred collision events (M11.3). Jolt contact callbacks run on
// worker threads; they only push into this thread-safe chunk queue.
// The main thread flushes the queue into ECS after PhysicsSystem::Update,
// so Flecs is never mutated from Jolt threads.

#include "ecs/world.hpp"

#include "math/math.hpp"
#include "spatial/world_position.hpp"

#include <flecs.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace starforge::physics
{

// One detected contact, in ECS terms. Entities are stored as raw Flecs
// IDs (handles are re-bound to the world at flush time). The point is
// kept in the camera-relative island frame until flush rebases it.
struct ContactEventData
{
    std::uint64_t idA = 0;
    std::uint64_t idB = 0;
    math::Vec3f islandPoint{0.0f, 0.0f, 0.0f};
    math::Vec3f normal{0.0f, 0.0f, 1.0f};
    float impulse = 0.0f;
};

class ContactQueue
{
public:
    void push(ContactEventData event);
    [[nodiscard]] std::size_t size() const;

    // Strips previous frame's transient collision components.
    void clearTransientEvents(ecs::Ecs& world);

    // Clears transient events, then emits CollisionEvent + CollisionData
    // on both involved entities (opposite normals). Camera-relative
    // Jolt points are rebased via the camera transform.
    void flush(ecs::Ecs& world, const spatial::WorldPosition& camera);

private:
    mutable std::mutex mutex_;
    std::vector<ContactEventData> events_;
};

} // namespace starforge::physics
