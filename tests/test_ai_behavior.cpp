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

    CHECK(nextBehavior(BehaviorState::Patrol, {.hasDestination = true}, 0.0, params) == BehaviorState::Transit);

    sensors.hasDestination = true;
    sensors.destinationArrived = true;
    CHECK(nextBehavior(BehaviorState::Transit, sensors, 0.0, params) == BehaviorState::Docking);

    sensors.hasPerceivedTarget = true;
    sensors.targetDistance = 100.0;
    CHECK(nextBehavior(BehaviorState::Transit, sensors, 0.0, params) == BehaviorState::Engaging);

    sensors.targetDistance = 3000.0;
    sensors.hasPerceivedTarget = false;
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, params.lostTargetSeconds, params) == BehaviorState::Transit);

    CHECK(nextBehavior(BehaviorState::Evading, {.hasDestination = true, .hasPerceivedTarget = false, .healthRatio = 0.2},
                       params.evadeDurationSeconds, params) == BehaviorState::Transit);

    sensors.hasPerceivedTarget = true;
    sensors.targetDistance = 100.0;
    sensors.healthRatio = 0.2;
    CHECK(nextBehavior(BehaviorState::Engaging, sensors, 0.0, params) == BehaviorState::Evading);

    sensors.healthRatio = 0.05;
    CHECK(nextBehavior(BehaviorState::Evading, sensors, 0.0, params) == BehaviorState::Retreating);
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
