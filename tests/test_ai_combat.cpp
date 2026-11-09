#include <doctest/doctest.h>

#include "ai/combat.hpp"
#include "ai/components.hpp"
#include "ecs/world.hpp"
#include "jobs/job_scheduler.hpp"
#include "physics/bridge.hpp"
#include "physics/physics_world.hpp"

using namespace starforge::ai;
namespace jobs = starforge::jobs;
namespace spatial = starforge::spatial;
namespace ecs = starforge::ecs;
using namespace starforge::physics;

static const spatial::WorldPosition kCamera = *spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);

TEST_CASE("AI combat: a hostile in range takes damage and fires a cooldown shot")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto targetPos = spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(targetPos.has_value());

    const auto attacker = createNpcShip(world, *origin, 1);
    const auto target = createNpcShip(world, *targetPos, 0);
    attacker.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    target.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
    attacker.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});
    REQUIRE(attachBody(world, attacker, physicsWorld, kCamera, MotionType::Dynamic).isValid());
    REQUIRE(attachBody(world, target, physicsWorld, kCamera, MotionType::Dynamic).isValid());
    registerBodyCleanup(world, physicsWorld);

    auto& health = target.get_mut<Health>();
    health.maxHealth = 100.0f;
    health.currentHealth = 100.0f;

    const auto shot = tryFire(world, physicsWorld, attacker, target, kCamera);
    REQUIRE(shot.fired);
    REQUIRE(shot.damage == doctest::Approx(5.0f));
    CHECK(target.get<Health>().currentHealth == doctest::Approx(95.0f));
    CHECK(attacker.has<CombatFireEvent>());
    CHECK(target.has<DamageEvent>());

    // The second attempt is blocked by the cooldown.
    CHECK(!tryFire(world, physicsWorld, attacker, target, kCamera).fired);
    jobs::shutdownGlobalScheduler();
}

TEST_CASE("AI combat: same-faction ships do not fire and long range blocks")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto allyPos = spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    const auto farPos = spatial::WorldPosition::fromMeters(600.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(allyPos.has_value());
    REQUIRE(farPos.has_value());

    const auto attacker = createNpcShip(world, *origin, 1);
    const auto ally = createNpcShip(world, *allyPos, 1);
    const auto farHostile = createNpcShip(world, *farPos, 0);
    for (const auto entity : {attacker, ally, farHostile})
    {
        entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        REQUIRE(attachBody(world, entity, physicsWorld, kCamera, MotionType::Dynamic).isValid());
        entity.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});
    }
    registerBodyCleanup(world, physicsWorld);

    CHECK(!tryFire(world, physicsWorld, attacker, ally, kCamera).fired);
    CHECK(!tryFire(world, physicsWorld, attacker, farHostile, kCamera).fired);
    jobs::shutdownGlobalScheduler();
}

TEST_CASE("AI combat: a body between attacker and target blocks the shot")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto targetPos = spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    const auto blockerPos = spatial::WorldPosition::fromMeters(20.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(targetPos.has_value());
    REQUIRE(blockerPos.has_value());

    const auto attacker = createNpcShip(world, *origin, 1);
    const auto target = createNpcShip(world, *targetPos, 0);
    const auto blocker = world.createAsteroid(*blockerPos, {0.0, 0.0, 0.0});
    for (const auto entity : {attacker, target, blocker})
    {
        entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        REQUIRE(attachBody(world, entity, physicsWorld, kCamera, MotionType::Dynamic).isValid());
    }
    registerBodyCleanup(world, physicsWorld);
    attacker.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});

    CHECK(!tryFire(world, physicsWorld, attacker, target, kCamera).fired);
    jobs::shutdownGlobalScheduler();
}

TEST_CASE("AI combat: a large attacker hull does not block its own shots")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto targetPos = spatial::WorldPosition::fromMeters(60.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(targetPos.has_value());

    // Frigate-scale colliders (radius >> 1 m): the ray origin sits inside
    // both hulls, so a fixed forward offset would self-occlude.
    const auto attacker = createNpcShip(world, *origin, 1);
    const auto target = createNpcShip(world, *targetPos, 0);
    for (const auto entity : {attacker, target})
    {
        entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 10.0f});
        REQUIRE(attachBody(world, entity, physicsWorld, kCamera, MotionType::Dynamic).isValid());
    }
    registerBodyCleanup(world, physicsWorld);
    attacker.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});

    const auto shot = tryFire(world, physicsWorld, attacker, target, kCamera);
    REQUIRE(shot.fired);
    CHECK(target.get<Health>().currentHealth == doctest::Approx(95.0f));
    jobs::shutdownGlobalScheduler();
}

TEST_CASE("AI combat: dead targets and dead attackers never fire")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto targetPos = spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(targetPos.has_value());

    const auto attacker = createNpcShip(world, *origin, 1);
    const auto target = createNpcShip(world, *targetPos, 0);
    for (const auto entity : {attacker, target})
    {
        entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        REQUIRE(attachBody(world, entity, physicsWorld, kCamera, MotionType::Dynamic).isValid());
    }
    registerBodyCleanup(world, physicsWorld);
    attacker.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});

    auto& wreckHealth = target.get_mut<Health>();
    wreckHealth.currentHealth = 0.0f;
    CHECK(!tryFire(world, physicsWorld, attacker, target, kCamera).fired);
    CHECK(!target.has<DamageEvent>());

    auto& attackerHealth = attacker.get_mut<Health>();
    attackerHealth.currentHealth = 0.0f;
    wreckHealth.currentHealth = 100.0f;
    CHECK(!tryFire(world, physicsWorld, attacker, target, kCamera).fired);
    jobs::shutdownGlobalScheduler();
}

TEST_CASE("AI combat: same-tick hits accumulate and events clear at frame end")
{
    jobs::initGlobalScheduler();
    PhysicsWorld physicsWorld(PhysicsWorld::Config{});
    ecs::Ecs world;
    registerAiComponents(world);

    const auto origin = spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto leftPos = spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    const auto rightPos = spatial::WorldPosition::fromMeters(0.0, 40.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(leftPos.has_value());
    REQUIRE(rightPos.has_value());

    const auto target = createNpcShip(world, *origin, 0);
    const auto attackerA = createNpcShip(world, *leftPos, 1);
    const auto attackerB = createNpcShip(world, *rightPos, 1);
    for (const auto entity : {target, attackerA, attackerB})
    {
        entity.set<Collider>({.shape = Collider::Shape::Sphere, .radius = 0.5f});
        REQUIRE(attachBody(world, entity, physicsWorld, kCamera, MotionType::Dynamic).isValid());
        entity.set<Weapon>({500.0f, 5.0f, 1.0f, 0.0f});
    }
    registerBodyCleanup(world, physicsWorld);

    REQUIRE(tryFire(world, physicsWorld, attackerA, target, kCamera).fired);
    REQUIRE(tryFire(world, physicsWorld, attackerB, target, kCamera).fired);
    CHECK(target.get<Health>().currentHealth == doctest::Approx(90.0f));
    REQUIRE(target.has<DamageEvent>());
    // Both shooters' damage is preserved in the single-tick event.
    CHECK(target.get<DamageEvent>().damage == doctest::Approx(10.0f));

    clearCombatEvents(world);
    CHECK(!target.has<DamageEvent>());
    CHECK(!attackerA.has<CombatFireEvent>());
    jobs::shutdownGlobalScheduler();
}
