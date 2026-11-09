#pragma once

#include "content/content_generation.hpp"
#include "world.hpp"

#include <vector>

namespace starforge::ecs
{

// A set of ECS entities mounted from one immutable generated chunk.
struct MountedChunk
{
    content::CellCoord cell;
    std::vector<flecs::entity> entities;
};

// Instantiates ECS entities for a generated chunk, one per content
// object. Local positions are converted through the cell origin into
// precision-preserving world positions. Only asteroid-tagged entities
// receive AsteroidTag; other content types use the AsteroidTag shell as
// a visual carrier until their dedicated entity kinds exist.
[[nodiscard]] MountedChunk instantiateChunk(Ecs& world, const content::ContentChunk& chunk);

// Destroys every entity owned by a mounted chunk.
void unmountChunk(MountedChunk& mounted);

} // namespace starforge::ecs
