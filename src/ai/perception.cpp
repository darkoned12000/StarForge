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

std::optional<PerceivedTarget> nearestHostile([[maybe_unused]] ecs::Ecs& world,
                                               const std::vector<flecs::entity>& candidates,
                                               flecs::entity agentEntity,
                                               const ecs::Transform& agentTransform,
                                               const AiAgent& agent, std::uint32_t selfFaction)
{
    std::optional<PerceivedTarget> best;
    double bestDistanceSq = 0.0;

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

        if (!best.has_value() || distSq < bestDistanceSq)
        {
            best = PerceivedTarget{entity, candidateFaction, std::sqrt(distSq), distSq};
            bestDistanceSq = distSq;
        }
    }

    return best;
}

} // namespace starforge::ai
