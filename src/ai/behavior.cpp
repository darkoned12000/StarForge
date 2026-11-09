#include "ai/behavior.hpp"

#include "ai/steering.hpp"
#include "spatial/camera_relative.hpp"

#include <array>
#include <cmath>
#include <span>

namespace starforge::ai
{

namespace
{

[[nodiscard]] double distanceTo(const spatial::WorldPosition& from, const spatial::WorldPosition& to) noexcept
{
    const math::Vec3d relative = spatial::toCameraRelative(to, from);
    return glm::length(relative);
}

void steerToward(flecs::entity entity, const spatial::WorldPosition& destination, double dt,
                 const AiAgent& agent, double arrivalRadius,
                 const std::vector<flecs::entity>& candidates)
{
    const auto& transform = entity.get<ecs::Transform>();

    // Stack buffer: steering is per-agent per-think, so a heap vector
    // here would allocate thousands of times per second at fleet scale.
    // Stations are excluded (docking ships must glide to the dock, not
    // be repelled by the station itself).
    std::array<math::Vec3d, 32> others {};
    const std::size_t otherCount = collectNeighbours(entity, candidates, transform, others);

    SteeringParams params = steeringParamsFor(agent, arrivalRadius);
    steerEntity(entity, destination, dt, params, std::span<const math::Vec3d>(others.data(), otherCount));
}

} // namespace

BehaviorState nextBehavior(BehaviorState current, const BehaviorSensors& sensors, double stateTimer,
                            const BehaviorParams& params, double targetLostTimer) noexcept
{
    // Low-health states hold until repaired to >= 0.5 health (recovery
    // hysteresis). Without this band a hurt ship with no enemies nearby
    // would bounce Evading -> Transit -> Evading every tick, since the
    // entry thresholds (0.25/0.1) are below the exit condition.
    if (current == BehaviorState::Evading)
    {
        if (sensors.healthRatio < 0.1)
        {
            return BehaviorState::Retreating;
        }
        if (sensors.healthRatio >= 0.5 && stateTimer >= params.evadeDurationSeconds)
        {
            // Healthy and the evasion has run its course: re-engage a
            // perceived hostile (bounded collision evasion) or resume
            // travel. Previously evasion at full health with a nearby
            // enemy fled forever and never re-engaged.
            if (sensors.hasPerceivedTarget && sensors.targetDistance <= params.engagementRadius)
            {
                return BehaviorState::Engaging;
            }
            if (!sensors.hasPerceivedTarget)
            {
                return sensors.hasDestination ? BehaviorState::Transit : BehaviorState::Patrol;
            }
        }
        return BehaviorState::Evading;
    }

    if (current == BehaviorState::Retreating)
    {
        if (sensors.healthRatio < 0.1)
        {
            return BehaviorState::Retreating;
        }
        if (sensors.healthRatio >= 0.5 && !sensors.hasPerceivedTarget &&
            stateTimer >= params.retreatDurationSeconds)
        {
            return sensors.hasDestination ? BehaviorState::Transit : BehaviorState::Patrol;
        }
        return BehaviorState::Retreating;
    }

    if (sensors.healthRatio < 0.1)
    {
        return BehaviorState::Retreating;
    }
    if (sensors.healthRatio < 0.25)
    {
        return BehaviorState::Evading;
    }

    switch (current)
    {
    case BehaviorState::Patrol:
        if (sensors.hasPerceivedTarget && sensors.targetDistance <= params.engagementRadius)
        {
            return BehaviorState::Engaging;
        }
        if (sensors.hasDestination)
        {
            return BehaviorState::Transit;
        }
        return BehaviorState::Patrol;

    case BehaviorState::Transit:
        if (sensors.hasPerceivedTarget && sensors.targetDistance <= params.engagementRadius)
        {
            return BehaviorState::Engaging;
        }
        if (sensors.destinationArrived)
        {
            return BehaviorState::Docking;
        }
        return BehaviorState::Transit;

    case BehaviorState::Engaging:
        if (sensors.collisionThreat)
        {
            return BehaviorState::Evading;
        }
        if (!sensors.hasPerceivedTarget)
        {
            // Time since the target was last perceived, not time in
            // state: a long fight must not disengage instantly the first
            // frame a target ducks behind cover.
            if (targetLostTimer >= params.lostTargetSeconds)
            {
                return sensors.hasDestination ? BehaviorState::Transit : BehaviorState::Patrol;
            }
            return BehaviorState::Engaging;
        }
        if (sensors.targetDistance > params.disengagementRadius)
        {
            return sensors.hasDestination ? BehaviorState::Transit : BehaviorState::Patrol;
        }
        return BehaviorState::Engaging;

    case BehaviorState::Docking:
        if (sensors.collisionThreat)
        {
            return BehaviorState::Evading;
        }
        if (!sensors.destinationArrived)
        {
            return BehaviorState::Docking;
        }
        if (stateTimer >= params.dockDwellSeconds)
        {
            return sensors.hasDestination ? BehaviorState::Transit : BehaviorState::Patrol;
        }
        return BehaviorState::Docking;

    case BehaviorState::Stationary:
    default:
        if (sensors.destinationArrived)
        {
            return BehaviorState::Docking;
        }
        if (sensors.hasPerceivedTarget)
        {
            return BehaviorState::Engaging;
        }
        if (sensors.hasDestination)
        {
            return BehaviorState::Transit;
        }
        return BehaviorState::Patrol;
    }
}

BehaviorState updateBehavior(ecs::Ecs& world, const ecs::SpatialIndex& index, flecs::entity entity, double dt,
                             const BehaviorParams& params)
{
    if (!entity.has<AiAgent>() || !entity.has<ecs::Transform>())
    {
        return BehaviorState::Stationary;
    }

    auto& agent = entity.get_mut<AiAgent>();
    const auto& transform = entity.get<ecs::Transform>();

    // Dead ships are wrecks: no thinking, no movement. M15 will add
    // destruction; until then the entity persists inertly.
    if (entity.has<Health>() && entity.get<Health>().currentHealth <= 0.0f)
    {
        agent.state = BehaviorState::Stationary;
        if (entity.has<ecs::Velocity>())
        {
            entity.set<ecs::Velocity>({{0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
        }
        return BehaviorState::Stationary;
    }

    std::uint32_t selfFaction = 0;
    if (entity.has<Faction>())
    {
        selfFaction = entity.get<Faction>().id;
    }

    BehaviorSensors sensors;
    float healthRatio = 1.0f;
    if (entity.has<Health>())
    {
        const auto health = entity.get<Health>();
        healthRatio = health.maxHealth > 0.0f ? health.currentHealth / health.maxHealth : 0.0f;
    }
    sensors.healthRatio = healthRatio;

    // One neighbourhood query per think, shared by perception and
    // separation (previously two allocations per think).
    const auto candidates = index.findNeighborhood(transform.position);

    const auto perceived = nearestHostile(world, candidates, entity, transform, agent, selfFaction);
    // Target stickiness: keep the current target unless the new nearest
    // hostile is clearly closer (under half the distance), so ships do
    // not flip-flop between two insurgents on every think.
    const bool currentTargetUsable =
        agent.target.is_alive() && agent.target.has<ecs::Transform>() &&
        !(agent.target.has<Health>() && agent.target.get<Health>().currentHealth <= 0.0f);
    if (perceived.has_value())
    {
        bool switchTarget = !currentTargetUsable;
        if (!switchTarget)
        {
            const math::Vec3d toCurrent =
                spatial::toCameraRelative(agent.target.get<ecs::Transform>().position, transform.position);
            const double currentDistSq = glm::dot(toCurrent, toCurrent);
            switchTarget = perceived->distanceSq < currentDistSq * 0.25;
        }
        if (switchTarget)
        {
            agent.target = perceived->entity;
        }
        agent.targetLostTimer = 0.0;
        sensors.hasTarget = true;
        sensors.hasPerceivedTarget = true;
        sensors.targetDistance = agent.target == perceived->entity
                                     ? perceived->distance
                                     : distanceTo(transform.position, agent.target.get<ecs::Transform>().position);
    }
    else if (currentTargetUsable)
    {
        // A 0-HP target is a wreck: drop it instead of circling it forever
        // (tryFire refuses to shoot wrecks, so holding one as a target
        // would otherwise permanently lock the agent in Engaging).
        sensors.hasTarget = true;
        sensors.targetDistance = distanceTo(transform.position, agent.target.get<ecs::Transform>().position);
    }
    else
    {
        agent.target = flecs::entity{};
    }

    sensors.hasDestination = entity.has<NavigationTarget>();
    if (sensors.hasDestination)
    {
        const auto& navigation = entity.get<NavigationTarget>();
        sensors.destinationArrived =
            distanceTo(transform.position, navigation.positionHint) <= navigation.arrivalRadius;
    }

    // CollisionEvent is latched into threatTimer by the scheduler's
    // per-tick pass: a throttled brain would otherwise see contentions
    // only on the exact tick they happen to think.
    sensors.collisionThreat = agent.threatTimer > 0.0;

    const BehaviorState previous = agent.state;
    const BehaviorState next =
        nextBehavior(previous, sensors, agent.stateTimer, params, agent.targetLostTimer);
    agent.state = next;
    if (next != previous)
    {
        agent.stateTimer = 0.0;
    }
    // stateTimer/targetLostTimer are advanced once per tick by the
    // scheduler's fixed pass (see scheduler.cpp); updateBehavior only
    // resets them here so FSM dwell rules run at real simulation speed.

    // Steering: each state has one deterministic destination rule. Routed
    // ships skip steering entirely — advanceRoute is the single owner of
    // route-following motion (previously both systems wrote Velocity
    // every tick on think frames and route won/lost arbitrarily).
    const bool routed = entity.has<NavigationRoute>();
    spatial::WorldPosition destination;
    bool steer = false;
    double arrivalRadius = 50.0;
    switch (next)
    {
    case BehaviorState::Engaging:
        if (agent.target.is_alive() && agent.target.has<ecs::Transform>())
        {
            destination = agent.target.get<ecs::Transform>().position;
            arrivalRadius = params.engagementRadius * 0.1;
            steer = true;
        }
        break;
    case BehaviorState::Evading:
        if (agent.target.is_alive() && agent.target.has<ecs::Transform>())
        {
            const auto targetPos = agent.target.get<ecs::Transform>().position;
            const auto away = spatial::toCameraRelative(transform.position, targetPos);
            const double awayLength = glm::length(away);
            if (awayLength > 1.0e-6)
            {
                // Full-throttle flee: the destination is placed far along
                // the away direction with no arrival braking. The old
                // 2x-distance destination with a 50 m arrival radius
                // braked to a stop whenever the attacker was < 25 m away.
                destination = transform.position + away * (1000.0 / awayLength);
                arrivalRadius = 0.0;
                steer = true;
            }
        }
        else if (entity.has<NavigationTarget>())
        {
            destination = entity.get<NavigationTarget>().positionHint;
            arrivalRadius = entity.get<NavigationTarget>().arrivalRadius;
            steer = true;
        }
        break;
    case BehaviorState::Retreating:
        // Retreat flees like Evading when there is no destination to
        // fall back to; previously a destination-less retreat sat still
        // in weapons range.
        if (agent.target.is_alive() && agent.target.has<ecs::Transform>())
        {
            const auto targetPos = agent.target.get<ecs::Transform>().position;
            const auto away = spatial::toCameraRelative(transform.position, targetPos);
            const double awayLength = glm::length(away);
            if (awayLength > 1.0e-6)
            {
                destination = transform.position + away * (1000.0 / awayLength);
                arrivalRadius = 0.0;
                steer = true;
                break;
            }
        }
        [[fallthrough]];
    case BehaviorState::Patrol:
    case BehaviorState::Transit:
    case BehaviorState::Docking:
        if (entity.has<NavigationTarget>())
        {
            destination = entity.get<NavigationTarget>().positionHint;
            arrivalRadius = entity.get<NavigationTarget>().arrivalRadius;
            steer = true;
        }
        break;
    case BehaviorState::Stationary:
        steer = false;
        break;
    }

    if (steer && !routed)
    {
        steerToward(entity, destination, dt, agent, arrivalRadius, candidates);
    }
    else if (!steer && !routed && entity.has<ecs::Velocity>())
    {
        entity.set<ecs::Velocity>({{0.0, 0.0, 0.0}, {0.0f, 0.0f, 0.0f}});
    }

    return next;
}

} // namespace starforge::ai
