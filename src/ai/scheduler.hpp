#pragma once

#include "ai/behavior.hpp"
#include "ecs/world.hpp"

#include <cstdint>
#include <vector>

namespace starforge::ai
{

// Per-tick AI think schedule. Each agent's AiAgent::thinkTimer counts down
// every tick; agents whose timer has expired are "due". Due agents are
// processed in deterministic entity-id order up to maxAgentsPerTick per
// tick, and any remainder stay due for later ticks — they are never
// dropped, so no agent can starve. Once an agent thinks, thinkTimer is
// incremented by thinkIntervalSeconds (not reset) so its cadence phase
// is preserved across thinks instead of drifting.
struct AiThinkSchedule
{
    std::uint32_t maxAgentsPerTick = 12;
    double thinkIntervalSeconds = 0.1;

    // Cached sorted agent snapshot. The per-tick think loop must not
    // allocate or sort at 60 Hz: the snapshot is rebuilt only when the
    // entity population changes, a cached entry dies or loses AiAgent, or
    // a periodic backstop fires (see scheduler.cpp). agentBuffer is
    // scratch owned by the schedule so its capacity is retained.
    std::vector<flecs::entity> agentBuffer;
    bool snapshotValid = false;
    std::uint64_t observedEntityCount = 0;
    std::uint32_t ticksSinceRebuild = 0;
    // Round-robin start index into agentBuffer. Without it, a fleet
    // larger than budget-per-think-cycle would let low entity ids consume
    // the budget every cycle and high ids would starve. The cursor is
    // preserved across snapshot rebuilds via lastProcessedId so rebuild
    // churn (e.g. projectile spawns changing entityCount) cannot reset
    // fairness.
    std::size_t cursor = 0;
    std::uint64_t lastProcessedId = 0;
};

void tickAiThink(ecs::Ecs& world, const ecs::SpatialIndex& index, double dt, AiThinkSchedule& schedule,
                 const BehaviorParams& params = {});

} // namespace starforge::ai
