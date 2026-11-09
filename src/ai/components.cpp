#include "ai/components.hpp"

namespace starforge::ai
{

void registerAiComponents(ecs::Ecs& world)
{
    auto& flecsWorld = world.world();
    flecsWorld.component<AiAgent>("AiAgent");
    flecsWorld.component<Faction>("Faction");
    flecsWorld.component<Health>("Health");
    flecsWorld.component<NavigationTarget>("NavigationTarget");
    flecsWorld.component<NavigationRoute>("NavigationRoute");
}

flecs::entity createNpcShip(ecs::Ecs& world, const spatial::WorldPosition& position,
                            std::uint32_t factionId, const AiAgent& agent)
{
    const auto entity = world.createShip(position);
    entity.set<Faction>({factionId});
    entity.set<AiAgent>(agent);
    entity.set<Health>({100.0f, 100.0f});
    return entity;
}

} // namespace starforge::ai
