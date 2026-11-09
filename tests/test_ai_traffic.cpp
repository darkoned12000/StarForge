#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ai/traffic.hpp"
#include "ecs/world.hpp"

#include <cstdint>
#include <set>

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("AI traffic: route slots and lane points are deterministic")
{
    Ecs world;
    registerAiComponents(world);

    const auto stationPos = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto from = starforge::spatial::WorldPosition::fromMeters(-150.0, 0.0, 0.0);
    REQUIRE(stationPos.has_value());
    REQUIRE(from.has_value());

    const auto station = world.createStation(*stationPos);
    const auto shipA = createNpcShip(world, *from, 1);
    const auto shipB = createNpcShip(world, *from, 1);

    TrafficManager traffic;
    const auto slot0 = traffic.reserveDockSlot(station, shipA, 2);
    const auto slot1 = traffic.reserveDockSlot(station, shipB, 2);
    REQUIRE(slot0.has_value());
    REQUIRE(slot1.has_value());
    CHECK(traffic.reserveDockSlot(station, shipA, 2) == std::nullopt);

    const auto route0 = routeToStation(*from, *stationPos, slot0.value());
    const auto route1 = routeToStation(*from, *stationPos, slot1.value());
    REQUIRE(route0.size() == 3);
    REQUIRE(route1.size() == route0.size());
    CHECK(route0.front().position.offsetX == doctest::Approx(0.0));
    CHECK(route1.front().position.offsetX == doctest::Approx(25.0));
}

TEST_CASE("AI traffic: a ship follows its route to the station")
{
    Ecs world;
    registerAiComponents(world);

    const auto origin = starforge::spatial::WorldPosition::fromMeters(0.0, 0.0, 0.0);
    const auto stationPos = starforge::spatial::WorldPosition::fromMeters(150.0, 0.0, 0.0);
    REQUIRE(origin.has_value());
    REQUIRE(stationPos.has_value());

    const auto ship = createNpcShip(world, *origin, 1);
    auto& agent = ship.get_mut<AiAgent>();
    agent.maxSpeed = 100.0f;
    agent.maxAcceleration = 200.0f;
    assignRoute(ship, *origin, *stationPos, 0);

    SpatialIndex index(world);
    bool complete = false;
    for (int frame = 0; frame < 20 * 60 && !complete; ++frame)
    {
        index.rebuild();
        complete = advanceRoute(world, ship, 1.0 / 60.0, index);
        world.step(1.0f / 60.0f);
    }

    REQUIRE(complete);
    const auto& transform = ship.get<starforge::ecs::Transform>();
    CHECK(transform.position.toMeters().x == doctest::Approx(150.0).epsilon(0.2));
    CHECK(transform.position.toMeters().y == doctest::Approx(0.0));
}

TEST_CASE("AI traffic: multiple ships reserve distinct slots and arrive")
{
    Ecs world;
    registerAiComponents(world);

    const auto stationPos = starforge::spatial::WorldPosition::fromMeters(150.0, 0.0, 0.0);
    REQUIRE(stationPos.has_value());
    const auto station = world.createStation(*stationPos);

    TrafficManager traffic;
    std::vector<flecs::entity> ships;
    for (std::int32_t lane = -1; lane <= 1; ++lane)
    {
        const auto start = starforge::spatial::WorldPosition::fromMeters(0.0, static_cast<double>(lane) * 25.0, 0.0);
        REQUIRE(start.has_value());
        const auto ship = createNpcShip(world, *start, 1);
        auto& agent = ship.get_mut<AiAgent>();
        agent.maxSpeed = 100.0f;
        agent.maxAcceleration = 200.0f;
        const auto slot = traffic.reserveDockSlot(station, ship, 4);
        REQUIRE(slot.has_value());
        assignRoute(ship, *start, *stationPos, slot.value(), station);
        REQUIRE(ship.has<NavigationRoute>());
        ships.push_back(ship);
    }
    REQUIRE(ships.size() == 3);

    SpatialIndex index(world);
    for (int frame = 0; frame < 20 * 60; ++frame)
    {
        index.rebuild();
        for (const auto ship : ships)
        {
            (void)advanceRoute(world, ship, 1.0 / 60.0, index);
        }
        world.step(1.0f / 60.0f);
    }

    for (std::size_t i = 0; i < ships.size(); ++i)
    {
        const auto ship = ships[i];
        const auto route = ship.get<NavigationRoute>();
        CHECK(route.complete);
        const auto position = ship.get<starforge::ecs::Transform>().position.toMeters();
        CHECK(position.x == doctest::Approx(150.0 + static_cast<double>(i) * 25.0).epsilon(0.2));
        CHECK(position.y == doctest::Approx(0.0).epsilon(0.01));
    }
}
