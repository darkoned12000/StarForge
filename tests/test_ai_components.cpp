#include <doctest/doctest.h>

#include "ai/components.hpp"
#include "ecs/world.hpp"

#include <vector>

using namespace starforge::ai;
using namespace starforge::ecs;
using namespace starforge::spatial;

TEST_CASE("AI components: 50 NPC ships register with faction and agent state")
{
    Ecs world;
    registerAiComponents(world);

    std::vector<flecs::entity> ships;
    for (std::uint32_t i = 0; i < 50; ++i)
    {
        auto position = starforge::spatial::WorldPosition::fromMeters(static_cast<double>(i) * 10.0, 0.0, 0.0);
        REQUIRE(position.has_value());
        ships.push_back(createNpcShip(world, *position, i % 3));
    }

    REQUIRE(world.entityCount() == 50);
    REQUIRE(ships.size() == 50);

    for (const auto ship : ships)
    {
        CHECK(ship.has<starforge::ecs::ShipTag>());
        CHECK(ship.has<AiAgent>());
        CHECK(ship.has<Faction>());
        const auto& agent = ship.get<AiAgent>();
        CHECK(agent.perceptionRadius > 0.0f);
        CHECK(agent.maxSpeed > 0.0f);
        CHECK(agent.maxAcceleration > 0.0f);
    }

    const auto firstFaction = ships.front().get<Faction>();
    CHECK(firstFaction.id == 0);
}
