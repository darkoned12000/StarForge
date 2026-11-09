#include "ai/perception.hpp"

#include "spatial/camera_relative.hpp"

#include <cmath>

namespace starforge::ai
{

FactionRelation classifyFaction(std::uint32_t selfFaction, std::uint32_t otherFaction) noexcept
{
    if (selfFaction == otherFaction)
    {
        return FactionRelation::Same;
    }
    if (selfFaction == 0 || otherFaction == 0)
    {
        return FactionRelation::Hostile;
    }
    return FactionRelation::Neutral;
}

std::optional<PerceivedTarget> nearestHostile([[maybe_unused]] ecs::Ecs& world, const ecs::SpatialIndex& index,
                                              flecs::entity agentEntity,
                                              const ecs::Transform& agentTransform,
                                              const AiAgent& agent, std::uint32_t selfFaction)
{
    // The index is bucketed by WorldPosition cell; local flight areas are
    // tiny compared to a 2^20 m cell, so the current and neighbouring cells
    // cover any realistic perception radius for Phase A targets.
    const auto candidates = index.findNeighborhood(agentTransform.position);
    std::optional<PerceivedTarget> best;

    const double radiusSq = static_cast<double>(agent.perceptionRadius) * static_cast<double>(agent.perceptionRadius);
    for (const auto entity : candidates)
    {
        if (entity == agentEntity || !entity.is_alive() || !entity.has<ecs::ShipTag>() ||
            !entity.has<ecs::Transform>() || !entity.has<Faction>())
        {
            continue;
        }

        // 0-HP ships are wrecks, not threats: never lock onto them or the
        // agent circles a corpse that tryFire correctly refuses to shoot.
        if (entity.has<Health>() && entity.get<Health>().currentHealth <= 0.0f)
        {
            continue;
        }

        const math::Vec3d relative =
            spatial::toCameraRelative(entity.get<ecs::Transform>().position, agentTransform.position);
        const double distSq = glm::dot(relative, relative);
        if (distSq > radiusSq)
        {
            continue;
        }

        const std::uint32_t candidateFaction = entity.get<Faction>().id;

        if (classifyFaction(selfFaction, candidateFaction) != FactionRelation::Hostile)
        {
            continue;
        }

        const double distance = std::sqrt(distSq);
        if (!best.has_value() || distSq < best->distance * best->distance)
        {
            best = PerceivedTarget{entity, candidateFaction, distance};
        }
    }

    return best;
}

} // namespace starforge::ai
