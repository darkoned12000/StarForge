#pragma once

// Physics world wrapper: owns Jolt's TempAllocator, the starforge::jobs
// JobSystem adapter, PhysicsSystem, and collision layers. Bodies are
// simulated in camera-relative float32 coordinates (see ROADMAP M11);
// Jolt never sees interstellar-range positions.

#include "contact_listener.hpp"
#include "ecs/world.hpp"
#include "job_system_adapter.hpp"
#include "physics_layers.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace starforge::physics
{

// Minimal body handle for the physics layer. Invalid bodies report
// isValid() == false.
struct BodyHandle
{
    JPH::BodyID id;

    [[nodiscard]] bool isValid() const noexcept { return id != JPH::BodyID(); }
    bool operator==(const BodyHandle& other) const noexcept { return id == other.id; }
};

class PhysicsWorld
{
public:
    struct Config
    {
        std::uint32_t maxBodies = 65536;
        std::uint32_t maxBodyPairs = 65536;
        std::uint32_t maxContactConstraints = 10240;
        std::size_t tempAllocatorBytes = 10 * 1024 * 1024;
    };

    explicit PhysicsWorld(const Config& config);
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;

    // Advances the simulation by dt seconds (fixed sub-step at 60 Hz
    // when dt allows; the caller passes the frame delta).
    void step(float dt);

    // Creates a static box body (half extents in metres).
    [[nodiscard]] BodyHandle createStaticBox(float halfX, float halfY, float halfZ, float x, float y, float z);

    // Creates a dynamic sphere body with the given radius and mass.
    [[nodiscard]] BodyHandle createDynamicSphere(float radius, float mass, float x, float y, float z);

    // Enables linear-cast continuous collision detection (fast bodies).
    void setMotionQualityLinearCast(BodyHandle body);

    // Creates a static sensor volume (no collision response, triggers
    // contact callbacks) on the Sensor layer.
    [[nodiscard]] BodyHandle createSensorBox(float halfX, float halfY, float halfZ, float x, float y, float z);

    struct RayHit
    {
        BodyHandle body;
        float fraction = 1.0f; // Along the ray direction, [0, 1]
        JPH::Vec3 point{0.0f, 0.0f, 0.0f};
    };

    // Casts a ray (direction encodes length) and returns the closest hit.
    [[nodiscard]] std::optional<RayHit> castRay(const JPH::Vec3& from, const JPH::Vec3& direction) const;

    [[nodiscard]] JPH::Vec3 getPosition(BodyHandle body) const;
    [[nodiscard]] JPH::Vec3 getLinearVelocity(BodyHandle body) const;
    void setLinearVelocity(BodyHandle body, JPH::Vec3 velocity);
    void activateBody(BodyHandle body);
    void destroyBody(BodyHandle body);

    [[nodiscard]] JPH::PhysicsSystem& system() noexcept { return system_; }
    [[nodiscard]] ContactQueue& contactQueue() noexcept { return contactQueue_; }

    // Flushes deferred contact events into ECS (main thread, after step).
    void flushContacts(ecs::Ecs& world, const spatial::WorldPosition& camera);

private:
    // First member: ensures Jolt allocator/types registration (see
    // physics_world.cpp for the global RAII guard) before any other
    // member allocates through Jolt.
    struct JoltRegistration
    {
        JoltRegistration();
    };
    JoltRegistration joltRegistration_;

    Config config_;
    float stepRemainder_ = 0.0f;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JobsAdapter> jobSystem_;
    BroadPhaseLayerInterfaceImpl broadPhaseLayers_;
    // Constructed after the interface and pair filter it derives from.
    ObjectLayerPairFilterImpl objectLayerFilter_;
    ObjectVsBroadPhaseLayerFilterImpl broadPhaseFilter_;
    ContactQueue contactQueue_;
    PhysicsContactListener contactListener_;
    JPH::PhysicsSystem system_;
};

} // namespace starforge::physics
