#pragma once

#include "world.hpp"

#include <flecs.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace starforge::ecs
{

// Exact cell coordinates (collision-free keys, unlike the 64-bit cell
// hash which is many-to-one).
struct CellCoord
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    bool operator==(const CellCoord&) const noexcept = default;
};

struct CellCoordHash
{
    [[nodiscard]] std::size_t operator()(const CellCoord& coord) const noexcept
    {
        std::size_t hash = 14695981039346656037ULL;
        hash = (hash ^ static_cast<std::size_t>(coord.x)) * 1099511628211ULL;
        hash = (hash ^ static_cast<std::size_t>(coord.y)) * 1099511628211ULL;
        hash = (hash ^ static_cast<std::size_t>(coord.z)) * 1099511628211ULL;
        return hash;
    }
};

// Per-frame spatial index: buckets entities by exact cell coordinates so
// systems can find nearby objects without scanning the world. The index
// is a snapshot taken at rebuild() time; handles may go stale if entities
// are deleted afterwards (check flecs::entity::is_alive() before use).
// The query is cached at construction; the index must not outlive the
// world it was built from.
class SpatialIndex
{
public:
    explicit SpatialIndex(Ecs& world);

    void rebuild();

    // Entities in the exact cell (nullptr when none).
    [[nodiscard]] const std::vector<flecs::entity>* findCell(const CellCoord& coord) const;

    // Entities in the cell and its 26 neighbors (exact keys, no duplicates).
    [[nodiscard]] std::vector<flecs::entity> findNeighborhood(const spatial::WorldPosition& center) const;

    [[nodiscard]] std::size_t cellCount() const noexcept { return cells_.size(); }
    [[nodiscard]] std::size_t entityCount() const noexcept { return entityCount_; }

private:
    flecs::query<const Transform> query_;
    std::unordered_map<CellCoord, std::vector<flecs::entity>, CellCoordHash> cells_;
    std::size_t entityCount_ = 0;
};

} // namespace starforge::ecs
