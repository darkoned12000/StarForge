// Physics layer tests (M11.1/11.2): Jolt world on the starforge::jobs
// scheduler — gravity, resting contact, fixed-step determinism, and the
// ECS bridge sync round-trip.
#include <doctest/doctest.h>

#include "ecs/world.hpp"
#include "jobs/job_scheduler.hpp"
#include "physics/bridge.hpp"
#include "physics/physics_world.hpp"

#include <cmath>

using namespace starforge::physics;
namespace jobs = starforge::jobs;
namespace spatial = starforge::spatial;
namespace ecs = starforge::ecs;

TEST_CASE("Physics: dynamic sphere falls and rests on a static floor")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});

    // Floor: box top at y = 0 (half height 1, center at y = -1).
    const auto floor = world.createStaticBox(50.0f, 1.0f, 50.0f, 0.0f, -1.0f, 0.0f);
    REQUIRE(floor.isValid());

    const auto sphere = world.createDynamicSphere(0.5f, 1.0f, 0.0f, 5.0f, 0.0f);
    REQUIRE(sphere.isValid());

    for (int step = 0; step < 240; ++step)
    {
        world.step(1.0f / 60.0f);
    }

    const JPH::Vec3 position = world.getPosition(sphere);
    // Sphere rests with its center one radius above the floor top.
    CHECK(std::abs(position.GetY() - 0.5f) < 0.05f);
    CHECK(std::abs(position.GetX()) < 0.05f);
    CHECK(std::abs(position.GetZ()) < 0.05f);

    const JPH::Vec3 velocity = world.getLinearVelocity(sphere);
    CHECK(velocity.Length() < 0.1f);

    world.destroyBody(sphere);
    world.destroyBody(floor);
}

TEST_CASE("Physics: fixed-step simulation is deterministic")
{
    jobs::initGlobalScheduler();

    const auto run = [] {
        PhysicsWorld world(PhysicsWorld::Config{});
        const auto floor = world.createStaticBox(50.0f, 1.0f, 50.0f, 0.0f, -1.0f, 0.0f);
        const auto sphere = world.createDynamicSphere(0.5f, 1.0f, 3.0f, 5.0f, -2.0f);
        world.setLinearVelocity(sphere, {1.0f, 0.0f, 0.5f});
        for (int step = 0; step < 180; ++step)
        {
            world.step(1.0f / 60.0f);
        }
        const auto result = world.getPosition(sphere);
        world.destroyBody(sphere);
        world.destroyBody(floor);
        return result;
    };

    const auto first = run();
    const auto second = run();
    CHECK(std::abs(first.GetX() - second.GetX()) < 1.0e-4f);
    CHECK(std::abs(first.GetY() - second.GetY()) < 1.0e-4f);
    CHECK(std::abs(first.GetZ() - second.GetZ()) < 1.0e-4f);
}

TEST_CASE("Physics bridge: ECS entity falls, rests, and syncs back")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});
    ecs::Ecs ecsWorld;

    const auto camera = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(camera.has_value());

    // Static floor in the camera-relative island.
    const auto floor = world.createStaticBox(50.0f, 1.0f, 50.0f, 0.0f, -1.0f, 0.0f);
    REQUIRE(floor.isValid());

    auto startPos = spatial::WorldPosition::fromMeters(0, 5, 0);
    REQUIRE(startPos.has_value());
    const auto entity = ecsWorld.createAsteroid(*startPos, {0.0, 0.0, 0.0});
    entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});

    const auto body = attachBody(ecsWorld, entity, world, *camera);
    REQUIRE(body.isValid());
    CHECK(entity.has<PhysicsBody>());

    for (int step = 0; step < 240; ++step)
    {
        world.step(1.0f / 60.0f);
        syncPhysicsToEcs(ecsWorld, world, *camera);
    }

    const auto transform = entity.get<ecs::Transform>();
    const auto meters = transform.position.toMeters();
    CHECK(std::abs(meters.y - 0.5) < 0.05);
    CHECK(std::abs(meters.x) < 0.05);
    CHECK(std::abs(meters.z) < 0.05);

    // Orientation round-trip: a non-identity quaternion must survive
    // ECS -> Jolt -> ECS unchanged (regression: swapped w component).
    {
        auto rotatedPos = spatial::WorldPosition::fromMeters(0, 5, 0);
        REQUIRE(rotatedPos.has_value());
        const auto rotated = ecsWorld.createAsteroid(*rotatedPos, {0.0, 0.0, 0.0});
        rotated.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        auto& transform = rotated.get_mut<ecs::Transform>();
        transform.orientation = {0.5f, 0.5f, 0.5f, 0.5f}; // unit, non-identity
        rotated.modified<ecs::Transform>();

        const auto rotatedBody = attachBody(ecsWorld, rotated, world, *camera);
        REQUIRE(rotatedBody.isValid());
        // Push the ECS orientation into Jolt, then read it back.
        syncEcsToPhysics(ecsWorld, world, *camera);
        syncPhysicsToEcs(ecsWorld, world, *camera);

        const auto readBack = rotated.get<ecs::Transform>().orientation;
        CHECK(std::abs(readBack.x - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.y - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.z - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.w - 0.5f) < 1.0e-4f);
    }

    // Teleport sync: moving the ECS entity moves the body.
    auto& mutableTransform = entity.get_mut<ecs::Transform>();
    auto movedPos = spatial::WorldPosition::fromMeters(3, 2, 0);
    REQUIRE(movedPos.has_value());
    mutableTransform.position = *movedPos;
    entity.modified<ecs::Transform>();
    syncEcsToPhysics(ecsWorld, world, *camera);
    const JPH::Vec3 bodyPos = world.getPosition(body);
    CHECK(std::abs(bodyPos.GetX() - 3.0f) < 1.0e-3f);
    CHECK(std::abs(bodyPos.GetY() - 2.0f) < 1.0e-3f);
}
