#include "content/cell_traversal.hpp"
#include "spatial/world_position.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>


namespace starforge::content
{

namespace
{

// Add a small signed cell offset with saturation instead of signed overflow.
std::int64_t addCellOffset(std::int64_t base, int delta) noexcept
{
    if (delta > 0 && base > std::numeric_limits<std::int64_t>::max() - delta)
        return std::numeric_limits<std::int64_t>::max();
    if (delta < 0 && base < std::numeric_limits<std::int64_t>::min() - delta)
        return std::numeric_limits<std::int64_t>::min();
    return base + delta;
}

[[nodiscard]] bool cellLess(const CellCoord& a, const CellCoord& b) noexcept
{
    return std::tie(a.level, a.x, a.y, a.z) < std::tie(b.level, b.x, b.y, b.z);
}

} // namespace

CellCoord worldToCell(const starforge::spatial::WorldPosition& pos, int level)
{
    if (level < 0) level = 0;
    if (level > kMaxScaleLevel) level = kMaxScaleLevel;

    CellCoord coord;
    coord.level = level;
    // WorldPosition owns the authoritative floor-shift / collapse semantics.
    coord.x = pos.coarseCellX(level);
    coord.y = pos.coarseCellY(level);
    coord.z = pos.coarseCellZ(level);
    return coord;
}

std::vector<CellCoord> traverseCells(
    const starforge::spatial::WorldPosition& cameraPos,
    int minLevel,
    int maxLevel,
    double radiusMetres)
{
    std::vector<CellCoord> result;

    if (minLevel < 0) minLevel = 0;
    if (maxLevel > kMaxScaleLevel) maxLevel = kMaxScaleLevel;
    if (maxLevel < minLevel || !(radiusMetres >= 0.0))
        return result;

    for (int level = minLevel; level <= maxLevel; ++level)
    {
        const double cellSize = cellSizeAtLevel(level);

        // Camera cell at this level.
        CellCoord camCell = worldToCell(cameraPos, level);

        // Per-axis cell index range whose cell volume intersects the sphere
        // around the camera. Using the camera's position inside its coarse
        // cell avoids forcing a 3x3x3 neighbourhood for small radii.
        int loX = 0, hiX = 0;
        int loY = 0, hiY = 0;
        int loZ = 0, hiZ = 0;

        if (level < kMaxScaleLevel)
        {
            const int shift = starforge::spatial::WorldPosition::kCellBits * level;
            const std::uint64_t mask =
                (shift == 0) ? 0ULL : ((1ULL << shift) - 1ULL);

            auto axisRange = [&](std::int64_t cellCoord, double offset,
                                 int& lo, int& hi) {
                // Position of the camera within the coarse cell, in metres.
                const std::uint64_t sub =
                    static_cast<std::uint64_t>(cellCoord) & mask;
                const double inCell =
                    static_cast<double>(sub) * starforge::spatial::WorldPosition::kCellSize + offset;

                const double loD = std::floor((inCell - radiusMetres) / cellSize);
                const double hiD = std::floor((inCell + radiusMetres) / cellSize);

                // Cap the neighbourhood to keep traversal bounded.
                constexpr double kCap = static_cast<double>(kMaxTraversalCellRadius);
                lo = static_cast<int>(std::max(-kCap, std::min(0.0, loD)));
                hi = static_cast<int>(std::max(0.0, std::min(kCap, hiD)));
            };

            axisRange(cameraPos.cellX, cameraPos.offsetX, loX, hiX);
            axisRange(cameraPos.cellY, cameraPos.offsetY, loY, hiY);
            axisRange(cameraPos.cellZ, cameraPos.offsetZ, loZ, hiZ);
        }
        // At kMaxScaleLevel the world is a single summary cell.

        for (int dx = loX; dx <= hiX; ++dx)
        {
            for (int dy = loY; dy <= hiY; ++dy)
            {
                for (int dz = loZ; dz <= hiZ; ++dz)
                {
                    CellCoord coord;
                    coord.x = addCellOffset(camCell.x, dx);
                    coord.y = addCellOffset(camCell.y, dy);
                    coord.z = addCellOffset(camCell.z, dz);
                    coord.level = level;

                    result.push_back(coord);
                }
            }
        }
    }

    std::sort(result.begin(), result.end(), cellLess);
        result.erase(std::unique(result.begin(), result.end(),
                                 [](const CellCoord& a, const CellCoord& b) { return a == b; }),
                     result.end());

        return result;
    }

    bool StreamingTraversal::contains(const CellCoord& cell) const
    {
        return std::find(cells_.begin(), cells_.end(), cell) != cells_.end();
    }

    StreamingTraversal::Update StreamingTraversal::advance(
        const starforge::spatial::WorldPosition& cameraPos,
        int minLevel,
        int maxLevel,
        double radiusMetres)
    {
        // Invalid radius: leave the cache unchanged rather than tearing
        // down every loaded cell on a caller bug.
        if (!(radiusMetres >= 0.0) || !std::isfinite(radiusMetres))
        {
            return {};
        }

        const std::vector<CellCoord> desired = traverseCells(cameraPos, minLevel, maxLevel, radiusMetres);

        std::vector<CellCoord> active = desired;
        std::vector<CellCoord> loaded;
        std::vector<CellCoord> unloaded;

        const double expandMetres = radiusMetres + hysteresisMetres_;
        const std::vector<CellCoord> expanded =
            hysteresisMetres_ <= 0.0 ? desired : traverseCells(cameraPos, minLevel, maxLevel, expandMetres);

        for (const CellCoord& cell : cells_)
        {
            const bool inDesired = std::binary_search(desired.begin(), desired.end(), cell, cellLess);
            if (!inDesired)
            {
                // Stays only inside the expanded radius; otherwise it
                // leaves the active set.
                const bool inExpanded = hysteresisMetres_ > 0.0 &&
                                        std::binary_search(expanded.begin(), expanded.end(), cell, cellLess);
                if (inExpanded)
                {
                    active.push_back(cell);
                }
                else
                {
                    unloaded.push_back(cell);
                }
            }
        }

        for (const CellCoord& cell : desired)
        {
            if (!std::binary_search(cells_.begin(), cells_.end(), cell, cellLess))
            {
                loaded.push_back(cell);
            }
        }

        std::sort(active.begin(), active.end(), cellLess);
        active.erase(std::unique(active.begin(), active.end()), active.end());
        std::sort(loaded.begin(), loaded.end(), cellLess);
        std::sort(unloaded.begin(), unloaded.end(), cellLess);

        cells_ = active;
        return Update{loaded, unloaded};
    }

} // namespace starforge::content
