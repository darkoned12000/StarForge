#pragma once

#include "ecs/components.hpp"
#include "spatial/world_position.hpp"

#include <cstdint>
#include <vector>

namespace starforge::ai
{

struct SteeringParams
{
    double maxSpeed = 20.0;
    double maxAcceleration = 10.0;
    double arrivalRadius = 5.0;
    double separationRadius = 0.0;
    double separationWeight = 1.0;
};

// Maximum safe speed for arriving at the target without overshoot.
[[nodiscard]] double arrivalSpeed(double distance, const SteeringParams& params) noexcept;

// Desired velocity for a seek/arrival maneuver, rate-limited to the
// acceleration budget over dt. Returns zero inside the arrival radius.
[[nodiscard]] math::Vec3d seekVelocity(const math::Vec3d& currentPos, const math::Vec3d& targetPos,
                                       const math::Vec3d& currentVelocity, const SteeringParams& params,
                                       double dt) noexcept;

// Separation force on the agent from all other nearby entities. Other agents
// are passed explicitly so the caller can supply whatever local index
// granularity it already has; Phase B wires in the SpatialIndex.
[[nodiscard]] math::Vec3d separationVelocity(const math::Vec3d& currentPos,
                                             const std::vector<math::Vec3d>& otherPositions,
                                             const SteeringParams& params) noexcept;

// Ship orientation quaternion pointing the local -Z axis along dir
// (ships carry their modeled front face toward -Z in STARFORGE assets).
[[nodiscard]] math::Vec4f headingForDirection(const math::Vec3d& dir) noexcept;

// Ecs call for Phase A: seeks the target, adds separation from the local
// neighbours, and writes the result to the entity's Velocity and
// orientation components. Does not move the entity; Ecs::step does that.
// Other positions are camera-relative to the agent (zero is the agent).
void steerEntity(flecs::entity entity, const spatial::WorldPosition& targetPos, double dt,
                 const SteeringParams& params, const std::vector<math::Vec3d>& otherPositions = {});

} // namespace starforge::ai
