#pragma once

#include "ai/components.hpp"
#include "ecs/spatial_index.hpp"

#include <cstdint>
#include <optional>

namespace starforge::ai
{

// Inter-faction stance. Same-faction entities are compatible, pirates
// (faction 0) are hostile to everyone else, and all other distinct
// factions remain neutral until the Phase C diplomacy layer adds a table.
enum class FactionRelation
{
    Same,
    Allies,
    Neutral,
    Hostile,
};

[[nodiscard]] FactionRelation classifyFaction(std::uint32_t selfFaction, std::uint32_t otherFaction) noexcept;

struct PerceivedTarget
{
    flecs::entity entity {};
    std::uint32_t factionId = 0;
    double distance = 0.0;
};

// Nearest hostile ship claimed from the fixed spatial-index neighbourhood.
// Returns nothing when no hostile ship is within perceptionRadius.
[[nodiscard]] std::optional<PerceivedTarget> nearestHostile(ecs::Ecs& world, const ecs::SpatialIndex& index,
                                                             flecs::entity agentEntity,
                                                             const ecs::Transform& agentTransform,
                                                             const AiAgent& agent, std::uint32_t selfFaction);

} // namespace starforge::ai
