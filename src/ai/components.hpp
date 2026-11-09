#pragma once

#include "ecs/components.hpp"
#include "ecs/world.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>

namespace starforge::ai
{

// Behavior states are assigned in Phase B (14.4). They are declared now so
// the component layout can remain stable while steering and perception land.
enum class BehaviorState
{
    Patrol,
    Transit,
    Docking,
    Engaging,
    Evading,
    Retreating,
    Stationary,
};

// Fixed-size AI behavior data. All values are simulation units (metres,
// metres/second) and are advanced only by the fixed-timestep system in
// later phases; no wall-clock time is stored here.
struct AiAgent
{
    BehaviorState state = BehaviorState::Patrol;
    flecs::entity target {};
    // Simulation seconds until the next perception/brain update. Phase C
    // converts this into the staggered think schedule.
    double thinkTimer = 0.0;
    double stateTimer = 0.0;
    float perceptionRadius = 1000.0f;
    float maxSpeed = 20.0f;
    float maxAcceleration = 10.0f;
    float turnRate = 1.0f;
};

// Faction membership for the perception/decision systems. Stations, ships,
// and future defenders carry this component; faction policy lives in the
// FactionRelation helper rather than in this data component.
struct Faction
{
    std::uint32_t id = 0;
};

// Navigation destination. The entity points at the destination (station,
// dock marker, or target entity); positionHint preserves that destination
// if the target has been destroyed or unloaded.
struct NavigationTarget
{
    flecs::entity target {};
    spatial::WorldPosition positionHint {};
    float arrivalRadius = 50.0f;
};

// Registers the AI component types with a world. Call once per Ecs.
void registerAiComponents(ecs::Ecs& world);

// Convenience factory for NPC/neutral ship entities used by tests and the
// gameplay layer. The entity is a normal ECS ship plus the AI components.
[[nodiscard]] flecs::entity createNpcShip(ecs::Ecs& world, const spatial::WorldPosition& position,
                                           std::uint32_t factionId = 0, const AiAgent& agent = {});

} // namespace starforge::ai
