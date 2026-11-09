#include "ai/components.hpp"

namespace starforge::ai
{

void registerAiComponents(ecs::Ecs& world)
{
    auto& flecsWorld = world.world();
    flecsWorld.component<AiAgent>("AiAgent");
    flecsWorld.component<Faction>("Faction");
    flecsWorld.component<Health>("Health");
    flecsWorld.component<Weapon>("Weapon");
    flecsWorld.component<DamageEvent>("DamageEvent");
    flecsWorld.component<CombatFireEvent>("CombatFireEvent");
    flecsWorld.component<NavigationTarget>("NavigationTarget");
    flecsWorld.component<NavigationRoute>("NavigationRoute");
}

flecs::entity createNpcShip(ecs::Ecs& world, const spatial::WorldPosition& position,
                            std::uint32_t factionId, const AiAgent& agent)
{
    NpcShipConfig config;
    config.agent = agent;
    return createNpcShip(world, position, factionId, config);
}

flecs::entity createNpcShip(ecs::Ecs& world, const spatial::WorldPosition& position,
                            std::uint32_t factionId, const NpcShipConfig& config)
{
    const auto entity = world.createShip(position);
    entity.set<Faction>({factionId});
    entity.set<AiAgent>(config.agent);
    entity.set<Health>(config.health);
    entity.set<Weapon>(config.weapon);
    return entity;
}

} // namespace starforge::ai
