#include <doctest/doctest.h>

#include "ai/behavior.hpp"
#include "ai/components.hpp"
#include "ai/perception.hpp"
#include "ecs/world.hpp"

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("AI behavior: finite-state transitions honor hysteresis")
{
    BehaviorParams params;
    BehaviorSensors sensors;

    CHECK(nextBehavior(BehaviorState::Patrol, {.hasDestination = true}, 0.0, params, 0.0) == BehaviorState::Transit);

    sensors.hasDestination = true;
    sensors.destinationArrived = true;
    CHECK(nextBehavior(BehaviorState::Transit, sensors, 0.0, params, 0.0) == BehaviorState::Docking);

    sensors.hasPerceivedTarget = true;
    sensors.targetDistance = 100.0;
    CHECK(nextBehavior(BehaviorState::Transit, sensors, 0.0, params, 0.0) == BehaviorState::Engaging);

    sensors.targetDistance = 3000.0;
    sensors.hasPerceivedTarget = false;
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, params.lostTargetSeconds, params, params.lostTargetSeconds) == BehaviorState::Transit);

    // Recovery hysteresis: at 0.2 health an evading ship must NOT stop
    // evading just because the attacker left perception — it holds
    // Evading until repaired to >= 0.5 health. (The old exit rule made
    // hurt ships bounce Evading -> Transit -> Evading every tick.)
    CHECK(nextBehavior(BehaviorState::Evading, {.hasDestination = true, .hasPerceivedTarget = false, .healthRatio = 0.2},
                       params.evadeDurationSeconds, params, 0.0) == BehaviorState::Evading);
    CHECK(nextBehavior(BehaviorState::Evading, {.hasDestination = true, .hasPerceivedTarget = false, .healthRatio = 0.6},
                       params.evadeDurationSeconds, params, 0.0) == BehaviorState::Transit);

    sensors.hasPerceivedTarget = true;
    sensors.targetDistance = 100.0;
    sensors.healthRatio = 0.2;
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, 0.0, params, 0.0) == BehaviorState::Evading);

    sensors.healthRatio = 0.05;
    CHECK(nextBehavior(BehaviorState::Evading, sensors, 0.0, params, 0.0) == BehaviorState::Retreating);

    // Lost-target timing uses targetLostTimer (time since last seen), so
    // a long fight does not disengage the first frame a target hides.
    sensors.healthRatio = 1.0;
    sensors.hasPerceivedTarget = false;
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, 0.0, params, 0.0) == BehaviorState::Engaging);
    // Long fight (stateTimer) but target freshly lost: stay engaged.
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, params.lostTargetSeconds, params, 0.0)
          == BehaviorState::Engaging);
    // Target gone for lostTargetSeconds: disengage.
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, params.lostTargetSeconds, params, params.lostTargetSeconds)
          == BehaviorState::Transit);

    // Bounded collision evasion: healthy evasion re-engages after its
    // duration instead of fleeing forever while a hostile is nearby.
    sensors.hasPerceivedTarget = true;
    sensors.targetDistance = 100.0;
    CHECK(nextBehavior(BehaviorState::Evading, sensors, params.evadeDurationSeconds, params, 0.0)
          == BehaviorState::Engaging);
    CHECK(nextBehavior(BehaviorState::Evading, sensors, 0.0, params, 0.0) == BehaviorState::Evading);

    // Docking under a collision threat breaks off to Evading.
    sensors.collisionThreat = true;
    sensors.destinationArrived = true;
    CHECK(nextBehavior(BehaviorState::Docking, sensors, 0.0, params, 0.0) == BehaviorState::Evading);
}

TEST_CASE("AI behavior: hostile perception triggers engagement")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    const auto agentShip = createNpcShip(world, *origin, 1);
    const auto hostileShip = createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    const BehaviorState state = updateBehavior(world, index, agentShip, 1.0 / 60.0, BehaviorParams{});
    CHECK(state == BehaviorState::Engaging);
    CHECK(agentShip.get<AiAgent>().target == hostileShip);
}

