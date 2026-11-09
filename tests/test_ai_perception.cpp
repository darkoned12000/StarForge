#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ai/perception.hpp"
#include "ecs/world.hpp"

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("AI perception: faction classification is deterministic")
{
    CHECK(classifyFaction(1, 1) == FactionRelation::Same);
    CHECK(classifyFaction(0, 2) == FactionRelation::Hostile);
    CHECK(classifyFaction(1, 2) == FactionRelation::Neutral);
}

TEST_CASE("AI perception: nearest hostile in the spatial neighbourhood")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto friendlyPos = starforge::spatial::WorldPosition::fromMeters(35.0, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(40.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(friendlyPos.has_value());
    REQUIRE(hostilePos.has_value());

    const auto agentShip = createNpcShip(world, *origin, 1);
    (void)createNpcShip(world, *friendlyPos, 1);
    const auto hostileShip = createNpcShip(world, *hostilePos, 0);
    const auto farHostilePos = starforge::spatial::WorldPosition::fromMeters(90.0, 0.0, 0.0);
    REQUIRE(farHostilePos.has_value());
    (void)createNpcShip(world, *farHostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiAgent agent;
    agent.perceptionRadius = 100.0f;
    const auto target = nearestHostile(world, index.findNeighborhood(agentShip.get<starforge::ecs::Transform>().position), agentShip, agentShip.get<starforge::ecs::Transform>(), agent, 1);
    REQUIRE(target.has_value());
    CHECK(target->entity == hostileShip);
    CHECK(target->distance == doctest::Approx(40.0));
}

TEST_CASE("AI perception: works at interstellar range through camera-relative offsets")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(1.0e12, 0.0, 0.0);
    const auto hostilePos = starforge::spatial::WorldPosition::fromMeters(1.0e12 + 25.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(hostilePos.has_value());

    const auto agentShip = createNpcShip(world, *origin, 1);
    const auto hostileShip = createNpcShip(world, *hostilePos, 0);

    SpatialIndex index(world);
    index.rebuild();

    AiAgent agent;
    agent.perceptionRadius = 100.0f;
    const auto target = nearestHostile(world, index.findNeighborhood(agentShip.get<starforge::ecs::Transform>().position), agentShip, agentShip.get<starforge::ecs::Transform>(), agent, 1);
    REQUIRE(target.has_value());
    CHECK(target->entity == hostileShip);
    CHECK(target->distance == doctest::Approx(25.0));
}
