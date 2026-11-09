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
        auto& rotatedTransform = rotated.get_mut<ecs::Transform>();
        rotatedTransform.orientation = {0.5f, 0.5f, 0.5f, 0.5f}; // unit, non-identity
        rotated.modified<ecs::Transform>();

        const auto rotatedBody = attachBody(ecsWorld, rotated, world, *camera);
        REQUIRE(rotatedBody.isValid());
        // Push the ECS orientation into Jolt, then read it back.
        syncEcsToPhysics(ecsWorld, world, *camera, 1.0f / 60.0f);
        syncPhysicsToEcs(ecsWorld, world, *camera);

        const auto readBack = rotated.get<ecs::Transform>().orientation;
        CHECK(std::abs(readBack.x - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.y - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.z - 0.5f) < 1.0e-4f);
        CHECK(std::abs(readBack.w - 0.5f) < 1.0e-4f);
    }

    // Teleport sync: kinematic bodies follow ECS transforms exactly.
    auto kinematicPos = spatial::WorldPosition::fromMeters(3, 2, 0);
    REQUIRE(kinematicPos.has_value());
    const auto kinematic = ecsWorld.createAsteroid(*kinematicPos, {0.0, 0.0, 0.0});
    kinematic.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    const auto kinematicBody = attachBody(ecsWorld, kinematic, world, *camera, MotionType::Kinematic);
    REQUIRE(kinematicBody.isValid());

    auto& mutableTransform = kinematic.get_mut<ecs::Transform>();
    auto movedPos = spatial::WorldPosition::fromMeters(4, 2, 0);
    REQUIRE(movedPos.has_value());
    mutableTransform.position = *movedPos;
    kinematic.modified<ecs::Transform>();
    syncEcsToPhysics(ecsWorld, world, *camera, 1.0f / 60.0f);
    // MoveKinematic interpolates toward the target over dt; step once.
    world.step(1.0f / 60.0f);
    const JPH::Vec3 bodyPos = world.getPosition(kinematicBody);
    CHECK(std::abs(bodyPos.GetX() - 4.0f) < 1.0e-3f);
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
    REQUIRE(data.count >= 1);
    REQUIRE(data.contacts[0].other.is_valid());
    CHECK(data.contacts[0].other.is_alive());
    CHECK(data.contacts[0].impulse >= 0.0f);

    // Equal-and-opposite normals (Newton's third law) for the
    // upper<->lower contact entries (floor contacts are also recorded).
    const auto lowerData = lower.get<ecs::CollisionData>();
    REQUIRE(lowerData.count >= 1);
    const auto findContact = [](const ecs::CollisionData& buffer, flecs::entity other) {
        for (std::uint32_t i = 0; i < buffer.count; ++i)
        {
            if (buffer.contacts[i].other == other)
            {
                return &buffer.contacts[i];
            }
        }
        return static_cast<const ecs::CollisionData::Entry*>(nullptr);
    };
    const auto* upperEntry = findContact(data, lower);
    const auto* lowerEntry = findContact(lowerData, upper);
    REQUIRE(upperEntry != nullptr);
    REQUIRE(lowerEntry != nullptr);
    CHECK(std::abs(upperEntry->normal.x + lowerEntry->normal.x) < 1.0e-4f);
    CHECK(std::abs(upperEntry->normal.y + lowerEntry->normal.y) < 1.0e-4f);
    CHECK(std::abs(upperEntry->normal.z + lowerEntry->normal.z) < 1.0e-4f);

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
    syncEcsToPhysics(ecsWorld, world, *camera, 1.0f / 60.0f);
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

TEST_CASE("Physics exit proof: ship through asteroid field within budget (11.5)")
{
    jobs::initGlobalScheduler();
    PhysicsWorld world(PhysicsWorld::Config{});
    ecs::Ecs ecsWorld;

    const auto camera = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(camera.has_value());

    // Static floor far below (bounce plane, not part of the flythrough).
    const auto floor = world.createStaticBox(500.0f, 1.0f, 500.0f, 0.0f, -30.0f, 0.0f);
    REQUIRE(floor.isValid());

    // Ship at the origin, driven purely by Jolt (no ECS velocity).
    auto shipPos = spatial::WorldPosition::fromMeters(0, 0, 0);
    REQUIRE(shipPos.has_value());
    const auto ship = ecsWorld.createShip(*shipPos, {0.0, 0.0, 0.0});
    ship.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 1.0f});
    const auto shipBody = attachBody(ecsWorld, ship, world, *camera);
    REQUIRE(shipBody.isValid());
    world.setLinearVelocity(shipBody, {30.0f, 0.0f, 0.0f});

    // Asteroid field: 200 deterministic rocks along the ship's path.
    for (std::uint32_t i = 0; i < 200; ++i)
    {
        const float x = 10.0f + static_cast<float>(i) * 2.0f;
        const float y = std::sin(static_cast<float>(i) * 1.7f) * 8.0f;
        const float z = std::cos(static_cast<float>(i) * 2.3f) * 8.0f;
        auto position = spatial::WorldPosition::fromMeters(x, y, z);
        REQUIRE(position.has_value());
        const auto asteroid = ecsWorld.createAsteroid(*position, {0.0, 0.0, 0.0});
        asteroid.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 1.5f});
    }

    ecs::SpatialIndex index(ecsWorld);
    bool collided = false;

    constexpr int kFrames = 180;
    std::vector<double> timings;
    timings.reserve(kFrames);

    for (int frame = 0; frame < kFrames; ++frame)
    {
        const auto frameStart = std::chrono::steady_clock::now();

        // Island activation: bodies only for the local neighborhood.
        index.rebuild();
        const auto attached = syncIslandBodies(ecsWorld, world, *camera, index);
        if (frame == 0)
        {
            CHECK(attached > 0); // Island activation attached nearby asteroids.
        }

        world.step(1.0f / 60.0f);
        syncPhysicsToEcs(ecsWorld, world, *camera);
        world.flushContacts(ecsWorld, *camera);
        if (ship.has<ecs::CollisionEvent>())
        {
            collided = true;
        }

        const auto frameEnd = std::chrono::steady_clock::now();
        if (frame >= 3)
        {
            timings.push_back(std::chrono::duration<double, std::milli>(frameEnd - frameStart).count());
        }
    }

    CHECK(collided);
    CHECK(ecsWorld.world().count<PhysicsBody>() == 201); // Ship + 200 asteroids, all local.

    const auto shipMeters = ship.get<ecs::Transform>().position.toMeters();
    CHECK(shipMeters.x > 5.0); // Traveled into the field.

    std::sort(timings.begin(), timings.end());
    const double p95 = timings[(timings.size() * 95) / 100];
    CHECK(p95 < 3.0); // Physics portion of the ADR-004 frame budget.
}
