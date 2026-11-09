#pragma once

#include "ai/components.hpp"
#include "ai/perception.hpp"
#include "ecs/spatial_index.hpp"

#include <cstdint>
#include <optional>

namespace starforge::ai
{

// Behavior thresholds for the finite-state machine. Distances and times are
// simulation units advanced with a fixed tick; no wall-clock time is read.
struct BehaviorParams
{
    float engagementRadius = 1000.0f;
    float disengagementRadius = 1250.0f;
    float lostTargetSeconds = 2.0f;
    float retreatDurationSeconds = 3.0f;
    float evadeDurationSeconds = 2.0f;
    float dockEnterRadius = 100.0f;
    float dockDwellSeconds = 2.0f;
};

struct BehaviorSensors
{
    bool hasDestination = false;
    bool destinationArrived = false;
    bool hasTarget = false;
    bool hasPerceivedTarget = false;
    double targetDistance = 0.0;
    bool collisionThreat = false;
    double healthRatio = 1.0;
};

// Pure finite-state transition function. It is deliberately allocation-free
// so unit tests can pin the hysteresis/timing rules without an ECS or
// spatial index.
[[nodiscard]] BehaviorState nextBehavior(BehaviorState current, const BehaviorSensors& sensors,
                                         double stateTimer, const BehaviorParams& params,
                                         double targetLostTimer) noexcept;

// ECS wrapper: refreshes the perceived target, advances timers, applies the
// transition above, and steers the agent toward whatever the resulting
// state selects. Perception is updated every tick in Phase B; Phase C will
// consult thinkTimer before paying for a full target scan.
BehaviorState updateBehavior(ecs::Ecs& world, const ecs::SpatialIndex& index, flecs::entity entity, double dt,
                             const BehaviorParams& params = {});

} // namespace starforge::ai
