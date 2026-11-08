#pragma once

// Physics world wrapper: owns Jolt's TempAllocator, the starforge::jobs
// JobSystem adapter, PhysicsSystem, and collision layers. Bodies are
// simulated in camera-relative float32 coordinates (see ROADMAP M11);
// Jolt never sees interstellar-range positions.

#include "job_system_adapter.hpp"
#include "physics_layers.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdint>
#include <memory>

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

    [[nodiscard]] JPH::Vec3 getPosition(BodyHandle body) const;
    [[nodiscard]] JPH::Vec3 getLinearVelocity(BodyHandle body) const;
    void setLinearVelocity(BodyHandle body, JPH::Vec3 velocity);
    void activateBody(BodyHandle body);
    void destroyBody(BodyHandle body);

    [[nodiscard]] JPH::PhysicsSystem& system() noexcept { return system_; }

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
    JPH::PhysicsSystem system_;
};

} // namespace starforge::physics
