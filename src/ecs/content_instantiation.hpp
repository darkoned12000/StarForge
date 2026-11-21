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
// precision-preserving world positions. Only asteroid content objects
// retain AsteroidTag; other content types currently get a Transform-only
// entity until dedicated entity kinds/components are added.
[[nodiscard]] MountedChunk instantiateChunk(Ecs& world, const content::ContentChunk& chunk);

// Destroys every entity owned by a mounted chunk.
void unmountChunk(MountedChunk& mounted);

} // namespace starforge::ecs
