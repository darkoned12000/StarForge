#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ai/steering.hpp"
#include "ecs/world.hpp"
#include "jobs/job_scheduler.hpp"
#include "physics/bridge.hpp"
#include "physics/physics_world.hpp"
#include "spatial/camera_relative.hpp"

#include <cmath>

using namespace starforge::ai;
namespace jobs = starforge::jobs;
namespace spatial = starforge::spatial;
namespace ecs = starforge::ecs;
using namespace starforge::physics;

TEST_CASE("AI physics coupling: kinematic NPC obeys steering and collides")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    starforge::ecs::Ecs worldEcs;
    registerAiComponents(worldEcs);

    const auto camera = spatial::WorldPosition::fromMeters(1.0e12, 0.0, 0.0);
    REQUIRE(camera.has_value());

    const auto obstaclePos = spatial::WorldPosition::fromMeters(1.0e12 + 25.0, 0.0, 0.0);
    const auto shipPos = spatial::WorldPosition::fromMeters(1.0e12 + 5.0, 0.0, 0.0);
    const auto targetPos = spatial::WorldPosition::fromMeters(1.0e12 + 60.0, 0.0, 0.0);
    REQUIRE(obstaclePos.has_value());
    REQUIRE(shipPos.has_value());
    REQUIRE(targetPos.has_value());

    // Static wall plus a static floor in local camera-relative space. The
    // dynamic NPC body must report a collision against the wall.
    const auto floor = physicsWorld.createStaticBox(50.0f, 1.0f, 50.0f, 0.0f, -6.0f, 0.0f);
    const auto wall = physicsWorld.createStaticBox(1.5f, 5.0f, 5.0f, 25.0f, 0.0f, 0.0f);
    REQUIRE(floor.isValid());
    REQUIRE(wall.isValid());

    const auto ship = createNpcShip(worldEcs, *shipPos, 1);
    auto& agent = ship.get_mut<AiAgent>();
    agent.maxSpeed = 20.0f;
    agent.maxAcceleration = 60.0f;
    ship.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    const auto shipBody = attachBody(worldEcs, ship, physicsWorld, *camera);
    REQUIRE(shipBody.isValid());
    registerBodyCleanup(worldEcs, physicsWorld);

    SteeringParams steering;
    steering.maxSpeed = 20.0;
    steering.maxAcceleration = 60.0;
    steering.arrivalRadius = 5.0;

    bool collided = false;
    for (int frame = 0; frame < 120; ++frame)
    {
        steerEntity(ship, *targetPos, 1.0 / 60.0, steering);
        if (ship.has<ecs::Velocity>())
        {
            const auto velocity = ship.get<ecs::Velocity>().linear;
            physicsWorld.setLinearVelocity(shipBody, {static_cast<float>(velocity.x),
                                                      static_cast<float>(velocity.y),
                                                      static_cast<float>(velocity.z)});
        }
        physicsWorld.step(1.0f / 60.0f);
        syncPhysicsToEcs(worldEcs, physicsWorld, *camera);
        physicsWorld.flushContacts(worldEcs, *camera);
        if (ship.has<ecs::CollisionEvent>())
        {
            collided = true;
        }
    }

    CHECK(collided);
    const auto relative = spatial::toCameraRelative(ship.get<starforge::ecs::Transform>().position, *camera);
    CHECK(relative.x > 15.0);
    CHECK(relative.x <= 25.0);
    jobs::shutdownGlobalScheduler();
}
