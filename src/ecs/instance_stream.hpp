#pragma once

#include "environment/asteroid_field.hpp"
#include "spatial_index.hpp"
#include "world.hpp"

#include <vector>

namespace starforge::ecs
{

// Collects asteroid-tagged ECS entities into the instanced asteroid
// renderer's per-instance format (camera-relative position, scale, and
// orientation quaternion). Camera-relative conversion happens in double
// precision; only the final relative vector is cast to float32.
// Uses the world's cached query and chunk iteration (no per-frame query
// recompilation). Iteration order follows Flecs storage order for a
// fixed world history.
[[nodiscard]] std::uint32_t
collectAsteroidInstances(const Ecs& world, const spatial::WorldPosition& camera,
                         std::vector<environment::AsteroidInstance>& out);

// Culled variant: streams only asteroids in the camera's cell
// neighborhood (27 cells) from a SpatialIndex, bounding GPU upload
// bandwidth as the world grows. The index must be rebuilt after each
// Ecs::step().
[[nodiscard]] std::uint32_t collectAsteroidInstances(
    const Ecs& world, const SpatialIndex& index, const spatial::WorldPosition& camera,
    std::vector<environment::AsteroidInstance>& out);

} // namespace starforge::ecs
