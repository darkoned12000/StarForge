#include "ai/scheduler.hpp"

#include <algorithm>

namespace starforge::ai
{

namespace
{

// Upper bound on how long a cached agent snapshot may go unverified. Even
// in the pathological case where balanced create/destroy pairs leave the
// total entity count unchanged (defeating the count check below), the
// snapshot is rebuilt at least once per second of ticks.
constexpr std::uint32_t kSnapshotRebuildIntervalTicks = 60;

// How long a latched collision threat stays visible to a throttled brain.
constexpr double kThreatWindowSeconds = 0.5;

void rebuildSnapshot(ecs::Ecs& world, AiThinkSchedule& schedule)
{
    auto& agents = schedule.agentBuffer;
    agents.clear();
    world.world().query<AiAgent, ecs::Transform>().each(
        [&agents](flecs::entity entity, const AiAgent&, const ecs::Transform&) {
            agents.push_back(entity);
        });
    std::sort(agents.begin(), agents.end(),
              [](flecs::entity lhs, flecs::entity rhs) { return lhs.id() < rhs.id(); });
    schedule.snapshotValid = true;
    schedule.observedEntityCount = world.entityCount();
    schedule.ticksSinceRebuild = 0;
    // Resume after the last processed agent BY ID, so rebuild churn
    // (projectile/debris spawns changing entityCount) cannot reset
    // round-robin fairness for high-id agents.
    schedule.cursor = static_cast<std::size_t>(
        std::lower_bound(agents.begin(), agents.end(), schedule.lastProcessedId,
                         [](flecs::entity lhs, std::uint64_t rhs) { return lhs.id() < rhs; }) -
        agents.begin());
}

[[nodiscard]] bool snapshotIntact(const AiThinkSchedule& schedule) noexcept
{
    for (const auto entity : schedule.agentBuffer)
    {
        if (!entity.is_alive() || !entity.has<AiAgent>())
        {
            return false;
        }
    }
    return true;
}

} // namespace

void tickAiThink(ecs::Ecs& world, const ecs::SpatialIndex& index, double dt, AiThinkSchedule& schedule,
                 const BehaviorParams& params)
{
    if (dt <= 0.0 || schedule.maxAgentsPerTick == 0)
    {
        return;
    }

    // Fixed pass: advance every agent's simulation timers once per tick.
    // thinkTimer counts down to the next think; stateTimer/targetLostTimer
    // count up so FSM dwell/lost-target rules run at real simulation
    // speed even though updateBehavior only runs on think ticks.
    // CollisionEvent is latched into threatTimer so a throttled brain
    // still sees contentions that happened between its thinks.
    world.world().query<AiAgent>().each([dt](flecs::entity entity, AiAgent& agent) {
        if (agent.thinkTimer > 0.0)
        {
            agent.thinkTimer -= dt;
        }
        agent.stateTimer += dt;
        agent.targetLostTimer += dt;
        if (entity.has<ecs::CollisionEvent>())
        {
            agent.threatTimer = kThreatWindowSeconds;
        }
        else if (agent.threatTimer > 0.0)
        {
            agent.threatTimer -= dt;
        }
    });

    // Reuse the cached sorted snapshot unless the entity population
    // changed (adds/removes), a cached entry died or lost its AiAgent
    // component, or the periodic backstop fired. Steady-state ticks pay
    // only an O(N) validity scan — no allocation and no sort.
    const bool populationChanged = schedule.snapshotValid && world.entityCount() != schedule.observedEntityCount;
    if (!schedule.snapshotValid || populationChanged ||
        schedule.ticksSinceRebuild >= kSnapshotRebuildIntervalTicks || !snapshotIntact(schedule))
    {
        rebuildSnapshot(world, schedule);
    }
    else
    {
        ++schedule.ticksSinceRebuild;
    }

    const std::size_t count = schedule.agentBuffer.size();
    if (count == 0)
    {
        return;
    }
    // Capture the start cursor: schedule.cursor is updated as agents are
    // processed and must not feed back into index computation mid-loop
    // (that skips entries — found by the snapshot invalidation test).
    const std::size_t start = schedule.cursor % count;
    schedule.cursor = start;
    std::uint32_t processed = 0;
    // Round-robin: resume where the last tick stopped so every agent gets
    // a turn proportional to its share, even when the due set exceeds the
    // budget × think-interval capacity. Leftover due agents keep their
    // expired timer and are picked up on later ticks (no starvation).
    for (std::size_t offset = 0; offset < count && processed < schedule.maxAgentsPerTick; ++offset)
    {
        const std::size_t agentIndex = (start + offset) % count;
        const auto entity = schedule.agentBuffer[agentIndex];
        if (!entity.has<AiAgent>() || entity.get<AiAgent>().thinkTimer > 0.0)
        {
            continue;
        }

        // Steering integrates over the time since the agent's last think,
        // not the single tick dt: with a 0.1 s think interval, passing
        // 1/60 would run acceleration and turning ~6x slower than
        // configured.
        const double thinkDt = schedule.thinkIntervalSeconds - entity.get<AiAgent>().thinkTimer;
        updateBehavior(world, index, entity, thinkDt, params);
        auto& agent = entity.get_mut<AiAgent>();
        agent.thinkTimer += schedule.thinkIntervalSeconds;
        schedule.cursor = (agentIndex + 1) % count;
        schedule.lastProcessedId = entity.id();
        ++processed;
    }
}

} // namespace starforge::ai