TEST_CASE("AI behavior: destination arrival docks and low health evades")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto stationPos = starforge::spatial::WorldPosition::fromMeters(10.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(stationPos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    auto& agent = ship.get_mut<AiAgent>();
    auto& health = ship.get_mut<Health>();
    agent.state = BehaviorState::Transit;
    health.maxHealth = 100.0f;
    health.currentHealth = 20.0f;
    NavigationTarget navigation;
    navigation.target = createNpcShip(world, *stationPos, 1);
    navigation.positionHint = *stationPos;
    navigation.arrivalRadius = 20.0f;
    ship.set<NavigationTarget>(navigation);

    SpatialIndex index(world);
    index.rebuild();

    const BehaviorState state = updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{});
    CHECK(state == BehaviorState::Evading);
}

TEST_CASE("AI behavior: evasion flees at full throttle even under 25 m")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(20.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    auto& agent = ship.get_mut<AiAgent>();
    agent.state = BehaviorState::Transit;
    agent.maxSpeed = 100.0f;
    agent.maxAcceleration = 10000.0f;
    auto& health = ship.get_mut<Health>();
    health.currentHealth = 20.0f; // < 0.25: evade
    const auto hostile = createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    CHECK(updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{}) == BehaviorState::Evading);
    REQUIRE(ship.has<starforge::ecs::Velocity>());
    const auto velocity = ship.get<starforge::ecs::Velocity>().linear;
    // Fleeing away from the attacker (+X), not braked to a stop by the
    // arrival radius.
    CHECK(velocity.x < -1.0);
    // Re-read through the entity: updateBehavior adds a Velocity
    // component, which may move the entity to a new table and
    // invalidate the earlier component reference.
    CHECK(ship.get<AiAgent>().target == hostile);
}

TEST_CASE("AI behavior: perception ignores 0-HP wrecks")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto wreckPos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    const auto threatPos = starforge::spatial::WorldPosition::fromMeters(80.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(wreckPos.has_value());
    REQUIRE(threatPos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    const auto wreck = createNpcShip(world, *wreckPos, 0);
    const auto threat = createNpcShip(world, *threatPos, 0);
    wreck.get_mut<Health>().currentHealth = 0.0f;

    SpatialIndex index(world);
    index.rebuild();

    CHECK(updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{}) == BehaviorState::Engaging);
    // The nearest ship is the wreck at 40 m, but the live threat at 80 m
    // must be acquired instead.
    CHECK(ship.get<AiAgent>().target == threat);
}

TEST_CASE("AI behavior: target stickiness resists flip-flopping")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto nearPos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    const auto farPos = starforge::spatial::WorldPosition::fromMeters(0.0, 60.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(nearPos.has_value());
    REQUIRE(farPos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    (void)createNpcShip(world, *nearPos, 0); // 40 m
    const auto hostileB = createNpcShip(world, *farPos, 0);  // 60 m
    auto& agent = ship.get_mut<AiAgent>();
    agent.target = hostileB;

    SpatialIndex index(world);
    index.rebuild();

    // Nearest is A at 40 m, but the current target B (60 m) is not less
    // than 2x farther, so the ship keeps B instead of switching every think.
    (void)updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{});
    CHECK(ship.get<AiAgent>().target == hostileB);
    CHECK(ship.get<AiAgent>().state == BehaviorState::Engaging);
}

TEST_CASE("AI behavior: a 0-HP ship is inert (no thinking, no movement)")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    auto& health = ship.get_mut<Health>();
    health.currentHealth = 0.0f;
    ship.set<starforge::ecs::Velocity>({starforge::math::Vec3d{10.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
    (void)createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    CHECK(updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{}) == BehaviorState::Stationary);
    CHECK(ship.get<starforge::ecs::Velocity>().linear.x == doctest::Approx(0.0));
    CHECK(ship.get<AiAgent>().target == flecs::entity{});
}

TEST_CASE("AI behavior: idle agent does not steer toward world origin")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(1.0e12, 0.0, 0.0);
    REQUIRE(origin.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    ship.set<starforge::ecs::Velocity>({starforge::math::Vec3d{10.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});


    SpatialIndex index(world);
    index.rebuild();

    const BehaviorState state = updateBehavior(world, index, ship, 1.0 / 60.0, BehaviorParams{});
    CHECK(state == BehaviorState::Patrol);
    CHECK(ship.get<starforge::ecs::Velocity>().linear.x == doctest::Approx(0.0));
}
