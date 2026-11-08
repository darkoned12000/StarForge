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

TEST_CASE("Physics bridge: collision emits ECS events via the deferred queue")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});
    ecs::Ecs ecsWorld;

    const auto camera = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(camera.has_value());

    const auto floor = world.createStaticBox(50.0f, 1.0f, 50.0f, 0.0f, -1.0f, 0.0f);
    REQUIRE(floor.isValid());

    auto lowerPos = spatial::WorldPosition::fromMeters(0, 0.6, 0);
    REQUIRE(lowerPos.has_value());
    const auto lower = ecsWorld.createAsteroid(*lowerPos, {0.0, 0.0, 0.0});
    lower.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    REQUIRE(attachBody(ecsWorld, lower, world, *camera).isValid());

    auto upperPos = spatial::WorldPosition::fromMeters(0, 3, 0);
    REQUIRE(upperPos.has_value());
    const auto upper = ecsWorld.createAsteroid(*upperPos, {0.0, 0.0, 0.0});
    upper.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    REQUIRE(attachBody(ecsWorld, upper, world, *camera).isValid());

    for (int step = 0; step < 240; ++step)
    {
        world.step(1.0f / 60.0f);
    }
    CHECK(world.contactQueue().size() >= 1);

    world.flushContacts(ecsWorld, *camera);
    CHECK(upper.has<ecs::CollisionEvent>());
    CHECK(lower.has<ecs::CollisionEvent>());

    const auto data = upper.get<ecs::CollisionData>();
    REQUIRE(data.other.is_valid());
    CHECK(data.other.is_alive());
    CHECK(data.impulse >= 0.0f);

    // Equal-and-opposite normals (Newton's third law).
    const auto lowerData = lower.get<ecs::CollisionData>();
    CHECK(std::abs(data.normal.x + lowerData.normal.x) < 1.0e-4f);
    CHECK(std::abs(data.normal.y + lowerData.normal.y) < 1.0e-4f);
    CHECK(std::abs(data.normal.z + lowerData.normal.z) < 1.0e-4f);

    // Transient lifecycle: after separation, the next flush clears the
    // tags instead of re-triggering damage systems.
    for (int step = 0; step < 240; ++step)
    {
        world.step(1.0f / 60.0f);
    }
    // Teleport the upper sphere away so the contact breaks.
    auto& upperTransform = upper.get_mut<ecs::Transform>();
    auto farPos = spatial::WorldPosition::fromMeters(100, 100, 100);
    REQUIRE(farPos.has_value());
    upperTransform.position = *farPos;
    upper.modified<ecs::Transform>();
    syncEcsToPhysics(ecsWorld, world, *camera);
    world.step(1.0f / 60.0f);
    world.flushContacts(ecsWorld, *camera);
    CHECK(!upper.has<ecs::CollisionEvent>());
    CHECK(!lower.has<ecs::CollisionEvent>());
}

TEST_CASE("Physics: linear-cast CCD stops a fast body at a thin wall")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});

    // Thin wall: 0.1 m thick, top at y = 5.
    const auto wall = world.createStaticBox(10.0f, 5.05f, 0.05f, 0.0f, 0.0f, 0.0f);
    REQUIRE(wall.isValid());

    // Fast sphere flying along -Z at 2000 m/s toward the wall.
    const auto bullet = world.createDynamicSphere(0.1f, 1.0f, 0.0f, 2.5f, 5.0f);
    REQUIRE(bullet.isValid());
    world.setMotionQualityLinearCast(bullet);
    world.setLinearVelocity(bullet, {0.0f, 0.0f, -2000.0f});

    for (int step = 0; step < 120; ++step)
    {
        world.step(1.0f / 60.0f);
    }

    const JPH::Vec3 position = world.getPosition(bullet);
    // Without CCD the sphere would tunnel to z << 0; it must stop at the
    // wall's near face (z = 0.05 + radius 0.1).
    CHECK(position.GetZ() > 0.0f);
    CHECK(position.GetZ() < 0.5f);
}

TEST_CASE("Physics: raycast hits the expected body and sensors report overlap")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});

    const auto target = world.createDynamicSphere(1.0f, 1.0f, 0.0f, 0.0f, -10.0f);
    REQUIRE(target.isValid());

    const auto hit = world.castRay({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -20.0f});
    REQUIRE(hit.has_value());
    CHECK(hit->body == target);
    CHECK(hit->fraction > 0.0f);
    CHECK(hit->fraction < 1.0f);

    // A ray missing everything returns nothing.
    CHECK(!world.castRay({50.0f, 50.0f, 5.0f}, {0.0f, 0.0f, -20.0f}).has_value());

    // Sensor volume around an ECS entity: overlap must produce a
    // contact event on the entity through the deferred queue.
    ecs::Ecs ecsWorld;
    auto entityPos = spatial::WorldPosition::fromMeters(0, 0, -10);
    REQUIRE(entityPos.has_value());
    const auto entity = ecsWorld.createAsteroid(*entityPos, {0.0, 0.0, 0.0});
    entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    const auto camera = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(camera.has_value());
    REQUIRE(attachBody(ecsWorld, entity, world, *camera).isValid());

    const auto sensor = world.createSensorBox(2.0f, 2.0f, 2.0f, 0.0f, 0.0f, -10.0f);
    REQUIRE(sensor.isValid());
    for (int step = 0; step < 10; ++step)
    {
        world.step(1.0f / 60.0f);
    }
    CHECK(world.contactQueue().size() >= 1);
    world.flushContacts(ecsWorld, *camera);
    CHECK(entity.has<ecs::CollisionEvent>());
}
