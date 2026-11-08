#include "spatial_index.hpp"

namespace starforge::ecs
{

SpatialIndex::SpatialIndex(Ecs& world) : query_(world.world().query<const Transform>())
{
}

void SpatialIndex::rebuild()
{
    // Clear bucket contents without releasing allocated capacities.
    for (auto& [coord, entities] : cells_)
    {
        (void)coord;
        entities.clear();
    }
    entityCount_ = 0;

    query_.each([this](flecs::entity entity, const Transform& transform) {
        const CellCoord coord{transform.position.cellX, transform.position.cellY, transform.position.cellZ};
        cells_[coord].push_back(entity);
        ++entityCount_;
    });
}

const std::vector<flecs::entity>* SpatialIndex::findCell(const CellCoord& coord) const
{
    const auto it = cells_.find(coord);
    return (it != cells_.end() && !it->second.empty()) ? &it->second : nullptr;
}

std::vector<flecs::entity> SpatialIndex::findNeighborhood(const spatial::WorldPosition& center) const
{
    std::vector<flecs::entity> result;
    result.reserve(64);

    for (std::int64_t dx = -1; dx <= 1; ++dx)
    {
        for (std::int64_t dy = -1; dy <= 1; ++dy)
        {
            for (std::int64_t dz = -1; dz <= 1; ++dz)
            {
                const CellCoord coord{center.cellX + dx, center.cellY + dy, center.cellZ + dz};
                if (const auto* cell = findCell(coord))
                {
                    result.insert(result.end(), cell->begin(), cell->end());
                }
            }
        }
    }
    return result;
}

} // namespace starforge::ecs
